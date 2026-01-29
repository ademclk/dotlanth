#pragma once

/// @file cost_model.hpp
/// @brief STATE-010 Cost model for query optimization
///
/// Estimates execution costs for different plans:
/// - I/O cost based on key count and scan type
/// - CPU cost based on predicate evaluation
/// - Selectivity estimation from histograms

#include <cstddef>
#include <span>

#include "dotvm/core/state/execution_plan.hpp"
#include "dotvm/core/state/query_ast.hpp"
#include "dotvm/core/state/statistics.hpp"

namespace dotvm::core::state {

// ============================================================================
// CostModelConfig
// ============================================================================

/// @brief Configuration for cost estimation
///
/// These cost factors are unitless weights for comparing different plans.
/// Default values are tuned for in-memory backends; persistent backends
/// should increase I/O costs.
struct CostModelConfig {
    double cost_per_key_scan{1.0};         ///< I/O cost per key scanned
    double cost_per_key_comparison{0.1};   ///< CPU cost per key comparison
    double cost_per_predicate_eval{0.05};  ///< CPU cost per predicate evaluation
    double cost_per_byte_read{0.001};      ///< I/O cost per byte read
    double full_scan_setup_cost{10.0};     ///< Fixed cost for full scan setup
    double prefix_scan_setup_cost{5.0};    ///< Fixed cost for prefix scan setup
    double aggregate_cost_factor{0.01};    ///< CPU cost factor for aggregation
    double memory_cost_per_row{0.1};       ///< Memory cost per intermediate row
};

// ============================================================================
// CostModel
// ============================================================================

/// @brief Estimates execution costs for query plans
///
/// The cost model uses statistics to estimate the cost of different
/// execution strategies. Costs are unitless and used only for comparison
/// between alternative plans.
///
/// Thread Safety: Stateless, safe for concurrent use.
class CostModel {
public:
    /// @brief Construct with default configuration
    CostModel() = default;

    /// @brief Construct with custom configuration
    explicit CostModel(CostModelConfig config) : config_{std::move(config)} {}

    /// @brief Estimate cost of an execution plan
    ///
    /// @param plan The execution plan to estimate
    /// @param stats Statistics for the scope being queried
    /// @return Estimated cost breakdown
    [[nodiscard]] PlanCost estimate(const ExecutionPlan& plan,
                                    const ScopeStatistics& stats) const noexcept;

    /// @brief Estimate selectivity of a predicate
    ///
    /// Selectivity is the fraction of rows that satisfy the predicate,
    /// ranging from 0.0 (matches nothing) to 1.0 (matches everything).
    ///
    /// @param stats Statistics with histogram
    /// @param pred Predicate to estimate
    /// @return Estimated selectivity (0.0 to 1.0)
    [[nodiscard]] double estimate_selectivity(const ScopeStatistics& stats,
                                              const Predicate& pred) const noexcept;

    /// @brief Estimate cardinality from selectivity
    ///
    /// @param stats Statistics for the scope
    /// @param selectivity Selectivity factor (0.0 to 1.0)
    /// @return Estimated number of rows
    [[nodiscard]] std::size_t estimate_cardinality(const ScopeStatistics& stats,
                                                   double selectivity) const noexcept;

    /// @brief Get the configuration
    [[nodiscard]] const CostModelConfig& config() const noexcept { return config_; }

private:
    /// @brief Estimate cost of a single operator
    [[nodiscard]] PlanCost estimate_operator(const Operator& op, const ScopeStatistics& stats,
                                             std::size_t input_rows) const noexcept;

    /// @brief Find histogram bucket containing a key
    [[nodiscard]] std::size_t find_bucket(const ScopeStatistics& stats,
                                          std::span<const std::byte> key) const noexcept;

    /// @brief Estimate selectivity using histogram interpolation
    [[nodiscard]] double histogram_selectivity(const ScopeStatistics& stats,
                                               const Predicate& pred) const noexcept;

    /// @brief Default selectivity when no histogram available
    [[nodiscard]] double default_selectivity(const Predicate& pred) const noexcept;

    CostModelConfig config_;
};

}  // namespace dotvm::core::state
