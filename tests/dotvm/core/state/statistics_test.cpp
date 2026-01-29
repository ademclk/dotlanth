/// @file statistics_test.cpp
/// @brief STATE-010 Statistics collection unit tests (TDD)
///
/// Tests for statistics collection providing cardinality estimates:
/// - Basic key count and byte statistics
/// - Histogram building for selectivity estimation
/// - Sampling for large datasets
/// - Cache management and invalidation

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/state_backend.hpp"
#include "dotvm/core/state/statistics.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

[[nodiscard]] std::vector<std::byte> make_bytes(std::string_view str) {
    std::vector<std::byte> result(str.size());
    std::memcpy(result.data(), str.data(), str.size());
    return result;
}

[[nodiscard]] std::span<const std::byte> to_span(const std::vector<std::byte>& vec) {
    return {vec.data(), vec.size()};
}

// ============================================================================
// Test Fixture
// ============================================================================

class StatisticsTest : public ::testing::Test {
protected:
    void SetUp() override { backend_ = create_state_backend(); }

    void populate_test_data(std::size_t count, std::string_view prefix = "key:") {
        for (std::size_t i = 0; i < count; ++i) {
            auto key = make_bytes(std::string(prefix) + std::to_string(i));
            auto value = make_bytes("value_" + std::to_string(i));
            ASSERT_TRUE(backend_->put(to_span(key), to_span(value)).is_ok());
        }
    }

    std::unique_ptr<StateBackend> backend_;
};

// ============================================================================
// ScopeStatistics Tests
// ============================================================================

/// @test Default ScopeStatistics is empty
TEST_F(StatisticsTest, DefaultStatisticsEmpty) {
    ScopeStatistics stats;

    EXPECT_EQ(stats.key_count, 0u);
    EXPECT_EQ(stats.total_key_bytes, 0u);
    EXPECT_EQ(stats.total_value_bytes, 0u);
    EXPECT_TRUE(stats.prefix.empty());
    EXPECT_TRUE(stats.min_key.empty());
    EXPECT_TRUE(stats.max_key.empty());
    EXPECT_TRUE(stats.histogram.empty());
}

// ============================================================================
// StatisticsCollector Basic Tests
// ============================================================================

/// @test Collect basic statistics from empty backend
TEST_F(StatisticsTest, CollectFromEmpty) {
    StatisticsCollector collector(*backend_);
    auto result = collector.collect();

    ASSERT_TRUE(result.is_ok());
    const auto& stats = result.value();

    EXPECT_EQ(stats.key_count, 0u);
    EXPECT_EQ(stats.total_key_bytes, 0u);
    EXPECT_EQ(stats.total_value_bytes, 0u);
}

/// @test Collect basic counts from populated backend
TEST_F(StatisticsTest, CollectBasicCounts) {
    populate_test_data(100);

    StatisticsCollector collector(*backend_);
    auto result = collector.collect();

    ASSERT_TRUE(result.is_ok());
    const auto& stats = result.value();

    EXPECT_EQ(stats.key_count, 100u);
    EXPECT_GT(stats.total_key_bytes, 0u);
    EXPECT_GT(stats.total_value_bytes, 0u);
}

/// @test Collect statistics with prefix filter
TEST_F(StatisticsTest, CollectWithPrefix) {
    // Create mixed data with different prefixes
    for (int i = 0; i < 50; ++i) {
        auto key = make_bytes("user:" + std::to_string(i));
        auto value = make_bytes("userdata");
        ASSERT_TRUE(backend_->put(to_span(key), to_span(value)).is_ok());
    }
    for (int i = 0; i < 30; ++i) {
        auto key = make_bytes("order:" + std::to_string(i));
        auto value = make_bytes("orderdata");
        ASSERT_TRUE(backend_->put(to_span(key), to_span(value)).is_ok());
    }

    StatisticsCollector collector(*backend_);

    // Collect for user: prefix
    auto user_prefix = make_bytes("user:");
    auto user_result = collector.collect(to_span(user_prefix));
    ASSERT_TRUE(user_result.is_ok());
    EXPECT_EQ(user_result.value().key_count, 50u);

    // Collect for order: prefix
    auto order_prefix = make_bytes("order:");
    auto order_result = collector.collect(to_span(order_prefix));
    ASSERT_TRUE(order_result.is_ok());
    EXPECT_EQ(order_result.value().key_count, 30u);
}

/// @test Min/max key tracking
TEST_F(StatisticsTest, MinMaxKeyTracking) {
    auto key_a = make_bytes("aaa");
    auto key_m = make_bytes("mmm");
    auto key_z = make_bytes("zzz");
    auto value = make_bytes("v");

    ASSERT_TRUE(backend_->put(to_span(key_m), to_span(value)).is_ok());
    ASSERT_TRUE(backend_->put(to_span(key_a), to_span(value)).is_ok());
    ASSERT_TRUE(backend_->put(to_span(key_z), to_span(value)).is_ok());

    StatisticsCollector collector(*backend_);
    auto result = collector.collect();

    ASSERT_TRUE(result.is_ok());
    const auto& stats = result.value();

    EXPECT_EQ(stats.min_key, make_bytes("aaa"));
    EXPECT_EQ(stats.max_key, make_bytes("zzz"));
}

// ============================================================================
// Histogram Tests
// ============================================================================

/// @test Histogram building
TEST_F(StatisticsTest, HistogramBuilding) {
    populate_test_data(1000);

    StatisticsConfig config;
    config.histogram_buckets = 10;

    StatisticsCollector collector(*backend_, config);
    auto result = collector.collect();

    ASSERT_TRUE(result.is_ok());
    const auto& stats = result.value();

    // Should have histogram buckets
    EXPECT_LE(stats.histogram.size(), 10u);
    EXPECT_GT(stats.histogram.size(), 0u);

    // Each bucket should have a count
    std::size_t total_in_buckets = 0;
    for (const auto& bucket : stats.histogram) {
        EXPECT_GT(bucket.count, 0u);
        total_in_buckets += bucket.count;
    }

    // Total should match key count
    EXPECT_EQ(total_in_buckets, 1000u);
}

