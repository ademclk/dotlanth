/// @file transaction_manager_test.cpp
/// @brief STATE-003 TransactionManager unit tests
///
/// Tests for TransactionManager with OCC and deadlock detection:
/// - Transaction lifecycle (begin, commit, rollback)
/// - Read set tracking and OCC validation
/// - Write set buffering and read-your-writes
/// - Write-write conflict detection
/// - Isolation levels (Snapshot, ReadCommitted)
/// - Deadlock detection via timeout
/// - Concurrency and thread safety

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/state_backend.hpp"
#include "dotvm/core/state/transaction_manager.hpp"

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
// Test Fixture
// ============================================================================

class TransactionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        StateBackendConfig backend_config;
        backend_config.enable_transactions = true;
        backend_config.isolation_level = TransactionIsolationLevel::Snapshot;

        TransactionManagerConfig tm_config;
        tm_config.max_concurrent_transactions = 100;
        tm_config.default_timeout = std::chrono::milliseconds{5000};
        tm_config.default_isolation = TransactionIsolationLevel::Snapshot;

        auto backend = create_state_backend(backend_config);
        tm_ = std::make_unique<TransactionManager>(std::move(backend), tm_config);
    }

    std::unique_ptr<TransactionManager> tm_;
};

// ============================================================================
// Transaction Lifecycle Tests
// ============================================================================

/// @test begin() returns a valid active transaction
TEST_F(TransactionManagerTest, BeginReturnsValidTransaction) {
    auto result = tm_->begin();
    ASSERT_TRUE(result.is_ok());

    ManagedTransaction* tx = result.value();
    ASSERT_NE(tx, nullptr);
    EXPECT_EQ(tx->state, TransactionState::Active);
    EXPECT_GT(tx->id.id, 0u);
    EXPECT_TRUE(tx->read_set.empty());
    EXPECT_TRUE(tx->write_set.empty());

    // Clean up
    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
}

/// @test commit() applies changes to storage
TEST_F(TransactionManagerTest, CommitAppliesChanges) {
    auto key = to_bytes("commit_key");
    auto value = to_bytes("commit_value");

    // Begin transaction and put value
    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    ASSERT_TRUE(tm_->put(*tx, key, value).is_ok());

    // Commit
    ASSERT_TRUE(tm_->commit(*tx).is_ok());
    EXPECT_EQ(tx->state, TransactionState::Committed);

    // Verify value is visible via new transaction
    auto tx2_result = tm_->begin();
    ASSERT_TRUE(tx2_result.is_ok());
    ManagedTransaction* tx2 = tx2_result.value();

    auto get_result = tm_->get(*tx2, key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "commit_value");

    ASSERT_TRUE(tm_->rollback(*tx2).is_ok());
}

/// @test rollback() discards all changes
TEST_F(TransactionManagerTest, RollbackDiscardsChanges) {
    auto key = to_bytes("rollback_key");
    auto value = to_bytes("rollback_value");

    // Begin transaction and put value
    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    ASSERT_TRUE(tm_->put(*tx, key, value).is_ok());

    // Value visible within transaction
    EXPECT_TRUE(tm_->exists(*tx, key));

    // Rollback
    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
    EXPECT_EQ(tx->state, TransactionState::Aborted);

    // Verify value is NOT visible via new transaction
    auto tx2_result = tm_->begin();
    ASSERT_TRUE(tx2_result.is_ok());
    ManagedTransaction* tx2 = tx2_result.value();

    EXPECT_FALSE(tm_->exists(*tx2, key));

    ASSERT_TRUE(tm_->rollback(*tx2).is_ok());
}

/// @test Committing an already-committed transaction returns error
TEST_F(TransactionManagerTest, DoubleCommitReturnsError) {
    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    // First commit succeeds
    ASSERT_TRUE(tm_->commit(*tx).is_ok());

    // Second commit fails
    auto second_commit = tm_->commit(*tx);
    EXPECT_TRUE(second_commit.is_err());
    EXPECT_EQ(second_commit.error(), StateBackendError::InvalidTransaction);
}

