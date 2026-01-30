#include "dotvm/core/graph/dependency_graph.hpp"

namespace dotvm::core::graph {

DependencyGraph::DependencyGraph(DependencyGraphConfig config) noexcept : config_(config) {}

DependencyGraph::Result<void> DependencyGraph::add_node(DotId id) noexcept {
    std::unique_lock lock(mutex_);

    if (nodes_.find(id) != nodes_.end()) {
        return DependencyGraphError::NodeAlreadyExists;
    }

    if (nodes_.size() >= config_.max_nodes) {
        return DependencyGraphError::MaxNodesExceeded;
    }

    Node node{id, NodeState::Pending, {}, {}, 0};
    nodes_.emplace(id, std::move(node));
    ready_set_.insert(id);
    return Result<void>{};
}

DependencyGraph::Result<void> DependencyGraph::remove_node(DotId id) noexcept {
    std::unique_lock lock(mutex_);

    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return DependencyGraphError::NodeNotFound;
    }

    if (!it->second.dependents.empty()) {
        return DependencyGraphError::InvalidOperation;
    }

    for (DotId dep_id : it->second.dependencies) {
        auto dep_it = nodes_.find(dep_id);
        if (dep_it != nodes_.end()) {
            dep_it->second.dependents.erase(id);
        }
        if (edge_count_ > 0) {
            --edge_count_;
        }
    }

    ready_set_.erase(id);
    nodes_.erase(it);
    return Result<void>{};
}

bool DependencyGraph::has_node(DotId id) const noexcept {
    std::shared_lock lock(mutex_);
    return nodes_.find(id) != nodes_.end();
}

DependencyGraph::Result<NodeState> DependencyGraph::get_state(DotId id) const noexcept {
    std::shared_lock lock(mutex_);

    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return DependencyGraphError::NodeNotFound;
    }

    return it->second.state;
}

DependencyGraph::Result<void> DependencyGraph::add_edge(DotId from, DotId to) noexcept {
    if (from == to) {
        return DependencyGraphError::SelfLoop;
    }

    std::unique_lock lock(mutex_);

    auto from_it = nodes_.find(from);
    auto to_it = nodes_.find(to);
    if (from_it == nodes_.end() || to_it == nodes_.end()) {
        return DependencyGraphError::NodeNotFound;
    }

    // Reject edge mutations during execution (from node must be Pending)
    if (from_it->second.state != NodeState::Pending) {
        return DependencyGraphError::InvalidOperation;
    }

    if (from_it->second.dependencies.contains(to)) {
        return DependencyGraphError::EdgeAlreadyExists;
    }

    if (config_.detect_cycles_on_add && would_create_cycle_unlocked(from, to)) {
        return DependencyGraphError::CycleDetected;
    }

    from_it->second.dependencies.insert(to);
    to_it->second.dependents.insert(from);
    ++edge_count_;

    if (to_it->second.state != NodeState::Completed) {
        ++from_it->second.pending_dep_count;
    }

    update_ready_set_unlocked(from);
    return Result<void>{};
}

DependencyGraph::Result<void> DependencyGraph::remove_edge(DotId from, DotId to) noexcept {
    std::unique_lock lock(mutex_);

    auto from_it = nodes_.find(from);
    auto to_it = nodes_.find(to);
    if (from_it == nodes_.end() || to_it == nodes_.end()) {
        return DependencyGraphError::NodeNotFound;
    }

    // Reject edge mutations during execution (from node must be Pending)
    if (from_it->second.state != NodeState::Pending) {
        return DependencyGraphError::InvalidOperation;
    }

    if (!from_it->second.dependencies.contains(to)) {
        return DependencyGraphError::EdgeNotFound;
    }

    from_it->second.dependencies.erase(to);
    to_it->second.dependents.erase(from);

    if (edge_count_ > 0) {
        --edge_count_;
    }

    if (to_it->second.state != NodeState::Completed) {
        if (from_it->second.pending_dep_count > 0) {
            --from_it->second.pending_dep_count;
        }
    }

    update_ready_set_unlocked(from);
    return Result<void>{};
}

