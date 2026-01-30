/// @file dependency_graph_test.cpp
/// @brief Unit tests for DependencyGraph scheduling and analysis

#include <algorithm>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/graph/dependency_graph.hpp"

namespace dotvm::core::graph {
namespace {

std::vector<DotId> sorted_ids(std::vector<DotId> ids) {
    std::sort(ids.begin(), ids.end());
    return ids;
}

// ============================================================================
// Enum Tests
// ============================================================================

TEST(DependencyGraphErrorTest, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::NodeNotFound), 144);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::NodeAlreadyExists), 145);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::EdgeNotFound), 146);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::MaxNodesExceeded), 147);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::EdgeAlreadyExists), 148);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::SelfLoop), 149);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::CycleDetected), 152);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::GraphNotDAG), 153);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::InvalidOperation), 156);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::NodeNotReady), 157);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::NodeAlreadyComplete), 158);
    EXPECT_EQ(static_cast<std::uint8_t>(DependencyGraphError::NodeAlreadyInProgress), 159);
}

TEST(DependencyGraphErrorTest, ToStringAllValues) {
    EXPECT_EQ(to_string(DependencyGraphError::NodeNotFound), "NodeNotFound");
    EXPECT_EQ(to_string(DependencyGraphError::NodeAlreadyExists), "NodeAlreadyExists");
    EXPECT_EQ(to_string(DependencyGraphError::EdgeNotFound), "EdgeNotFound");
    EXPECT_EQ(to_string(DependencyGraphError::MaxNodesExceeded), "MaxNodesExceeded");
    EXPECT_EQ(to_string(DependencyGraphError::EdgeAlreadyExists), "EdgeAlreadyExists");
    EXPECT_EQ(to_string(DependencyGraphError::SelfLoop), "SelfLoop");
    EXPECT_EQ(to_string(DependencyGraphError::CycleDetected), "CycleDetected");
    EXPECT_EQ(to_string(DependencyGraphError::GraphNotDAG), "GraphNotDAG");
    EXPECT_EQ(to_string(DependencyGraphError::InvalidOperation), "InvalidOperation");
    EXPECT_EQ(to_string(DependencyGraphError::NodeNotReady), "NodeNotReady");
    EXPECT_EQ(to_string(DependencyGraphError::NodeAlreadyComplete), "NodeAlreadyComplete");
    EXPECT_EQ(to_string(DependencyGraphError::NodeAlreadyInProgress), "NodeAlreadyInProgress");
}

TEST(DependencyGraphNodeStateTest, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(NodeState::Pending), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(NodeState::InProgress), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(NodeState::Completed), 2);
}

TEST(DependencyGraphNodeStateTest, ToString) {
    EXPECT_EQ(to_string(NodeState::Pending), "Pending");
    EXPECT_EQ(to_string(NodeState::InProgress), "InProgress");
    EXPECT_EQ(to_string(NodeState::Completed), "Completed");
}

// ============================================================================
// Basic Node Tests
// ============================================================================

TEST(DependencyGraphTest, AddAndRemoveNodes) {
    DependencyGraph graph;

    EXPECT_EQ(graph.node_count(), 0U);

    auto add_result = graph.add_node(1);
    EXPECT_TRUE(add_result.is_ok());
    EXPECT_TRUE(graph.has_node(1));
    EXPECT_EQ(graph.node_count(), 1U);

    auto state_result = graph.get_state(1);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), NodeState::Pending);

    auto ready = sorted_ids(graph.get_ready());
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_EQ(ready.front(), 1U);

    auto remove_result = graph.remove_node(1);
    EXPECT_TRUE(remove_result.is_ok());
    EXPECT_FALSE(graph.has_node(1));
    EXPECT_EQ(graph.node_count(), 0U);
}

TEST(DependencyGraphTest, RemoveMissingNodeFails) {
    DependencyGraph graph;

    auto result = graph.remove_node(42);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), DependencyGraphError::NodeNotFound);
}

TEST(DependencyGraphTest, RespectsMaxNodes) {
    DependencyGraphConfig config;
    config.max_nodes = 1;

    DependencyGraph graph(config);

    EXPECT_TRUE(graph.add_node(1).is_ok());

    auto result = graph.add_node(2);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), DependencyGraphError::MaxNodesExceeded);
}

