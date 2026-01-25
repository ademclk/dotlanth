/// @file wal_benchmark_test.cpp
/// @brief Performance benchmarks for STATE-007 WAL implementation
///
/// Validates that WAL meets the >50K records/sec append throughput target.

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>

#include "dotvm/core/state/state_backend.hpp"
#include "dotvm/core/state/wal_backend.hpp"
#include "dotvm/core/state/write_ahead_log.hpp"

namespace dotvm::core::state {
namespace {

/// @brief Target throughput: 50,000 records per second
constexpr std::size_t TARGET_RECORDS_PER_SEC = 50000;

/// @brief Number of records to write in benchmark
constexpr std::size_t BENCHMARK_RECORD_COUNT = 100000;

/// @brief Key size in bytes
constexpr std::size_t KEY_SIZE = 32;

/// @brief Value size in bytes
constexpr std::size_t VALUE_SIZE = 256;

/// @brief Generate random bytes
[[nodiscard]] std::vector<std::byte> random_bytes(std::size_t size, std::mt19937& rng) {
    std::vector<std::byte> bytes(size);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : bytes) {
        b = static_cast<std::byte>(dist(rng));
    }
    return bytes;
}

/// @brief Test fixture for WAL benchmarks
class WalBenchmarkTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() /
                    ("wal_benchmark_test_" + std::to_string(test_counter_++));
        std::filesystem::create_directories(test_dir_);

        // Seed RNG for reproducibility
        rng_.seed(42);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    WalConfig default_wal_config() {
        WalConfig config;
        config.wal_directory = test_dir_;
        config.segment_size = 64 * 1024 * 1024;  // 64MB
        config.buffer_size = 64 * 1024;           // 64KB buffer for performance
        config.sync_policy = WalSyncPolicy::Manual;  // No auto-sync for raw throughput
        return config;
    }

    WalBackendConfig default_backend_config() {
        WalBackendConfig config;
        config.wal_config = default_wal_config();
        return config;
    }

    /// @brief Generate test data
    void generate_test_data(std::size_t count) {
        keys_.clear();
        values_.clear();
        keys_.reserve(count);
        values_.reserve(count);

        for (std::size_t i = 0; i < count; ++i) {
            keys_.push_back(random_bytes(KEY_SIZE, rng_));
            values_.push_back(random_bytes(VALUE_SIZE, rng_));
        }
    }

    std::filesystem::path test_dir_;
    static inline int test_counter_ = 0;
    std::mt19937 rng_;
    std::vector<std::vector<std::byte>> keys_;
    std::vector<std::vector<std::byte>> values_;
};

// ============================================================================
// Raw WriteAheadLog Throughput Tests
// ============================================================================

