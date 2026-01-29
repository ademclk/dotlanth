#pragma once

/// @file query_ast.hpp
/// @brief STATE-010 Query AST for cost-based query optimizer
///
/// Provides query representation types:
/// - PredicateOp: Comparison operators for filtering
/// - Predicate: Single filter condition with matching logic
/// - RangeBounds: Key range derived from predicates
/// - QueryNode variants: AST nodes for query operations
/// - Query::Builder: Fluent API for query construction

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace dotvm::core::state {

// ============================================================================
// Predicate Operations
// ============================================================================

/// @brief Predicate comparison operators
enum class PredicateOp : std::uint8_t {
    Eq = 0,      ///< Equality (key == operand)
    Lt = 1,      ///< Less than (key < operand)
    Le = 2,      ///< Less than or equal (key <= operand)
    Gt = 3,      ///< Greater than (key > operand)
    Ge = 4,      ///< Greater than or equal (key >= operand)
    Prefix = 5,  ///< Prefix match (key starts with operand)
};

/// @brief Convert PredicateOp to string
[[nodiscard]] constexpr const char* to_string(PredicateOp op) noexcept {
    switch (op) {
        case PredicateOp::Eq:
            return "Eq";
        case PredicateOp::Lt:
            return "Lt";
        case PredicateOp::Le:
            return "Le";
        case PredicateOp::Gt:
            return "Gt";
        case PredicateOp::Ge:
            return "Ge";
        case PredicateOp::Prefix:
            return "Prefix";
    }
    return "Unknown";
}

// ============================================================================
// Aggregate Functions
// ============================================================================

/// @brief Aggregate function types
enum class AggregateFunc : std::uint8_t {
    Count = 0,  ///< Count rows
    Sum = 1,    ///< Sum values (numeric)
    Min = 2,    ///< Minimum value
    Max = 3,    ///< Maximum value
};

/// @brief Convert AggregateFunc to string
[[nodiscard]] constexpr const char* to_string(AggregateFunc func) noexcept {
    switch (func) {
        case AggregateFunc::Count:
            return "Count";
        case AggregateFunc::Sum:
            return "Sum";
        case AggregateFunc::Min:
            return "Min";
        case AggregateFunc::Max:
            return "Max";
    }
    return "Unknown";
}

// ============================================================================
// Predicate
// ============================================================================

/// @brief Single filter predicate
///
/// Represents a comparison between a key and an operand value.
/// The matches() method evaluates whether a given key satisfies
/// this predicate.
struct Predicate {
    PredicateOp op;                  ///< Comparison operator
    std::vector<std::byte> operand;  ///< Value to compare against

    /// @brief Check if a key matches this predicate
    /// @param key The key to test
    /// @return true if key satisfies this predicate
    [[nodiscard]] bool matches(std::span<const std::byte> key) const noexcept;
};

// ============================================================================
// RangeBounds
// ============================================================================

/// @brief Key range bounds derived from predicates
///
/// Represents an optional lower and upper bound on keys.
/// Used by the optimizer to determine scan ranges.
struct RangeBounds {
    std::optional<std::vector<std::byte>> lower;  ///< Lower bound (if set)
    std::optional<std::vector<std::byte>> upper;  ///< Upper bound (if set)
    bool lower_inclusive{true};                   ///< Include lower bound
    bool upper_inclusive{false};                  ///< Include upper bound

    /// @brief Check if a key is within these bounds
    [[nodiscard]] bool contains(std::span<const std::byte> key) const noexcept;

    /// @brief Derive bounds from a set of predicates
    [[nodiscard]] static RangeBounds from_predicates(const std::vector<Predicate>& predicates);
};

// ============================================================================
// AST Node Types
// ============================================================================

struct QueryNode;  // Forward declaration

/// @brief Full table/prefix scan node
struct ScanNode {
    std::vector<std::byte> prefix;  ///< Scan prefix (empty = full scan)
};

/// @brief Filter node - applies predicates to input
struct FilterNode {
    std::vector<Predicate> predicates;  ///< Filter predicates (AND)
    std::unique_ptr<QueryNode> input;   ///< Input node to filter
};

/// @brief Project node - selects key/value fields
struct ProjectNode {
    bool include_key{true};            ///< Include key in output
    bool include_value{true};          ///< Include value in output
    std::unique_ptr<QueryNode> input;  ///< Input node to project
};

/// @brief Aggregate node - computes aggregate function
struct AggregateNode {
    AggregateFunc func;                ///< Aggregate function
    std::unique_ptr<QueryNode> input;  ///< Input node to aggregate
};

/// @brief Limit node - caps output rows
struct LimitNode {
    std::size_t count;                 ///< Maximum rows to return
    std::unique_ptr<QueryNode> input;  ///< Input node to limit
};

/// @brief Variant type for all query node types
using QueryNodeVariant = std::variant<ScanNode, FilterNode, ProjectNode, AggregateNode, LimitNode>;

/// @brief Query AST node wrapper
///
/// Contains the node variant and provides traversal helpers.
struct QueryNode {
    QueryNodeVariant node;

    /// @brief Deep clone this node and all children
    [[nodiscard]] std::unique_ptr<QueryNode> clone() const;

    /// @brief Get the input node (if any)
    [[nodiscard]] QueryNode* input() noexcept;
    [[nodiscard]] const QueryNode* input() const noexcept;
};

// ============================================================================
// Query
// ============================================================================

/// @brief Complete query with AST root
///
/// Represents a query ready for optimization and execution.
/// Use Query::Builder for fluent construction.
struct Query {
    std::unique_ptr<QueryNode> root;  ///< Root of the query AST

    /// @brief Deep clone this query
    [[nodiscard]] Query clone() const;

    /// @brief Collect all predicates from filter nodes
    [[nodiscard]] std::vector<Predicate> collect_predicates() const;

    /// @brief Fluent query builder
    class Builder;
};

/// @brief Fluent query builder
///
/// Enables construction of queries using a chainable API:
/// @code
/// auto query = Query::Builder()
///     .scan(prefix)
///     .filter(PredicateOp::Ge, lower)
///     .filter(PredicateOp::Lt, upper)
///     .limit(100)
///     .build();
/// @endcode
class Query::Builder {
public:
    Builder() = default;

    /// @brief Start with a full scan or prefix scan
    /// @param prefix Scan prefix (empty = full scan)
    Builder& scan(std::vector<std::byte> prefix = {});

    /// @brief Add a filter predicate
    /// @param op Comparison operator
    /// @param operand Value to compare against
    Builder& filter(PredicateOp op, std::vector<std::byte> operand);

    /// @brief Add projection
    /// @param include_key Include key in output
    /// @param include_value Include value in output
    Builder& project(bool include_key, bool include_value);

    /// @brief Add aggregate function
    /// @param func Aggregate function to apply
    Builder& aggregate(AggregateFunc func);

    /// @brief Add row limit
    /// @param count Maximum rows to return
    Builder& limit(std::size_t count);

    /// @brief Build the final query
    [[nodiscard]] Query build();

private:
    std::unique_ptr<QueryNode> root_;
    std::vector<Predicate> pending_filters_;
};

}  // namespace dotvm::core::state