// ============================================================================
// Edge Tests
// ============================================================================

TEST(DependencyGraphTest, AddEdgeAndQueries) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_node(3).is_ok());

    auto add_result = graph.add_edge(1, 2);
    EXPECT_TRUE(add_result.is_ok());
    EXPECT_TRUE(graph.has_edge(1, 2));
    EXPECT_EQ(graph.edge_count(), 1U);

    auto deps_result = graph.get_dependencies(1);
    ASSERT_TRUE(deps_result.is_ok());
    auto deps = sorted_ids(deps_result.value());
    ASSERT_EQ(deps.size(), 1U);
    EXPECT_EQ(deps.front(), 2U);

    auto dependents_result = graph.get_dependents(2);
    ASSERT_TRUE(dependents_result.is_ok());
    auto dependents = sorted_ids(dependents_result.value());
    ASSERT_EQ(dependents.size(), 1U);
    EXPECT_EQ(dependents.front(), 1U);

    auto remove_result = graph.remove_edge(1, 2);
    EXPECT_TRUE(remove_result.is_ok());
    EXPECT_FALSE(graph.has_edge(1, 2));
    EXPECT_EQ(graph.edge_count(), 0U);
}

TEST(DependencyGraphTest, EdgeErrors) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());

    auto self_loop = graph.add_edge(1, 1);
    EXPECT_TRUE(self_loop.is_err());
    EXPECT_EQ(self_loop.error(), DependencyGraphError::SelfLoop);

    auto missing_node = graph.add_edge(1, 2);
    EXPECT_TRUE(missing_node.is_err());
    EXPECT_EQ(missing_node.error(), DependencyGraphError::NodeNotFound);

    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_edge(1, 2).is_ok());

    auto duplicate = graph.add_edge(1, 2);
    EXPECT_TRUE(duplicate.is_err());
    EXPECT_EQ(duplicate.error(), DependencyGraphError::EdgeAlreadyExists);

    auto missing_edge = graph.remove_edge(2, 1);
    EXPECT_TRUE(missing_edge.is_err());
    EXPECT_EQ(missing_edge.error(), DependencyGraphError::EdgeNotFound);
}

TEST(DependencyGraphTest, RemoveNodeWithDependentsFails) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_edge(2, 1).is_ok());

    auto result = graph.remove_node(1);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), DependencyGraphError::InvalidOperation);
    EXPECT_TRUE(graph.has_node(1));
}

// ============================================================================
// Cycle Detection Tests
// ============================================================================

TEST(DependencyGraphTest, CycleDetectionOnAdd) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_node(3).is_ok());

    EXPECT_TRUE(graph.add_edge(1, 2).is_ok());
    EXPECT_TRUE(graph.add_edge(2, 3).is_ok());
    EXPECT_FALSE(graph.has_cycle());

    auto cycle_result = graph.add_edge(3, 1);
    EXPECT_TRUE(cycle_result.is_err());
    EXPECT_EQ(cycle_result.error(), DependencyGraphError::CycleDetected);
    EXPECT_FALSE(graph.has_edge(3, 1));
    EXPECT_FALSE(graph.has_cycle());
}

TEST(DependencyGraphTest, HasCycleWhenDetectionDisabled) {
    DependencyGraphConfig config;
    config.detect_cycles_on_add = false;

    DependencyGraph graph(config);

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_node(3).is_ok());

    EXPECT_TRUE(graph.add_edge(1, 2).is_ok());
    EXPECT_TRUE(graph.add_edge(2, 3).is_ok());
    EXPECT_TRUE(graph.add_edge(3, 1).is_ok());

    EXPECT_TRUE(graph.has_cycle());
}

// ============================================================================
// Topological Order Tests
// ============================================================================

TEST(DependencyGraphTest, TopologicalOrderEmpty) {
    DependencyGraph graph;

    auto result = graph.topological_order();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().empty());
}

TEST(DependencyGraphTest, TopologicalOrderSingle) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(10).is_ok());

    auto result = graph.topological_order();
    ASSERT_TRUE(result.is_ok());
    auto order = result.value();
    ASSERT_EQ(order.size(), 1U);
    EXPECT_EQ(order.front(), 10U);
}

