/// @file snapshot_benchmark_test.cpp
/// @brief Performance benchmarks for STATE-009 SnapshotManager
///
/// Validates snapshot performance targets:
/// - create_snapshot(): <100 us
/// - release_snapshot(): <50 us
/// - get() at snapshot: <2x direct get
/// - 10 concurrent snapshots: No contention

#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/snapshot_manager.hpp"
#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {
namespace {

// Detect sanitizer builds to skip throughput assertions
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr bool kRunningUnderSanitizer = true;
#elif defined(__has_feature)
    #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
constexpr bool kRunningUnderSanitizer = true;
    #else
constexpr bool kRunningUnderSanitizer = false;
    #endif
#else
constexpr bool kRunningUnderSanitizer = false;
#endif

/// @brief Target latency for create_snapshot(): 100 microseconds
constexpr std::int64_t TARGET_CREATE_LATENCY_US = 100;

/// @brief Target latency for release_snapshot(): 50 microseconds
constexpr std::int64_t TARGET_RELEASE_LATENCY_US = 50;

/// @brief Number of keys for benchmarking
constexpr std::size_t BENCHMARK_KEY_COUNT = 10000;

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

/// @brief Measure execution time in microseconds
template <typename F>
[[nodiscard]] std::int64_t measure_us(F&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}

/// @brief Test fixture for snapshot benchmarks
class SnapshotBenchmarkTest : public ::testing::Test {
protected:
    void SetUp() override {
        StateBackendConfig backend_config;
        backend_config.enable_transactions = true;

        SnapshotManagerConfig sm_config;
        sm_config.max_concurrent_snapshots = 10;

        auto backend = create_state_backend(backend_config);
        sm_ = std::make_unique<SnapshotManager>(std::move(backend), sm_config);

        rng_.seed(42);
    }

    void populate_data(std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            auto key = random_bytes(KEY_SIZE, rng_);
            auto value = random_bytes(VALUE_SIZE, rng_);
            keys_.push_back(std::move(key));
            values_.push_back(std::move(value));
            ASSERT_TRUE(sm_->backend().put(keys_.back(), values_.back()).is_ok());
        }
    }

    std::unique_ptr<SnapshotManager> sm_;
    std::mt19937 rng_;
    std::vector<std::vector<std::byte>> keys_;
    std::vector<std::vector<std::byte>> values_;
};

// ============================================================================
// Snapshot Latency Tests
// ============================================================================

TEST_F(SnapshotBenchmarkTest, CreateSnapshotLatency) {
    // Populate some data first
    populate_data(1000);

    // Warm up
    for (int i = 0; i < 5; ++i) {
        auto snap = sm_->create_snapshot();
        ASSERT_TRUE(snap.is_ok());
        ASSERT_TRUE(sm_->release_snapshot(snap.value()).is_ok());
    }

    // Measure create latency over multiple iterations
    constexpr int iterations = 100;
    std::int64_t total_us = 0;
    std::int64_t max_us = 0;
    std::int64_t min_us = std::numeric_limits<std::int64_t>::max();

    for (int i = 0; i < iterations; ++i) {
        SnapshotId id{};
        std::int64_t latency = measure_us([&] {
            auto result = sm_->create_snapshot();
            ASSERT_TRUE(result.is_ok());
            id = result.value();
        });

        total_us += latency;
        max_us = std::max(max_us, latency);
        min_us = std::min(min_us, latency);

        ASSERT_TRUE(sm_->release_snapshot(id).is_ok());
    }

    std::int64_t avg_us = total_us / iterations;

    std::cout << "\n=== create_snapshot() Latency ===" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Average:    " << avg_us << " us" << std::endl;
    std::cout << "Min:        " << min_us << " us" << std::endl;
    std::cout << "Max:        " << max_us << " us" << std::endl;
    std::cout << "Target:     <" << TARGET_CREATE_LATENCY_US << " us" << std::endl;
    std::cout << "================================\n" << std::endl;

    if (!kRunningUnderSanitizer) {
        EXPECT_LT(avg_us, TARGET_CREATE_LATENCY_US)
            << "create_snapshot() average latency " << avg_us << " us exceeds target "
            << TARGET_CREATE_LATENCY_US << " us";
    }
}

