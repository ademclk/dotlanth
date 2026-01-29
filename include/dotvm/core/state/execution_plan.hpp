#pragma once

/// @file execution_plan.hpp
/// @brief STATE-010 Execution plan types for query optimizer
///
/// Physical execution plan representation:
/// - Operator types: FullScan, PrefixScan, Filter, Project, Aggregate, Limit
/// - ExecutionPlan: Pipeline of operators with cost estimate
/// - PlanExecutor: Executes plans against StateBackend

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/query_ast.hpp"
#include "dotvm/core/state/statistics.hpp"

namespace dotvm::core::state {

// Forward declarations
class StateBackend;
struct PlanCost;

// ============================================================================
// Operator Types
// ============================================================================

/// @brief Type of operator in execution plan
enum class OperatorType : std::uint8_t {
    FullScan = 0,    ///< Scan all keys
    PrefixScan = 1,  ///< Scan keys with prefix
    Filter = 2,      ///< Apply predicates
    Project = 3,     ///< Select fields
    Aggregate = 4,   ///< Compute aggregate
    Limit = 5,       ///< Cap output rows
};

/// @brief Convert OperatorType to string
[[nodiscard]] constexpr const char* to_string(OperatorType type) noexcept {
    switch (type) {
        case OperatorType::FullScan:
            return "FullScan";
        case OperatorType::PrefixScan:
            return "PrefixScan";
        case OperatorType::Filter:
            return "Filter";
        case OperatorType::Project:
            return "Project";
        case OperatorType::Aggregate:
            return "Aggregate";
        case OperatorType::Limit:
            return "Limit";
    }
    return "Unknown";
}

// ============================================================================
// Operator Structs
// ============================================================================

/// @brief Full table scan operator
struct FullScanOp {
    // No additional parameters - scans everything
};

/// @brief Prefix scan operator
struct PrefixScanOp {
    std::vector<std::byte> prefix;  ///< Key prefix to scan
};

/// @brief Filter operator - applies predicates
struct FilterOp {
    std::vector<Predicate> predicates;  ///< Predicates to apply (AND)
};

/// @brief Project operator - selects key/value fields
struct ProjectOp {
    bool include_key{true};     ///< Include key in output
    bool include_value{true};   ///< Include value in output
};

/// @brief Aggregate operator - computes aggregate function
struct AggregateOp {
    AggregateFunc func;  ///< Aggregate function to compute
};

/// @brief Limit operator - caps output rows
struct LimitOp {
    std::size_t count;  ///< Maximum rows to output
};

/// @brief Variant type for all operator types
using Operator = std::variant<FullScanOp, PrefixScanOp, FilterOp, ProjectOp, AggregateOp, LimitOp>;

/// @brief Get the type of an operator
[[nodiscard]] inline OperatorType get_type(const Operator& op) noexcept {
    return std::visit([](const auto& o) -> OperatorType {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, FullScanOp>) return OperatorType::FullScan;
        else if constexpr (std::is_same_v<T, PrefixScanOp>) return OperatorType::PrefixScan;
        else if constexpr (std::is_same_v<T, FilterOp>) return OperatorType::Filter;
        else if constexpr (std::is_same_v<T, ProjectOp>) return OperatorType::Project;
        else if constexpr (std::is_same_v<T, AggregateOp>) return OperatorType::Aggregate;
        else if constexpr (std::is_same_v<T, LimitOp>) return OperatorType::Limit;
    }, op);
}

// ============================================================================
// PlanCost
// ============================================================================

/// @brief Cost estimate for an execution plan
///
/// Costs are unitless estimates used for comparison. Lower is better.
/// The cost model estimates these based on statistics.
struct PlanCost {
    double io_cost{0.0};      ///< I/O cost (disk/memory reads)
    double cpu_cost{0.0};     ///< CPU cost (comparisons, computation)
    double memory_cost{0.0};  ///< Memory cost (intermediate storage)

    /// @brief Get total cost
    [[nodiscard]] double total() const noexcept {
        return io_cost + cpu_cost + memory_cost;
    }

    /// @brief Comparison (by total cost)
    [[nodiscard]] bool operator<(const PlanCost& other) const noexcept {
        return total() < other.total();
    }

    /// @brief Addition
    [[nodiscard]] PlanCost operator+(const PlanCost& other) const noexcept {
        return {
            .io_cost = io_cost + other.io_cost,
            .cpu_cost = cpu_cost + other.cpu_cost,
            .memory_cost = memory_cost + other.memory_cost
        };
    }

    PlanCost& operator+=(const PlanCost& other) noexcept {
        io_cost += other.io_cost;
        cpu_cost += other.cpu_cost;
        memory_cost += other.memory_cost;
        return *this;
    }
};

// ============================================================================
// ExecutionPlan
// ============================================================================

/// @brief Physical execution plan
///
/// A pipeline of operators executed in order. The first operator must be
/// a scan (FullScan or PrefixScan), followed by transformation operators.
struct ExecutionPlan {
    std::vector<Operator> operators;  ///< Operators in pipeline order
    PlanCost total_cost;               ///< Estimated total cost
    std::size_t estimated_output_rows{0};  ///< Estimated output cardinality

    /// @brief Check if plan is valid (has at least a scan operator)
    [[nodiscard]] bool is_valid() const noexcept {
        if (operators.empty()) return false;
        const auto type = get_type(operators.front());
        return type == OperatorType::FullScan || type == OperatorType::PrefixScan;
    }

    /// @brief Get the scan operator (first operator)
    [[nodiscard]] const Operator* scan_operator() const noexcept {
        if (operators.empty()) return nullptr;
        const auto type = get_type(operators.front());
        if (type == OperatorType::FullScan || type == OperatorType::PrefixScan) {
            return &operators.front();
        }
        return nullptr;
    }

    /// @brief Convert plan to human-readable string
    [[nodiscard]] std::string to_string() const;
};

// ============================================================================
// PlanExecutor
// ============================================================================

/// @brief Executes plans against a StateBackend
///
/// Processes operators in pipeline fashion, streaming results through
/// the callback function.
class PlanExecutor {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, QueryOptimizerError>;

    /// @brief Callback for iteration results
    using IterateCallback = std::function<bool(std::span<const std::byte> key,
                                                std::span<const std::byte> value)>;

    /// @brief Construct executor for a backend
    explicit PlanExecutor(const StateBackend& backend);

    /// @brief Execute a plan
    ///
    /// @param plan The plan to execute
    /// @param callback Called for each output row
    /// @return Success or error
    [[nodiscard]] Result<void> execute(const ExecutionPlan& plan,
                                        const IterateCallback& callback) const;

private:
    const StateBackend& backend_;
};

}  // namespace dotvm::core::state