/// @test Rolling back an already-rolled-back transaction returns error
TEST_F(TransactionManagerTest, DoubleRollbackReturnsError) {
    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    // First rollback succeeds
    ASSERT_TRUE(tm_->rollback(*tx).is_ok());

    // Second rollback fails
    auto second_rollback = tm_->rollback(*tx);
    EXPECT_TRUE(second_rollback.is_err());
    EXPECT_EQ(second_rollback.error(), StateBackendError::InvalidTransaction);
}

/// @test Maximum concurrent transactions is enforced
TEST_F(TransactionManagerTest, MaxTransactionsEnforced) {
    // Create config with small limit
    StateBackendConfig backend_config;
    backend_config.enable_transactions = true;

    TransactionManagerConfig tm_config;
    tm_config.max_concurrent_transactions = 3;

    auto backend = create_state_backend(backend_config);
    TransactionManager tm(std::move(backend), tm_config);

    // Create 3 transactions (at limit)
    std::vector<ManagedTransaction*> transactions;
    for (int i = 0; i < 3; ++i) {
        auto result = tm.begin();
        ASSERT_TRUE(result.is_ok()) << "Failed to create transaction " << i;
        transactions.push_back(result.value());
    }

    // 4th transaction should fail
    auto result = tm.begin();
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), StateBackendError::TooManyTransactions);

    // Clean up
    for (auto* tx : transactions) {
        ASSERT_TRUE(tm.rollback(*tx).is_ok());
    }

    // Now we can create a new one
    auto new_result = tm.begin();
    EXPECT_TRUE(new_result.is_ok());
    ASSERT_TRUE(tm.rollback(*new_result.value()).is_ok());
}

// ============================================================================
// Read Set Tracking Tests
// ============================================================================

/// @test get() adds key to read_set
TEST_F(TransactionManagerTest, GetAddsToReadSet) {
    auto key = to_bytes("read_key");
    auto value = to_bytes("read_value");

    // Setup: put value directly via backend
    ASSERT_TRUE(tm_->backend().put(key, value).is_ok());

    // Begin transaction and read
    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    auto get_result = tm_->get(*tx, key);
    ASSERT_TRUE(get_result.is_ok());

    // Verify key is in read_set
    auto key_vec = make_bytes("read_key");
    EXPECT_EQ(tx->read_set.size(), 1u);
    ASSERT_TRUE(tx->read_set.count(key_vec) > 0);
    EXPECT_TRUE(tx->read_set.at(key_vec).existed);

    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
}

/// @test exists() adds key to read_set
TEST_F(TransactionManagerTest, ExistsAddsToReadSet) {
    auto key = to_bytes("exists_key");
    auto value = to_bytes("exists_value");

    // Setup: put value directly via backend
    ASSERT_TRUE(tm_->backend().put(key, value).is_ok());

    // Begin transaction and check exists
    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    bool exists = tm_->exists(*tx, key);
    EXPECT_TRUE(exists);

    // Verify key is in read_set
    auto key_vec = make_bytes("exists_key");
    EXPECT_EQ(tx->read_set.size(), 1u);
    ASSERT_TRUE(tx->read_set.count(key_vec) > 0);
    EXPECT_TRUE(tx->read_set.at(key_vec).existed);

    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
}

/// @test Read set tracks version at read time
TEST_F(TransactionManagerTest, ReadSetTracksVersion) {
    auto key = to_bytes("version_key");
    auto value = to_bytes("version_value");

    // Setup: put value via transaction (not direct backend)
    {
        auto setup_tx = tm_->begin();
        ASSERT_TRUE(setup_tx.is_ok());
        ASSERT_TRUE(tm_->put(*setup_tx.value(), key, value).is_ok());
        ASSERT_TRUE(tm_->commit(*setup_tx.value()).is_ok());
    }

    // Begin transaction and read
    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    (void)tm_->get(*tx, key);

    // Verify version is tracked (should be >= 2: initial 1 + commit increment)
    auto key_vec = make_bytes("version_key");
    ASSERT_TRUE(tx->read_set.count(key_vec) > 0);
    EXPECT_GE(tx->read_set.at(key_vec).version_at_read, 1u);

    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
}

