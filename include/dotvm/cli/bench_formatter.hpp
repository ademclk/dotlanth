#pragma once

/// @file bench_formatter.hpp
/// @brief TOOL-010 Benchmark output formatters
///
/// Provides formatting functions for benchmark results in console, JSON, and CSV formats.

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace dotvm::cli {

/// @brief Individual benchmark result
struct BenchmarkResult {
    std::string name;               ///< Benchmark name (e.g., "BM_Fibonacci/35")
    double cpu_time_ns = 0.0;       ///< CPU time in nanoseconds
    double real_time_ns = 0.0;      ///< Real (wall-clock) time in nanoseconds
    std::uint64_t iterations = 0;   ///< Number of iterations run
    double items_per_second = 0.0;  ///< Items processed per second (if set)
    double bytes_per_second = 0.0;  ///< Bytes processed per second (if set)

    /// @brief Target CPU time in nanoseconds (0 = no target) - SEC-010
    double target_cpu_time_ns = 0.0;

    /// @brief Check if this benchmark met its target (if any)
    /// @return true if no target set or target met, false if target missed
    [[nodiscard]] bool met_target() const noexcept {
        if (target_cpu_time_ns <= 0.0) {
            return true;  // No target set
        }
        return cpu_time_ns <= target_cpu_time_ns;
    }
};

/// @brief Collection of benchmark results with metadata
struct BenchmarkReport {
    std::string version;                   ///< Tool version (e.g., "0.1.0")
    std::string timestamp;                 ///< ISO 8601 timestamp
    std::vector<BenchmarkResult> results;  ///< Individual benchmark results

    /// @brief Check if all benchmarks with targets met their targets (SEC-010)
    /// @return true if all targets met (or no targets set), false otherwise
    [[nodiscard]] bool all_targets_met() const noexcept {
        for (const auto& result : results) {
            if (!result.met_target()) {
                return false;
            }
        }
        return true;
    }

    /// @brief Get list of benchmarks that missed their targets (SEC-010)
    /// @return Vector of benchmark names that missed their targets
    [[nodiscard]] std::vector<std::string> missed_targets() const {
        std::vector<std::string> missed;
        for (const auto& result : results) {
            if (!result.met_target()) {
                missed.push_back(result.name);
            }
        }
        return missed;
    }
};

/// @brief Format benchmark report as human-readable console output
///
/// Outputs a table with benchmark names, times, iterations, and throughput.
/// Includes header with version information.
///
/// @param out Output stream
/// @param report Benchmark report to format
/// @param use_color Whether to use ANSI color codes
void format_bench_console(std::ostream& out, const BenchmarkReport& report, bool use_color);

/// @brief Format benchmark report as JSON
///
/// Outputs structured JSON with version, timestamp, and benchmark array.
///
/// @param out Output stream
/// @param report Benchmark report to format
void format_bench_json(std::ostream& out, const BenchmarkReport& report);

/// @brief Format benchmark report as CSV
///
/// Outputs header row followed by data rows for each benchmark.
///
/// @param out Output stream
/// @param report Benchmark report to format
void format_bench_csv(std::ostream& out, const BenchmarkReport& report);

/// @brief Format time in human-readable units
///
/// Automatically selects appropriate unit (ns, us, ms, s) based on magnitude.
///
/// @param time_ns Time in nanoseconds
/// @return Formatted string with unit suffix
[[nodiscard]] std::string format_time_human(double time_ns);

/// @brief Format throughput in human-readable units
///
/// Automatically selects appropriate prefix (K, M, G) based on magnitude.
///
/// @param items_per_sec Items per second
/// @return Formatted string with unit suffix
[[nodiscard]] std::string format_throughput(double items_per_sec);

/// @brief Format bytes per second in human-readable units
///
/// Automatically selects appropriate prefix (KB/s, MB/s, GB/s) based on magnitude.
///
/// @param bytes_per_sec Bytes per second
/// @return Formatted string with unit suffix
[[nodiscard]] std::string format_bytes_per_second(double bytes_per_sec);

}  // namespace dotvm::cli
