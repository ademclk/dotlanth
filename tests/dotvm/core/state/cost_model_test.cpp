/// @file cost_model_test.cpp
/// @brief STATE-010 Cost model unit tests (TDD)
///
/// Tests for cost estimation:
/// - PlanCost comparison and aggregation
/// - Cost formulas for different operators
/// - Selectivity estimation from histograms

#include <cstring>

#include <gtest/gtest.h>

#include "dotvm/core/state/cost_model.hpp"
#include "dotvm/core/state/execution_plan.hpp"
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

// ============================================================================
// PlanCost Tests
// ============================================================================

class PlanCostTest : public ::testing::Test {};

/// @test Default PlanCost is zero
TEST_F(PlanCostTest, DefaultIsZero) {
    PlanCost cost;
    EXPECT_DOUBLE_EQ(cost.io_cost, 0.0);
    EXPECT_DOUBLE_EQ(cost.cpu_cost, 0.0);
    EXPECT_DOUBLE_EQ(cost.memory_cost, 0.0);
    EXPECT_DOUBLE_EQ(cost.total(), 0.0);
}

/// @test Total cost aggregation
TEST_F(PlanCostTest, TotalAggregation) {
    PlanCost cost;
    cost.io_cost = 10.0;
    cost.cpu_cost = 5.0;
    cost.memory_cost = 2.0;

    EXPECT_DOUBLE_EQ(cost.total(), 17.0);
}

/// @test PlanCost comparison (spaceship operator)
TEST_F(PlanCostTest, Comparison) {
    PlanCost low{.io_cost = 5.0, .cpu_cost = 2.0, .memory_cost = 1.0};
    PlanCost high{.io_cost = 10.0, .cpu_cost = 3.0, .memory_cost = 2.0};

    EXPECT_TRUE(low < high);
    EXPECT_FALSE(high < low);
    EXPECT_FALSE(low < low);
}

/// @test PlanCost addition
TEST_F(PlanCostTest, Addition) {
    PlanCost a{.io_cost = 5.0, .cpu_cost = 2.0, .memory_cost = 1.0};
    PlanCost b{.io_cost = 3.0, .cpu_cost = 1.0, .memory_cost = 0.5};

    PlanCost sum = a + b;

    EXPECT_DOUBLE_EQ(sum.io_cost, 8.0);
    EXPECT_DOUBLE_EQ(sum.cpu_cost, 3.0);
    EXPECT_DOUBLE_EQ(sum.memory_cost, 1.5);
}

// ============================================================================
// CostModel Configuration Tests
// ============================================================================

class CostModelConfigTest : public ::testing::Test {};

/// @test Default config values
TEST_F(CostModelConfigTest, DefaultValues) {
    CostModelConfig config;

    EXPECT_DOUBLE_EQ(config.cost_per_key_scan, 1.0);
    EXPECT_DOUBLE_EQ(config.cost_per_predicate_eval, 0.05);
    EXPECT_DOUBLE_EQ(config.full_scan_setup_cost, 10.0);
    EXPECT_DOUBLE_EQ(config.prefix_scan_setup_cost, 5.0);
}

// ============================================================================
// CostModel Estimation Tests
// ============================================================================

class CostModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create sample statistics
        stats_.key_count = 10000;
        stats_.total_key_bytes = 100000;
        stats_.total_value_bytes = 1000000;

        // Build a simple histogram (10 buckets, 1000 keys each)
        for (int i = 0; i < 10; ++i) {
            HistogramBucket bucket;
            bucket.upper_bound = make_bytes(std::string(1, static_cast<char>('a' + i)));
            bucket.count = 1000;
            bucket.distinct_count = 900;
            stats_.histogram.push_back(std::move(bucket));
        }
    }

    ScopeStatistics stats_;
    CostModel model_;
};

/// @test Full scan cost formula
TEST_F(CostModelTest, FullScanCostFormula) {
    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.estimated_output_rows = stats_.key_count;

    auto cost = model_.estimate(plan, stats_);

    // Full scan: setup + N * cost_per_key
    const auto& config = model_.config();
    double expected_io = config.full_scan_setup_cost +
                         static_cast<double>(stats_.key_count) * config.cost_per_key_scan;

    EXPECT_DOUBLE_EQ(cost.io_cost, expected_io);
}

/// @test Prefix scan cost is lower than full scan for selective prefix
TEST_F(CostModelTest, PrefixScanCostLower) {
    ExecutionPlan full_scan_plan;
    full_scan_plan.operators.push_back(FullScanOp{});
    full_scan_plan.estimated_output_rows = stats_.key_count;

    ExecutionPlan prefix_plan;
    prefix_plan.operators.push_back(PrefixScanOp{make_bytes("a")});
    prefix_plan.estimated_output_rows = 1000;  // Only 10% of data

    auto full_cost = model_.estimate(full_scan_plan, stats_);
    auto prefix_cost = model_.estimate(prefix_plan, stats_);

    EXPECT_LT(prefix_cost.total(), full_cost.total());
}