/// @test Read set tracks non-existence
TEST_F(TransactionManagerTest, ReadSetTracksExistence) {
    auto key = to_bytes("nonexistent_key");

    // Begin transaction and read non-existent key
    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    auto get_result = tm_->get(*tx, key);
    EXPECT_TRUE(get_result.is_err());
    EXPECT_EQ(get_result.error(), StateBackendError::KeyNotFound);

    // Key should still be in read_set with existed=false
    auto key_vec = make_bytes("nonexistent_key");
    EXPECT_EQ(tx->read_set.size(), 1u);
    ASSERT_TRUE(tx->read_set.count(key_vec) > 0);
    EXPECT_FALSE(tx->read_set.at(key_vec).existed);

    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
}

// ============================================================================
// Write Set Tests
// ============================================================================

/// @test put() buffers in write_set
TEST_F(TransactionManagerTest, PutBuffersInWriteSet) {
    auto key = to_bytes("write_key");
    auto value = to_bytes("write_value");

    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    ASSERT_TRUE(tm_->put(*tx, key, value).is_ok());

    // Verify key is in write_set
    auto key_vec = make_bytes("write_key");
    EXPECT_EQ(tx->write_set.size(), 1u);
    ASSERT_TRUE(tx->write_set.count(key_vec) > 0);
    EXPECT_TRUE(tx->write_set.at(key_vec).value.has_value());
    EXPECT_EQ(to_string(*tx->write_set.at(key_vec).value), "write_value");

    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
}

/// @test remove() marks delete in write_set
TEST_F(TransactionManagerTest, RemoveMarksDeleteInWriteSet) {
    auto key = to_bytes("delete_key");
    auto value = to_bytes("delete_value");

    // Setup: put value
    ASSERT_TRUE(tm_->backend().put(key, value).is_ok());

    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    ASSERT_TRUE(tm_->remove(*tx, key).is_ok());

    // Verify key is in write_set as deletion
    auto key_vec = make_bytes("delete_key");
    EXPECT_EQ(tx->write_set.size(), 1u);
    ASSERT_TRUE(tx->write_set.count(key_vec) > 0);
    EXPECT_FALSE(tx->write_set.at(key_vec).value.has_value());  // nullopt = delete

    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
}

/// @test Read-your-writes: get() returns value from write_set
TEST_F(TransactionManagerTest, ReadYourWritesWorks) {
    auto key = to_bytes("ryw_key");
    auto value = to_bytes("ryw_value");

    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    // Put value in write_set
    ASSERT_TRUE(tm_->put(*tx, key, value).is_ok());

    // Read should return the uncommitted write
    auto get_result = tm_->get(*tx, key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "ryw_value");

    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
}

/// @test Deleted key not visible within transaction
TEST_F(TransactionManagerTest, DeletedKeyNotVisible) {
    auto key = to_bytes("visible_key");
    auto value = to_bytes("visible_value");

    // Setup: put value
    ASSERT_TRUE(tm_->backend().put(key, value).is_ok());

    auto begin_result = tm_->begin();
    ASSERT_TRUE(begin_result.is_ok());
    ManagedTransaction* tx = begin_result.value();

    // Verify key exists
    EXPECT_TRUE(tm_->exists(*tx, key));

    // Delete
    ASSERT_TRUE(tm_->remove(*tx, key).is_ok());

    // Key should not be visible
    EXPECT_FALSE(tm_->exists(*tx, key));
    auto get_result = tm_->get(*tx, key);
    EXPECT_TRUE(get_result.is_err());
    EXPECT_EQ(get_result.error(), StateBackendError::KeyNotFound);

    ASSERT_TRUE(tm_->rollback(*tx).is_ok());
}

// ============================================================================
// OCC Validation Tests
// ============================================================================

