#pragma once

/// @file statistics.hpp
/// @brief STATE-010 Statistics collection for query optimization
///
/// Provides cardinality and distribution statistics for cost-based optimization:
/// - ScopeStatistics: Statistics for a key prefix scope
/// - HistogramBucket: Equi-depth histogram bucket
/// - StatisticsCollector: Collects and caches statistics from StateBackend

#include <chrono>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/state_backend_fwd.hpp"

namespace dotvm::core::state {

// Forward declarations
class StateBackend;

// ============================================================================
// Error Codes
// ============================================================================

/// @brief Error codes for query optimizer operations (144-159)
enum class QueryOptimizerError : std::uint8_t {
    // AST errors (144-147)
    InvalidQuery = 144,          ///< Query is malformed
    UnsupportedPredicate = 145,  ///< Predicate type not supported
    EmptyQuery = 146,            ///< Query has no operations

    // Statistics errors (148-151)
    StatisticsUnavailable = 148,  ///< Statistics not available for scope
    StaleStatistics = 149,        ///< Statistics are outdated

    // Plan errors (152-155)
    PlanGenerationFailed = 152,  ///< Could not generate execution plan
    InvalidPlan = 153,           ///< Execution plan is invalid

    // Execution errors (156-159)
    ExecutionFailed = 156,  ///< Plan execution failed
    OperatorError = 157,    ///< Operator execution error
};

/// @brief Convert QueryOptimizerError to string
[[nodiscard]] constexpr const char* to_string(QueryOptimizerError error) noexcept {
    switch (error) {
        case QueryOptimizerError::InvalidQuery:
            return "InvalidQuery";
        case QueryOptimizerError::UnsupportedPredicate:
            return "UnsupportedPredicate";
        case QueryOptimizerError::EmptyQuery:
            return "EmptyQuery";
        case QueryOptimizerError::StatisticsUnavailable:
            return "StatisticsUnavailable";
        case QueryOptimizerError::StaleStatistics:
            return "StaleStatistics";
        case QueryOptimizerError::PlanGenerationFailed:
            return "PlanGenerationFailed";
        case QueryOptimizerError::InvalidPlan:
            return "InvalidPlan";
        case QueryOptimizerError::ExecutionFailed:
            return "ExecutionFailed";
        case QueryOptimizerError::OperatorError:
            return "OperatorError";
    }
    return "Unknown";
}

/// @brief Check if error is recoverable
[[nodiscard]] constexpr bool is_recoverable(QueryOptimizerError error) noexcept {
    switch (error) {
        case QueryOptimizerError::StaleStatistics:
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Histogram Types
// ============================================================================

/// @brief Single bucket in an equi-depth histogram
///
/// Histograms divide the key space into buckets with approximately equal
/// numbers of keys. This enables accurate selectivity estimation for
/// range predicates.
struct HistogramBucket {
    std::vector<std::byte> upper_bound;  ///< Upper bound of this bucket (exclusive for next)
    std::size_t count{0};                ///< Number of keys in this bucket
    std::size_t distinct_count{0};       ///< Distinct key count (for cardinality)
};

// ============================================================================
// ScopeStatistics
// ============================================================================

/// @brief Statistics for a key prefix scope
///
/// Contains cardinality estimates and distribution information for
/// a specific key prefix (or the entire keyspace if prefix is empty).
struct ScopeStatistics {
    std::vector<std::byte> prefix;                         ///< Key prefix this scope covers
    std::size_t key_count{0};                              ///< Total keys in scope
    std::size_t total_key_bytes{0};                        ///< Sum of all key sizes
    std::size_t total_value_bytes{0};                      ///< Sum of all value sizes
    std::vector<std::byte> min_key;                        ///< Minimum key in scope
    std::vector<std::byte> max_key;                        ///< Maximum key in scope
    std::vector<HistogramBucket> histogram;                ///< Key distribution histogram
    std::chrono::steady_clock::time_point collected_at{};  ///< When stats were collected
    std::uint64_t collected_at_version{0};                 ///< Backend version at collection time

    /// @brief Get average key size
    [[nodiscard]] double avg_key_size() const noexcept {
        return key_count > 0 ? static_cast<double>(total_key_bytes) / static_cast<double>(key_count)
                             : 0.0;
    }

    /// @brief Get average value size
    [[nodiscard]] double avg_value_size() const noexcept {
        return key_count > 0
                   ? static_cast<double>(total_value_bytes) / static_cast<double>(key_count)
                   : 0.0;
    }
};

// ============================================================================
// StatisticsConfig
// ============================================================================

/// @brief Configuration for statistics collection
struct StatisticsConfig {
    std::size_t histogram_buckets{100};         ///< Number of histogram buckets
    std::size_t sample_size{10000};             ///< Sample size for large datasets
    bool enable_sampling{true};                 ///< Enable sampling for large datasets
    double staleness_threshold_seconds{300.0};  ///< Max age before stats are stale
};

// ============================================================================
// StatisticsCollector
// ============================================================================

/// @brief Collects and caches statistics from a StateBackend
///
/// StatisticsCollector provides cardinality estimates for query optimization.
/// Statistics are cached per-prefix and can be invalidated when data changes.
///
/// Thread Safety: Thread-safe for concurrent reads. Collection and invalidation
/// acquire exclusive locks.
class StatisticsCollector {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, QueryOptimizerError>;

    /// @brief Construct a collector for a backend
    /// @param backend The state backend to collect from
    /// @param config Collection configuration
    explicit StatisticsCollector(const StateBackend& backend, StatisticsConfig config = {});

    /// @brief Collect statistics for a scope
    ///
    /// If statistics are already cached and not stale, returns cached version.
    /// Otherwise, collects fresh statistics from the backend.
    ///
    /// @param prefix Key prefix for scope (empty = all keys)
    /// @return Statistics for the scope
    [[nodiscard]] Result<ScopeStatistics> collect(std::span<const std::byte> prefix = {});

    /// @brief Get cached statistics without collecting
    ///
    /// @param prefix Key prefix for scope
    /// @return Pointer to cached stats, or nullptr if not cached
    [[nodiscard]] const ScopeStatistics*
    get_cached(std::span<const std::byte> prefix) const noexcept;

    /// @brief Invalidate cached statistics
    ///
    /// @param prefix Prefix to invalidate (empty = invalidate all)
    void invalidate(std::span<const std::byte> prefix = {});

    /// @brief Get the configuration
    [[nodiscard]] const StatisticsConfig& config() const noexcept { return config_; }

private:
    /// @brief Perform actual statistics collection
    [[nodiscard]] ScopeStatistics collect_impl(std::span<const std::byte> prefix);

    /// @brief Build histogram from collected keys
    void build_histogram(ScopeStatistics& stats, std::vector<std::vector<std::byte>>& keys);

    /// @brief Check if cached stats are stale
    [[nodiscard]] bool is_stale(const ScopeStatistics& stats) const noexcept;

    /// @brief Convert prefix to cache key string
    [[nodiscard]] static std::string prefix_to_key(std::span<const std::byte> prefix);

    const StateBackend& backend_;
    StatisticsConfig config_;

    mutable std::shared_mutex cache_mutex_;
    std::unordered_map<std::string, ScopeStatistics> cache_;
};

}  // namespace dotvm::core::state
