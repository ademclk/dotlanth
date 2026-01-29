/// @file query_optimizer_test.cpp
/// @brief STATE-010 Query optimizer unit tests (TDD)
///
/// End-to-end tests for query optimization:
/// - Plan generation and selection
/// - Index selection heuristics
/// - Predicate pushdown
/// - Full optimization workflow

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/query_optimizer.hpp"
#include "dotvm/core/state/state_backend.hpp"

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

[[nodiscard]] std::string to_string(std::span<const std::byte> data) {
    std::string result;
    result.reserve(data.size());
    for (auto b : data) {
        result.push_back(static_cast<char>(b));
    }
    return result;
}

// ============================================================================
// Test Fixture
// ============================================================================

class QueryOptimizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        backend_ = create_state_backend();
        optimizer_ = std::make_unique<QueryOptimizer>(*backend_);
    }

    void populate_uniform_data(std::size_t count, std::string_view prefix = "key:") {
        for (std::size_t i = 0; i < count; ++i) {
            auto key = make_bytes(std::string(prefix) + std::to_string(i));
            auto value = make_bytes("value_" + std::to_string(i));
            ASSERT_TRUE(backend_->put(to_span(key), to_span(value)).is_ok());
        }
    }

    void populate_prefixed_data() {
        // Create data with different prefixes (skewed distribution)
        for (int i = 0; i < 100; ++i) {
            auto key = make_bytes("user:" + std::to_string(i));
            ASSERT_TRUE(backend_->put(to_span(key), make_bytes("u")).is_ok());
        }
        for (int i = 0; i < 1000; ++i) {
            auto key = make_bytes("order:" + std::to_string(i));
            ASSERT_TRUE(backend_->put(to_span(key), make_bytes("o")).is_ok());
        }
        for (int i = 0; i < 50; ++i) {
            auto key = make_bytes("admin:" + std::to_string(i));
            ASSERT_TRUE(backend_->put(to_span(key), make_bytes("a")).is_ok());
        }
    }

    std::unique_ptr<StateBackend> backend_;
    std::unique_ptr<QueryOptimizer> optimizer_;
};

// ============================================================================
// Basic Optimization Tests
// ============================================================================

/// @test Optimize simple scan query
TEST_F(QueryOptimizerTest, OptimizeSimpleScan) {
    populate_uniform_data(100);
    (void)optimizer_->analyze();

    auto query = Query::Builder().scan().build();

    auto result = optimizer_->optimize(query);

    ASSERT_TRUE(result.is_ok());
    const auto& plan = result.value();

    EXPECT_TRUE(plan.is_valid());
    EXPECT_FALSE(plan.operators.empty());
}

/// @test Optimize prefix scan query
TEST_F(QueryOptimizerTest, OptimizePrefixScan) {
    populate_prefixed_data();
    (void)optimizer_->analyze();

    auto query = Query::Builder().scan(make_bytes("user:")).build();

    auto result = optimizer_->optimize(query);

    ASSERT_TRUE(result.is_ok());
    const auto& plan = result.value();

    // Should use prefix scan, not full scan
    EXPECT_TRUE(plan.is_valid());
    const auto* scan = plan.scan_operator();
    ASSERT_NE(scan, nullptr);
    EXPECT_TRUE(std::holds_alternative<PrefixScanOp>(*scan));
}

/// @test Optimize with filter
TEST_F(QueryOptimizerTest, OptimizeWithFilter) {
    populate_uniform_data(1000);
    (void)optimizer_->analyze();

    auto query = Query::Builder()
                     .scan()
                     .filter(PredicateOp::Ge, make_bytes("key:500"))
                     .filter(PredicateOp::Lt, make_bytes("key:600"))
                     .build();

    auto result = optimizer_->optimize(query);

    ASSERT_TRUE(result.is_ok());
    const auto& plan = result.value();

    EXPECT_TRUE(plan.is_valid());
    EXPECT_GT(plan.operators.size(), 1u);  // Scan + Filter
}

// ============================================================================
// Index Selection Tests
// ============================================================================

/// @test Chooses prefix scan for selective prefix
TEST_F(QueryOptimizerTest, ChoosesPrefixScanForSelectivePrefix) {
    populate_prefixed_data();
    (void)optimizer_->analyze();

    // admin: is very selective (50 out of 1150)
    auto query = Query::Builder().scan(make_bytes("admin:")).build();

    auto result = optimizer_->optimize(query);

    ASSERT_TRUE(result.is_ok());
    const auto& plan = result.value();

    const auto* scan = plan.scan_operator();
    ASSERT_NE(scan, nullptr);
    EXPECT_TRUE(std::holds_alternative<PrefixScanOp>(*scan));
}

