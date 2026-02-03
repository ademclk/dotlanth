/// @file benchmark_statistics.cpp
/// @brief Implementation of benchmark statistics calculations

#include "dotvm/cli/bench/benchmark_statistics.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dotvm::cli::bench {

namespace {

/// @brief Calculate percentile using linear interpolation
/// @param sorted Sorted vector of values
/// @param percentile Percentile (0.0 to 1.0)
/// @return Interpolated percentile value
[[nodiscard]] double compute_percentile(const std::vector<double>& sorted, double percentile) {
    if (sorted.empty()) {
        return 0.0;
    }
    if (sorted.size() == 1) {
        return sorted[0];
    }

    // Use linear interpolation between nearest ranks
    double rank = percentile * static_cast<double>(sorted.size() - 1);
    auto lower_idx = static_cast<std::size_t>(std::floor(rank));
    auto upper_idx = static_cast<std::size_t>(std::ceil(rank));

    if (lower_idx == upper_idx) {
        return sorted[lower_idx];
    }

    double fraction = rank - static_cast<double>(lower_idx);
    return sorted[lower_idx] + fraction * (sorted[upper_idx] - sorted[lower_idx]);
}

}  // namespace

BenchmarkStatistics BenchmarkStatistics::compute(std::span<const double> samples) {
    BenchmarkStatistics stats{};

    if (samples.empty()) {
        return stats;
    }

    stats.sample_count = samples.size();

    // Copy and sort for median/percentile calculations
    std::vector<double> sorted(samples.begin(), samples.end());
    std::ranges::sort(sorted);

    stats.min_ns = sorted.front();
    stats.max_ns = sorted.back();

    // Mean
    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    stats.mean_ns = sum / static_cast<double>(samples.size());

    // Median
    std::size_t n = sorted.size();
    if (n % 2 == 0) {
        stats.median_ns = (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
    } else {
        stats.median_ns = sorted[n / 2];
    }

    // Population standard deviation
    if (samples.size() > 1) {
        double sq_sum = 0.0;
        for (double val : sorted) {
            double diff = val - stats.mean_ns;
            sq_sum += diff * diff;
        }
        stats.stddev_ns = std::sqrt(sq_sum / static_cast<double>(samples.size()));
    }

    // Percentiles
    stats.p50_ns = stats.median_ns;
    stats.p90_ns = compute_percentile(sorted, 0.90);
    stats.p95_ns = compute_percentile(sorted, 0.95);
    stats.p99_ns = compute_percentile(sorted, 0.99);

    return stats;
}

}  // namespace dotvm::cli::bench