TEST_F(SnapshotBenchmarkTest, ReleaseSnapshotLatency) {
    // Populate some data first
    populate_data(1000);

    // Warm up
    for (int i = 0; i < 5; ++i) {
        auto snap = sm_->create_snapshot();
        ASSERT_TRUE(snap.is_ok());
        ASSERT_TRUE(sm_->release_snapshot(snap.value()).is_ok());
    }

    // Measure release latency over multiple iterations
    constexpr int iterations = 100;
    std::int64_t total_us = 0;
    std::int64_t max_us = 0;
    std::int64_t min_us = std::numeric_limits<std::int64_t>::max();

    for (int i = 0; i < iterations; ++i) {
        auto create_result = sm_->create_snapshot();
        ASSERT_TRUE(create_result.is_ok());
        SnapshotId id = create_result.value();

        std::int64_t latency = measure_us([&] {
            auto result = sm_->release_snapshot(id);
            ASSERT_TRUE(result.is_ok());
        });

        total_us += latency;
        max_us = std::max(max_us, latency);
        min_us = std::min(min_us, latency);
    }

    std::int64_t avg_us = total_us / iterations;

    std::cout << "\n=== release_snapshot() Latency ===" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Average:    " << avg_us << " us" << std::endl;
    std::cout << "Min:        " << min_us << " us" << std::endl;
    std::cout << "Max:        " << max_us << " us" << std::endl;
    std::cout << "Target:     <" << TARGET_RELEASE_LATENCY_US << " us" << std::endl;
    std::cout << "==================================\n" << std::endl;

    if (!kRunningUnderSanitizer) {
        EXPECT_LT(avg_us, TARGET_RELEASE_LATENCY_US)
            << "release_snapshot() average latency " << avg_us << " us exceeds target "
            << TARGET_RELEASE_LATENCY_US << " us";
    }
}

// ============================================================================
// Snapshot Read Performance Tests
// ============================================================================

TEST_F(SnapshotBenchmarkTest, GetAtSnapshotVersion_10kKeys) {
    // Populate 10k keys
    populate_data(BENCHMARK_KEY_COUNT);

    // Create snapshot
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    // Measure direct backend get throughput
    std::int64_t direct_total_us = 0;
    for (std::size_t i = 0; i < BENCHMARK_KEY_COUNT; ++i) {
        direct_total_us += measure_us([&] {
            auto result = sm_->backend().get(keys_[i]);
            ASSERT_TRUE(result.is_ok());
        });
    }

    // Measure snapshot get throughput
    std::int64_t snapshot_total_us = 0;
    for (std::size_t i = 0; i < BENCHMARK_KEY_COUNT; ++i) {
        snapshot_total_us += measure_us([&] {
            auto result = sm_->get(snap, keys_[i]);
            ASSERT_TRUE(result.is_ok());
        });
    }

    double direct_avg_us = static_cast<double>(direct_total_us) / BENCHMARK_KEY_COUNT;
    double snapshot_avg_us = static_cast<double>(snapshot_total_us) / BENCHMARK_KEY_COUNT;
    double overhead_ratio = snapshot_avg_us / direct_avg_us;

    std::cout << "\n=== get() at Snapshot vs Direct ===" << std::endl;
    std::cout << "Keys:             " << BENCHMARK_KEY_COUNT << std::endl;
    std::cout << "Direct avg:       " << direct_avg_us << " us" << std::endl;
    std::cout << "Snapshot avg:     " << snapshot_avg_us << " us" << std::endl;
    std::cout << "Overhead ratio:   " << overhead_ratio << "x" << std::endl;
    std::cout << "Target:           <2.0x" << std::endl;
    std::cout << "====================================\n" << std::endl;

    ASSERT_TRUE(sm_->release_snapshot(snap).is_ok());

    if (!kRunningUnderSanitizer) {
        EXPECT_LT(overhead_ratio, 2.0)
            << "Snapshot get() overhead " << overhead_ratio << "x exceeds 2x target";
    }
}

// ============================================================================
// Concurrent Snapshot Tests
// ============================================================================

