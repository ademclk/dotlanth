/// @file benchmark_statistics_test.cpp
/// @brief Unit tests for BenchmarkStatistics (CLI-005 Benchmark Runner)

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/cli/bench/benchmark_statistics.hpp"

using namespace dotvm::cli::bench;

// ============================================================================
// Basic Statistical Calculations
// ============================================================================

TEST(BenchmarkStatisticsTest, ComputeMean_SingleValue) {
    std::vector<double> samples = {100.0};
    auto stats = BenchmarkStatistics::compute(samples);
    EXPECT_DOUBLE_EQ(stats.mean_ns, 100.0);
}

TEST(BenchmarkStatisticsTest, ComputeMean_MultipleValues) {
    std::vector<double> samples = {100.0, 200.0, 300.0};
    auto stats = BenchmarkStatistics::compute(samples);
    EXPECT_DOUBLE_EQ(stats.mean_ns, 200.0);
}

TEST(BenchmarkStatisticsTest, ComputeMedian_OddCount) {
    std::vector<double> samples = {300.0, 100.0, 200.0};
    auto stats = BenchmarkStatistics::compute(samples);
    EXPECT_DOUBLE_EQ(stats.median_ns, 200.0);
}

TEST(BenchmarkStatisticsTest, ComputeMedian_EvenCount) {
    std::vector<double> samples = {100.0, 200.0, 300.0, 400.0};
    auto stats = BenchmarkStatistics::compute(samples);
    EXPECT_DOUBLE_EQ(stats.median_ns, 250.0);  // (200 + 300) / 2
}

TEST(BenchmarkStatisticsTest, ComputeStddev_Uniform) {
    std::vector<double> samples = {100.0, 100.0, 100.0};
    auto stats = BenchmarkStatistics::compute(samples);
    EXPECT_DOUBLE_EQ(stats.stddev_ns, 0.0);
}

