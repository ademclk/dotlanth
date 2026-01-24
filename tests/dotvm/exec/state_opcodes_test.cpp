/// @file state_opcodes_test.cpp
/// @brief STATE-004 State Opcodes Tests

#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/opcode.hpp"
#include "dotvm/core/state/state_backend.hpp"
#include "dotvm/core/state/transaction_manager.hpp"
#include "dotvm/core/vm_context.hpp"
#include "dotvm/exec/execution_context.hpp"
#include "dotvm/exec/state_execution_context.hpp"

namespace dotvm::exec::test {

/// Helper to discard nodiscard return values in tests
template <typename T>
void discard([[maybe_unused]] T&& value) {}

// ============================================================================
// Opcode Definition Tests
// ============================================================================

class StateOpcodeDefinitions : public ::testing::Test {};

TEST_F(StateOpcodeDefinitions, OpcodeValues) {
    // State read operations (0xA0-0xA7)
    EXPECT_EQ(core::opcode::STATE_GET, 0xA0);
    EXPECT_EQ(core::opcode::STATE_EXISTS, 0xA1);

    // State write operations (0xA8-0xAF)
    EXPECT_EQ(core::opcode::TX_BEGIN, 0xA8);
    EXPECT_EQ(core::opcode::TX_COMMIT, 0xA9);
    EXPECT_EQ(core::opcode::TX_ROLLBACK, 0xAA);
    EXPECT_EQ(core::opcode::STATE_PUT, 0xAB);
    EXPECT_EQ(core::opcode::STATE_DELETE, 0xAC);
}

TEST_F(StateOpcodeDefinitions, IsStateOp) {
    EXPECT_TRUE(core::is_state_op(core::opcode::STATE_GET));
    EXPECT_TRUE(core::is_state_op(core::opcode::STATE_EXISTS));
    EXPECT_TRUE(core::is_state_op(core::opcode::TX_BEGIN));
    EXPECT_TRUE(core::is_state_op(core::opcode::TX_COMMIT));
    EXPECT_TRUE(core::is_state_op(core::opcode::TX_ROLLBACK));
    EXPECT_TRUE(core::is_state_op(core::opcode::STATE_PUT));
    EXPECT_TRUE(core::is_state_op(core::opcode::STATE_DELETE));

    EXPECT_FALSE(core::is_state_op(core::opcode::NOP));
    EXPECT_FALSE(core::is_state_op(core::opcode::ADD));
}

TEST_F(StateOpcodeDefinitions, ReadWriteOps) {
    // Read operations
    EXPECT_TRUE(core::is_state_read_op(core::opcode::STATE_GET));
    EXPECT_TRUE(core::is_state_read_op(core::opcode::STATE_EXISTS));
    EXPECT_FALSE(core::is_state_read_op(core::opcode::STATE_PUT));
    EXPECT_FALSE(core::is_state_read_op(core::opcode::STATE_DELETE));

    // Write operations
    EXPECT_TRUE(core::is_state_write_op(core::opcode::STATE_PUT));
    EXPECT_TRUE(core::is_state_write_op(core::opcode::STATE_DELETE));
    EXPECT_TRUE(core::is_state_write_op(core::opcode::TX_BEGIN));
    EXPECT_TRUE(core::is_state_write_op(core::opcode::TX_COMMIT));
    EXPECT_TRUE(core::is_state_write_op(core::opcode::TX_ROLLBACK));
    EXPECT_FALSE(core::is_state_write_op(core::opcode::STATE_GET));
}

TEST_F(StateOpcodeDefinitions, TransactionOps) {
    EXPECT_TRUE(core::is_transaction_op(core::opcode::TX_BEGIN));
    EXPECT_TRUE(core::is_transaction_op(core::opcode::TX_COMMIT));
    EXPECT_TRUE(core::is_transaction_op(core::opcode::TX_ROLLBACK));
    EXPECT_FALSE(core::is_transaction_op(core::opcode::STATE_GET));
    EXPECT_FALSE(core::is_transaction_op(core::opcode::STATE_PUT));
}

// ============================================================================
// ExecResult Code Tests
// ============================================================================

class ExecResultCodes : public ::testing::Test {};

TEST_F(ExecResultCodes, StateRelatedCodes) {
    EXPECT_EQ(static_cast<int>(ExecResult::StateKeyNotFound), 14);
    EXPECT_EQ(static_cast<int>(ExecResult::TransactionConflict), 15);
    EXPECT_EQ(static_cast<int>(ExecResult::TransactionAborted), 16);
    EXPECT_EQ(static_cast<int>(ExecResult::StateNotEnabled), 17);
}

// ============================================================================
// StateExecError Conversion Tests
// ============================================================================

class StateExecErrorConversion : public ::testing::Test {};

TEST_F(StateExecErrorConversion, ToExecResult) {
    EXPECT_EQ(to_exec_result(StateExecError::Success), ExecResult::Success);
    EXPECT_EQ(to_exec_result(StateExecError::NotEnabled), ExecResult::StateNotEnabled);
    EXPECT_EQ(to_exec_result(StateExecError::InvalidHandle), ExecResult::TransactionAborted);
    EXPECT_EQ(to_exec_result(StateExecError::KeyNotFound), ExecResult::StateKeyNotFound);
    EXPECT_EQ(to_exec_result(StateExecError::TransactionConflict), ExecResult::TransactionConflict);
    EXPECT_EQ(to_exec_result(StateExecError::BackendError), ExecResult::Error);
}

// ============================================================================
// StateExecutionContext Tests
// ============================================================================

class StateExecutionContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        core::state::StateBackendConfig backend_config;
        backend_config.enable_transactions = true;
        backend_config.isolation_level = core::state::TransactionIsolationLevel::Snapshot;