TEST_F(SnapshotBenchmarkTest, TenConcurrentSnapshots) {
    // Populate data
    populate_data(BENCHMARK_KEY_COUNT);

    // Create 10 concurrent snapshots
    std::vector<SnapshotId> snapshots;
    for (int i = 0; i < 10; ++i) {
        auto result = sm_->create_snapshot();
        ASSERT_TRUE(result.is_ok()) << "Failed to create snapshot " << i;
        snapshots.push_back(result.value());
    }

    // Measure parallel read throughput from all snapshots
    constexpr int reads_per_snapshot = 1000;
    std::atomic<int> total_reads{0};

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::future<void>> futures;
    for (int s = 0; s < 10; ++s) {
        futures.push_back(std::async(std::launch::async, [&, s]() {
            for (int i = 0; i < reads_per_snapshot; ++i) {
                std::size_t key_idx = static_cast<std::size_t>((s * reads_per_snapshot + i) %
                                                               static_cast<int>(keys_.size()));
                auto result = sm_->get(snapshots[static_cast<std::size_t>(s)], keys_[key_idx]);
                if (result.is_ok()) {
                    total_reads++;
                }
            }
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double seconds = static_cast<double>(duration.count()) / 1'000'000.0;
    double reads_per_sec = static_cast<double>(total_reads.load()) / seconds;

    std::cout << "\n=== 10 Concurrent Snapshots Read ===" << std::endl;
    std::cout << "Snapshots:     10" << std::endl;
    std::cout << "Reads/snap:    " << reads_per_snapshot << std::endl;
    std::cout << "Total reads:   " << total_reads.load() << std::endl;
    std::cout << "Duration:      " << duration.count() << " us (" << seconds << " s)" << std::endl;
    std::cout << "Throughput:    " << static_cast<std::size_t>(reads_per_sec) << " reads/sec"
              << std::endl;
    std::cout << "=====================================\n" << std::endl;

    // Clean up
    for (auto& snap : snapshots) {
        ASSERT_TRUE(sm_->release_snapshot(snap).is_ok());
    }

    // All reads should have succeeded
    EXPECT_EQ(total_reads.load(), 10 * reads_per_snapshot);
}

TEST_F(SnapshotBenchmarkTest, ConcurrentCreateReleaseStress) {
    // Populate some data
    populate_data(1000);

    constexpr int num_threads = 4;
    constexpr int ops_per_thread = 100;

    std::atomic<int> create_success{0};
    std::atomic<int> release_success{0};
    std::atomic<int> limit_hit{0};

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::future<void>> futures;
    for (int t = 0; t < num_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                auto result = sm_->create_snapshot();
                if (result.is_ok()) {
                    create_success++;
                    // Small delay
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    if (sm_->release_snapshot(result.value()).is_ok()) {
                        release_success++;
                    }
                } else {
                    limit_hit++;
                }
            }
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "\n=== Concurrent Create/Release Stress ===" << std::endl;
    std::cout << "Threads:       " << num_threads << std::endl;
    std::cout << "Ops/thread:    " << ops_per_thread << std::endl;
    std::cout << "Total ops:     " << num_threads * ops_per_thread << std::endl;
    std::cout << "Creates:       " << create_success.load() << std::endl;
    std::cout << "Releases:      " << release_success.load() << std::endl;
    std::cout << "Limit hits:    " << limit_hit.load() << std::endl;
    std::cout << "Duration:      " << duration.count() << " ms" << std::endl;
    std::cout << "==========================================\n" << std::endl;

    // Verify counts are consistent
    EXPECT_EQ(create_success.load(), release_success.load());
    EXPECT_EQ(create_success.load() + limit_hit.load(), num_threads * ops_per_thread);
}

// ============================================================================
// Snapshot Iteration Performance Tests
// ============================================================================

TEST_F(SnapshotBenchmarkTest, IterateAtSnapshotVersion) {
    // Populate with prefixed keys
    for (std::size_t i = 0; i < 5000; ++i) {
        std::string key = "benchmark_key_" + std::to_string(i);
        auto value = random_bytes(VALUE_SIZE, rng_);
        ASSERT_TRUE(sm_->backend()
                        .put({reinterpret_cast<const std::byte*>(key.data()), key.size()}, value)
                        .is_ok());
    }

    // Create snapshot
    auto snap_result = sm_->create_snapshot();
    ASSERT_TRUE(snap_result.is_ok());
    SnapshotId snap = snap_result.value();

    // Measure direct iteration throughput
    const std::string_view prefix = "benchmark_key_";
    int direct_count = 0;
    std::int64_t direct_us = measure_us([&] {
        (void)sm_->backend().iterate(
            {reinterpret_cast<const std::byte*>(prefix.data()), prefix.size()},
            [&direct_count](StateBackend::Key, StateBackend::Value) {
                direct_count++;
                return true;
            });
    });

    // Measure snapshot iteration throughput
    int snapshot_count = 0;
    std::int64_t snapshot_us = measure_us([&] {
        (void)sm_->iterate(snap, {reinterpret_cast<const std::byte*>(prefix.data()), prefix.size()},
                           [&snapshot_count](StateBackend::Key, StateBackend::Value) {
                               snapshot_count++;
                               return true;
                           });
    });

    double overhead_ratio =
        (direct_us > 0) ? (static_cast<double>(snapshot_us) / static_cast<double>(direct_us)) : 1.0;

    std::cout << "\n=== Iteration at Snapshot vs Direct ===" << std::endl;
    std::cout << "Keys matched:     " << direct_count << " / " << snapshot_count << std::endl;
    std::cout << "Direct time:      " << direct_us << " us" << std::endl;
    std::cout << "Snapshot time:    " << snapshot_us << " us" << std::endl;
    std::cout << "Overhead ratio:   " << overhead_ratio << "x" << std::endl;
    std::cout << "========================================\n" << std::endl;

    ASSERT_TRUE(sm_->release_snapshot(snap).is_ok());

    EXPECT_EQ(direct_count, 5000);
    EXPECT_EQ(snapshot_count, 5000);
}

}  // namespace
}  // namespace dotvm::core::state