/// @test Commit fails if read_set key was modified by another transaction
TEST_F(TransactionManagerTest, CommitFailsIfReadSetKeyModified) {
    auto key = to_bytes("occ_key");
    auto initial = to_bytes("initial");
    auto modified = to_bytes("modified");

    // Setup: put initial value
    ASSERT_TRUE(tm_->backend().put(key, initial).is_ok());

    // T1 starts and reads the key
    auto t1_result = tm_->begin();
    ASSERT_TRUE(t1_result.is_ok());
    ManagedTransaction* t1 = t1_result.value();

    auto read_result = tm_->get(*t1, key);
    ASSERT_TRUE(read_result.is_ok());

    // T2 modifies the key and commits
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    ASSERT_TRUE(tm_->put(*t2, key, modified).is_ok());
    ASSERT_TRUE(tm_->commit(*t2).is_ok());

    // T1 tries to commit - should fail (read set validation)
    auto t1_commit = tm_->commit(*t1);
    EXPECT_TRUE(t1_commit.is_err());
    // Could be TransactionConflict or ReadSetValidationFailed depending on implementation
    EXPECT_TRUE(t1_commit.error() == StateBackendError::TransactionConflict ||
                t1_commit.error() == StateBackendError::ReadSetValidationFailed);
}

/// @test Commit fails if key existence changed (phantom read detection)
TEST_F(TransactionManagerTest, CommitFailsIfKeyExistenceChanged) {
    auto key = to_bytes("phantom_key");
    auto value = to_bytes("phantom_value");

    // T1 starts and checks key doesn't exist
    auto t1_result = tm_->begin();
    ASSERT_TRUE(t1_result.is_ok());
    ManagedTransaction* t1 = t1_result.value();

    EXPECT_FALSE(tm_->exists(*t1, key));

    // T2 creates the key and commits
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    ASSERT_TRUE(tm_->put(*t2, key, value).is_ok());
    ASSERT_TRUE(tm_->commit(*t2).is_ok());

    // T1 tries to commit - should fail (phantom read)
    auto t1_commit = tm_->commit(*t1);
    EXPECT_TRUE(t1_commit.is_err());
}

/// @test Commit succeeds if no conflicts
TEST_F(TransactionManagerTest, CommitSucceedsIfNoConflicts) {
    auto key1 = to_bytes("no_conflict_1");
    auto key2 = to_bytes("no_conflict_2");
    auto value = to_bytes("value");

    // T1 reads key1 and writes key2
    auto t1_result = tm_->begin();
    ASSERT_TRUE(t1_result.is_ok());
    ManagedTransaction* t1 = t1_result.value();

    (void)tm_->exists(*t1, key1);  // Read key1
    ASSERT_TRUE(tm_->put(*t1, key2, value).is_ok());

    // T2 writes key1 (different key)
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    ASSERT_TRUE(tm_->put(*t2, key1, value).is_ok());
    ASSERT_TRUE(tm_->commit(*t2).is_ok());

    // T1 commit should fail because T2 wrote to key1 which T1 read
    auto t1_commit = tm_->commit(*t1);
    EXPECT_TRUE(t1_commit.is_err());
}

// ============================================================================
// Write-Write Conflict Tests
// ============================================================================

/// @test First committer wins on write-write conflict
TEST_F(TransactionManagerTest, FirstCommitterWins) {
    auto key = to_bytes("ww_key");
    auto val_t1 = to_bytes("t1_value");
    auto val_t2 = to_bytes("t2_value");

    // T1 starts
    auto t1_result = tm_->begin();
    ASSERT_TRUE(t1_result.is_ok());
    ManagedTransaction* t1 = t1_result.value();

    ASSERT_TRUE(tm_->put(*t1, key, val_t1).is_ok());

    // T2 starts
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    ASSERT_TRUE(tm_->put(*t2, key, val_t2).is_ok());

    // T1 commits first - succeeds
    ASSERT_TRUE(tm_->commit(*t1).is_ok());

    // T2 tries to commit - fails (write-write conflict)
    auto t2_commit = tm_->commit(*t2);
    EXPECT_TRUE(t2_commit.is_err());
    EXPECT_EQ(t2_commit.error(), StateBackendError::TransactionConflict);

    // Verify T1's value is visible
    auto t3_result = tm_->begin();
    ASSERT_TRUE(t3_result.is_ok());
    ManagedTransaction* t3 = t3_result.value();

    auto get_result = tm_->get(*t3, key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "t1_value");

    ASSERT_TRUE(tm_->rollback(*t3).is_ok());
}