TEST(DependencyGraphTest, TopologicalOrderLinear) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_node(3).is_ok());

    EXPECT_TRUE(graph.add_edge(3, 2).is_ok());
    EXPECT_TRUE(graph.add_edge(2, 1).is_ok());

    auto result = graph.topological_order();
    ASSERT_TRUE(result.is_ok());
    auto order = result.value();
    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], 1U);
    EXPECT_EQ(order[1], 2U);
    EXPECT_EQ(order[2], 3U);
}

TEST(DependencyGraphTest, TopologicalOrderDiamond) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_node(3).is_ok());
    EXPECT_TRUE(graph.add_node(4).is_ok());

    EXPECT_TRUE(graph.add_edge(4, 2).is_ok());
    EXPECT_TRUE(graph.add_edge(4, 3).is_ok());
    EXPECT_TRUE(graph.add_edge(2, 1).is_ok());
    EXPECT_TRUE(graph.add_edge(3, 1).is_ok());

    auto result = graph.topological_order();
    ASSERT_TRUE(result.is_ok());
    auto order = result.value();
    ASSERT_EQ(order.size(), 4U);

    std::unordered_map<DotId, std::size_t> index;
    for (std::size_t i = 0; i < order.size(); ++i) {
        index[order[i]] = i;
    }

    EXPECT_LT(index[1], index[2]);
    EXPECT_LT(index[1], index[3]);
    EXPECT_LT(index[2], index[4]);
    EXPECT_LT(index[3], index[4]);
}

TEST(DependencyGraphTest, TopologicalOrderFailsOnCycle) {
    DependencyGraphConfig config;
    config.detect_cycles_on_add = false;

    DependencyGraph graph(config);

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_edge(1, 2).is_ok());
    EXPECT_TRUE(graph.add_edge(2, 1).is_ok());

    auto result = graph.topological_order();
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), DependencyGraphError::GraphNotDAG);
}

// ============================================================================
// Ready Set Tests
// ============================================================================

TEST(DependencyGraphTest, ReadySetProgression) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_node(3).is_ok());

    EXPECT_TRUE(graph.add_edge(2, 1).is_ok());
    EXPECT_TRUE(graph.add_edge(3, 2).is_ok());

    auto ready = sorted_ids(graph.get_ready());
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_EQ(ready.front(), 1U);

    auto not_ready = graph.mark_in_progress(2);
    EXPECT_TRUE(not_ready.is_err());
    EXPECT_EQ(not_ready.error(), DependencyGraphError::NodeNotReady);

    EXPECT_TRUE(graph.mark_in_progress(1).is_ok());

    auto in_progress_again = graph.mark_in_progress(1);
    EXPECT_TRUE(in_progress_again.is_err());
    EXPECT_EQ(in_progress_again.error(), DependencyGraphError::NodeAlreadyInProgress);

    EXPECT_TRUE(graph.notify_complete(1).is_ok());

    auto complete_again = graph.notify_complete(1);
    EXPECT_TRUE(complete_again.is_err());
    EXPECT_EQ(complete_again.error(), DependencyGraphError::NodeAlreadyComplete);

    ready = sorted_ids(graph.get_ready());
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_EQ(ready.front(), 2U);

    EXPECT_TRUE(graph.mark_in_progress(2).is_ok());
    EXPECT_TRUE(graph.notify_complete(2).is_ok());

    ready = sorted_ids(graph.get_ready());
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_EQ(ready.front(), 3U);

    EXPECT_TRUE(graph.mark_in_progress(3).is_ok());
    EXPECT_TRUE(graph.notify_complete(3).is_ok());

    ready = sorted_ids(graph.get_ready());
    EXPECT_TRUE(ready.empty());

    auto already_complete = graph.mark_in_progress(3);
    EXPECT_TRUE(already_complete.is_err());
    EXPECT_EQ(already_complete.error(), DependencyGraphError::NodeAlreadyComplete);
}