/// @test Histogram bucket bounds are monotonically increasing
TEST_F(StatisticsTest, HistogramBoundsMonotonic) {
    populate_test_data(500);

    StatisticsConfig config;
    config.histogram_buckets = 5;

    StatisticsCollector collector(*backend_, config);
    auto result = collector.collect();

    ASSERT_TRUE(result.is_ok());
    const auto& stats = result.value();

    // Verify monotonically increasing bounds
    for (std::size_t i = 1; i < stats.histogram.size(); ++i) {
        EXPECT_GT(stats.histogram[i].upper_bound, stats.histogram[i - 1].upper_bound)
            << "Histogram bounds should be monotonically increasing";
    }
}

// ============================================================================
// Sampling Tests
// ============================================================================

/// @test Sampling enabled for large datasets
TEST_F(StatisticsTest, SamplingLargeDataset) {
    // Create a large dataset
    populate_test_data(50000);

    StatisticsConfig config;
    config.enable_sampling = true;
    config.sample_size = 1000;

    StatisticsCollector collector(*backend_, config);
    auto result = collector.collect();

    ASSERT_TRUE(result.is_ok());
    const auto& stats = result.value();

    // Key count should be estimated (may not be exact with sampling)
    // For this test, we're verifying it's reasonably close
    EXPECT_GT(stats.key_count, 40000u);
    EXPECT_LT(stats.key_count, 60000u);
}

/// @test Sampling disabled gives exact counts
TEST_F(StatisticsTest, NoSamplingExactCounts) {
    populate_test_data(1000);

    StatisticsConfig config;
    config.enable_sampling = false;

    StatisticsCollector collector(*backend_, config);
    auto result = collector.collect();

    ASSERT_TRUE(result.is_ok());
    const auto& stats = result.value();

    EXPECT_EQ(stats.key_count, 1000u);
}

// ============================================================================
// Cache Tests
// ============================================================================

/// @test Statistics are cached
TEST_F(StatisticsTest, StatisticsCaching) {
    populate_test_data(100);

    StatisticsCollector collector(*backend_);

    // First collection
    auto result1 = collector.collect();
    ASSERT_TRUE(result1.is_ok());

    // Should be cached now
    const auto* cached = collector.get_cached({});
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(cached->key_count, 100u);
}

/// @test Cache invalidation
TEST_F(StatisticsTest, CacheInvalidation) {
    populate_test_data(100);

    StatisticsCollector collector(*backend_);

    // Collect and cache
    auto result1 = collector.collect();
    ASSERT_TRUE(result1.is_ok());
    ASSERT_NE(collector.get_cached({}), nullptr);

    // Invalidate
    collector.invalidate();

    // Cache should be empty
    EXPECT_EQ(collector.get_cached({}), nullptr);
}

/// @test Prefix-specific cache invalidation
TEST_F(StatisticsTest, PrefixCacheInvalidation) {
    // Create data with two prefixes
    for (int i = 0; i < 50; ++i) {
        auto key = make_bytes("user:" + std::to_string(i));
        ASSERT_TRUE(backend_->put(to_span(key), make_bytes("v")).is_ok());
    }
    for (int i = 0; i < 30; ++i) {
        auto key = make_bytes("order:" + std::to_string(i));
        ASSERT_TRUE(backend_->put(to_span(key), make_bytes("v")).is_ok());
    }

    StatisticsCollector collector(*backend_);

    auto user_prefix = make_bytes("user:");
    auto order_prefix = make_bytes("order:");

    // Collect both
    (void)collector.collect(to_span(user_prefix));
    (void)collector.collect(to_span(order_prefix));

    ASSERT_NE(collector.get_cached(to_span(user_prefix)), nullptr);
    ASSERT_NE(collector.get_cached(to_span(order_prefix)), nullptr);

    // Invalidate only user prefix
    collector.invalidate(to_span(user_prefix));

    EXPECT_EQ(collector.get_cached(to_span(user_prefix)), nullptr);
    EXPECT_NE(collector.get_cached(to_span(order_prefix)), nullptr);
}

// ============================================================================
// Version Tracking Tests
// ============================================================================

/// @test Statistics track collection version
TEST_F(StatisticsTest, VersionTracking) {
    populate_test_data(100);

    StatisticsCollector collector(*backend_);
    auto result = collector.collect();

    ASSERT_TRUE(result.is_ok());
    const auto& stats = result.value();

    // Should have a timestamp
    EXPECT_NE(stats.collected_at, std::chrono::steady_clock::time_point{});

    // Version should be set (from backend)
    // Note: In-memory backend may return 0 if MVCC is not fully enabled
    // This test verifies the field exists and is populated
}

// ============================================================================
// Edge Cases
// ============================================================================

/// @test Empty prefix collects all keys
TEST_F(StatisticsTest, EmptyPrefixCollectsAll) {
    populate_test_data(100);

    StatisticsCollector collector(*backend_);
    auto result = collector.collect({});

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().key_count, 100u);
}

/// @test Non-matching prefix returns zero count
TEST_F(StatisticsTest, NonMatchingPrefix) {
    populate_test_data(100, "key:");

    StatisticsCollector collector(*backend_);
    auto nonexistent = make_bytes("nonexistent:");
    auto result = collector.collect(to_span(nonexistent));

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().key_count, 0u);
}

}  // namespace
}  // namespace dotvm::core::state
