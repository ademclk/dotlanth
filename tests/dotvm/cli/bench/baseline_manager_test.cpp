/// @file baseline_manager_test.cpp
/// @brief Unit tests for BaselineManager (CLI-005 Benchmark Runner)

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "dotvm/cli/bench/baseline_manager.hpp"
#include "dotvm/cli/bench/benchmark_statistics.hpp"

using namespace dotvm::cli::bench;

// ============================================================================
// Test Fixture
// ============================================================================

class BaselineManagerTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;
    std::vector<std::filesystem::path> temp_files_;

    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() /
                    ("baseline_test_" + std::to_string(std::time(nullptr)));
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        for (const auto& path : temp_files_) {
            std::filesystem::remove(path);
        }
        std::filesystem::remove_all(temp_dir_);
    }

    std::filesystem::path get_temp_path(const std::string& name) {
        auto path = temp_dir_ / name;
        temp_files_.push_back(path);
        return path;
    }

    BenchmarkStatistics make_sample_stats() {
        BenchmarkStatistics stats;
        stats.mean_ns = 1000.0;
        stats.median_ns = 950.0;
        stats.stddev_ns = 50.0;
        stats.min_ns = 800.0;
        stats.max_ns = 1200.0;
        stats.p50_ns = 950.0;
        stats.p90_ns = 1100.0;
        stats.p95_ns = 1150.0;
        stats.p99_ns = 1180.0;
        stats.instructions_per_run = 500;
        stats.total_cycles = 2500;
        stats.sample_count = 100;
        return stats;
    }
};

// ============================================================================
// Baseline Save Tests
// ============================================================================

