#pragma once

/// @file benchmark_statistics.hpp
/// @brief Statistical analysis for benchmark results (CLI-005 Benchmark Runner)
///
/// Provides computation of mean, median, standard deviation, and percentiles
/// for benchmark timing measurements.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dotvm::cli::bench {

/// @brief Statistics computed from benchmark timing samples
///
/// All timing values are in nanoseconds. Compute using the static
/// compute() method from a span of timing samples.
struct BenchmarkStatistics {
    double mean_ns = 0.0;    ///< Arithmetic mean
    double median_ns = 0.0;  ///< Median (50th percentile)
    double stddev_ns = 0.0;  ///< Population standard deviation
    double min_ns = 0.0;     ///< Minimum value
    double max_ns = 0.0;     ///< Maximum value

    double p50_ns = 0.0;  ///< 50th percentile (same as median)
    double p90_ns = 0.0;  ///< 90th percentile
    double p95_ns = 0.0;  ///< 95th percentile
    double p99_ns = 0.0;  ///< 99th percentile

    std::uint64_t instructions_per_run = 0;  ///< Instructions executed per run
    std::uint64_t total_cycles = 0;          ///< Total CPU cycles (if available)
    std::size_t sample_count = 0;            ///< Number of samples

    /// @brief Compute statistics from timing samples
    /// @param samples Vector of timing values in nanoseconds
    /// @return Computed statistics
    [[nodiscard]] static BenchmarkStatistics compute(std::span<const double> samples);

    /// @brief Calculate instructions per second based on mean time
    /// @return Instructions per second, or 0 if mean is zero
    [[nodiscard]] double instructions_per_second() const noexcept {
        if (mean_ns <= 0.0 || instructions_per_run == 0) {
            return 0.0;
        }
        // instructions_per_run / (mean_ns * 1e-9) = instructions_per_run * 1e9 / mean_ns
        return static_cast<double>(instructions_per_run) * 1'000'000'000.0 / mean_ns;
    }

    /// @brief Calculate cycles per instruction
    /// @return CPI, or 0 if no instructions
    [[nodiscard]] double cycles_per_instruction() const noexcept {
        if (instructions_per_run == 0) {
            return 0.0;
        }
        return static_cast<double>(total_cycles) / static_cast<double>(instructions_per_run);
    }

    /// @brief Calculate coefficient of variation (CV = stddev / mean)
    /// @return CV as a ratio (0.0 to 1.0+), or 0 if mean is zero
    [[nodiscard]] double coefficient_of_variation() const noexcept {
        if (mean_ns <= 0.0) {
            return 0.0;
        }
        return stddev_ns / mean_ns;
    }
};

}  // namespace dotvm::cli::bench
