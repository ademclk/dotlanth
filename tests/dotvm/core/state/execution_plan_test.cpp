/// @file execution_plan_test.cpp
/// @brief STATE-010 Execution plan unit tests (TDD)
///
/// Tests for execution plan:
/// - Plan validity checks
/// - Plan execution semantics
/// - Operator pipeline processing

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/execution_plan.hpp"
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

class ExecutionPlanTest : public ::testing::Test {
protected:
    void SetUp() override {
        backend_ = create_state_backend();
    }

    void populate_test_data() {
        // Create ordered test data
        std::vector<std::string> keys = {
            "apple", "banana", "cherry", "date", "elderberry",
            "fig", "grape", "honeydew", "kiwi", "lemon"
        };
        for (const auto& key : keys) {
            auto kb = make_bytes(key);
            auto vb = make_bytes("value_" + key);
            ASSERT_TRUE(backend_->put(to_span(kb), to_span(vb)).is_ok());
        }
    }

    std::unique_ptr<StateBackend> backend_;
};

// ============================================================================
// Plan Validity Tests
// ============================================================================

/// @test Empty plan is invalid
TEST_F(ExecutionPlanTest, EmptyPlanInvalid) {
    ExecutionPlan plan;
    EXPECT_FALSE(plan.is_valid());
}

/// @test Plan starting with FullScan is valid
TEST_F(ExecutionPlanTest, FullScanPlanValid) {
    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});

    EXPECT_TRUE(plan.is_valid());
    EXPECT_NE(plan.scan_operator(), nullptr);
}

/// @test Plan starting with PrefixScan is valid
TEST_F(ExecutionPlanTest, PrefixScanPlanValid) {
    ExecutionPlan plan;
    plan.operators.push_back(PrefixScanOp{make_bytes("test:")});

    EXPECT_TRUE(plan.is_valid());
}

/// @test Plan starting with Filter is invalid
TEST_F(ExecutionPlanTest, FilterFirstInvalid) {
    ExecutionPlan plan;
    plan.operators.push_back(FilterOp{{{PredicateOp::Ge, make_bytes("a")}}});

    EXPECT_FALSE(plan.is_valid());
}

// ============================================================================
// Plan Execution Tests
// ============================================================================