/// @test Chooses full scan when prefix is not selective
TEST_F(QueryOptimizerTest, UsesAppropriateMethodForBroadQuery) {
    populate_uniform_data(100);
    (void)optimizer_->analyze();

    // Empty prefix = full scan
    auto query = Query::Builder().scan().build();

    auto result = optimizer_->optimize(query);

    ASSERT_TRUE(result.is_ok());
    const auto& plan = result.value();

    const auto* scan = plan.scan_operator();
    ASSERT_NE(scan, nullptr);
    // Full scan is appropriate for scanning everything
    EXPECT_TRUE(std::holds_alternative<FullScanOp>(*scan));
}

// ============================================================================
// Predicate Pushdown Tests
// ============================================================================

/// @test Prefix predicate pushed to scan
TEST_F(QueryOptimizerTest, PredicatePushdown) {
    populate_prefixed_data();
    (void)optimizer_->analyze();

    auto query = Query::Builder().scan().filter(PredicateOp::Prefix, make_bytes("user:")).build();

    auto result = optimizer_->optimize(query);

    ASSERT_TRUE(result.is_ok());
    const auto& plan = result.value();

    // Prefix predicate should be pushed down to become a PrefixScan
    const auto* scan = plan.scan_operator();
    ASSERT_NE(scan, nullptr);
    EXPECT_TRUE(std::holds_alternative<PrefixScanOp>(*scan));
}

// ============================================================================
// Execute Tests
// ============================================================================

/// @test Execute optimized query
TEST_F(QueryOptimizerTest, ExecuteOptimizedQuery) {
    populate_uniform_data(100);
    (void)optimizer_->analyze();

    auto query =
        Query::Builder().scan().filter(PredicateOp::Ge, make_bytes("key:50")).limit(10).build();

    std::vector<std::string> results;
    auto result = optimizer_->execute(query, [&results](auto key, auto /*value*/) {
        results.push_back(to_string(key));
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_LE(results.size(), 10u);
}

// ============================================================================
// Statistics Management Tests
// ============================================================================

/// @test Analyze collects statistics
TEST_F(QueryOptimizerTest, AnalyzeCollectsStatistics) {
    populate_uniform_data(500);

    // Before analyze, no stats
    EXPECT_EQ(optimizer_->statistics(), nullptr);

    auto result = optimizer_->analyze();
    ASSERT_TRUE(result.is_ok());

    // After analyze, stats available
    const auto* stats = optimizer_->statistics();
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->key_count, 500u);
}

/// @test Analyze with prefix
TEST_F(QueryOptimizerTest, AnalyzeWithPrefix) {
    populate_prefixed_data();

    auto prefix = make_bytes("user:");
    auto result = optimizer_->analyze(to_span(prefix));
    ASSERT_TRUE(result.is_ok());

    const auto* stats = optimizer_->statistics(to_span(prefix));
    ASSERT_NE(stats, nullptr);
    EXPECT_EQ(stats->key_count, 100u);
}

/// @test Invalidate statistics
TEST_F(QueryOptimizerTest, InvalidateStatistics) {
    populate_uniform_data(100);

    (void)optimizer_->analyze();
    ASSERT_NE(optimizer_->statistics(), nullptr);

    optimizer_->invalidate_statistics();
    EXPECT_EQ(optimizer_->statistics(), nullptr);
}

// ============================================================================
// Edge Cases
// ============================================================================

/// @test Optimize without statistics uses defaults
TEST_F(QueryOptimizerTest, OptimizeWithoutStatistics) {
    populate_uniform_data(100);
    // Don't call analyze()

    auto query = Query::Builder().scan().limit(10).build();

    auto result = optimizer_->optimize(query);

    // Should still work, just use default heuristics
    ASSERT_TRUE(result.is_ok());
}

/// @test Empty query fails
TEST_F(QueryOptimizerTest, EmptyQueryFails) {
    Query empty_query;  // No root

    auto result = optimizer_->optimize(empty_query);

    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(), QueryOptimizerError::EmptyQuery);
}

/// @test Execute on empty backend
TEST_F(QueryOptimizerTest, ExecuteOnEmptyBackend) {
    (void)optimizer_->analyze();

    auto query = Query::Builder().scan().build();

    std::size_t count = 0;
    auto result = optimizer_->execute(query, [&count](auto, auto) {
        ++count;
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(count, 0u);
}

// ============================================================================
// Configuration Tests
// ============================================================================

/// @test Custom configuration
TEST_F(QueryOptimizerTest, CustomConfiguration) {
    QueryOptimizerConfig config;
    config.max_plan_alternatives = 5;
    config.enable_statistics = false;

    QueryOptimizer custom_optimizer(*backend_, config);

    populate_uniform_data(100);

    auto query = Query::Builder().scan().build();

    auto result = custom_optimizer.optimize(query);
    ASSERT_TRUE(result.is_ok());
}

}  // namespace
}  // namespace dotvm::core::state
