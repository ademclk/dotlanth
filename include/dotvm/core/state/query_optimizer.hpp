#pragma once

/// @file query_optimizer.hpp
/// @brief STATE-010 Cost-based query optimizer
///
/// Main query optimizer interface:
/// - Analyzes queries and generates execution plans
/// - Uses statistics for cost-based plan selection
/// - Supports predicate pushdown optimization

#include <cstddef>
#include <functional>
#include <memory>
#include <span>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/cost_model.hpp"
#include "dotvm/core/state/execution_plan.hpp"
#include "dotvm/core/state/query_ast.hpp"
#include "dotvm/core/state/statistics.hpp"

namespace dotvm::core::state {

// Forward declarations
class StateBackend;

// ============================================================================
// QueryOptimizerConfig
// ============================================================================

/// @brief Configuration for the query optimizer
struct QueryOptimizerConfig {
    std::size_t max_plan_alternatives{10};  ///< Max plans to consider
    bool enable_statistics{true};           ///< Use statistics for optimization
    bool prefer_prefix_scan{true};          ///< Prefer prefix scan when applicable
    double prefix_selectivity_threshold{0.3};  ///< Use prefix scan if selectivity < this
    StatisticsConfig stats_config{};        ///< Statistics collection config
    CostModelConfig cost_config{};          ///< Cost estimation config
};

// ============================================================================
// QueryOptimizer
// ============================================================================

/// @brief Cost-based query optimizer for StateBackend
///
/// Analyzes Query AST and generates optimal ExecutionPlan based on:
/// - Collected statistics (cardinality, histograms)
/// - Cost model estimates
/// - Predicate pushdown opportunities
///
/// Target: <1ms plan generation for typical queries.
///
/// @par Thread Safety
/// Thread-safe for concurrent optimize() calls. analyze() and
/// invalidate_statistics() require exclusive access.
///
/// @par Example Usage
/// @code
/// QueryOptimizer optimizer(backend);
/// optimizer.analyze();  // Collect statistics
///
/// auto query = Query::Builder()
///     .scan(prefix)
///     .filter(PredicateOp::Ge, lower)
///     .limit(100)
///     .build();
///
/// auto plan = optimizer.optimize(query);
/// optimizer.execute(query, callback);
/// @endcode
class QueryOptimizer {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, QueryOptimizerError>;

    /// @brief Callback for query results
    using IterateCallback = std::function<bool(std::span<const std::byte> key,
                                                std::span<const std::byte> value)>;

    /// @brief Construct optimizer for a backend
    /// @param backend The state backend to optimize queries for
    /// @param config Optimizer configuration
    explicit QueryOptimizer(const StateBackend& backend,
                             QueryOptimizerConfig config = {});

    // ========================================================================
    // Query Optimization
    // ========================================================================

    /// @brief Optimize a query and generate execution plan
    ///
    /// Analyzes the query AST, generates candidate plans, and selects
    /// the lowest-cost plan based on statistics.
    ///
    /// @param query The query to optimize
    /// @return Optimized execution plan
    [[nodiscard]] Result<ExecutionPlan> optimize(const Query& query);

    /// @brief Optimize and execute a query
    ///
    /// Convenience method that optimizes and immediately executes.
    ///
    /// @param query The query to execute
    /// @param callback Called for each result row
    /// @return Success or error
    [[nodiscard]] Result<void> execute(const Query& query,
                                        const IterateCallback& callback);

    // ========================================================================
    // Statistics Management
    // ========================================================================

    /// @brief Collect statistics for optimization
    ///
    /// Should be called before optimization for best results.
    /// Statistics are cached and can be invalidated when data changes.
    ///
    /// @param prefix Prefix to analyze (empty = global statistics)
    /// @return Success or error
    [[nodiscard]] Result<void> analyze(std::span<const std::byte> prefix = {});

    /// @brief Get cached statistics
    ///
    /// @param prefix Prefix to get statistics for
    /// @return Pointer to statistics, or nullptr if not available
    [[nodiscard]] const ScopeStatistics* statistics(
        std::span<const std::byte> prefix = {}) const noexcept;

    /// @brief Invalidate cached statistics
    ///
    /// Call this when underlying data changes to ensure
    /// fresh statistics are collected on next optimize().
    ///
    /// @param prefix Prefix to invalidate (empty = all)
    void invalidate_statistics(std::span<const std::byte> prefix = {});

    /// @brief Get the configuration
    [[nodiscard]] const QueryOptimizerConfig& config() const noexcept { return config_; }

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    /// @brief Generate candidate execution plans for a query
    [[nodiscard]] std::vector<ExecutionPlan> generate_plans(const Query& query);

    /// @brief Select the best plan from candidates
    [[nodiscard]] ExecutionPlan select_best_plan(std::vector<ExecutionPlan>& candidates,
                                                   const ScopeStatistics& stats);

    /// @brief Choose scan method based on predicates and statistics
    [[nodiscard]] Operator choose_scan_method(const Query& query,
                                               const ScopeStatistics& stats);

    /// @brief Extract prefix from query predicates
    [[nodiscard]] std::vector<std::byte> extract_prefix(const Query& query) const;

    /// @brief Check if a predicate can be pushed to scan
    [[nodiscard]] bool is_pushable_to_scan(const Predicate& pred) const noexcept;

    /// @brief Create default statistics when none available
    [[nodiscard]] ScopeStatistics create_default_stats() const;

    const StateBackend& backend_;
    QueryOptimizerConfig config_;
    StatisticsCollector stats_collector_;
    CostModel cost_model_;
    PlanExecutor executor_;
};

}  // namespace dotvm::core::state