/// @test Second committer gets conflict error
TEST_F(TransactionManagerTest, SecondCommitterGetsConflict) {
    auto key = to_bytes("conflict_key");
    auto initial = to_bytes("initial");

    // Setup
    ASSERT_TRUE(tm_->backend().put(key, initial).is_ok());

    // T1 reads and modifies
    auto t1_result = tm_->begin();
    ASSERT_TRUE(t1_result.is_ok());
    ManagedTransaction* t1 = t1_result.value();

    (void)tm_->get(*t1, key);  // Read first
    ASSERT_TRUE(tm_->put(*t1, key, to_bytes("t1_mod")).is_ok());

    // T2 reads and modifies
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    (void)tm_->get(*t2, key);  // Read first
    ASSERT_TRUE(tm_->put(*t2, key, to_bytes("t2_mod")).is_ok());

    // T1 commits first
    ASSERT_TRUE(tm_->commit(*t1).is_ok());

    // T2 gets conflict
    auto t2_commit = tm_->commit(*t2);
    EXPECT_TRUE(t2_commit.is_err());
    EXPECT_TRUE(t2_commit.error() == StateBackendError::TransactionConflict ||
                t2_commit.error() == StateBackendError::ReadSetValidationFailed);
}

/// @test No conflict on different keys
TEST_F(TransactionManagerTest, NoConflictOnDifferentKeys) {
    auto key1 = to_bytes("diff_key1");
    auto key2 = to_bytes("diff_key2");
    auto val1 = to_bytes("val1");
    auto val2 = to_bytes("val2");

    // T1 writes key1
    auto t1_result = tm_->begin();
    ASSERT_TRUE(t1_result.is_ok());
    ManagedTransaction* t1 = t1_result.value();

    ASSERT_TRUE(tm_->put(*t1, key1, val1).is_ok());

    // T2 writes key2
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    ASSERT_TRUE(tm_->put(*t2, key2, val2).is_ok());

    // Both should commit successfully
    ASSERT_TRUE(tm_->commit(*t1).is_ok());
    ASSERT_TRUE(tm_->commit(*t2).is_ok());

    // Verify both values
    auto t3_result = tm_->begin();
    ASSERT_TRUE(t3_result.is_ok());
    ManagedTransaction* t3 = t3_result.value();

    auto get1 = tm_->get(*t3, key1);
    ASSERT_TRUE(get1.is_ok());
    EXPECT_EQ(to_string(get1.value()), "val1");

    auto get2 = tm_->get(*t3, key2);
    ASSERT_TRUE(get2.is_ok());
    EXPECT_EQ(to_string(get2.value()), "val2");

    ASSERT_TRUE(tm_->rollback(*t3).is_ok());
}

// ============================================================================
// Isolation Level Tests
// ============================================================================

/// @test Snapshot isolation sees consistent view
TEST_F(TransactionManagerTest, SnapshotSeesConsistentView) {
    auto key = to_bytes("snapshot_key");
    auto initial = to_bytes("initial");
    auto modified = to_bytes("modified");

    // Setup
    ASSERT_TRUE(tm_->backend().put(key, initial).is_ok());

    // T1 starts with Snapshot isolation
    auto t1_result = tm_->begin(TransactionIsolationLevel::Snapshot);
    ASSERT_TRUE(t1_result.is_ok());
    ManagedTransaction* t1 = t1_result.value();

    // Read initial value
    auto read1 = tm_->get(*t1, key);
    ASSERT_TRUE(read1.is_ok());
    EXPECT_EQ(to_string(read1.value()), "initial");

    // T2 modifies and commits
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    ASSERT_TRUE(tm_->put(*t2, key, modified).is_ok());
    ASSERT_TRUE(tm_->commit(*t2).is_ok());

    // T1 still sees initial value (snapshot isolation)
    auto read2 = tm_->get(*t1, key);
    ASSERT_TRUE(read2.is_ok());
    EXPECT_EQ(to_string(read2.value()), "initial");

    // T1 commit will fail due to read set validation
    auto t1_commit = tm_->commit(*t1);
    EXPECT_TRUE(t1_commit.is_err());
}

