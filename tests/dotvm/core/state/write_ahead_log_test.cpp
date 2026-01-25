/// @file write_ahead_log_test.cpp
/// @brief Unit tests for STATE-007 WriteAheadLog
///
/// TDD tests for WriteAheadLog class - written before implementation.

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "dotvm/core/state/write_ahead_log.hpp"

namespace dotvm::core::state {
namespace {

/// @brief Test fixture that creates a temporary directory for WAL files
class WriteAheadLogTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique temp directory for each test
        test_dir_ = std::filesystem::temp_directory_path() /
                    ("wal_test_" +
                     std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
                     "_" + std::to_string(test_counter_++));
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override { std::filesystem::remove_all(test_dir_); }

    WalConfig default_config() {
        WalConfig config;
        config.wal_directory = test_dir_;
        config.segment_size = 64 * 1024;  // 64KB for tests
        config.buffer_size = 4096;
        config.sync_policy = WalSyncPolicy::EveryCommit;
        return config;
    }

    std::filesystem::path test_dir_;
    static inline int test_counter_ = 0;
};

// ============================================================================
// WalConfig Tests
// ============================================================================

TEST(WalConfigTest, DefaultsAreValid) {
    auto config = WalConfig::defaults();

    EXPECT_FALSE(config.wal_directory.empty());
    EXPECT_GT(config.segment_size, 0);
    EXPECT_GT(config.buffer_size, 0);
    EXPECT_TRUE(config.is_valid());
}

TEST(WalConfigTest, InvalidEmptyDirectory) {
    WalConfig config = WalConfig::defaults();
    config.wal_directory = "";

    EXPECT_FALSE(config.is_valid());
}

TEST(WalConfigTest, InvalidZeroSegmentSize) {
    WalConfig config = WalConfig::defaults();
    config.segment_size = 0;

    EXPECT_FALSE(config.is_valid());
}

TEST(WalConfigTest, InvalidZeroBufferSize) {
    WalConfig config = WalConfig::defaults();
    config.buffer_size = 0;

    EXPECT_FALSE(config.is_valid());
}

TEST(WalConfigTest, SyncPolicyValues) {
    EXPECT_EQ(static_cast<int>(WalSyncPolicy::None), 0);
    EXPECT_EQ(static_cast<int>(WalSyncPolicy::EveryCommit), 1);
    EXPECT_EQ(static_cast<int>(WalSyncPolicy::EveryNRecords), 2);
    EXPECT_EQ(static_cast<int>(WalSyncPolicy::Periodic), 3);
}

// ============================================================================
// WriteAheadLog Creation Tests
// ============================================================================

TEST_F(WriteAheadLogTest, CreateWithValidConfig) {
    auto result = WriteAheadLog::create(default_config());

    ASSERT_TRUE(result.is_ok()) << "Failed: " << to_string(result.error());
    EXPECT_NE(result.value(), nullptr);
}

TEST_F(WriteAheadLogTest, CreateWithInvalidConfigReturnsError) {
    WalConfig config;
    config.wal_directory = "";
    config.segment_size = 0;

    auto result = WriteAheadLog::create(config);

    EXPECT_TRUE(result.is_err());
}

TEST_F(WriteAheadLogTest, CreateCreatesDirectory) {
    std::filesystem::path new_dir = test_dir_ / "subdir" / "wal";
    WalConfig config = default_config();
    config.wal_directory = new_dir;

    auto result = WriteAheadLog::create(config);

    ASSERT_TRUE(result.is_ok()) << "Failed: " << to_string(result.error());
    EXPECT_TRUE(std::filesystem::exists(new_dir));
}

// ============================================================================
// LSN Tests
// ============================================================================

TEST_F(WriteAheadLogTest, InitialLsnIsFirst) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    EXPECT_EQ(wal->current_lsn(), LSN::first());
}

TEST_F(WriteAheadLogTest, AppendIncrementsLsn) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    std::vector<std::byte> key = {std::byte{'k'}};
    std::vector<std::byte> value = {std::byte{'v'}};
    TxId tx{.id = 1, .generation = 0};

    auto append_result = wal->append(LogRecordType::Put, key, value, tx);
    ASSERT_TRUE(append_result.is_ok());

    EXPECT_EQ(append_result.value().value, 1);
    EXPECT_EQ(wal->current_lsn().value, 2);
}

// ============================================================================
// Append Tests
// ============================================================================

TEST_F(WriteAheadLogTest, AppendPutRecord) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    std::vector<std::byte> key = {std::byte{'k'}, std::byte{'e'}, std::byte{'y'}};
    std::vector<std::byte> value = {std::byte{'v'}, std::byte{'a'}, std::byte{'l'}};
    TxId tx{.id = 42, .generation = 1};

    auto append_result = wal->append(LogRecordType::Put, key, value, tx);

    ASSERT_TRUE(append_result.is_ok());
    EXPECT_EQ(append_result.value().value, 1);
}