/// @test Execute full scan returns all keys
TEST_F(ExecutionPlanTest, ExecuteFullScan) {
    populate_test_data();

    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});

    PlanExecutor executor(*backend_);

    std::vector<std::string> results;
    auto result = executor.execute(plan, [&results](auto key, auto /*value*/) {
        results.push_back(to_string(key));
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(results.size(), 10u);
}

/// @test Execute prefix scan returns matching keys
TEST_F(ExecutionPlanTest, ExecutePrefixScan) {
    // Add some prefixed data
    auto k1 = make_bytes("user:alice");
    auto k2 = make_bytes("user:bob");
    auto k3 = make_bytes("order:123");
    auto v = make_bytes("value");

    ASSERT_TRUE(backend_->put(to_span(k1), to_span(v)).is_ok());
    ASSERT_TRUE(backend_->put(to_span(k2), to_span(v)).is_ok());
    ASSERT_TRUE(backend_->put(to_span(k3), to_span(v)).is_ok());

    ExecutionPlan plan;
    plan.operators.push_back(PrefixScanOp{make_bytes("user:")});

    PlanExecutor executor(*backend_);

    std::vector<std::string> results;
    auto result = executor.execute(plan, [&results](auto key, auto /*value*/) {
        results.push_back(to_string(key));
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(results.size(), 2u);
}

/// @test Execute with filter
TEST_F(ExecutionPlanTest, ExecuteWithFilter) {
    populate_test_data();

    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.operators.push_back(FilterOp{{
        {PredicateOp::Ge, make_bytes("cherry")},
        {PredicateOp::Lt, make_bytes("grape")}
    }});

    PlanExecutor executor(*backend_);

    std::vector<std::string> results;
    auto result = executor.execute(plan, [&results](auto key, auto /*value*/) {
        results.push_back(to_string(key));
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    // Should match: cherry, date, elderberry, fig
    EXPECT_EQ(results.size(), 4u);
    EXPECT_EQ(results[0], "cherry");
    EXPECT_EQ(results[3], "fig");
}

/// @test Execute with limit
TEST_F(ExecutionPlanTest, ExecuteWithLimit) {
    populate_test_data();

    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.operators.push_back(LimitOp{3});

    PlanExecutor executor(*backend_);

    std::vector<std::string> results;
    auto result = executor.execute(plan, [&results](auto key, auto /*value*/) {
        results.push_back(to_string(key));
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(results.size(), 3u);
}

/// @test Execute with project (keys only)
TEST_F(ExecutionPlanTest, ExecuteWithProjectKeysOnly) {
    populate_test_data();

    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.operators.push_back(ProjectOp{.include_key = true, .include_value = false});

    PlanExecutor executor(*backend_);

    std::size_t count = 0;
    auto result = executor.execute(plan, [&count](auto key, auto value) {
        // Key should be present, value should be empty
        EXPECT_FALSE(key.empty());
        EXPECT_TRUE(value.empty());
        ++count;
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(count, 10u);
}

/// @test Execute with aggregate (count)
TEST_F(ExecutionPlanTest, ExecuteWithAggregateCount) {
    populate_test_data();

    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.operators.push_back(AggregateOp{AggregateFunc::Count});

    PlanExecutor executor(*backend_);

    std::size_t result_count = 0;
    std::string aggregate_result;
    auto result = executor.execute(plan, [&result_count, &aggregate_result](auto key, auto value) {
        // Aggregate should produce a single "result" row
        aggregate_result = to_string(value);
        ++result_count;
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result_count, 1u);
    EXPECT_EQ(aggregate_result, "10");  // 10 keys
}

/// @test Execute complex pipeline
TEST_F(ExecutionPlanTest, ExecuteComplexPipeline) {
    populate_test_data();

    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.operators.push_back(FilterOp{{
        {PredicateOp::Ge, make_bytes("b")}
    }});
    plan.operators.push_back(LimitOp{5});

    PlanExecutor executor(*backend_);

    std::vector<std::string> results;
    auto result = executor.execute(plan, [&results](auto key, auto /*value*/) {
        results.push_back(to_string(key));
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(results.size(), 5u);
    // All results should be >= "b"
    for (const auto& r : results) {
        EXPECT_GE(r, "b");
    }
}

/// @test Early termination via callback
TEST_F(ExecutionPlanTest, EarlyTerminationViaCallback) {
    populate_test_data();

    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});

    PlanExecutor executor(*backend_);

    std::size_t count = 0;
    auto result = executor.execute(plan, [&count](auto /*key*/, auto /*value*/) {
        ++count;
        return count < 5;  // Stop after 5
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(count, 5u);
}

// ============================================================================
// Plan to_string Tests
// ============================================================================

/// @test Plan to_string for full scan
TEST_F(ExecutionPlanTest, ToStringFullScan) {
    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.total_cost.io_cost = 100.0;
    plan.estimated_output_rows = 1000;

    std::string str = plan.to_string();

    EXPECT_NE(str.find("FullScan"), std::string::npos);
}

/// @test Plan to_string for complex plan
TEST_F(ExecutionPlanTest, ToStringComplexPlan) {
    ExecutionPlan plan;
    plan.operators.push_back(PrefixScanOp{make_bytes("user:")});
    plan.operators.push_back(FilterOp{{{PredicateOp::Ge, make_bytes("a")}}});
    plan.operators.push_back(LimitOp{10});

    std::string str = plan.to_string();

    EXPECT_NE(str.find("PrefixScan"), std::string::npos);
    EXPECT_NE(str.find("Filter"), std::string::npos);
    EXPECT_NE(str.find("Limit"), std::string::npos);
}

// ============================================================================
// Edge Cases
// ============================================================================

/// @test Execute on empty backend
TEST_F(ExecutionPlanTest, ExecuteOnEmptyBackend) {
    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});

    PlanExecutor executor(*backend_);

    std::size_t count = 0;
    auto result = executor.execute(plan, [&count](auto /*key*/, auto /*value*/) {
        ++count;
        return true;
    });

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(count, 0u);
}

/// @test Invalid plan execution fails
TEST_F(ExecutionPlanTest, InvalidPlanFails) {
    ExecutionPlan plan;  // Empty, invalid

    PlanExecutor executor(*backend_);

    auto result = executor.execute(plan, [](auto, auto) { return true; });

    EXPECT_FALSE(result.is_ok());
    EXPECT_EQ(result.error(), QueryOptimizerError::InvalidPlan);
}

}  // namespace
}  // namespace dotvm::core::state