        core::state::TransactionManagerConfig tm_config;
        tm_config.max_concurrent_transactions = 100;
        tm_config.default_timeout = std::chrono::milliseconds{5000};
        tm_config.default_isolation = core::state::TransactionIsolationLevel::Snapshot;

        auto backend = core::state::create_state_backend(backend_config);
        tx_mgr_ = std::make_unique<core::state::TransactionManager>(std::move(backend), tm_config);
    }

    std::unique_ptr<core::state::TransactionManager> tx_mgr_;
};

TEST_F(StateExecutionContextTest, DefaultNotEnabled) {
    StateExecutionContext ctx;
    EXPECT_FALSE(ctx.enabled());
    EXPECT_EQ(ctx.active_transaction_count(), 0);
}

TEST_F(StateExecutionContextTest, EnableEnablesOperations) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());
    EXPECT_TRUE(ctx.enabled());
}

TEST_F(StateExecutionContextTest, DisableRollsBackTransactions) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    auto handle = ctx.begin_transaction();
    EXPECT_NE(handle, 0);
    EXPECT_EQ(ctx.active_transaction_count(), 1);

    ctx.disable();
    EXPECT_FALSE(ctx.enabled());
    EXPECT_EQ(ctx.active_transaction_count(), 0);
}

TEST_F(StateExecutionContextTest, BeginReturnsUniqueHandles) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    auto h1 = ctx.begin_transaction();
    auto h2 = ctx.begin_transaction();
    auto h3 = ctx.begin_transaction();

    EXPECT_NE(h1, 0);
    EXPECT_NE(h2, 0);
    EXPECT_NE(h3, 0);
    EXPECT_NE(h1, h2);
    EXPECT_NE(h2, h3);
    EXPECT_NE(h1, h3);

    EXPECT_EQ(ctx.active_transaction_count(), 3);

    // Cleanup
    EXPECT_EQ(ctx.rollback(h1), StateExecError::Success);
    EXPECT_EQ(ctx.rollback(h2), StateExecError::Success);
    EXPECT_EQ(ctx.rollback(h3), StateExecError::Success);
}

TEST_F(StateExecutionContextTest, BeginFailsWhenNotEnabled) {
    StateExecutionContext ctx;
    EXPECT_EQ(ctx.begin_transaction(), 0);
}