TEST_F(BaselineManagerTest, SaveBaseline_CreatesFile) {
    auto path = get_temp_path("baseline.json");
    auto stats = make_sample_stats();

    BaselineManager manager;
    auto result = manager.save_baseline(stats, path.string(), "test.dot");

    EXPECT_TRUE(result);
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST_F(BaselineManagerTest, SaveBaseline_ValidJson) {
    auto path = get_temp_path("baseline.json");
    auto stats = make_sample_stats();

    BaselineManager manager;
    auto result = manager.save_baseline(stats, path.string(), "test.dot");
    ASSERT_TRUE(result);

    // Read and verify JSON structure
    std::ifstream in(path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("\"file\""), std::string::npos);
    EXPECT_NE(content.find("\"test.dot\""), std::string::npos);
    EXPECT_NE(content.find("\"mean_ns\""), std::string::npos);
    EXPECT_NE(content.find("\"median_ns\""), std::string::npos);
    EXPECT_NE(content.find("\"version\""), std::string::npos);
}

TEST_F(BaselineManagerTest, SaveBaseline_InvalidPath_ReturnsFalse) {
    auto stats = make_sample_stats();

    BaselineManager manager;
    auto result = manager.save_baseline(stats, "/nonexistent/dir/file.json", "test.dot");

    EXPECT_FALSE(result);
}

// ============================================================================
// Baseline Load Tests
// ============================================================================

TEST_F(BaselineManagerTest, LoadBaseline_Success) {
    auto path = get_temp_path("baseline.json");
    auto stats = make_sample_stats();

    BaselineManager manager;
    ASSERT_TRUE(manager.save_baseline(stats, path.string(), "test.dot"));

    auto loaded = manager.load_baseline(path.string());
    ASSERT_TRUE(loaded.has_value());

    EXPECT_DOUBLE_EQ(loaded->stats.mean_ns, stats.mean_ns);
    EXPECT_DOUBLE_EQ(loaded->stats.median_ns, stats.median_ns);
    EXPECT_DOUBLE_EQ(loaded->stats.stddev_ns, stats.stddev_ns);
    EXPECT_EQ(loaded->stats.instructions_per_run, stats.instructions_per_run);
    EXPECT_EQ(loaded->file, "test.dot");
}

TEST_F(BaselineManagerTest, LoadBaseline_NonExistentFile_ReturnsError) {
    BaselineManager manager;
    auto loaded = manager.load_baseline("/nonexistent/path.json");

    EXPECT_FALSE(loaded.has_value());
}

TEST_F(BaselineManagerTest, LoadBaseline_InvalidJson_ReturnsError) {
    auto path = get_temp_path("invalid.json");

    // Write invalid JSON
    std::ofstream out(path);
    out << "{ invalid json }";
    out.close();

    BaselineManager manager;
    auto loaded = manager.load_baseline(path.string());

    EXPECT_FALSE(loaded.has_value());
}

// ============================================================================
// Baseline Comparison Tests
// ============================================================================

TEST_F(BaselineManagerTest, Compare_NoRegression) {
    BenchmarkStatistics baseline;
    baseline.mean_ns = 1000.0;

    BenchmarkStatistics current;
    current.mean_ns = 1000.0;  // Same as baseline

    BaselineManager manager;
    auto comparison = manager.compare(current, baseline, 5.0);

    EXPECT_FALSE(comparison.is_regression);
    EXPECT_NEAR(comparison.delta_percent, 0.0, 0.001);
}

TEST_F(BaselineManagerTest, Compare_SlowdownUnderThreshold) {
    BenchmarkStatistics baseline;
    baseline.mean_ns = 1000.0;

    BenchmarkStatistics current;
    current.mean_ns = 1030.0;  // 3% slower

    BaselineManager manager;
    auto comparison = manager.compare(current, baseline, 5.0);  // 5% threshold

    EXPECT_FALSE(comparison.is_regression);  // Under threshold
    EXPECT_NEAR(comparison.delta_percent, 3.0, 0.001);
}

TEST_F(BaselineManagerTest, Compare_SlowdownOverThreshold) {
    BenchmarkStatistics baseline;
    baseline.mean_ns = 1000.0;

    BenchmarkStatistics current;
    current.mean_ns = 1100.0;  // 10% slower

    BaselineManager manager;
    auto comparison = manager.compare(current, baseline, 5.0);  // 5% threshold

    EXPECT_TRUE(comparison.is_regression);
    EXPECT_NEAR(comparison.delta_percent, 10.0, 0.001);
}

TEST_F(BaselineManagerTest, Compare_Improvement) {
    BenchmarkStatistics baseline;
    baseline.mean_ns = 1000.0;

    BenchmarkStatistics current;
    current.mean_ns = 900.0;  // 10% faster

    BaselineManager manager;
    auto comparison = manager.compare(current, baseline, 5.0);

    EXPECT_FALSE(comparison.is_regression);               // Faster is not a regression
    EXPECT_NEAR(comparison.delta_percent, -10.0, 0.001);  // Negative = improvement
}

TEST_F(BaselineManagerTest, Compare_ZeroBaseline_HandlesGracefully) {
    BenchmarkStatistics baseline;
    baseline.mean_ns = 0.0;  // Edge case

    BenchmarkStatistics current;
    current.mean_ns = 1000.0;

    BaselineManager manager;
    auto comparison = manager.compare(current, baseline, 5.0);

    // Should not crash, returns non-regression since we can't compare
    EXPECT_FALSE(comparison.is_regression);
}

// ============================================================================
// Roundtrip Tests
// ============================================================================

TEST_F(BaselineManagerTest, Roundtrip_PreservesAllFields) {
    auto path = get_temp_path("roundtrip.json");
    auto original = make_sample_stats();

    BaselineManager manager;
    ASSERT_TRUE(manager.save_baseline(original, path.string(), "roundtrip.dot"));

    auto loaded = manager.load_baseline(path.string());
    ASSERT_TRUE(loaded.has_value());

    const auto& loaded_stats = loaded->stats;
    EXPECT_DOUBLE_EQ(loaded_stats.mean_ns, original.mean_ns);
    EXPECT_DOUBLE_EQ(loaded_stats.median_ns, original.median_ns);
    EXPECT_DOUBLE_EQ(loaded_stats.stddev_ns, original.stddev_ns);
    EXPECT_DOUBLE_EQ(loaded_stats.min_ns, original.min_ns);
    EXPECT_DOUBLE_EQ(loaded_stats.max_ns, original.max_ns);
    EXPECT_DOUBLE_EQ(loaded_stats.p50_ns, original.p50_ns);
    EXPECT_DOUBLE_EQ(loaded_stats.p90_ns, original.p90_ns);
    EXPECT_DOUBLE_EQ(loaded_stats.p95_ns, original.p95_ns);
    EXPECT_DOUBLE_EQ(loaded_stats.p99_ns, original.p99_ns);
    EXPECT_EQ(loaded_stats.instructions_per_run, original.instructions_per_run);
    EXPECT_EQ(loaded_stats.total_cycles, original.total_cycles);
    EXPECT_EQ(loaded_stats.sample_count, original.sample_count);
}
