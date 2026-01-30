#pragma once

/// @file dependency_graph.hpp
/// @brief Dependency graph for scheduling and execution ordering
///
/// Provides a thread-safe dependency graph with cycle detection, topological
/// ordering, and execution scheduling support.

#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/security/isolation_manager.hpp"

namespace dotvm::core::graph {

// ============================================================================
// Type Aliases
// ============================================================================

/// @brief Unique identifier for a Dot execution context
using DotId = dotvm::core::security::DotId;

// ============================================================================
// DependencyGraphError Enum
// ============================================================================

/// @brief Error codes for dependency graph operations
enum class DependencyGraphError : std::uint8_t {
    /// Node not found
    NodeNotFound = 144,

    /// Node already exists
    NodeAlreadyExists = 145,

    /// Edge not found
    EdgeNotFound = 146,

    /// Max nodes exceeded
    MaxNodesExceeded = 147,

    /// Edge already exists
    EdgeAlreadyExists = 148,

    /// Self-loop detected
    SelfLoop = 149,

    /// Cycle detected during edge insertion
    CycleDetected = 152,

    /// Graph is not a DAG
    GraphNotDAG = 153,

    /// Invalid operation
    InvalidOperation = 156,

    /// Node is not ready for execution
    NodeNotReady = 157,

    /// Node already completed
    NodeAlreadyComplete = 158,

    /// Node already in progress
    NodeAlreadyInProgress = 159,
};

/// @brief Convert DependencyGraphError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(DependencyGraphError error) noexcept {
    switch (error) {
        case DependencyGraphError::NodeNotFound:
            return "NodeNotFound";
        case DependencyGraphError::NodeAlreadyExists:
            return "NodeAlreadyExists";
        case DependencyGraphError::EdgeNotFound:
            return "EdgeNotFound";
        case DependencyGraphError::MaxNodesExceeded:
            return "MaxNodesExceeded";
        case DependencyGraphError::EdgeAlreadyExists:
            return "EdgeAlreadyExists";
        case DependencyGraphError::SelfLoop:
            return "SelfLoop";
        case DependencyGraphError::CycleDetected:
            return "CycleDetected";
        case DependencyGraphError::GraphNotDAG:
            return "GraphNotDAG";
        case DependencyGraphError::InvalidOperation:
            return "InvalidOperation";
        case DependencyGraphError::NodeNotReady:
            return "NodeNotReady";
        case DependencyGraphError::NodeAlreadyComplete:
            return "NodeAlreadyComplete";
        case DependencyGraphError::NodeAlreadyInProgress:
            return "NodeAlreadyInProgress";
    }
    return "Unknown";
}

// ============================================================================
// NodeState Enum
// ============================================================================

/// @brief Execution state of a node
enum class NodeState : std::uint8_t {
    Pending = 0,
    InProgress = 1,
    Completed = 2,
};

/// @brief Convert NodeState to human-readable string
[[nodiscard]] constexpr std::string_view to_string(NodeState state) noexcept {
    switch (state) {
        case NodeState::Pending:
            return "Pending";
        case NodeState::InProgress:
            return "InProgress";
        case NodeState::Completed:
            return "Completed";
    }
    return "Unknown";
}

// ============================================================================
// DependencyGraphConfig Struct
// ============================================================================

/// @brief Configuration for DependencyGraph
struct DependencyGraphConfig {
    /// Enable cycle detection on edge insertion
    bool detect_cycles_on_add{true};

    /// Maximum number of nodes allowed
    std::size_t max_nodes{10000};

    /// @brief Default configuration
    static constexpr DependencyGraphConfig defaults() noexcept { return DependencyGraphConfig{}; }
};

// ============================================================================
// DependencyGraph Class
// ============================================================================

/// @brief Thread-safe dependency graph for scheduling and execution ordering
class DependencyGraph {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, DependencyGraphError>;

    /// @brief Construct a DependencyGraph with optional configuration
    explicit DependencyGraph(
        DependencyGraphConfig config = DependencyGraphConfig::defaults()) noexcept;