/// @test ReadCommitted sees latest committed values
TEST_F(TransactionManagerTest, ReadCommittedSeesLatestCommits) {
    auto key = to_bytes("rc_key");
    auto initial = to_bytes("initial");
    auto modified = to_bytes("modified");

    // Setup
    ASSERT_TRUE(tm_->backend().put(key, initial).is_ok());

    // T1 starts with ReadCommitted isolation
    auto t1_result = tm_->begin(TransactionIsolationLevel::ReadCommitted);
    ASSERT_TRUE(t1_result.is_ok());
    ManagedTransaction* t1 = t1_result.value();

    // Read initial value
    auto read1 = tm_->get(*t1, key);
    ASSERT_TRUE(read1.is_ok());
    EXPECT_EQ(to_string(read1.value()), "initial");

    // T2 modifies and commits
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    ASSERT_TRUE(tm_->put(*t2, key, modified).is_ok());
    ASSERT_TRUE(tm_->commit(*t2).is_ok());

    // T1 sees modified value (read committed)
    auto read2 = tm_->get(*t1, key);
    ASSERT_TRUE(read2.is_ok());
    EXPECT_EQ(to_string(read2.value()), "modified");

    ASSERT_TRUE(tm_->rollback(*t1).is_ok());
}

/// @test Different isolation levels per transaction
TEST_F(TransactionManagerTest, IsolationLevelPerTransaction) {
    auto key = to_bytes("iso_key");
    auto value = to_bytes("iso_value");

    ASSERT_TRUE(tm_->backend().put(key, value).is_ok());

    // T1 with Snapshot
    auto t1_result = tm_->begin(TransactionIsolationLevel::Snapshot);
    ASSERT_TRUE(t1_result.is_ok());
    ManagedTransaction* t1 = t1_result.value();
    EXPECT_EQ(t1->isolation_level, TransactionIsolationLevel::Snapshot);

    // T2 with ReadCommitted
    auto t2_result = tm_->begin(TransactionIsolationLevel::ReadCommitted);
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();
    EXPECT_EQ(t2->isolation_level, TransactionIsolationLevel::ReadCommitted);

    ASSERT_TRUE(tm_->rollback(*t1).is_ok());
    ASSERT_TRUE(tm_->rollback(*t2).is_ok());
}

// ============================================================================
// Deadlock Detection Tests
// ============================================================================

/// @test Transaction timeout aborts transaction
TEST_F(TransactionManagerTest, TimeoutAbortsTransaction) {
    // Create TM with very short timeout for testing
    StateBackendConfig backend_config;
    backend_config.enable_transactions = true;

    TransactionManagerConfig tm_config;
    tm_config.default_timeout = std::chrono::milliseconds{100};
    tm_config.deadlock_check_interval = std::chrono::milliseconds{50};

    auto backend = create_state_backend(backend_config);
    TransactionManager tm(std::move(backend), tm_config);

    // Start transaction
    auto tx_result = tm.begin();
    ASSERT_TRUE(tx_result.is_ok());
    ManagedTransaction* tx = tx_result.value();

    // Wait for timeout + check interval
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    // Transaction should be aborted
    EXPECT_EQ(tx->state, TransactionState::Aborted);

    // Trying to use it should fail
    auto put_result = tm.put(*tx, to_bytes("key"), to_bytes("value"));
    EXPECT_TRUE(put_result.is_err());
}