TEST_F(WriteAheadLogTest, AppendDeleteRecord) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    std::vector<std::byte> key = {std::byte{'k'}};
    TxId tx{.id = 1, .generation = 0};

    auto append_result = wal->append(LogRecordType::Delete, key, {}, tx);

    ASSERT_TRUE(append_result.is_ok());
}

TEST_F(WriteAheadLogTest, AppendTxBeginRecord) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    TxId tx{.id = 5, .generation = 0};

    auto append_result = wal->append(LogRecordType::TxBegin, {}, {}, tx);

    ASSERT_TRUE(append_result.is_ok());
}

TEST_F(WriteAheadLogTest, AppendMultipleRecords) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    TxId tx{.id = 1, .generation = 0};

    // Append 100 records
    for (int i = 0; i < 100; ++i) {
        std::vector<std::byte> key = {static_cast<std::byte>(i)};
        std::vector<std::byte> value = {static_cast<std::byte>(i * 2)};

        auto append_result = wal->append(LogRecordType::Put, key, value, tx);
        ASSERT_TRUE(append_result.is_ok()) << "Failed at record " << i;
        EXPECT_EQ(append_result.value().value, static_cast<std::uint64_t>(i + 1));
    }

    EXPECT_EQ(wal->current_lsn().value, 101);
}

// ============================================================================
// Sync Tests
// ============================================================================

TEST_F(WriteAheadLogTest, SyncFlushesBuffer) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    std::vector<std::byte> key = {std::byte{'k'}};
    std::vector<std::byte> value = {std::byte{'v'}};
    TxId tx{.id = 1, .generation = 0};

    ASSERT_TRUE(wal->append(LogRecordType::Put, key, value, tx).is_ok());

    auto sync_result = wal->sync();
    ASSERT_TRUE(sync_result.is_ok());

    // After sync, last_synced_lsn should match current
    EXPECT_EQ(wal->last_synced_lsn().value, 1);
}

TEST_F(WriteAheadLogTest, SyncEmptyBufferSucceeds) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    auto sync_result = wal->sync();
    EXPECT_TRUE(sync_result.is_ok());
}

// ============================================================================
// Recovery Tests
// ============================================================================

TEST_F(WriteAheadLogTest, RecoverEmptyWal) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    auto sync_result = wal->sync();
    ASSERT_TRUE(sync_result.is_ok());

    // Re-open the WAL
    auto open_result = WriteAheadLog::open(test_dir_);
    ASSERT_TRUE(open_result.is_ok());
    auto& wal2 = open_result.value();

    auto recover_result = wal2->recover();
    ASSERT_TRUE(recover_result.is_ok());
    EXPECT_TRUE(recover_result.value().empty());
}

TEST_F(WriteAheadLogTest, RecoverSingleRecord) {
    std::vector<std::byte> key = {std::byte{'k'}};
    std::vector<std::byte> value = {std::byte{'v'}};
    TxId tx{.id = 1, .generation = 0};

    // Write and sync
    {
        auto result = WriteAheadLog::create(default_config());
        ASSERT_TRUE(result.is_ok());
        auto& wal = result.value();

        ASSERT_TRUE(wal->append(LogRecordType::Put, key, value, tx).is_ok());
        ASSERT_TRUE(wal->sync().is_ok());
    }

    // Re-open and recover
    {
        auto open_result = WriteAheadLog::open(test_dir_);
        ASSERT_TRUE(open_result.is_ok());
        auto& wal = open_result.value();

        auto recover_result = wal->recover();
        ASSERT_TRUE(recover_result.is_ok());

        auto& records = recover_result.value();
        ASSERT_EQ(records.size(), 1);
        EXPECT_EQ(records[0].type, LogRecordType::Put);
        EXPECT_EQ(records[0].key, key);
        EXPECT_EQ(records[0].value, value);
    }
}

TEST_F(WriteAheadLogTest, RecoverMultipleRecords) {
    constexpr int NUM_RECORDS = 50;
    TxId tx{.id = 1, .generation = 0};

    // Write records
    {
        auto result = WriteAheadLog::create(default_config());
        ASSERT_TRUE(result.is_ok());
        auto& wal = result.value();

        for (int i = 0; i < NUM_RECORDS; ++i) {
            std::vector<std::byte> key = {static_cast<std::byte>(i)};
            std::vector<std::byte> value = {static_cast<std::byte>(i * 2)};
            ASSERT_TRUE(wal->append(LogRecordType::Put, key, value, tx).is_ok());
        }
        ASSERT_TRUE(wal->sync().is_ok());
    }

    // Recover
    {
        auto open_result = WriteAheadLog::open(test_dir_);
        ASSERT_TRUE(open_result.is_ok());
        auto& wal = open_result.value();

        auto recover_result = wal->recover();
        ASSERT_TRUE(recover_result.is_ok());

        auto& records = recover_result.value();
        EXPECT_EQ(records.size(), NUM_RECORDS);

        for (int i = 0; i < NUM_RECORDS; ++i) {
            EXPECT_EQ(records[static_cast<std::size_t>(i)].lsn.value,
                      static_cast<std::uint64_t>(i + 1));
            EXPECT_EQ(records[static_cast<std::size_t>(i)].key[0], static_cast<std::byte>(i));
        }
    }
}