bool DependencyGraph::has_edge(DotId from, DotId to) const noexcept {
    std::shared_lock lock(mutex_);

    auto it = nodes_.find(from);
    if (it == nodes_.end()) {
        return false;
    }

    return it->second.dependencies.contains(to);
}

DependencyGraph::Result<std::vector<DotId>>
DependencyGraph::get_dependencies(DotId id) const noexcept {
    std::shared_lock lock(mutex_);

    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return DependencyGraphError::NodeNotFound;
    }

    std::vector<DotId> deps;
    deps.reserve(it->second.dependencies.size());
    for (DotId dep_id : it->second.dependencies) {
        deps.push_back(dep_id);
    }

    return deps;
}

DependencyGraph::Result<std::vector<DotId>>
DependencyGraph::get_dependents(DotId id) const noexcept {
    std::shared_lock lock(mutex_);

    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return DependencyGraphError::NodeNotFound;
    }

    std::vector<DotId> deps;
    deps.reserve(it->second.dependents.size());
    for (DotId dep_id : it->second.dependents) {
        deps.push_back(dep_id);
    }

    return deps;
}

DependencyGraph::Result<std::vector<DotId>> DependencyGraph::topological_order() const noexcept {
    std::shared_lock lock(mutex_);

    std::unordered_map<DotId, std::size_t> indegree;
    indegree.reserve(nodes_.size());

    for (const auto& [id, node] : nodes_) {
        indegree.emplace(id, node.dependencies.size());
    }

    std::vector<DotId> queue;
    queue.reserve(nodes_.size());
    for (const auto& [id, count] : indegree) {
        if (count == 0) {
            queue.push_back(id);
        }
    }

    std::vector<DotId> order;
    order.reserve(nodes_.size());

    for (std::size_t index = 0; index < queue.size(); ++index) {
        DotId id = queue[index];
        order.push_back(id);

        auto node_it = nodes_.find(id);
        if (node_it == nodes_.end()) {
            continue;
        }

        for (DotId dependent_id : node_it->second.dependents) {
            auto count_it = indegree.find(dependent_id);
            if (count_it == indegree.end()) {
                continue;
            }
            if (count_it->second > 0) {
                --count_it->second;
                if (count_it->second == 0) {
                    queue.push_back(dependent_id);
                }
            }
        }
    }

    if (order.size() != nodes_.size()) {
        return DependencyGraphError::GraphNotDAG;
    }

    return order;
}

bool DependencyGraph::has_cycle() const noexcept {
    std::shared_lock lock(mutex_);

    enum class Color : std::uint8_t { White = 0, Gray = 1, Black = 2 };

    std::unordered_map<DotId, Color> colors;
    colors.reserve(nodes_.size());

    for (const auto& [id, node] : nodes_) {
        colors.emplace(id, Color::White);
    }

    auto dfs = [&](auto&& self, DotId id) -> bool {
        colors[id] = Color::Gray;

        auto node_it = nodes_.find(id);
        if (node_it != nodes_.end()) {
            for (DotId dep_id : node_it->second.dependencies) {
                auto color_it = colors.find(dep_id);
                if (color_it == colors.end()) {
                    continue;
                }
                if (color_it->second == Color::Gray) {
                    return true;
                }
                if (color_it->second == Color::White) {
                    if (self(self, dep_id)) {
                        return true;
                    }
                }
            }
        }

        colors[id] = Color::Black;
        return false;
    };

    for (const auto& [id, node] : nodes_) {
        if (colors[id] == Color::White) {
            if (dfs(dfs, id)) {
                return true;
            }
        }
    }

    return false;
}

std::vector<DotId> DependencyGraph::get_ready() const noexcept {
    std::shared_lock lock(mutex_);

    std::vector<DotId> ready;
    ready.reserve(ready_set_.size());
    for (DotId id : ready_set_) {
        ready.push_back(id);
    }

    return ready;
}

DependencyGraph::Result<void> DependencyGraph::mark_in_progress(DotId id) noexcept {
    std::unique_lock lock(mutex_);

    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return DependencyGraphError::NodeNotFound;
    }

    Node& node = it->second;
    if (node.state == NodeState::Completed) {
        return DependencyGraphError::NodeAlreadyComplete;
    }
    if (node.state == NodeState::InProgress) {
        return DependencyGraphError::NodeAlreadyInProgress;
    }
    if (node.pending_dep_count != 0) {
        return DependencyGraphError::NodeNotReady;
    }

    node.state = NodeState::InProgress;
    ready_set_.erase(id);
    return Result<void>{};
}