    /// @brief Destructor
    ~DependencyGraph() = default;

    // Non-copyable, non-movable (std::shared_mutex is non-movable)
    DependencyGraph(const DependencyGraph&) = delete;
    DependencyGraph& operator=(const DependencyGraph&) = delete;
    DependencyGraph(DependencyGraph&&) = delete;
    DependencyGraph& operator=(DependencyGraph&&) = delete;

    // Node Operations

    /// @brief Add a node to the graph
    [[nodiscard]] Result<void> add_node(DotId id) noexcept;

    /// @brief Remove a node (fails if it has dependents)
    [[nodiscard]] Result<void> remove_node(DotId id) noexcept;

    /// @brief Check if a node exists
    [[nodiscard]] bool has_node(DotId id) const noexcept;

    /// @brief Get the state of a node
    [[nodiscard]] Result<NodeState> get_state(DotId id) const noexcept;

    // Edge Operations

    /// @brief Add a dependency edge: from depends on to
    [[nodiscard]] Result<void> add_edge(DotId from, DotId to) noexcept;

    /// @brief Remove a dependency edge
    [[nodiscard]] Result<void> remove_edge(DotId from, DotId to) noexcept;

    /// @brief Check if an edge exists
    [[nodiscard]] bool has_edge(DotId from, DotId to) const noexcept;

    /// @brief Get dependencies of a node
    [[nodiscard]] Result<std::vector<DotId>> get_dependencies(DotId id) const noexcept;

    /// @brief Get dependents of a node
    [[nodiscard]] Result<std::vector<DotId>> get_dependents(DotId id) const noexcept;

    // Graph Analysis

    /// @brief Get topological execution order (dependencies first)
    [[nodiscard]] Result<std::vector<DotId>> topological_order() const noexcept;

    /// @brief Check if the graph contains a cycle
    [[nodiscard]] bool has_cycle() const noexcept;

    // Execution Scheduling

    /// @brief Get all nodes ready for execution
    [[nodiscard]] std::vector<DotId> get_ready() const noexcept;

    /// @brief Mark a node as in progress (fails if not ready)
    [[nodiscard]] Result<void> mark_in_progress(DotId id) noexcept;

    /// @brief Mark a node as completed and update dependents
    [[nodiscard]] Result<void> notify_complete(DotId id) noexcept;

    /// @brief Reset a node to pending (fails if it has active dependents)
    [[nodiscard]] Result<void> reset_node(DotId id) noexcept;

    // Statistics

    /// @brief Get number of nodes in the graph
    [[nodiscard]] std::size_t node_count() const noexcept;

    /// @brief Get number of edges in the graph
    [[nodiscard]] std::size_t edge_count() const noexcept;

    /// @brief Clear the graph
    void clear() noexcept;

private:
    struct Node {
        DotId id;
        NodeState state{NodeState::Pending};
        std::unordered_set<DotId> dependencies;
        std::unordered_set<DotId> dependents;
        std::size_t pending_dep_count{0};
    };

    DependencyGraphConfig config_;
    std::unordered_map<DotId, Node> nodes_;
    std::unordered_set<DotId> ready_set_;
    std::size_t edge_count_{0};
    mutable std::shared_mutex mutex_;

    // Internal helpers (called with lock held)
    [[nodiscard]] bool would_create_cycle_unlocked(DotId from, DotId to) const noexcept;
    void update_ready_set_unlocked(DotId id) noexcept;
    void recompute_pending_count_unlocked(Node& node) noexcept;
};

}  // namespace dotvm::core::graph

// ============================================================================
// std::formatter specializations
// ============================================================================

template <>
struct std::formatter<dotvm::core::graph::DependencyGraphError> : std::formatter<std::string_view> {
    auto format(dotvm::core::graph::DependencyGraphError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};

template <>
struct std::formatter<dotvm::core::graph::NodeState> : std::formatter<std::string_view> {
    auto format(dotvm::core::graph::NodeState state, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(state), ctx);
    }
};
