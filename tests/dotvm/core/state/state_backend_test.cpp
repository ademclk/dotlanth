/// @file state_backend_test.cpp
/// @brief Unit tests for STATE-001 StateBackend interface
///
/// These tests follow TDD principles - written before implementation.
/// They define the expected behavior of the StateBackend interface.

#include "dotvm/core/state/state_backend.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace dotvm::core::state {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Create a byte span from a string literal
[[nodiscard]] std::span<const std::byte> to_bytes(std::string_view str) {
    return {reinterpret_cast<const std::byte*>(str.data()), str.size()};
}

/// @brief Create a byte vector from a string
[[nodiscard]] std::vector<std::byte> make_bytes(std::string_view str) {
    std::vector<std::byte> result(str.size());
    std::memcpy(result.data(), str.data(), str.size());
    return result;
}

/// @brief Convert byte vector to string for comparison
[[nodiscard]] std::string to_string(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// ============================================================================
// StateBackendError Tests
// ============================================================================

TEST(StateBackendErrorTest, ToStringAllValues) {
    // Key/Value errors (1-15)
    EXPECT_STREQ(to_string(StateBackendError::KeyNotFound), "KeyNotFound");
    EXPECT_STREQ(to_string(StateBackendError::KeyTooLarge), "KeyTooLarge");
    EXPECT_STREQ(to_string(StateBackendError::ValueTooLarge), "ValueTooLarge");
    EXPECT_STREQ(to_string(StateBackendError::InvalidKey), "InvalidKey");

    // Transaction errors (16-31)
    EXPECT_STREQ(to_string(StateBackendError::TransactionNotActive), "TransactionNotActive");
    EXPECT_STREQ(to_string(StateBackendError::TransactionConflict), "TransactionConflict");
    EXPECT_STREQ(to_string(StateBackendError::InvalidTransaction), "InvalidTransaction");

    // Backend errors (32-47)
    EXPECT_STREQ(to_string(StateBackendError::StorageFull), "StorageFull");
    EXPECT_STREQ(to_string(StateBackendError::BackendClosed), "BackendClosed");

    // Iteration errors (48-63)
    EXPECT_STREQ(to_string(StateBackendError::IterationAborted), "IterationAborted");
    EXPECT_STREQ(to_string(StateBackendError::InvalidPrefix), "InvalidPrefix");

    // Config errors (64-79)
    EXPECT_STREQ(to_string(StateBackendError::InvalidConfig), "InvalidConfig");
    EXPECT_STREQ(to_string(StateBackendError::UnsupportedOperation), "UnsupportedOperation");
}

TEST(StateBackendErrorTest, IsRecoverable) {
    // Recoverable errors - can retry or handle gracefully
    EXPECT_TRUE(is_recoverable(StateBackendError::KeyNotFound));
    EXPECT_TRUE(is_recoverable(StateBackendError::TransactionConflict));
    EXPECT_TRUE(is_recoverable(StateBackendError::IterationAborted));

    // Non-recoverable errors - indicate bugs or fatal conditions
    EXPECT_FALSE(is_recoverable(StateBackendError::InvalidKey));
    EXPECT_FALSE(is_recoverable(StateBackendError::InvalidConfig));
    EXPECT_FALSE(is_recoverable(StateBackendError::BackendClosed));
}

// ============================================================================
// TransactionIsolationLevel Tests
// ============================================================================

TEST(TransactionIsolationLevelTest, ToStringAllValues) {
    EXPECT_STREQ(to_string(TransactionIsolationLevel::ReadCommitted), "ReadCommitted");
    EXPECT_STREQ(to_string(TransactionIsolationLevel::Snapshot), "Snapshot");
}

// ============================================================================
// StateBackendConfig Tests
// ============================================================================

TEST(StateBackendConfigTest, DefaultsAreValid) {
    auto config = StateBackendConfig::defaults();

    EXPECT_EQ(config.max_key_size, 1024);           // 1KB
    EXPECT_EQ(config.max_value_size, 1024 * 1024);  // 1MB
    EXPECT_EQ(config.isolation_level, TransactionIsolationLevel::ReadCommitted);
    EXPECT_TRUE(config.enable_transactions);
    EXPECT_EQ(config.initial_capacity, 1024);
    EXPECT_TRUE(config.is_valid());
}

TEST(StateBackendConfigTest, IsValidChecksKeySize) {
    auto config = StateBackendConfig::defaults();
    config.max_key_size = 0;
    EXPECT_FALSE(config.is_valid());
}

TEST(StateBackendConfigTest, IsValidChecksValueSize) {
    auto config = StateBackendConfig::defaults();
    config.max_value_size = 0;
    EXPECT_FALSE(config.is_valid());
}

TEST(StateBackendConfigTest, IsValidChecksCapacity) {
    auto config = StateBackendConfig::defaults();
    config.initial_capacity = 0;
    EXPECT_FALSE(config.is_valid());
}

// ============================================================================
// TxId Tests
// ============================================================================

TEST(TxIdTest, DefaultConstruction) {
    TxId id{};
    EXPECT_EQ(id.id, 0);
    EXPECT_EQ(id.generation, 0);
}

TEST(TxIdTest, Equality) {
    TxId a{.id = 1, .generation = 2};
    TxId b{.id = 1, .generation = 2};
    TxId c{.id = 1, .generation = 3};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ============================================================================
// BatchOp Tests
// ============================================================================

TEST(BatchOpTest, PutOperation) {
    auto key = make_bytes("key");
    auto value = make_bytes("value");

    BatchOp op{
        .type = BatchOpType::Put,
        .key = key,
        .value = value,
    };

    EXPECT_EQ(op.type, BatchOpType::Put);
    EXPECT_EQ(to_string(op.key), "key");
    EXPECT_EQ(to_string(op.value), "value");
}

TEST(BatchOpTest, RemoveOperation) {
    auto key = make_bytes("key");

    BatchOp op{
        .type = BatchOpType::Remove,
        .key = key,
        .value = {},
    };

    EXPECT_EQ(op.type, BatchOpType::Remove);
    EXPECT_EQ(to_string(op.key), "key");
}

// ============================================================================
// StateBackend Test Fixture
// ============================================================================

class StateBackendTest : public ::testing::Test {
protected:
    void SetUp() override { backend_ = create_state_backend(StateBackendConfig::defaults()); }

    std::unique_ptr<StateBackend> backend_;
};

// ============================================================================
// Factory Tests
// ============================================================================

TEST(StateBackendFactoryTest, CreateWithDefaults) {
    auto backend = create_state_backend();
    ASSERT_NE(backend, nullptr);
    EXPECT_TRUE(backend->supports_transactions());
}

TEST(StateBackendFactoryTest, CreateWithCustomConfig) {
    StateBackendConfig config;
    config.max_key_size = 512;
    config.enable_transactions = false;

    auto backend = create_state_backend(config);
    ASSERT_NE(backend, nullptr);
    EXPECT_FALSE(backend->supports_transactions());
    EXPECT_EQ(backend->config().max_key_size, 512);
}

// ============================================================================
// CRUD Tests
// ============================================================================

TEST_F(StateBackendTest, PutAndGetRoundtrip) {
    auto key = to_bytes("test_key");
    auto value = to_bytes("test_value");

    auto put_result = backend_->put(key, value);
    ASSERT_TRUE(put_result.is_ok());

    auto get_result = backend_->get(key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "test_value");
}

TEST_F(StateBackendTest, GetNonExistentKeyReturnsKeyNotFound) {
    auto key = to_bytes("nonexistent");

    auto result = backend_->get(key);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::KeyNotFound);
}

TEST_F(StateBackendTest, PutOverwritesExistingValue) {
    auto key = to_bytes("key");
    auto value1 = to_bytes("value1");
    auto value2 = to_bytes("value2");

    ASSERT_TRUE(backend_->put(key, value1).is_ok());
    ASSERT_TRUE(backend_->put(key, value2).is_ok());

    auto result = backend_->get(key);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(to_string(result.value()), "value2");
}

TEST_F(StateBackendTest, RemoveExistingKey) {
    auto key = to_bytes("key");
    auto value = to_bytes("value");

    ASSERT_TRUE(backend_->put(key, value).is_ok());
    EXPECT_TRUE(backend_->exists(key));

    auto remove_result = backend_->remove(key);
    ASSERT_TRUE(remove_result.is_ok());
    EXPECT_FALSE(backend_->exists(key));
}

TEST_F(StateBackendTest, RemoveNonExistentKeyReturnsKeyNotFound) {
    auto key = to_bytes("nonexistent");

    auto result = backend_->remove(key);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::KeyNotFound);
}