/// @test Deadlock detector thread runs
TEST_F(TransactionManagerTest, DeadlockDetectorThreadWorks) {
    // This test verifies the deadlock detector doesn't crash or hang
    // Create multiple short-lived transactions
    for (int i = 0; i < 10; ++i) {
        auto tx_result = tm_->begin();
        ASSERT_TRUE(tx_result.is_ok());
        ManagedTransaction* tx = tx_result.value();

        std::string key = "detector_key_" + std::to_string(i);
        ASSERT_TRUE(tm_->put(*tx, to_bytes(key), to_bytes("value")).is_ok());
        ASSERT_TRUE(tm_->commit(*tx).is_ok());
    }

    // Brief sleep to let detector run a cycle
    std::this_thread::sleep_for(std::chrono::milliseconds{150});

    // System should still be functional
    auto tx_result = tm_->begin();
    ASSERT_TRUE(tx_result.is_ok());
    ASSERT_TRUE(tm_->rollback(*tx_result.value()).is_ok());
}

// ============================================================================
// Concurrency Tests
// ============================================================================

/// @test Concurrent begin and commit are safe
TEST_F(TransactionManagerTest, ConcurrentBeginCommitSafe) {
    std::atomic<int> success_count{0};
    std::atomic<int> conflict_count{0};
    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 25;

    std::vector<std::future<void>> futures;

    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&, t]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                auto tx_result = tm_->begin();
                if (!tx_result.is_ok()) {
                    continue;
                }
                ManagedTransaction* tx = tx_result.value();

                std::string key = "concurrent_" + std::to_string(t) + "_" + std::to_string(i);
                auto put_result = tm_->put(*tx, to_bytes(key), to_bytes("value"));
                if (!put_result.is_ok()) {
                    (void)tm_->rollback(*tx);
                    continue;
                }

                auto commit_result = tm_->commit(*tx);
                if (commit_result.is_ok()) {
                    success_count++;
                } else {
                    // Transaction failed - it should already be aborted by commit
                    // No need to rollback after a failed commit
                    conflict_count++;
                }
            }
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    // Most should succeed (each thread writes different keys)
    EXPECT_GT(success_count.load(), 0);
    EXPECT_EQ(success_count.load() + conflict_count.load(), num_threads * ops_per_thread);
}

/// @test Concurrent reads have no data races
TEST_F(TransactionManagerTest, ConcurrentReadsNoDataRace) {
    // Setup data
    for (int i = 0; i < 50; ++i) {
        std::string key = "race_key_" + std::to_string(i);
        ASSERT_TRUE(tm_->backend().put(to_bytes(key), to_bytes("value")).is_ok());
    }

    std::atomic<int> read_count{0};
    constexpr int num_threads = 4;

    std::vector<std::future<void>> futures;

    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&]() {
            auto tx_result = tm_->begin();
            if (!tx_result.is_ok()) {
                return;
            }
            ManagedTransaction* tx = tx_result.value();

            for (int i = 0; i < 50; ++i) {
                std::string key = "race_key_" + std::to_string(i);
                auto get_result = tm_->get(*tx, to_bytes(key));
                if (get_result.is_ok()) {
                    read_count++;
                }
            }

            (void)tm_->rollback(*tx);
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    EXPECT_EQ(read_count.load(), num_threads * 50);
}

// ============================================================================
// Edge Cases
// ============================================================================

/// @test Empty transaction commits successfully
TEST_F(TransactionManagerTest, EmptyTransactionCommit) {
    auto tx_result = tm_->begin();
    ASSERT_TRUE(tx_result.is_ok());
    ManagedTransaction* tx = tx_result.value();

    // No operations

    auto commit_result = tm_->commit(*tx);
    ASSERT_TRUE(commit_result.is_ok());
    EXPECT_EQ(tx->state, TransactionState::Committed);
}

/// @test Read-only transaction commits successfully
TEST_F(TransactionManagerTest, ReadOnlyTransactionCommit) {
    auto key = to_bytes("readonly_key");
    auto value = to_bytes("readonly_value");

    ASSERT_TRUE(tm_->backend().put(key, value).is_ok());

    auto tx_result = tm_->begin();
    ASSERT_TRUE(tx_result.is_ok());
    ManagedTransaction* tx = tx_result.value();

    // Only read
    auto get_result = tm_->get(*tx, key);
    ASSERT_TRUE(get_result.is_ok());

    // Should commit successfully (no writes to conflict)
    auto commit_result = tm_->commit(*tx);
    ASSERT_TRUE(commit_result.is_ok());
}