TEST(DependencyGraphTest, ResetNodeUpdatesReadySet) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_edge(2, 1).is_ok());

    EXPECT_TRUE(graph.mark_in_progress(1).is_ok());
    EXPECT_TRUE(graph.notify_complete(1).is_ok());
    EXPECT_TRUE(graph.mark_in_progress(2).is_ok());
    EXPECT_TRUE(graph.notify_complete(2).is_ok());

    auto reset_result = graph.reset_node(1);
    EXPECT_TRUE(reset_result.is_ok());

    auto state_result = graph.get_state(1);
    ASSERT_TRUE(state_result.is_ok());
    EXPECT_EQ(state_result.value(), NodeState::Pending);

    auto ready = sorted_ids(graph.get_ready());
    ASSERT_EQ(ready.size(), 1U);
    EXPECT_EQ(ready.front(), 1U);
}

TEST(DependencyGraphTest, ResetNodeFailsWithActiveDependents) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_edge(2, 1).is_ok());

    auto reset_result = graph.reset_node(1);
    EXPECT_TRUE(reset_result.is_err());
    EXPECT_EQ(reset_result.error(), DependencyGraphError::InvalidOperation);
}

TEST(DependencyGraphTest, EdgeMutationRejectedDuringExecution) {
    DependencyGraph graph;

    EXPECT_TRUE(graph.add_node(1).is_ok());
    EXPECT_TRUE(graph.add_node(2).is_ok());
    EXPECT_TRUE(graph.add_node(3).is_ok());

    // Mark node 1 as in progress
    EXPECT_TRUE(graph.mark_in_progress(1).is_ok());

    // Adding edge TO a node in progress is allowed (1 is a dependency)
    EXPECT_TRUE(graph.add_edge(2, 1).is_ok());

    // Adding edge FROM a node in progress is rejected
    auto add_result = graph.add_edge(1, 3);
    EXPECT_TRUE(add_result.is_err());
    EXPECT_EQ(add_result.error(), DependencyGraphError::InvalidOperation);

    // Removing edge FROM a node in progress is also rejected
    auto remove_result = graph.remove_edge(1, 3);
    EXPECT_TRUE(remove_result.is_err());
    // Edge doesn't exist, but we check state first
    EXPECT_EQ(remove_result.error(), DependencyGraphError::InvalidOperation);

    // Complete node 1, now node 2 should be ready
    EXPECT_TRUE(graph.notify_complete(1).is_ok());

    // Node 2 is now InProgress - edge mutations from it should be rejected
    EXPECT_TRUE(graph.mark_in_progress(2).is_ok());
    auto add_result2 = graph.add_edge(2, 3);
    EXPECT_TRUE(add_result2.is_err());
    EXPECT_EQ(add_result2.error(), DependencyGraphError::InvalidOperation);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST(DependencyGraphTest, ConcurrentAddNodes) {
    DependencyGraph graph;

    const int thread_count = 4;
    const int nodes_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> ok_count{0};
    std::atomic<int> err_count{0};

    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([&graph, &ok_count, &err_count, t]() {
            for (int i = 0; i < nodes_per_thread; ++i) {
                DotId id = static_cast<DotId>(t * nodes_per_thread + i + 1);
                auto result = graph.add_node(id);
                if (result.is_ok()) {
                    ok_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    err_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(ok_count.load(), thread_count * nodes_per_thread);
    EXPECT_EQ(err_count.load(), 0);
    EXPECT_EQ(graph.node_count(), static_cast<std::size_t>(thread_count * nodes_per_thread));
}

TEST(DependencyGraphTest, ConcurrentReadWrite) {
    DependencyGraph graph;
    const DotId node_count = 200;

    for (DotId id = 1; id <= node_count; ++id) {
        EXPECT_TRUE(graph.add_node(id).is_ok());
    }

    std::atomic<int> read_count{0};
    std::atomic<int> add_errors{0};

    std::thread reader([&graph, &read_count]() {
        for (int i = 0; i < 500; ++i) {
            (void)graph.get_ready();
            (void)graph.has_cycle();
            read_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    std::thread writer([&graph, &add_errors, node_count]() {
        for (DotId id = 2; id <= node_count; ++id) {
            auto result = graph.add_edge(id, id - 1);
            if (result.is_err()) {
                add_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    writer.join();
    reader.join();

    EXPECT_GT(read_count.load(), 0);
    EXPECT_EQ(add_errors.load(), 0);
    EXPECT_EQ(graph.edge_count(), static_cast<std::size_t>(node_count - 1));
    EXPECT_FALSE(graph.has_cycle());
}

}  // namespace
}  // namespace dotvm::core::graph