TEST_F(WalBenchmarkTest, AppendThroughputMeetsTarget) {
    // Create WAL with optimized config
    auto wal_result = WriteAheadLog::create(default_wal_config());
    ASSERT_TRUE(wal_result.is_ok()) << "Failed to create WAL";
    auto& wal = wal_result.value();

    // Generate test data
    generate_test_data(BENCHMARK_RECORD_COUNT);

    // Measure append throughput (no sync)
    auto start = std::chrono::high_resolution_clock::now();

    for (std::size_t i = 0; i < BENCHMARK_RECORD_COUNT; ++i) {
        auto result = wal->append(
            LogRecordType::Put,
            keys_[i],
            values_[i],
            TxId{0, 0});
        ASSERT_TRUE(result.is_ok()) << "Append failed at record " << i;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Calculate throughput
    double seconds = static_cast<double>(duration.count()) / 1'000'000.0;
    double records_per_sec = static_cast<double>(BENCHMARK_RECORD_COUNT) / seconds;

    std::cout << "\n=== WAL Append Throughput (No Sync) ===" << std::endl;
    std::cout << "Records:    " << BENCHMARK_RECORD_COUNT << std::endl;
    std::cout << "Duration:   " << duration.count() << " us (" << seconds << " s)" << std::endl;
    std::cout << "Throughput: " << static_cast<std::size_t>(records_per_sec) << " rec/sec" << std::endl;
    std::cout << "Target:     " << TARGET_RECORDS_PER_SEC << " rec/sec" << std::endl;
    std::cout << "========================================\n" << std::endl;

    EXPECT_GE(records_per_sec, TARGET_RECORDS_PER_SEC)
        << "Throughput " << records_per_sec << " rec/sec is below target "
        << TARGET_RECORDS_PER_SEC << " rec/sec";
}

TEST_F(WalBenchmarkTest, AppendWithPeriodicSyncThroughput) {
    // Create WAL with periodic sync
    WalConfig config = default_wal_config();
    config.sync_policy = WalSyncPolicy::EveryNRecords;
    config.sync_every_n_records = 1000;  // Sync every 1000 records

    auto wal_result = WriteAheadLog::create(config);
    ASSERT_TRUE(wal_result.is_ok()) << "Failed to create WAL";
    auto& wal = wal_result.value();

    // Generate test data
    generate_test_data(BENCHMARK_RECORD_COUNT);

    // Measure append throughput with periodic sync
    auto start = std::chrono::high_resolution_clock::now();

    for (std::size_t i = 0; i < BENCHMARK_RECORD_COUNT; ++i) {
        auto result = wal->append(
            LogRecordType::Put,
            keys_[i],
            values_[i],
            TxId{0, 0});
        ASSERT_TRUE(result.is_ok()) << "Append failed at record " << i;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Calculate throughput
    double seconds = static_cast<double>(duration.count()) / 1'000'000.0;
    double records_per_sec = static_cast<double>(BENCHMARK_RECORD_COUNT) / seconds;

    std::cout << "\n=== WAL Append Throughput (Sync every 1000) ===" << std::endl;
    std::cout << "Records:    " << BENCHMARK_RECORD_COUNT << std::endl;
    std::cout << "Duration:   " << duration.count() << " us (" << seconds << " s)" << std::endl;
    std::cout << "Throughput: " << static_cast<std::size_t>(records_per_sec) << " rec/sec" << std::endl;
    std::cout << "Syncs:      " << (BENCHMARK_RECORD_COUNT / 1000) << std::endl;
    std::cout << "===============================================\n" << std::endl;

    // With periodic sync, we accept 50% of target throughput
    // (real-world durable writes have latency)
    EXPECT_GE(records_per_sec, TARGET_RECORDS_PER_SEC / 2)
        << "Throughput " << records_per_sec << " rec/sec is below minimum "
        << (TARGET_RECORDS_PER_SEC / 2) << " rec/sec";
}

// ============================================================================
// WalBackend Integration Throughput Tests
// ============================================================================

TEST_F(WalBenchmarkTest, WalBackendPutThroughput) {
    // Create WalBackend
    auto inner = create_state_backend();
    auto result = WalBackend::create(std::move(inner), default_backend_config());
    ASSERT_TRUE(result.is_ok()) << "Failed to create WalBackend";
    auto& backend = result.value();

    // Generate test data
    generate_test_data(BENCHMARK_RECORD_COUNT);

    // Measure put throughput
    auto start = std::chrono::high_resolution_clock::now();

    for (std::size_t i = 0; i < BENCHMARK_RECORD_COUNT; ++i) {
        auto put_result = backend->put(keys_[i], values_[i]);
        ASSERT_TRUE(put_result.is_ok()) << "Put failed at record " << i;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Calculate throughput
    double seconds = static_cast<double>(duration.count()) / 1'000'000.0;
    double records_per_sec = static_cast<double>(BENCHMARK_RECORD_COUNT) / seconds;

    std::cout << "\n=== WalBackend Put Throughput ===" << std::endl;
    std::cout << "Records:    " << BENCHMARK_RECORD_COUNT << std::endl;
    std::cout << "Duration:   " << duration.count() << " us (" << seconds << " s)" << std::endl;
    std::cout << "Throughput: " << static_cast<std::size_t>(records_per_sec) << " rec/sec" << std::endl;
    std::cout << "Target:     " << TARGET_RECORDS_PER_SEC << " rec/sec" << std::endl;
    std::cout << "=================================\n" << std::endl;

    EXPECT_GE(records_per_sec, TARGET_RECORDS_PER_SEC)
        << "Throughput " << records_per_sec << " rec/sec is below target "
        << TARGET_RECORDS_PER_SEC << " rec/sec";
}

// ============================================================================
// Recovery Performance Tests
// ============================================================================

TEST_F(WalBenchmarkTest, RecoveryThroughput) {
    constexpr std::size_t RECOVERY_RECORD_COUNT = 50000;

    // Write data to WAL
    {
        auto inner = create_state_backend();
        auto result = WalBackend::create(std::move(inner), default_backend_config());
        ASSERT_TRUE(result.is_ok());
        auto& backend = result.value();

        generate_test_data(RECOVERY_RECORD_COUNT);

        for (std::size_t i = 0; i < RECOVERY_RECORD_COUNT; ++i) {
            ASSERT_TRUE(backend->put(keys_[i], values_[i]).is_ok());
        }

        ASSERT_TRUE(backend->sync_wal().is_ok());
    }

    // Measure recovery time
    auto start = std::chrono::high_resolution_clock::now();

    auto inner = create_state_backend();
    auto result = WalBackend::open(std::move(inner), test_dir_);
    ASSERT_TRUE(result.is_ok());
    auto& backend = result.value();

    auto recover_result = backend->recover();
    ASSERT_TRUE(recover_result.is_ok());

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Verify data integrity
    EXPECT_EQ(backend->key_count(), RECOVERY_RECORD_COUNT);

    // Calculate recovery throughput
    double seconds = static_cast<double>(duration.count()) / 1'000'000.0;
    double records_per_sec = static_cast<double>(RECOVERY_RECORD_COUNT) / seconds;

    std::cout << "\n=== WAL Recovery Throughput ===" << std::endl;
    std::cout << "Records:    " << RECOVERY_RECORD_COUNT << std::endl;
    std::cout << "Duration:   " << duration.count() << " us (" << seconds << " s)" << std::endl;
    std::cout << "Throughput: " << static_cast<std::size_t>(records_per_sec) << " rec/sec" << std::endl;
    std::cout << "===============================\n" << std::endl;

    // Recovery should be at least as fast as writes (no fdatasync needed)
    EXPECT_GE(records_per_sec, TARGET_RECORDS_PER_SEC)
        << "Recovery throughput " << records_per_sec << " rec/sec is below target";
}

// ============================================================================
// Batch Operation Throughput Tests
// ============================================================================

TEST_F(WalBenchmarkTest, BatchOperationThroughput) {
    constexpr std::size_t BATCH_SIZE = 1000;
    constexpr std::size_t NUM_BATCHES = 100;
    constexpr std::size_t TOTAL_RECORDS = BATCH_SIZE * NUM_BATCHES;

    // Create WalBackend
    auto inner = create_state_backend();
    auto result = WalBackend::create(std::move(inner), default_backend_config());
    ASSERT_TRUE(result.is_ok());
    auto& backend = result.value();

    // Generate test data
    generate_test_data(TOTAL_RECORDS);

    // Measure batch throughput
    auto start = std::chrono::high_resolution_clock::now();

    for (std::size_t batch = 0; batch < NUM_BATCHES; ++batch) {
        std::vector<BatchOp> ops;
        ops.reserve(BATCH_SIZE);

        for (std::size_t i = 0; i < BATCH_SIZE; ++i) {
            std::size_t idx = batch * BATCH_SIZE + i;
            ops.push_back(BatchOp{
                .type = BatchOpType::Put,
                .key = keys_[idx],
                .value = values_[idx]
            });
        }

        auto batch_result = backend->batch(ops);
        ASSERT_TRUE(batch_result.is_ok()) << "Batch " << batch << " failed";
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Calculate throughput
    double seconds = static_cast<double>(duration.count()) / 1'000'000.0;
    double records_per_sec = static_cast<double>(TOTAL_RECORDS) / seconds;

    std::cout << "\n=== WalBackend Batch Throughput ===" << std::endl;
    std::cout << "Records:    " << TOTAL_RECORDS << std::endl;
    std::cout << "Batch size: " << BATCH_SIZE << std::endl;
    std::cout << "Batches:    " << NUM_BATCHES << std::endl;
    std::cout << "Duration:   " << duration.count() << " us (" << seconds << " s)" << std::endl;
    std::cout << "Throughput: " << static_cast<std::size_t>(records_per_sec) << " rec/sec" << std::endl;
    std::cout << "===================================\n" << std::endl;

    EXPECT_GE(records_per_sec, TARGET_RECORDS_PER_SEC)
        << "Batch throughput " << records_per_sec << " rec/sec is below target";
}

// ============================================================================
// Variable Record Size Tests
// ============================================================================

TEST_F(WalBenchmarkTest, SmallRecordThroughput) {
    constexpr std::size_t SMALL_KEY_SIZE = 8;
    constexpr std::size_t SMALL_VALUE_SIZE = 32;
    constexpr std::size_t SMALL_RECORD_COUNT = 200000;

    auto wal_result = WriteAheadLog::create(default_wal_config());
    ASSERT_TRUE(wal_result.is_ok());
    auto& wal = wal_result.value();

    // Generate small records
    std::vector<std::vector<std::byte>> small_keys;
    std::vector<std::vector<std::byte>> small_values;
    small_keys.reserve(SMALL_RECORD_COUNT);
    small_values.reserve(SMALL_RECORD_COUNT);

    for (std::size_t i = 0; i < SMALL_RECORD_COUNT; ++i) {
        small_keys.push_back(random_bytes(SMALL_KEY_SIZE, rng_));
        small_values.push_back(random_bytes(SMALL_VALUE_SIZE, rng_));
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (std::size_t i = 0; i < SMALL_RECORD_COUNT; ++i) {
        auto result = wal->append(
            LogRecordType::Put,
            small_keys[i],
            small_values[i],
            TxId{0, 0});
        ASSERT_TRUE(result.is_ok());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double seconds = static_cast<double>(duration.count()) / 1'000'000.0;
    double records_per_sec = static_cast<double>(SMALL_RECORD_COUNT) / seconds;

    std::cout << "\n=== Small Record Throughput (8B key + 32B value) ===" << std::endl;
    std::cout << "Records:    " << SMALL_RECORD_COUNT << std::endl;
    std::cout << "Duration:   " << duration.count() << " us (" << seconds << " s)" << std::endl;
    std::cout << "Throughput: " << static_cast<std::size_t>(records_per_sec) << " rec/sec" << std::endl;
    std::cout << "===================================================\n" << std::endl;

    // Small records should have higher throughput
    EXPECT_GE(records_per_sec, TARGET_RECORDS_PER_SEC * 1.5)
        << "Small record throughput below expected";
}

TEST_F(WalBenchmarkTest, LargeRecordThroughput) {
    constexpr std::size_t LARGE_KEY_SIZE = 64;
    constexpr std::size_t LARGE_VALUE_SIZE = 4096;
    constexpr std::size_t LARGE_RECORD_COUNT = 25000;

    auto wal_result = WriteAheadLog::create(default_wal_config());
    ASSERT_TRUE(wal_result.is_ok());
    auto& wal = wal_result.value();

    // Generate large records
    std::vector<std::vector<std::byte>> large_keys;
    std::vector<std::vector<std::byte>> large_values;
    large_keys.reserve(LARGE_RECORD_COUNT);
    large_values.reserve(LARGE_RECORD_COUNT);

    for (std::size_t i = 0; i < LARGE_RECORD_COUNT; ++i) {
        large_keys.push_back(random_bytes(LARGE_KEY_SIZE, rng_));
        large_values.push_back(random_bytes(LARGE_VALUE_SIZE, rng_));
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (std::size_t i = 0; i < LARGE_RECORD_COUNT; ++i) {
        auto result = wal->append(
            LogRecordType::Put,
            large_keys[i],
            large_values[i],
            TxId{0, 0});
        ASSERT_TRUE(result.is_ok());
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double seconds = static_cast<double>(duration.count()) / 1'000'000.0;
    double records_per_sec = static_cast<double>(LARGE_RECORD_COUNT) / seconds;
    double mb_per_sec = static_cast<double>(LARGE_RECORD_COUNT * (LARGE_KEY_SIZE + LARGE_VALUE_SIZE)) / (seconds * 1024.0 * 1024.0);

    std::cout << "\n=== Large Record Throughput (64B key + 4KB value) ===" << std::endl;
    std::cout << "Records:    " << LARGE_RECORD_COUNT << std::endl;
    std::cout << "Duration:   " << duration.count() << " us (" << seconds << " s)" << std::endl;
    std::cout << "Throughput: " << static_cast<std::size_t>(records_per_sec) << " rec/sec" << std::endl;
    std::cout << "Data rate:  " << mb_per_sec << " MB/sec" << std::endl;
    std::cout << "====================================================\n" << std::endl;

    // Large records will have lower rec/sec but higher MB/sec
    // At least 5K rec/sec for 4KB records (conservative for CI environments)
    EXPECT_GE(records_per_sec, 5000)
        << "Large record throughput too low";
}

}  // namespace
}  // namespace dotvm::core::state