TEST_F(StateBackendTest, ExistsReturnsTrueForExistingKey) {
    auto key = to_bytes("key");
    auto value = to_bytes("value");

    EXPECT_FALSE(backend_->exists(key));
    ASSERT_TRUE(backend_->put(key, value).is_ok());
    EXPECT_TRUE(backend_->exists(key));
}

TEST_F(StateBackendTest, ExistsReturnsFalseForNonExistentKey) {
    auto key = to_bytes("nonexistent");
    EXPECT_FALSE(backend_->exists(key));
}

// ============================================================================
// Boundary Tests
// ============================================================================

TEST_F(StateBackendTest, PutEmptyValue) {
    auto key = to_bytes("key");
    auto empty_value = std::span<const std::byte>{};

    auto put_result = backend_->put(key, empty_value);
    ASSERT_TRUE(put_result.is_ok());

    auto get_result = backend_->get(key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_TRUE(get_result.value().empty());
}

TEST_F(StateBackendTest, PutKeyTooLargeReturnsError) {
    auto config = backend_->config();
    std::vector<std::byte> large_key(config.max_key_size + 1);
    auto value = to_bytes("value");

    auto result = backend_->put(large_key, value);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::KeyTooLarge);
}

TEST_F(StateBackendTest, PutValueTooLargeReturnsError) {
    auto key = to_bytes("key");
    auto config = backend_->config();
    std::vector<std::byte> large_value(config.max_value_size + 1);

    auto result = backend_->put(key, large_value);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::ValueTooLarge);
}