TEST_F(StateExecutionContextTest, CommitRemovesTransaction) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    auto handle = ctx.begin_transaction();
    EXPECT_NE(handle, 0);
    EXPECT_EQ(ctx.active_transaction_count(), 1);

    EXPECT_EQ(ctx.commit(handle), StateExecError::Success);
    EXPECT_EQ(ctx.active_transaction_count(), 0);
}

TEST_F(StateExecutionContextTest, RollbackRemovesTransaction) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    auto handle = ctx.begin_transaction();
    EXPECT_NE(handle, 0);
    EXPECT_EQ(ctx.active_transaction_count(), 1);

    EXPECT_EQ(ctx.rollback(handle), StateExecError::Success);
    EXPECT_EQ(ctx.active_transaction_count(), 0);
}

TEST_F(StateExecutionContextTest, CommitInvalidHandleReturnsError) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    EXPECT_EQ(ctx.commit(999), StateExecError::InvalidHandle);
}

TEST_F(StateExecutionContextTest, RollbackInvalidHandleReturnsError) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    EXPECT_EQ(ctx.rollback(999), StateExecError::InvalidHandle);
}

TEST_F(StateExecutionContextTest, RollbackAllClearsAll) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    discard(ctx.begin_transaction());
    discard(ctx.begin_transaction());
    discard(ctx.begin_transaction());
    EXPECT_EQ(ctx.active_transaction_count(), 3);

    ctx.rollback_all();
    EXPECT_EQ(ctx.active_transaction_count(), 0);
}

TEST_F(StateExecutionContextTest, PutGetWithinTransaction) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    auto handle = ctx.begin_transaction();

    // Put a value
    std::vector<std::byte> key = {std::byte{0x01}, std::byte{0x02}};
    std::vector<std::byte> value = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};

    EXPECT_EQ(ctx.put(handle, key, value), StateExecError::Success);

    // Get the value back
    std::vector<std::byte> result;
    EXPECT_EQ(ctx.get(handle, key, result), StateExecError::Success);
    EXPECT_EQ(result, value);

    EXPECT_EQ(ctx.commit(handle), StateExecError::Success);
}

TEST_F(StateExecutionContextTest, ExistsWithinTransaction) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    auto handle = ctx.begin_transaction();

    std::vector<std::byte> key = {std::byte{0x01}};
    std::vector<std::byte> value = {std::byte{0xFF}};

    bool exists = true;
    EXPECT_EQ(ctx.exists(handle, key, exists), StateExecError::Success);
    EXPECT_FALSE(exists);

    EXPECT_EQ(ctx.put(handle, key, value), StateExecError::Success);

    EXPECT_EQ(ctx.exists(handle, key, exists), StateExecError::Success);
    EXPECT_TRUE(exists);

    EXPECT_EQ(ctx.rollback(handle), StateExecError::Success);
}

TEST_F(StateExecutionContextTest, RemoveWithinTransaction) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    auto handle = ctx.begin_transaction();

    std::vector<std::byte> key = {std::byte{0x01}};
    std::vector<std::byte> value = {std::byte{0xFF}};

    EXPECT_EQ(ctx.put(handle, key, value), StateExecError::Success);

    bool exists = false;
    EXPECT_EQ(ctx.exists(handle, key, exists), StateExecError::Success);
    EXPECT_TRUE(exists);

    EXPECT_EQ(ctx.remove(handle, key), StateExecError::Success);

    EXPECT_EQ(ctx.exists(handle, key, exists), StateExecError::Success);
    EXPECT_FALSE(exists);

    EXPECT_EQ(ctx.rollback(handle), StateExecError::Success);
}

TEST_F(StateExecutionContextTest, GetMissingKeyReturnsError) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    auto handle = ctx.begin_transaction();

    std::vector<std::byte> key = {std::byte{0x99}};
    std::vector<std::byte> result;

    EXPECT_EQ(ctx.get(handle, key, result), StateExecError::KeyNotFound);

    EXPECT_EQ(ctx.rollback(handle), StateExecError::Success);
}