TEST(BenchmarkStatisticsTest, ComputeStddev_Varying) {
    // stddev of {2, 4, 4, 4, 5, 5, 7, 9} = 2.0
    std::vector<double> samples = {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    auto stats = BenchmarkStatistics::compute(samples);
    EXPECT_NEAR(stats.stddev_ns, 2.0, 0.001);
}

TEST(BenchmarkStatisticsTest, ComputeMinMax) {
    std::vector<double> samples = {300.0, 100.0, 500.0, 200.0};
    auto stats = BenchmarkStatistics::compute(samples);
    EXPECT_DOUBLE_EQ(stats.min_ns, 100.0);
    EXPECT_DOUBLE_EQ(stats.max_ns, 500.0);
}

// ============================================================================
// Percentile Calculations
// ============================================================================

TEST(BenchmarkStatisticsTest, ComputePercentiles_Small) {
    // With 10 samples, percentiles are approximations
    std::vector<double> samples = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    auto stats = BenchmarkStatistics::compute(samples);

    // p50 = median = 55 (between 50 and 60)
    EXPECT_DOUBLE_EQ(stats.p50_ns, stats.median_ns);
    // p90 should be around 90
    EXPECT_GE(stats.p90_ns, 85.0);
    EXPECT_LE(stats.p90_ns, 95.0);
    // p95 should be around 95
    EXPECT_GE(stats.p95_ns, 90.0);
    EXPECT_LE(stats.p95_ns, 100.0);
    // p99 should be close to max for small samples
    EXPECT_GE(stats.p99_ns, 95.0);
}

TEST(BenchmarkStatisticsTest, ComputePercentiles_Large) {
    // Generate 100 sequential values for predictable percentiles
    std::vector<double> samples;
    samples.reserve(100);
    for (int i = 1; i <= 100; ++i) {
        samples.push_back(static_cast<double>(i));
    }

    auto stats = BenchmarkStatistics::compute(samples);

    EXPECT_NEAR(stats.p50_ns, 50.5, 1.0);  // p50 ≈ 50
    EXPECT_NEAR(stats.p90_ns, 90.5, 1.0);  // p90 ≈ 90
    EXPECT_NEAR(stats.p95_ns, 95.5, 1.0);  // p95 ≈ 95
    EXPECT_NEAR(stats.p99_ns, 99.5, 1.0);  // p99 ≈ 99
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(BenchmarkStatisticsTest, EmptySamples_ReturnsZeroed) {
    std::vector<double> samples;
    auto stats = BenchmarkStatistics::compute(samples);

    EXPECT_DOUBLE_EQ(stats.mean_ns, 0.0);
    EXPECT_DOUBLE_EQ(stats.median_ns, 0.0);
    EXPECT_DOUBLE_EQ(stats.stddev_ns, 0.0);
    EXPECT_DOUBLE_EQ(stats.min_ns, 0.0);
    EXPECT_DOUBLE_EQ(stats.max_ns, 0.0);
    EXPECT_EQ(stats.sample_count, 0);
}

TEST(BenchmarkStatisticsTest, SingleSample_StddevIsZero) {
    std::vector<double> samples = {42.0};
    auto stats = BenchmarkStatistics::compute(samples);

    EXPECT_DOUBLE_EQ(stats.mean_ns, 42.0);
    EXPECT_DOUBLE_EQ(stats.median_ns, 42.0);
    EXPECT_DOUBLE_EQ(stats.stddev_ns, 0.0);
    EXPECT_DOUBLE_EQ(stats.min_ns, 42.0);
    EXPECT_DOUBLE_EQ(stats.max_ns, 42.0);
}

TEST(BenchmarkStatisticsTest, TwoSamples) {
    std::vector<double> samples = {100.0, 200.0};
    auto stats = BenchmarkStatistics::compute(samples);

    EXPECT_DOUBLE_EQ(stats.mean_ns, 150.0);
    EXPECT_DOUBLE_EQ(stats.median_ns, 150.0);
    // stddev of {100, 200} with population formula = 50
    EXPECT_NEAR(stats.stddev_ns, 50.0, 0.001);
}

TEST(BenchmarkStatisticsTest, SampleCount) {
    std::vector<double> samples = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto stats = BenchmarkStatistics::compute(samples);
    EXPECT_EQ(stats.sample_count, 5);
}

// ============================================================================
// Derived Metrics
// ============================================================================

TEST(BenchmarkStatisticsTest, InstructionsPerSecond) {
    BenchmarkStatistics stats{};
    stats.mean_ns = 1000.0;  // 1 microsecond per run
    stats.instructions_per_run = 100;

    // 100 instructions in 1000ns = 100M instructions/second
    EXPECT_DOUBLE_EQ(stats.instructions_per_second(), 100'000'000.0);
}

TEST(BenchmarkStatisticsTest, InstructionsPerSecond_ZeroTime) {
    BenchmarkStatistics stats{};
    stats.mean_ns = 0.0;
    stats.instructions_per_run = 100;

    EXPECT_DOUBLE_EQ(stats.instructions_per_second(), 0.0);
}

TEST(BenchmarkStatisticsTest, CyclesPerInstruction) {
    BenchmarkStatistics stats{};
    stats.total_cycles = 500;
    stats.instructions_per_run = 100;

    EXPECT_DOUBLE_EQ(stats.cycles_per_instruction(), 5.0);
}

TEST(BenchmarkStatisticsTest, CyclesPerInstruction_ZeroInstructions) {
    BenchmarkStatistics stats{};
    stats.total_cycles = 500;
    stats.instructions_per_run = 0;

    EXPECT_DOUBLE_EQ(stats.cycles_per_instruction(), 0.0);
}

// ============================================================================
// Coefficient of Variation
// ============================================================================

TEST(BenchmarkStatisticsTest, CoefficientOfVariation) {
    std::vector<double> samples = {100.0, 100.0, 100.0};
    auto stats = BenchmarkStatistics::compute(samples);

    // CV = stddev / mean = 0 / 100 = 0%
    EXPECT_DOUBLE_EQ(stats.coefficient_of_variation(), 0.0);
}

TEST(BenchmarkStatisticsTest, CoefficientOfVariation_NonZero) {
    // stddev ≈ 15.81, mean = 50, CV ≈ 31.6%
    std::vector<double> samples = {30.0, 40.0, 50.0, 60.0, 70.0};
    auto stats = BenchmarkStatistics::compute(samples);

    EXPECT_GT(stats.coefficient_of_variation(), 0.0);
    EXPECT_LT(stats.coefficient_of_variation(), 1.0);  // Less than 100%
}