TEST_F(StateBackendTest, PutEmptyKeyReturnsInvalidKey) {
    auto empty_key = std::span<const std::byte>{};
    auto value = to_bytes("value");

    auto result = backend_->put(empty_key, value);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::InvalidKey);
}

// ============================================================================
// Iteration Tests
// ============================================================================

TEST_F(StateBackendTest, IterateEmptyBackend) {
    std::vector<std::string> keys;
    auto result = backend_->iterate({}, [&keys](auto key, auto /*value*/) {
        keys.push_back(to_string(key));
        return true;  // Continue iteration
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(keys.empty());
}

TEST_F(StateBackendTest, IterateAllKeys) {
    ASSERT_TRUE(backend_->put(to_bytes("a"), to_bytes("1")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes("b"), to_bytes("2")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes("c"), to_bytes("3")).is_ok());

    std::vector<std::string> keys;
    auto result = backend_->iterate({}, [&keys](auto key, auto /*value*/) {
        keys.push_back(to_string(key));
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(keys.size(), 3);
    // Keys should be in sorted order
    EXPECT_TRUE(std::is_sorted(keys.begin(), keys.end()));
}

TEST_F(StateBackendTest, IterateWithPrefix) {
    ASSERT_TRUE(backend_->put(to_bytes("user:1"), to_bytes("alice")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes("user:2"), to_bytes("bob")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes("item:1"), to_bytes("widget")).is_ok());

    std::vector<std::string> keys;
    auto result = backend_->iterate(to_bytes("user:"), [&keys](auto key, auto /*value*/) {
        keys.push_back(to_string(key));
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(keys.size(), 2);
    for (const auto& key : keys) {
        EXPECT_TRUE(key.starts_with("user:"));
    }
}

TEST_F(StateBackendTest, IterateAbortEarly) {
    ASSERT_TRUE(backend_->put(to_bytes("a"), to_bytes("1")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes("b"), to_bytes("2")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes("c"), to_bytes("3")).is_ok());

    int count = 0;
    auto result = backend_->iterate({}, [&count](auto /*key*/, auto /*value*/) {
        ++count;
        return count < 2;  // Stop after 2 items
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(count, 2);
}

// ============================================================================
// Batch Tests
// ============================================================================

TEST_F(StateBackendTest, BatchEmpty) {
    auto result = backend_->batch({});
    ASSERT_TRUE(result.is_ok());
}

TEST_F(StateBackendTest, BatchMultiplePuts) {
    auto key1 = make_bytes("key1");
    auto key2 = make_bytes("key2");
    auto val1 = make_bytes("value1");
    auto val2 = make_bytes("value2");

    std::vector<BatchOp> ops = {
        {.type = BatchOpType::Put, .key = key1, .value = val1},
        {.type = BatchOpType::Put, .key = key2, .value = val2},
    };

    auto result = backend_->batch(ops);
    ASSERT_TRUE(result.is_ok());

    EXPECT_TRUE(backend_->exists(key1));
    EXPECT_TRUE(backend_->exists(key2));
}

TEST_F(StateBackendTest, BatchMixedOperations) {
    auto key1 = make_bytes("existing");
    auto key2 = make_bytes("new");
    auto val = make_bytes("value");

    // Pre-populate
    ASSERT_TRUE(backend_->put(key1, val).is_ok());

    std::vector<BatchOp> ops = {
        {.type = BatchOpType::Remove, .key = key1, .value = {}},
        {.type = BatchOpType::Put, .key = key2, .value = val},
    };

    auto result = backend_->batch(ops);
    ASSERT_TRUE(result.is_ok());

    EXPECT_FALSE(backend_->exists(key1));
    EXPECT_TRUE(backend_->exists(key2));
}

TEST_F(StateBackendTest, BatchAtomicityOnError) {
    auto key1 = make_bytes("key1");
    auto val1 = make_bytes("value1");

    // Pre-populate key1
    ASSERT_TRUE(backend_->put(key1, val1).is_ok());

    // Create a batch where one operation will fail (remove non-existent key)
    auto nonexistent = make_bytes("nonexistent");
    auto key2 = make_bytes("key2");
    auto val2 = make_bytes("value2");

    std::vector<BatchOp> ops = {
        {.type = BatchOpType::Put, .key = key2, .value = val2},      // Should succeed
        {.type = BatchOpType::Remove, .key = nonexistent, .value = {}},  // Should fail
    };

    auto result = backend_->batch(ops);
    ASSERT_TRUE(result.is_err());

    // Atomicity: key2 should NOT have been added since batch failed
    EXPECT_FALSE(backend_->exists(key2));
    // Original key1 should still exist
    EXPECT_TRUE(backend_->exists(key1));
}

// ============================================================================
// Transaction Tests
// ============================================================================

TEST_F(StateBackendTest, BeginAndCommitTransaction) {
    auto tx_result = backend_->begin_transaction();
    ASSERT_TRUE(tx_result.is_ok());

    TxHandle tx = std::move(tx_result.value());
    EXPECT_TRUE(tx.is_valid());

    // Put within transaction
    auto key = to_bytes("key");
    auto value = to_bytes("value");
    ASSERT_TRUE(backend_->put(key, value).is_ok());

    auto commit_result = backend_->commit(std::move(tx));
    ASSERT_TRUE(commit_result.is_ok());

    // Value should be visible after commit
    EXPECT_TRUE(backend_->exists(key));
}

TEST_F(StateBackendTest, BeginAndRollbackTransaction) {
    auto key = to_bytes("key");
    auto value = to_bytes("value");

    {
        auto tx_result = backend_->begin_transaction();
        ASSERT_TRUE(tx_result.is_ok());
        TxHandle tx = std::move(tx_result.value());

        ASSERT_TRUE(backend_->put(key, value).is_ok());

        auto rollback_result = backend_->rollback(std::move(tx));
        ASSERT_TRUE(rollback_result.is_ok());
    }

    // Value should NOT be visible after rollback
    EXPECT_FALSE(backend_->exists(key));
}

TEST_F(StateBackendTest, TxHandleRAIIAutoRollback) {
    auto key = to_bytes("key");
    auto value = to_bytes("value");

    {
        auto tx_result = backend_->begin_transaction();
        ASSERT_TRUE(tx_result.is_ok());
        TxHandle tx = std::move(tx_result.value());

        ASSERT_TRUE(backend_->put(key, value).is_ok());

        // TxHandle goes out of scope without commit - should auto-rollback
    }

    // Value should NOT be visible after auto-rollback
    EXPECT_FALSE(backend_->exists(key));
}

TEST_F(StateBackendTest, TxHandleMoveSemantics) {
    auto tx_result = backend_->begin_transaction();
    ASSERT_TRUE(tx_result.is_ok());

    TxHandle tx1 = std::move(tx_result.value());
    EXPECT_TRUE(tx1.is_valid());

    // Move to tx2
    TxHandle tx2 = std::move(tx1);
    EXPECT_FALSE(tx1.is_valid());  // tx1 should be invalidated
    EXPECT_TRUE(tx2.is_valid());

    // Commit tx2
    auto commit_result = backend_->commit(std::move(tx2));
    ASSERT_TRUE(commit_result.is_ok());
}

TEST_F(StateBackendTest, CommitInvalidTransactionReturnsError) {
    TxHandle invalid_tx;  // Default-constructed, invalid

    auto result = backend_->commit(std::move(invalid_tx));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::InvalidTransaction);
}

TEST_F(StateBackendTest, RollbackInvalidTransactionReturnsError) {
    TxHandle invalid_tx;  // Default-constructed, invalid

    auto result = backend_->rollback(std::move(invalid_tx));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::InvalidTransaction);
}

// ============================================================================
// Transactions Disabled Tests
// ============================================================================

TEST(StateBackendTransactionsDisabledTest, BeginTransactionReturnsUnsupportedOperation) {
    StateBackendConfig config;
    config.enable_transactions = false;
    auto backend = create_state_backend(config);

    auto result = backend->begin_transaction();
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::UnsupportedOperation);
}

// ============================================================================
// Stats Tests
// ============================================================================

TEST_F(StateBackendTest, KeyCountInitiallyZero) {
    EXPECT_EQ(backend_->key_count(), 0);
}

TEST_F(StateBackendTest, KeyCountAfterPut) {
    ASSERT_TRUE(backend_->put(to_bytes("a"), to_bytes("1")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes("b"), to_bytes("2")).is_ok());

    EXPECT_EQ(backend_->key_count(), 2);
}

TEST_F(StateBackendTest, KeyCountAfterRemove) {
    ASSERT_TRUE(backend_->put(to_bytes("a"), to_bytes("1")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes("b"), to_bytes("2")).is_ok());
    ASSERT_TRUE(backend_->remove(to_bytes("a")).is_ok());

    EXPECT_EQ(backend_->key_count(), 1);
}

TEST_F(StateBackendTest, StorageBytesInitiallyZero) {
    EXPECT_EQ(backend_->storage_bytes(), 0);
}

TEST_F(StateBackendTest, StorageBytesIncludesKeyAndValue) {
    auto key = to_bytes("key");
    auto value = to_bytes("value");

    ASSERT_TRUE(backend_->put(key, value).is_ok());

    // Storage should include at least key + value sizes
    EXPECT_GE(backend_->storage_bytes(), key.size() + value.size());
}

TEST_F(StateBackendTest, ClearRemovesAllData) {
    ASSERT_TRUE(backend_->put(to_bytes("a"), to_bytes("1")).is_ok());
    ASSERT_TRUE(backend_->put(to_bytes("b"), to_bytes("2")).is_ok());

    backend_->clear();

    EXPECT_EQ(backend_->key_count(), 0);
    EXPECT_EQ(backend_->storage_bytes(), 0);
    EXPECT_FALSE(backend_->exists(to_bytes("a")));
}

TEST_F(StateBackendTest, ConfigReturnsOriginalConfig) {
    auto config = StateBackendConfig::defaults();
    config.max_key_size = 512;

    auto backend = create_state_backend(config);
    EXPECT_EQ(backend->config().max_key_size, 512);
}

TEST_F(StateBackendTest, SupportsTransactionsMatchesConfig) {
    StateBackendConfig config_enabled;
    config_enabled.enable_transactions = true;
    auto backend_enabled = create_state_backend(config_enabled);
    EXPECT_TRUE(backend_enabled->supports_transactions());

    StateBackendConfig config_disabled;
    config_disabled.enable_transactions = false;
    auto backend_disabled = create_state_backend(config_disabled);
    EXPECT_FALSE(backend_disabled->supports_transactions());
}

}  // namespace
}  // namespace dotvm::core::state