/// @test Filter operator adds CPU cost
TEST_F(CostModelTest, FilterOperatorCost) {
    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.operators.push_back(FilterOp{{
        {PredicateOp::Ge, make_bytes("c")},
        {PredicateOp::Lt, make_bytes("f")}
    }});
    plan.estimated_output_rows = 3000;  // After filter

    auto cost = model_.estimate(plan, stats_);

    // Filter should add CPU cost for predicate evaluation
    EXPECT_GT(cost.cpu_cost, 0.0);
}

/// @test Aggregate operator cost
TEST_F(CostModelTest, AggregateOperatorCost) {
    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.operators.push_back(AggregateOp{AggregateFunc::Count});
    plan.estimated_output_rows = 1;  // Aggregate produces single row

    auto cost = model_.estimate(plan, stats_);

    // Should have some CPU cost for aggregation
    EXPECT_GT(cost.cpu_cost, 0.0);
}

/// @test Limit operator reduces cost
TEST_F(CostModelTest, LimitOperatorReducesCost) {
    ExecutionPlan unlimited;
    unlimited.operators.push_back(FullScanOp{});
    unlimited.estimated_output_rows = stats_.key_count;

    ExecutionPlan limited;
    limited.operators.push_back(FullScanOp{});
    limited.operators.push_back(LimitOp{100});
    limited.estimated_output_rows = 100;

    auto unlimited_cost = model_.estimate(unlimited, stats_);
    auto limited_cost = model_.estimate(limited, stats_);

    // Limited should be cheaper (early termination)
    EXPECT_LT(limited_cost.total(), unlimited_cost.total());
}

// ============================================================================
// Selectivity Estimation Tests
// ============================================================================

/// @test Equality selectivity from histogram
TEST_F(CostModelTest, EqualitySelectivity) {
    Predicate eq_pred{PredicateOp::Eq, make_bytes("c")};

    double selectivity = model_.estimate_selectivity(stats_, eq_pred);

    // Equality on 10000 keys with ~900 distinct per bucket
    // Selectivity should be roughly 1/distinct_count
    EXPECT_GT(selectivity, 0.0);
    EXPECT_LT(selectivity, 0.1);  // Should be small
}

/// @test Range selectivity from histogram
TEST_F(CostModelTest, RangeSelectivity) {
    // Range covering ~30% of data (c to f out of a-j)
    std::vector<Predicate> range_preds = {
        {PredicateOp::Ge, make_bytes("c")},
        {PredicateOp::Lt, make_bytes("f")}
    };

    double combined_selectivity = 1.0;
    for (const auto& pred : range_preds) {
        combined_selectivity *= model_.estimate_selectivity(stats_, pred);
    }

    // Should be roughly 0.3 (3 buckets out of 10)
    EXPECT_GT(combined_selectivity, 0.1);
    EXPECT_LT(combined_selectivity, 0.5);
}

/// @test Prefix selectivity
TEST_F(CostModelTest, PrefixSelectivity) {
    Predicate prefix_pred{PredicateOp::Prefix, make_bytes("abc")};

    double selectivity = model_.estimate_selectivity(stats_, prefix_pred);

    // Prefix should have some selectivity
    EXPECT_GT(selectivity, 0.0);
    EXPECT_LE(selectivity, 1.0);
}

/// @test Estimate cardinality from selectivity
TEST_F(CostModelTest, CardinalityFromSelectivity) {
    Predicate pred{PredicateOp::Ge, make_bytes("e")};

    double selectivity = model_.estimate_selectivity(stats_, pred);
    std::size_t estimated_rows = model_.estimate_cardinality(stats_, selectivity);

    // Should be roughly half the rows (e-j = 5 buckets out of 10)
    EXPECT_GT(estimated_rows, 3000u);
    EXPECT_LT(estimated_rows, 7000u);
}

// ============================================================================
// Edge Cases
// ============================================================================

/// @test Empty statistics
TEST_F(CostModelTest, EmptyStatistics) {
    ScopeStatistics empty;

    ExecutionPlan plan;
    plan.operators.push_back(FullScanOp{});
    plan.estimated_output_rows = 0;

    auto cost = model_.estimate(plan, empty);

    // Should still return a valid cost (just setup cost)
    EXPECT_GE(cost.total(), 0.0);
}

/// @test No histogram selectivity fallback
TEST_F(CostModelTest, NoHistogramFallback) {
    ScopeStatistics no_hist;
    no_hist.key_count = 1000;

    Predicate pred{PredicateOp::Ge, make_bytes("m")};

    double selectivity = model_.estimate_selectivity(no_hist, pred);

    // Should use fallback estimate (typically 0.5 for range)
    EXPECT_GT(selectivity, 0.0);
    EXPECT_LE(selectivity, 1.0);
}

}  // namespace
}  // namespace dotvm::core::state
