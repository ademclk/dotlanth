#pragma once

/// @file baseline_manager.hpp
/// @brief Baseline management for benchmark comparison (CLI-005 Benchmark Runner)
///
/// Provides saving, loading, and comparison of benchmark baselines
/// for regression detection.

#include <cstdint>
#include <expected>
#include <string>

#include "dotvm/cli/bench/benchmark_statistics.hpp"

namespace dotvm::cli::bench {

/// @brief Result of comparing current benchmark to baseline
struct ComparisonResult {
    bool is_regression = false;      ///< True if current is slower than baseline + threshold
    double delta_percent = 0.0;      ///< Percentage change (positive = slower)
    double baseline_mean_ns = 0.0;   ///< Baseline mean time
    double current_mean_ns = 0.0;    ///< Current mean time
    double threshold_percent = 0.0;  ///< Threshold used for comparison
};

/// @brief Loaded baseline data
struct Baseline {
    BenchmarkStatistics stats;  ///< Statistics from baseline
    std::string file;           ///< Original file that was benchmarked
    std::string version;        ///< Baseline format version
    std::uint64_t timestamp;    ///< When baseline was created
};

/// @brief Manages baseline save/load and comparison
///
/// Baselines are stored as JSON files containing benchmark statistics
/// that can be used to detect performance regressions.
class BaselineManager {
public:
    /// @brief Save statistics as a baseline file
    /// @param stats Statistics to save
    /// @param path Output file path
    /// @param input_file Original bytecode file name
    /// @return true if saved successfully
    [[nodiscard]] bool save_baseline(const BenchmarkStatistics& stats, const std::string& path,
                                     const std::string& input_file);

    /// @brief Load a baseline from file
    /// @param path Input file path
    /// @return Loaded baseline or error message
    [[nodiscard]] std::expected<Baseline, std::string> load_baseline(const std::string& path);

    /// @brief Compare current stats against baseline
    /// @param current Current benchmark statistics
    /// @param baseline Baseline statistics to compare against
    /// @param threshold_percent Regression threshold percentage
    /// @return Comparison result
    [[nodiscard]] ComparisonResult compare(const BenchmarkStatistics& current,
                                           const BenchmarkStatistics& baseline,
                                           double threshold_percent) const;

private:
    static constexpr const char* kVersion = "1.0";
};

}  // namespace dotvm::cli::bench