TEST_F(StateExecutionContextTest, PutGetWithoutTransaction) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    // Use handle 0 for direct access
    std::vector<std::byte> key = {std::byte{0x01}};
    std::vector<std::byte> value = {std::byte{0xAA}};

    EXPECT_EQ(ctx.put(0, key, value), StateExecError::Success);

    std::vector<std::byte> result;
    EXPECT_EQ(ctx.get(0, key, result), StateExecError::Success);
    EXPECT_EQ(result, value);
}

TEST_F(StateExecutionContextTest, ExistsWithoutTransaction) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    std::vector<std::byte> key = {std::byte{0x01}};
    std::vector<std::byte> value = {std::byte{0xAA}};

    bool exists = true;
    EXPECT_EQ(ctx.exists(0, key, exists), StateExecError::Success);
    EXPECT_FALSE(exists);

    EXPECT_EQ(ctx.put(0, key, value), StateExecError::Success);

    EXPECT_EQ(ctx.exists(0, key, exists), StateExecError::Success);
    EXPECT_TRUE(exists);
}

TEST_F(StateExecutionContextTest, MultipleIndependentTransactions) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    auto h1 = ctx.begin_transaction();
    auto h2 = ctx.begin_transaction();

    // Each transaction writes to a different key
    std::vector<std::byte> key1 = {std::byte{0x01}};
    std::vector<std::byte> key2 = {std::byte{0x02}};
    std::vector<std::byte> value1 = {std::byte{0xAA}};
    std::vector<std::byte> value2 = {std::byte{0xBB}};

    EXPECT_EQ(ctx.put(h1, key1, value1), StateExecError::Success);
    EXPECT_EQ(ctx.put(h2, key2, value2), StateExecError::Success);

    // Both should commit without conflict
    EXPECT_EQ(ctx.commit(h1), StateExecError::Success);
    EXPECT_EQ(ctx.commit(h2), StateExecError::Success);

    // Verify both values are in the backend
    std::vector<std::byte> result;
    EXPECT_EQ(ctx.get(0, key1, result), StateExecError::Success);
    EXPECT_EQ(result, value1);

    result.clear();
    EXPECT_EQ(ctx.get(0, key2, result), StateExecError::Success);
    EXPECT_EQ(result, value2);
}

TEST_F(StateExecutionContextTest, TransactionIsolation) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    // Pre-populate a key
    std::vector<std::byte> key = {std::byte{0x01}};
    std::vector<std::byte> initial = {std::byte{0x00}};
    EXPECT_EQ(ctx.put(0, key, initial), StateExecError::Success);

    // Start a transaction and read the value
    auto handle = ctx.begin_transaction();
    std::vector<std::byte> result;
    EXPECT_EQ(ctx.get(handle, key, result), StateExecError::Success);
    EXPECT_EQ(result, initial);

    // Modify via direct access (simulating another transaction)
    std::vector<std::byte> updated = {std::byte{0xFF}};
    EXPECT_EQ(ctx.put(0, key, updated), StateExecError::Success);

    // Transaction should still see the old value (snapshot isolation)
    result.clear();
    EXPECT_EQ(ctx.get(handle, key, result), StateExecError::Success);
    EXPECT_EQ(result, initial);  // Still sees snapshot

    EXPECT_EQ(ctx.rollback(handle), StateExecError::Success);
}