DependencyGraph::Result<void> DependencyGraph::notify_complete(DotId id) noexcept {
    std::unique_lock lock(mutex_);

    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return DependencyGraphError::NodeNotFound;
    }

    Node& node = it->second;
    if (node.state == NodeState::Completed) {
        return DependencyGraphError::NodeAlreadyComplete;
    }
    if (node.state == NodeState::Pending) {
        return DependencyGraphError::InvalidOperation;
    }

    node.state = NodeState::Completed;
    ready_set_.erase(id);

    for (DotId dependent_id : node.dependents) {
        auto dep_it = nodes_.find(dependent_id);
        if (dep_it == nodes_.end()) {
            continue;
        }

        Node& dependent = dep_it->second;
        if (dependent.pending_dep_count > 0) {
            --dependent.pending_dep_count;
        }

        update_ready_set_unlocked(dependent_id);
    }

    return Result<void>{};
}

DependencyGraph::Result<void> DependencyGraph::reset_node(DotId id) noexcept {
    std::unique_lock lock(mutex_);

    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return DependencyGraphError::NodeNotFound;
    }

    Node& node = it->second;
    for (DotId dependent_id : node.dependents) {
        auto dep_it = nodes_.find(dependent_id);
        if (dep_it == nodes_.end()) {
            continue;
        }
        if (dep_it->second.state != NodeState::Completed) {
            return DependencyGraphError::InvalidOperation;
        }
    }

    const bool was_completed = (node.state == NodeState::Completed);
    node.state = NodeState::Pending;

    recompute_pending_count_unlocked(node);
    update_ready_set_unlocked(id);

    if (was_completed) {
        for (DotId dependent_id : node.dependents) {
            auto dep_it = nodes_.find(dependent_id);
            if (dep_it == nodes_.end()) {
                continue;
            }
            ++dep_it->second.pending_dep_count;
            update_ready_set_unlocked(dependent_id);
        }
    }

    return Result<void>{};
}

std::size_t DependencyGraph::node_count() const noexcept {
    std::shared_lock lock(mutex_);
    return nodes_.size();
}

std::size_t DependencyGraph::edge_count() const noexcept {
    std::shared_lock lock(mutex_);
    return edge_count_;
}

void DependencyGraph::clear() noexcept {
    std::unique_lock lock(mutex_);
    nodes_.clear();
    ready_set_.clear();
    edge_count_ = 0;
}

bool DependencyGraph::would_create_cycle_unlocked(DotId from, DotId to) const noexcept {
    std::unordered_set<DotId> visited;
    visited.reserve(nodes_.size());

    std::vector<DotId> stack;
    stack.push_back(to);

    while (!stack.empty()) {
        DotId current = stack.back();
        stack.pop_back();

        if (current == from) {
            return true;
        }

        if (!visited.insert(current).second) {
            continue;
        }

        auto it = nodes_.find(current);
        if (it == nodes_.end()) {
            continue;
        }

        for (DotId dep_id : it->second.dependencies) {
            if (!visited.contains(dep_id)) {
                stack.push_back(dep_id);
            }
        }
    }

    return false;
}

void DependencyGraph::update_ready_set_unlocked(DotId id) noexcept {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return;
    }

    const Node& node = it->second;
    if (node.state == NodeState::Pending && node.pending_dep_count == 0) {
        ready_set_.insert(id);
    } else {
        ready_set_.erase(id);
    }
}

void DependencyGraph::recompute_pending_count_unlocked(Node& node) noexcept {
    std::size_t count = 0;
    for (DotId dep_id : node.dependencies) {
        auto it = nodes_.find(dep_id);
        if (it == nodes_.end()) {
            continue;
        }
        if (it->second.state != NodeState::Completed) {
            ++count;
        }
    }
    node.pending_dep_count = count;
}

}  // namespace dotvm::core::graph