TEST_F(WriteAheadLogTest, RecoverContinuesLsn) {
    TxId tx{.id = 1, .generation = 0};

    // Write 10 records
    {
        auto result = WriteAheadLog::create(default_config());
        ASSERT_TRUE(result.is_ok());
        auto& wal = result.value();

        for (int i = 0; i < 10; ++i) {
            std::vector<std::byte> key = {static_cast<std::byte>(i)};
            ASSERT_TRUE(wal->append(LogRecordType::Put, key, {}, tx).is_ok());
        }
        ASSERT_TRUE(wal->sync().is_ok());
    }

    // Re-open and verify LSN continues
    {
        auto open_result = WriteAheadLog::open(test_dir_);
        ASSERT_TRUE(open_result.is_ok());
        auto& wal = open_result.value();

        ASSERT_TRUE(wal->recover().is_ok());

        // Next LSN should be 11
        EXPECT_EQ(wal->current_lsn().value, 11);

        // Write another record
        std::vector<std::byte> key = {std::byte{0xFF}};
        auto append_result = wal->append(LogRecordType::Put, key, {}, tx);
        ASSERT_TRUE(append_result.is_ok());
        EXPECT_EQ(append_result.value().value, 11);
    }
}

// ============================================================================
// Truncate Tests
// ============================================================================

TEST_F(WriteAheadLogTest, TruncateBeforeRemovesOldRecords) {
    TxId tx{.id = 1, .generation = 0};

    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    // Write 10 records
    for (int i = 0; i < 10; ++i) {
        std::vector<std::byte> key = {static_cast<std::byte>(i)};
        ASSERT_TRUE(wal->append(LogRecordType::Put, key, {}, tx).is_ok());
    }
    ASSERT_TRUE(wal->sync().is_ok());

    // Truncate before LSN 6 (keeps records 6-10)
    auto truncate_result = wal->truncate_before(LSN{6});
    ASSERT_TRUE(truncate_result.is_ok());

    // Recover should only return records 6-10
    auto recover_result = wal->recover();
    ASSERT_TRUE(recover_result.is_ok());

    auto& records = recover_result.value();
    EXPECT_GE(records.size(), 5);  // At least records 6-10 should remain
    if (!records.empty()) {
        EXPECT_GE(records[0].lsn.value, 6);
    }
}

// ============================================================================
// Checkpoint Tests
// ============================================================================

TEST_F(WriteAheadLogTest, CheckpointCreatesRecord) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    TxId tx{.id = 1, .generation = 0};

    // Write some records
    for (int i = 0; i < 5; ++i) {
        std::vector<std::byte> key = {static_cast<std::byte>(i)};
        ASSERT_TRUE(wal->append(LogRecordType::Put, key, {}, tx).is_ok());
    }

    // Create checkpoint
    auto checkpoint_result = wal->checkpoint();
    ASSERT_TRUE(checkpoint_result.is_ok());

    auto& info = checkpoint_result.value();
    EXPECT_EQ(info.checkpoint_lsn.value, 5);  // Checkpoint at last synced record
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(WriteAheadLogTest, OpenNonExistentDirectoryFails) {
    auto result = WriteAheadLog::open("/nonexistent/path/to/wal");
    EXPECT_TRUE(result.is_err());
}

// ============================================================================
// Concurrent Access Tests (Basic)
// ============================================================================

TEST_F(WriteAheadLogTest, AppendIsThreadSafe) {
    auto result = WriteAheadLog::create(default_config());
    ASSERT_TRUE(result.is_ok());
    auto& wal = result.value();

    constexpr int NUM_THREADS = 4;
    constexpr int RECORDS_PER_THREAD = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&wal, &success_count, t]() {
            TxId tx{.id = static_cast<std::uint64_t>(t), .generation = 0};
            for (int i = 0; i < RECORDS_PER_THREAD; ++i) {
                std::vector<std::byte> key = {static_cast<std::byte>(t), static_cast<std::byte>(i)};
                auto append_result = wal->append(LogRecordType::Put, key, {}, tx);
                if (append_result.is_ok()) {
                    ++success_count;
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), NUM_THREADS * RECORDS_PER_THREAD);
}

}  // namespace
}  // namespace dotvm::core::state