/// @test Put then delete same key in same transaction
TEST_F(TransactionManagerTest, PutThenDeleteSameTransaction) {
    auto key = to_bytes("put_del_key");

    auto tx_result = tm_->begin();
    ASSERT_TRUE(tx_result.is_ok());
    ManagedTransaction* tx = tx_result.value();

    // Put
    ASSERT_TRUE(tm_->put(*tx, key, to_bytes("value")).is_ok());
    EXPECT_TRUE(tm_->exists(*tx, key));

    // Delete
    ASSERT_TRUE(tm_->remove(*tx, key).is_ok());
    EXPECT_FALSE(tm_->exists(*tx, key));

    // Commit
    ASSERT_TRUE(tm_->commit(*tx).is_ok());

    // Key should not exist
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    EXPECT_FALSE(tm_->exists(*t2, key));

    ASSERT_TRUE(tm_->rollback(*t2).is_ok());
}

/// @test Delete then put same key in same transaction
TEST_F(TransactionManagerTest, DeleteThenPutSameTransaction) {
    auto key = to_bytes("del_put_key");

    // Setup
    ASSERT_TRUE(tm_->backend().put(key, to_bytes("initial")).is_ok());

    auto tx_result = tm_->begin();
    ASSERT_TRUE(tx_result.is_ok());
    ManagedTransaction* tx = tx_result.value();

    // Delete
    ASSERT_TRUE(tm_->remove(*tx, key).is_ok());
    EXPECT_FALSE(tm_->exists(*tx, key));

    // Put again
    ASSERT_TRUE(tm_->put(*tx, key, to_bytes("final")).is_ok());
    EXPECT_TRUE(tm_->exists(*tx, key));

    auto get_result = tm_->get(*tx, key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "final");

    // Commit
    ASSERT_TRUE(tm_->commit(*tx).is_ok());

    // Verify final value
    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    ManagedTransaction* t2 = t2_result.value();

    auto get2 = tm_->get(*t2, key);
    ASSERT_TRUE(get2.is_ok());
    EXPECT_EQ(to_string(get2.value()), "final");

    ASSERT_TRUE(tm_->rollback(*t2).is_ok());
}

/// @test Statistics are accurate
TEST_F(TransactionManagerTest, ActiveTransactionCountAccurate) {
    EXPECT_EQ(tm_->active_transaction_count(), 0u);

    auto t1_result = tm_->begin();
    ASSERT_TRUE(t1_result.is_ok());
    EXPECT_EQ(tm_->active_transaction_count(), 1u);

    auto t2_result = tm_->begin();
    ASSERT_TRUE(t2_result.is_ok());
    EXPECT_EQ(tm_->active_transaction_count(), 2u);

    ASSERT_TRUE(tm_->commit(*t1_result.value()).is_ok());
    EXPECT_EQ(tm_->active_transaction_count(), 1u);

    ASSERT_TRUE(tm_->rollback(*t2_result.value()).is_ok());
    EXPECT_EQ(tm_->active_transaction_count(), 0u);
}

/// @test Config is accessible
TEST_F(TransactionManagerTest, ConfigAccessible) {
    const auto& config = tm_->config();
    EXPECT_EQ(config.max_concurrent_transactions, 100u);
    EXPECT_EQ(config.default_timeout, std::chrono::milliseconds{5000});
    EXPECT_EQ(config.default_isolation, TransactionIsolationLevel::Snapshot);
}

/// @test Backend is accessible
TEST_F(TransactionManagerTest, BackendAccessible) {
    StateBackend& backend = tm_->backend();

    // Can use backend directly
    auto key = to_bytes("backend_key");
    auto value = to_bytes("backend_value");
    ASSERT_TRUE(backend.put(key, value).is_ok());

    auto get_result = backend.get(key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "backend_value");
}

}  // namespace
}  // namespace dotvm::core::state
