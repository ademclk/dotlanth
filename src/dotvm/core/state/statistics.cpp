/// @file statistics.cpp
/// @brief STATE-010 Statistics collection implementation

#include "dotvm/core/state/statistics.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <random>

#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {

// ============================================================================
// StatisticsCollector Implementation
// ============================================================================

StatisticsCollector::StatisticsCollector(const StateBackend& backend, StatisticsConfig config)
    : backend_{backend}, config_{std::move(config)} {}

StatisticsCollector::Result<ScopeStatistics>
StatisticsCollector::collect(std::span<const std::byte> prefix) {
    const std::string cache_key = prefix_to_key(prefix);

    // Check cache first (shared lock)
    {
        std::shared_lock lock(cache_mutex_);
        auto it = cache_.find(cache_key);
        if (it != cache_.end() && !is_stale(it->second)) {
            return it->second;
        }
    }

    // Collect fresh statistics
    ScopeStatistics stats = collect_impl(prefix);

    // Update cache (exclusive lock)
    {
        std::unique_lock lock(cache_mutex_);
        cache_[cache_key] = stats;
    }

    return stats;
}

const ScopeStatistics*
StatisticsCollector::get_cached(std::span<const std::byte> prefix) const noexcept {
    const std::string cache_key = prefix_to_key(prefix);

    std::shared_lock lock(cache_mutex_);
    auto it = cache_.find(cache_key);
    if (it != cache_.end()) {
        return &it->second;
    }
    return nullptr;
}

void StatisticsCollector::invalidate(std::span<const std::byte> prefix) {
    std::unique_lock lock(cache_mutex_);

    if (prefix.empty()) {
        cache_.clear();
    } else {
        const std::string cache_key = prefix_to_key(prefix);
        cache_.erase(cache_key);
    }
}

ScopeStatistics StatisticsCollector::collect_impl(std::span<const std::byte> prefix) {
    ScopeStatistics stats;

    // Set prefix
    stats.prefix.assign(prefix.begin(), prefix.end());
    stats.collected_at = std::chrono::steady_clock::now();
    stats.collected_at_version = backend_.current_version();

    // Collect all keys (or sample) for statistics
    std::vector<std::vector<std::byte>> keys;
    std::size_t total_keys = 0;

    // Determine if we should sample
    const std::size_t backend_key_count = backend_.key_count();
    const bool should_sample = config_.enable_sampling &&
                               backend_key_count > config_.sample_size * 2;

    // Random sampling setup
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    double sample_rate = 1.0;
    if (should_sample && backend_key_count > 0) {
        sample_rate = static_cast<double>(config_.sample_size) /
                      static_cast<double>(backend_key_count);
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    // Iterate and collect
    (void)backend_.iterate(prefix, [&](StateBackend::Key key, StateBackend::Value value) -> bool {
        total_keys++;

        // Track min/max - use lexicographic comparison via memcmp
        std::vector<std::byte> key_vec(key.begin(), key.end());

        auto compare_keys = [](const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
            const std::size_t min_len = std::min(a.size(), b.size());
            const int cmp = std::memcmp(a.data(), b.data(), min_len);
            if (cmp != 0) return cmp;
            if (a.size() < b.size()) return -1;
            if (a.size() > b.size()) return 1;
            return 0;
        };

        if (stats.min_key.empty() || compare_keys(key_vec, stats.min_key) < 0) {
            stats.min_key = key_vec;
        }
        if (stats.max_key.empty() || compare_keys(key_vec, stats.max_key) > 0) {
            stats.max_key = key_vec;
        }

        // Always count bytes
        stats.total_key_bytes += key.size();
        stats.total_value_bytes += value.size();

        // Sample for histogram
        if (!should_sample || dist(rng) < sample_rate) {
            keys.push_back(std::move(key_vec));
        }

        return true;  // Continue iteration
    });

    // Handle iteration errors gracefully (stats will be partial)
    if (should_sample && !keys.empty()) {
        // Estimate total count from sample
        stats.key_count = static_cast<std::size_t>(
            static_cast<double>(keys.size()) / sample_rate);

        // Scale byte counts
        const double scale = static_cast<double>(stats.key_count) /
                              static_cast<double>(total_keys);
        stats.total_key_bytes = static_cast<std::size_t>(
            static_cast<double>(stats.total_key_bytes) * scale);
        stats.total_value_bytes = static_cast<std::size_t>(
            static_cast<double>(stats.total_value_bytes) * scale);
    } else {
        stats.key_count = total_keys;
    }

    // Build histogram if we have data
    if (!keys.empty()) {
        build_histogram(stats, keys);
    }

    return stats;
}

void StatisticsCollector::build_histogram(ScopeStatistics& stats,
                                           std::vector<std::vector<std::byte>>& keys) {
    if (keys.empty()) {
        return;
    }

    // Custom comparator to avoid GCC 14 false-positive on memcmp bounds
    auto key_compare = [](const std::vector<std::byte>& a, const std::vector<std::byte>& b) {
        const std::size_t min_len = std::min(a.size(), b.size());
        if (min_len > 0) {
            const int cmp = std::memcmp(a.data(), b.data(), min_len);
            if (cmp != 0) return cmp < 0;
        }
        return a.size() < b.size();
    };

    // Sort keys for equi-depth histogram
    std::sort(keys.begin(), keys.end(), key_compare);

    // Determine actual bucket count (may be less than configured if few keys)
    const std::size_t actual_buckets = std::min(config_.histogram_buckets, keys.size());
    if (actual_buckets == 0) {
        return;
    }

    const std::size_t keys_per_bucket = keys.size() / actual_buckets;
    std::size_t remainder = keys.size() % actual_buckets;

    stats.histogram.clear();
    stats.histogram.reserve(actual_buckets);

    std::size_t idx = 0;
    for (std::size_t bucket = 0; bucket < actual_buckets; ++bucket) {
        HistogramBucket hb;

        // Distribute remainder across first buckets
        std::size_t bucket_size = keys_per_bucket + (remainder > 0 ? 1 : 0);
        if (remainder > 0) {
            --remainder;
        }

        // Count distinct keys in this bucket
        std::size_t distinct = 0;
        std::vector<std::byte>* prev = nullptr;
        for (std::size_t i = 0; i < bucket_size && idx < keys.size(); ++i, ++idx) {
            if (!prev || keys[idx] != *prev) {
                ++distinct;
            }
            prev = &keys[idx];
        }

        // Set bucket properties
        if (idx > 0) {
            hb.upper_bound = keys[idx - 1];
        }
        hb.count = bucket_size;
        hb.distinct_count = distinct;

        stats.histogram.push_back(std::move(hb));
    }
}

bool StatisticsCollector::is_stale(const ScopeStatistics& stats) const noexcept {
    const auto now = std::chrono::steady_clock::now();
    const auto age = std::chrono::duration<double>(now - stats.collected_at).count();
    return age > config_.staleness_threshold_seconds;
}

std::string StatisticsCollector::prefix_to_key(std::span<const std::byte> prefix) {
    std::string result;
    result.reserve(prefix.size());
    for (const auto b : prefix) {
        result.push_back(static_cast<char>(b));
    }
    return result;
}

}  // namespace dotvm::core::state