TEST_F(StateExecutionContextTest, ConflictDetection) {
    StateExecutionContext ctx;
    ctx.enable(tx_mgr_.get());

    // Pre-populate a key
    std::vector<std::byte> key = {std::byte{0x01}};
    std::vector<std::byte> initial = {std::byte{0x00}};
    EXPECT_EQ(ctx.put(0, key, initial), StateExecError::Success);

    // Start two transactions that both read the same key
    auto h1 = ctx.begin_transaction();
    auto h2 = ctx.begin_transaction();

    std::vector<std::byte> result1, result2;
    EXPECT_EQ(ctx.get(h1, key, result1), StateExecError::Success);
    EXPECT_EQ(ctx.get(h2, key, result2), StateExecError::Success);

    // Both modify the same key
    std::vector<std::byte> value1 = {std::byte{0xAA}};
    std::vector<std::byte> value2 = {std::byte{0xBB}};
    EXPECT_EQ(ctx.put(h1, key, value1), StateExecError::Success);
    EXPECT_EQ(ctx.put(h2, key, value2), StateExecError::Success);

    // First commit succeeds
    EXPECT_EQ(ctx.commit(h1), StateExecError::Success);

    // Second commit fails due to conflict (key was modified after h2's snapshot)
    EXPECT_EQ(ctx.commit(h2), StateExecError::TransactionConflict);

    // Cleanup - h2 should still be active after conflict
    EXPECT_EQ(ctx.rollback(h2), StateExecError::Success);
}

TEST_F(StateExecutionContextTest, DestructorRollsBackActive) {
    std::uint64_t handle = 0;
    {
        StateExecutionContext ctx;
        ctx.enable(tx_mgr_.get());
        handle = ctx.begin_transaction();
        EXPECT_NE(handle, 0);

        std::vector<std::byte> key = {std::byte{0x01}};
        std::vector<std::byte> value = {std::byte{0xFF}};
        EXPECT_EQ(ctx.put(handle, key, value), StateExecError::Success);

        // Destructor should rollback
    }

    // Value should not be in backend (was rolled back)
    std::vector<std::byte> key = {std::byte{0x01}};
    auto result = tx_mgr_->backend().get(key);
    EXPECT_FALSE(result.is_ok());
}

// ============================================================================
// VmContext State Integration Tests
// ============================================================================

class VmContextStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        core::state::StateBackendConfig backend_config;
        backend_config.enable_transactions = true;
        backend_config.isolation_level = core::state::TransactionIsolationLevel::Snapshot;

        core::state::TransactionManagerConfig tm_config;
        tm_config.max_concurrent_transactions = 100;
        tm_config.default_timeout = std::chrono::milliseconds{5000};
        tm_config.default_isolation = core::state::TransactionIsolationLevel::Snapshot;

        auto backend = core::state::create_state_backend(backend_config);
        tx_mgr_ = std::make_unique<core::state::TransactionManager>(std::move(backend), tm_config);
    }

    std::unique_ptr<core::state::TransactionManager> tx_mgr_;
};

TEST_F(VmContextStateTest, StateDisabledByDefault) {
    core::VmContext ctx{core::Architecture::Arch64};
    EXPECT_FALSE(ctx.state_enabled());
}

TEST_F(VmContextStateTest, EnableStateWorks) {
    core::VmContext ctx{core::Architecture::Arch64};
    ctx.enable_state(tx_mgr_.get());
    EXPECT_TRUE(ctx.state_enabled());
}

TEST_F(VmContextStateTest, DisableStateWorks) {
    core::VmContext ctx{core::Architecture::Arch64};
    ctx.enable_state(tx_mgr_.get());
    EXPECT_TRUE(ctx.state_enabled());

    ctx.disable_state();
    EXPECT_FALSE(ctx.state_enabled());
}

TEST_F(VmContextStateTest, StateContextAccessible) {
    core::VmContext ctx{core::Architecture::Arch64};
    ctx.enable_state(tx_mgr_.get());

    auto& state_ctx = ctx.state_context();
    auto handle = state_ctx.begin_transaction();
    EXPECT_NE(handle, 0);

    EXPECT_EQ(state_ctx.rollback(handle), StateExecError::Success);
}

TEST_F(VmContextStateTest, ResetRollsBackTransactions) {
    core::VmContext ctx{core::Architecture::Arch64};
    ctx.enable_state(tx_mgr_.get());

    auto& state_ctx = ctx.state_context();
    discard(state_ctx.begin_transaction());
    discard(state_ctx.begin_transaction());
    EXPECT_EQ(state_ctx.active_transaction_count(), 2);

    ctx.reset();
    EXPECT_EQ(state_ctx.active_transaction_count(), 0);
}

}  // namespace dotvm::exec::test
