#pragma once

/// @file test_cluster.hpp
/// @brief STATE-006 Multi-node test harness for replication testing
///
/// Provides a TestCluster class that manages multiple TestNode instances
/// with shared MockTransport for simulating cluster behavior, fault injection,
/// and verification of replication scenarios.

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "dotvm/core/state/log_record.hpp"
#include "dotvm/core/state/replication/delta_publisher.hpp"
#include "dotvm/core/state/replication/delta_subscriber.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/replication_manager.hpp"
#include "dotvm/core/state/replication/snapshot_receiver.hpp"
#include "dotvm/core/state/replication/snapshot_sender.hpp"
#include "dotvm/core/state/replication/transport.hpp"

namespace dotvm::core::state::replication::testing {

// ============================================================================
// Mock DeltaSource for TestNode
// ============================================================================

/// @brief Mock DeltaSource for test nodes
///
/// Stores log entries in memory for testing delta streaming.
class MockDeltaSource : public DeltaSource {
public:
    MockDeltaSource() = default;

    [[nodiscard]] LSN current_lsn() const noexcept override {
        std::lock_guard lock(mtx_);
        return current_lsn_;
    }

    [[nodiscard]] std::vector<LogRecord> read_entries(LSN from_lsn, std::size_t max_entries,
                                                       std::size_t max_bytes) const override {
        std::lock_guard lock(mtx_);

        std::vector<LogRecord> result;
        std::size_t bytes_read = 0;

        for (const auto& [lsn, record] : entries_) {
            if (lsn < from_lsn) {
                continue;
            }

            std::size_t record_size = record.key.size() + record.value.size() + sizeof(LogRecord);
            if (bytes_read + record_size > max_bytes && !result.empty()) {
                break;
            }

            result.push_back(record);
            bytes_read += record_size;

            if (result.size() >= max_entries) {
                break;
            }
        }

        return result;
    }

    [[nodiscard]] bool is_available(LSN lsn) const noexcept override {
        std::lock_guard lock(mtx_);
        return !entries_.empty() && lsn.value <= current_lsn_.value;
    }

    // ========================================================================
    // Test Helpers
    // ========================================================================

    /// @brief Add an entry to the log
    void add_entry(LogRecord record) {
        std::lock_guard lock(mtx_);
        auto lsn = record.lsn;
        entries_.emplace(lsn, std::move(record));

        if (lsn > current_lsn_) {
            current_lsn_ = lsn;
        }
    }

    /// @brief Set the current LSN directly
    void set_current_lsn(LSN lsn) {
        std::lock_guard lock(mtx_);
        current_lsn_ = lsn;
    }

    /// @brief Get the current LSN (for testing)
    [[nodiscard]] LSN get_current_lsn() const {
        std::lock_guard lock(mtx_);
        return current_lsn_;
    }

    /// @brief Get all entries (for verification)
    [[nodiscard]] std::map<LSN, LogRecord, std::less<>> get_entries() const {
        std::lock_guard lock(mtx_);
        return entries_;
    }

    /// @brief Clear all entries
    void clear() {
        std::lock_guard lock(mtx_);
        entries_.clear();
        current_lsn_ = LSN{0};
    }

private:
    mutable std::mutex mtx_;
    std::map<LSN, LogRecord, std::less<>> entries_;
    LSN current_lsn_{0};
};

// ============================================================================
// Mock DeltaSink for TestNode
// ============================================================================

/// @brief Mock DeltaSink for test nodes
///
/// Receives and stores applied log records for verification.
class MockDeltaSink : public DeltaSink {
public:
    MockDeltaSink() = default;

    [[nodiscard]] LSN applied_lsn() const noexcept override {
        std::lock_guard lock(mtx_);
        return applied_lsn_;
    }

    [[nodiscard]] Result<void> apply_batch(const std::vector<LogRecord>& records) override {
        std::lock_guard lock(mtx_);

        if (fail_applies_) {
            return ReplicationError::DeltaApplyFailed;
        }

        for (const auto& record : records) {
            applied_records_.push_back(record);
            if (record.lsn > applied_lsn_) {
                applied_lsn_ = record.lsn;
            }
        }

        batches_applied_++;
        return {};
    }

    [[nodiscard]] MptHash mpt_root() const override {
        std::lock_guard lock(mtx_);
        return root_;
    }

    // ========================================================================
    // Test Helpers
    // ========================================================================

    /// @brief Set the MPT root hash
    void set_mpt_root(MptHash root) {
        std::lock_guard lock(mtx_);
        root_ = root;
    }

    /// @brief Configure the sink to fail applies
    void set_fail_applies(bool fail) {
        std::lock_guard lock(mtx_);
        fail_applies_ = fail;
    }

    /// @brief Get the number of batches applied
    [[nodiscard]] std::size_t batches_applied() const {
        std::lock_guard lock(mtx_);
        return batches_applied_;
    }

    /// @brief Get all applied records
    [[nodiscard]] std::vector<LogRecord> applied_records() const {
        std::lock_guard lock(mtx_);
        return applied_records_;
    }

    /// @brief Get the applied LSN (for testing)
    [[nodiscard]] LSN get_applied_lsn() const {
        std::lock_guard lock(mtx_);
        return applied_lsn_;
    }

    /// @brief Clear applied records
    void clear() {
        std::lock_guard lock(mtx_);
        applied_records_.clear();
        applied_lsn_ = LSN{0};
        batches_applied_ = 0;
    }

private:
    mutable std::mutex mtx_;
    LSN applied_lsn_{0};
    MptHash root_{};
    std::vector<LogRecord> applied_records_;
    std::size_t batches_applied_{0};
    bool fail_applies_{false};
};

// ============================================================================
// Mock SnapshotSource for TestNode
// ============================================================================

/// @brief Mock SnapshotSource for test nodes
///
/// Provides snapshot data from memory for testing.
class MockSnapshotSource : public SnapshotSource {
public:
    MockSnapshotSource() = default;

    [[nodiscard]] LSN snapshot_lsn() const override {
        std::lock_guard lock(mtx_);
        return lsn_;
    }

    [[nodiscard]] std::size_t total_size() const override {
        std::lock_guard lock(mtx_);
        return data_.size();
    }

    [[nodiscard]] Result<std::vector<std::byte>> read_chunk(std::size_t offset,
                                                             std::size_t size) const override {
        std::lock_guard lock(mtx_);

        if (offset >= data_.size()) {
            return std::vector<std::byte>{};
        }

        auto end = std::min(offset + size, data_.size());
        return std::vector<std::byte>(data_.begin() + static_cast<std::ptrdiff_t>(offset),
                                      data_.begin() + static_cast<std::ptrdiff_t>(end));
    }

    [[nodiscard]] MptHash mpt_root() const override {
        std::lock_guard lock(mtx_);
        return root_;
    }

    // ========================================================================
    // Test Helpers
    // ========================================================================

    /// @brief Set the snapshot data
    void set_snapshot(LSN lsn, std::vector<std::byte> data, MptHash root = {}) {
        std::lock_guard lock(mtx_);
        lsn_ = lsn;
        data_ = std::move(data);
        root_ = root;
    }

    /// @brief Clear snapshot data
    void clear() {
        std::lock_guard lock(mtx_);
        lsn_ = LSN{0};
        data_.clear();
        root_ = {};
    }

private:
    mutable std::mutex mtx_;
    LSN lsn_{0};
    std::vector<std::byte> data_;
    MptHash root_{};
};

// ============================================================================
// Mock SnapshotSink for TestNode
// ============================================================================

/// @brief Mock SnapshotSink for test nodes
///
/// Receives and stores snapshot data for verification.
class MockSnapshotSink : public SnapshotSink {
public:
    MockSnapshotSink() = default;

    [[nodiscard]] Result<void> begin_snapshot(LSN lsn, std::size_t size) override {
        std::lock_guard lock(mtx_);

        if (fail_operations_) {
            return ReplicationError::SnapshotTransferFailed;
        }

        current_lsn_ = lsn;
        expected_size_ = size;
        data_.clear();
        data_.resize(size);
        receiving_ = true;
        return {};
    }

    [[nodiscard]] Result<void> write_chunk(std::size_t offset,
                                            std::span<const std::byte> data) override {
        std::lock_guard lock(mtx_);

        if (fail_operations_) {
            return ReplicationError::SnapshotTransferFailed;
        }

        if (!receiving_) {
            return ReplicationError::InvalidMessage;
        }

        if (offset + data.size() > data_.size()) {
            return ReplicationError::InvalidMessage;
        }

        std::copy(data.begin(), data.end(), data_.begin() + static_cast<std::ptrdiff_t>(offset));
        bytes_written_ += data.size();
        chunks_received_++;
        return {};
    }

    [[nodiscard]] Result<void> finalize_snapshot() override {
        std::lock_guard lock(mtx_);

        if (!receiving_) {
            return ReplicationError::InvalidMessage;
        }

        receiving_ = false;
        finalized_ = true;
        return {};
    }

    void abort_snapshot() override {
        std::lock_guard lock(mtx_);
        receiving_ = false;
        data_.clear();
    }

    [[nodiscard]] MptHash mpt_root() const override {
        std::lock_guard lock(mtx_);
        return root_;
    }

    // ========================================================================
    // Test Helpers
    // ========================================================================

    /// @brief Set the MPT root hash
    void set_mpt_root(MptHash root) {
        std::lock_guard lock(mtx_);
        root_ = root;
    }

    /// @brief Configure the sink to fail operations
    void set_fail_operations(bool fail) {
        std::lock_guard lock(mtx_);
        fail_operations_ = fail;
    }

    /// @brief Check if the snapshot was finalized
    [[nodiscard]] bool is_finalized() const {
        std::lock_guard lock(mtx_);
        return finalized_;
    }

    /// @brief Get the received snapshot data
    [[nodiscard]] std::vector<std::byte> received_data() const {
        std::lock_guard lock(mtx_);
        return data_;
    }

    /// @brief Get the number of chunks received
    [[nodiscard]] std::size_t chunks_received() const {
        std::lock_guard lock(mtx_);
        return chunks_received_;
    }

    /// @brief Clear the sink state
    void clear() {
        std::lock_guard lock(mtx_);
        data_.clear();
        current_lsn_ = LSN{0};
        expected_size_ = 0;
        bytes_written_ = 0;
        chunks_received_ = 0;
        receiving_ = false;
        finalized_ = false;
    }

private:
    mutable std::mutex mtx_;
    LSN current_lsn_{0};
    std::size_t expected_size_{0};
    std::vector<std::byte> data_;
    std::size_t bytes_written_{0};
    std::size_t chunks_received_{0};
    bool receiving_{false};
    bool finalized_{false};
    MptHash root_{};
    bool fail_operations_{false};
};

// ============================================================================
// TestNode
// ============================================================================

/// @brief Represents one node in a test cluster
///
/// Encapsulates a ReplicationManager with all its mock dependencies.
struct TestNode {
    NodeId id;
    std::unique_ptr<ReplicationManager> manager;
    std::unique_ptr<MockDeltaSource> delta_source;
    std::unique_ptr<MockDeltaSink> delta_sink;
    std::unique_ptr<MockSnapshotSource> snapshot_source;
    std::unique_ptr<MockSnapshotSink> snapshot_sink;
    std::unique_ptr<MockTransport> transport;
    bool running{false};

    TestNode() = default;
    ~TestNode() = default;

    // Move-only
    TestNode(const TestNode&) = delete;
    TestNode& operator=(const TestNode&) = delete;
    TestNode(TestNode&&) = default;
    TestNode& operator=(TestNode&&) = default;
};

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Create a NodeId for testing from an index
///
/// Creates deterministic NodeIds based on the index value.
/// @param index The node index (0-based)
/// @return A NodeId with data based on the index
[[nodiscard]] inline NodeId make_test_node_id(std::size_t index) {
    NodeId id;
    // Fill with index-based pattern
    for (std::size_t i = 0; i < id.data.size(); ++i) {
        id.data[i] = static_cast<std::uint8_t>((index + i + 1) % 256);
    }
    // Set first byte to index for easy identification
    id.data[0] = static_cast<std::uint8_t>(index & 0xFF);
    return id;
}

/// @brief Create a LogRecord for testing
///
/// Creates a test log record with the given LSN and key-value data.
/// @param lsn The LSN for the record
/// @param key_str String view for the key
/// @param value_str String view for the value
/// @return A LogRecord with the specified data
[[nodiscard]] inline LogRecord make_test_record(LSN lsn, std::string_view key_str,
                                                 std::string_view value_str) {
    LogRecord record;
    record.lsn = lsn;
    record.type = LogRecordType::Put;

    // Convert strings to byte vectors
    record.key.reserve(key_str.size());
    for (char c : key_str) {
        record.key.push_back(static_cast<std::byte>(c));
    }

    record.value.reserve(value_str.size());
    for (char c : value_str) {
        record.value.push_back(static_cast<std::byte>(c));
    }

    record.tx_id = TxId{lsn.value, 1};
    record.checksum = 0;  // Can compute if needed

    return record;
}

/// @brief Create a simple test record with default key/value
///
/// @param lsn The LSN for the record
/// @return A LogRecord with default test data
[[nodiscard]] inline LogRecord make_test_record(LSN lsn) {
    return make_test_record(lsn, "key", "value");
}

// ============================================================================
// TestCluster
// ============================================================================

/// @brief Multi-node test harness for replication scenarios
///
/// TestCluster manages a collection of TestNode instances with shared
/// MockTransport infrastructure. It provides methods for:
/// - Starting/stopping individual nodes or the entire cluster
/// - Simulating network partitions
/// - Waiting for leader election and commits
/// - Verifying cluster consistency
///
/// @par Usage Example
/// ```cpp
/// auto cluster = TestCluster::create(3);
/// cluster->start_all();
/// cluster->wait_for_leader(5s);
///
/// auto& leader = cluster->leader();
/// cluster->write_to_leader({key}, {value});
///
/// cluster->wait_for_commit(lsn, 1s);
/// EXPECT_TRUE(cluster->all_nodes_consistent());
/// ```
class TestCluster {
public:
    /// @brief Create a cluster with the specified number of nodes
    ///
    /// Creates all nodes but does not start them. Each node gets its own
    /// MockTransport linked to all other nodes.
    ///
    /// @param num_nodes Number of nodes in the cluster (minimum 1)
    /// @return A unique_ptr to the created cluster
    [[nodiscard]] static std::unique_ptr<TestCluster> create(std::size_t num_nodes) {
        assert(num_nodes > 0 && "Cluster must have at least one node");

        auto cluster = std::unique_ptr<TestCluster>(new TestCluster());
        cluster->nodes_.reserve(num_nodes);

        // Create node IDs
        std::vector<NodeId> all_node_ids;
        all_node_ids.reserve(num_nodes);
        for (std::size_t i = 0; i < num_nodes; ++i) {
            all_node_ids.push_back(make_test_node_id(i));
        }

        // Create each node
        for (std::size_t i = 0; i < num_nodes; ++i) {
            TestNode node;
            node.id = all_node_ids[i];
            node.delta_source = std::make_unique<MockDeltaSource>();
            node.delta_sink = std::make_unique<MockDeltaSink>();
            node.snapshot_source = std::make_unique<MockSnapshotSource>();
            node.snapshot_sink = std::make_unique<MockSnapshotSink>();
            node.transport = std::make_unique<MockTransport>();

            // Get peers (all nodes except this one)
            std::vector<NodeId> peers;
            peers.reserve(num_nodes - 1);
            for (std::size_t j = 0; j < num_nodes; ++j) {
                if (j != i) {
                    peers.push_back(all_node_ids[j]);
                }
            }

            // Create ReplicationManager with fast election timeouts for testing
            auto config = ReplicationConfig::defaults(node.id);
            config.initial_peers = std::move(peers);
            config.election_timeout_min = std::chrono::milliseconds{50};
            config.election_timeout_max = std::chrono::milliseconds{100};
            config.heartbeat_interval = std::chrono::milliseconds{20};

            auto result = ReplicationManager::create(
                config, *node.delta_source, *node.delta_sink,
                *node.snapshot_source, *node.snapshot_sink, *node.transport);

            if (result.is_ok()) {
                node.manager = std::move(result.value());
            }

            cluster->nodes_.push_back(std::move(node));
        }

        // Link all transports together
        for (std::size_t i = 0; i < num_nodes; ++i) {
            // Start transport so it has a local_id
            cluster->nodes_[i].transport->start(cluster->nodes_[i].id);

            for (std::size_t j = i + 1; j < num_nodes; ++j) {
                // Start j's transport if not already started
                if (!cluster->nodes_[j].transport->is_running()) {
                    cluster->nodes_[j].transport->start(cluster->nodes_[j].id);
                }
                cluster->nodes_[i].transport->link_to(*cluster->nodes_[j].transport);
            }
        }

        // Connect all nodes to each other
        for (std::size_t i = 0; i < num_nodes; ++i) {
            for (std::size_t j = 0; j < num_nodes; ++j) {
                if (i != j) {
                    cluster->nodes_[i].transport->connect(cluster->nodes_[j].id, "test-address");
                }
            }
        }

        return cluster;
    }

    /// @brief Get node by index
    ///
    /// @param index Zero-based node index
    /// @return Reference to the TestNode
    TestNode& node(std::size_t index) {
        assert(index < nodes_.size() && "Node index out of range");
        return nodes_[index];
    }

    /// @brief Get node by index (const)
    [[nodiscard]] const TestNode& node(std::size_t index) const {
        assert(index < nodes_.size() && "Node index out of range");
        return nodes_[index];
    }

    /// @brief Get the current leader node
    ///
    /// Asserts that exactly one leader exists.
    /// @return Reference to the leader TestNode
    TestNode& leader() {
        TestNode* leader_ptr = nullptr;
        for (auto& n : nodes_) {
            if (n.manager && n.manager->is_leader()) {
                assert(leader_ptr == nullptr && "Multiple leaders found");
                leader_ptr = &n;
            }
        }
        assert(leader_ptr != nullptr && "No leader found");
        return *leader_ptr;
    }

    /// @brief Get the current leader (const)
    [[nodiscard]] const TestNode& leader() const {
        const TestNode* leader_ptr = nullptr;
        for (const auto& n : nodes_) {
            if (n.manager && n.manager->is_leader()) {
                assert(leader_ptr == nullptr && "Multiple leaders found");
                leader_ptr = &n;
            }
        }
        assert(leader_ptr != nullptr && "No leader found");
        return *leader_ptr;
    }

    /// @brief Get all follower nodes
    ///
    /// @return Vector of pointers to follower TestNodes
    [[nodiscard]] std::vector<TestNode*> followers() {
        std::vector<TestNode*> result;
        for (auto& n : nodes_) {
            if (n.manager && !n.manager->is_leader() && n.running) {
                result.push_back(&n);
            }
        }
        return result;
    }

    /// @brief Get all follower nodes (const)
    [[nodiscard]] std::vector<const TestNode*> followers() const {
        std::vector<const TestNode*> result;
        for (const auto& n : nodes_) {
            if (n.manager && !n.manager->is_leader() && n.running) {
                result.push_back(&n);
            }
        }
        return result;
    }

    /// @brief Get the number of nodes in the cluster
    [[nodiscard]] std::size_t size() const { return nodes_.size(); }

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Start all nodes in the cluster
    void start_all() {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            start_node(i);
        }
    }

    /// @brief Stop all nodes in the cluster
    void stop_all() {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            stop_node(i);
        }
    }

    /// @brief Start a specific node
    ///
    /// @param index Node index to start
    void start_node(std::size_t index) {
        assert(index < nodes_.size() && "Node index out of range");
        auto& n = nodes_[index];
        if (n.manager && !n.running) {
            auto result = n.manager->start();
            if (result.is_ok()) {
                n.running = true;
            }
        }
    }

    /// @brief Stop a specific node
    ///
    /// @param index Node index to stop
    void stop_node(std::size_t index) {
        assert(index < nodes_.size() && "Node index out of range");
        auto& n = nodes_[index];
        if (n.manager && n.running) {
            n.manager->stop();
            n.running = false;
        }
    }

    // ========================================================================
    // Cluster Operations
    // ========================================================================

    /// @brief Tick all running nodes
    ///
    /// Calls tick() on each running node's ReplicationManager.
    /// @param iterations Number of tick iterations (default: 1)
    void tick_all(std::size_t iterations = 1) {
        for (std::size_t i = 0; i < iterations; ++i) {
            for (auto& n : nodes_) {
                if (n.manager && n.running) {
                    n.manager->tick();
                }
            }
        }
    }

    /// @brief Wait until a leader is elected
    ///
    /// Blocks until exactly one node becomes leader or timeout expires.
    /// @param timeout Maximum time to wait
    /// @return true if leader was elected, false on timeout
    [[nodiscard]] bool wait_for_leader(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{1000}) {
        auto start = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - start < timeout) {
            tick_all();

            // Count leaders
            int leader_count = 0;
            for (const auto& n : nodes_) {
                if (n.manager && n.running && n.manager->is_leader()) {
                    leader_count++;
                }
            }

            if (leader_count == 1) {
                return true;
            }

            // Small sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }

        return false;
    }

    /// @brief Wait until all nodes commit to a specific LSN
    ///
    /// @param lsn The LSN to wait for
    /// @param timeout Maximum time to wait
    /// @return true if all nodes committed, false on timeout
    [[nodiscard]] bool wait_for_commit(
        LSN lsn, std::chrono::milliseconds timeout = std::chrono::milliseconds{1000}) {
        auto start = std::chrono::steady_clock::now();

        while (std::chrono::steady_clock::now() - start < timeout) {
            tick_all();

            // Check if all running nodes have committed
            bool all_committed = true;
            for (const auto& n : nodes_) {
                if (n.running) {
                    auto stats = n.manager->stats();
                    if (stats.committed_lsn < lsn) {
                        all_committed = false;
                        break;
                    }
                }
            }

            if (all_committed) {
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }

        return false;
    }

    // ========================================================================
    // Fault Injection
    // ========================================================================

    /// @brief Partition a node from the cluster
    ///
    /// The partitioned node will not be able to send or receive messages
    /// from any other node.
    /// @param index Node index to partition
    void partition_node(std::size_t index) {
        assert(index < nodes_.size() && "Node index out of range");

        auto& partitioned = nodes_[index];

        // Partition from all other nodes (bidirectional)
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (i != index) {
                partitioned.transport->partition_from(nodes_[i].id);
                nodes_[i].transport->partition_from(partitioned.id);
            }
        }
    }

    /// @brief Heal partition for a node
    ///
    /// Reconnects the node to all other nodes.
    /// @param index Node index to heal
    void heal_partition(std::size_t index) {
        assert(index < nodes_.size() && "Node index out of range");

        auto& healed = nodes_[index];

        // Heal partition with all other nodes (bidirectional)
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (i != index) {
                healed.transport->heal_partition(nodes_[i].id);
                nodes_[i].transport->heal_partition(healed.id);
            }
        }
    }

    /// @brief Create a bidirectional partition between two nodes
    ///
    /// @param node1 First node index
    /// @param node2 Second node index
    void partition_between(std::size_t node1, std::size_t node2) {
        assert(node1 < nodes_.size() && "Node1 index out of range");
        assert(node2 < nodes_.size() && "Node2 index out of range");
        assert(node1 != node2 && "Cannot partition a node from itself");

        nodes_[node1].transport->partition_from(nodes_[node2].id);
        nodes_[node2].transport->partition_from(nodes_[node1].id);
    }

    /// @brief Heal a bidirectional partition between two nodes
    ///
    /// @param node1 First node index
    /// @param node2 Second node index
    void heal_partition_between(std::size_t node1, std::size_t node2) {
        assert(node1 < nodes_.size() && "Node1 index out of range");
        assert(node2 < nodes_.size() && "Node2 index out of range");

        nodes_[node1].transport->heal_partition(nodes_[node2].id);
        nodes_[node2].transport->heal_partition(nodes_[node1].id);
    }

    // ========================================================================
    // Simulation Helpers
    // ========================================================================

    /// @brief Write a key-value pair to the leader
    ///
    /// Adds a log record to the leader's delta source and triggers replication.
    /// @param key The key bytes
    /// @param value The value bytes
    /// @return The LSN of the written record
    [[nodiscard]] LSN write_to_leader(const std::vector<std::byte>& key,
                                       const std::vector<std::byte>& value) {
        auto& leader_node = leader();

        // Determine next LSN
        LSN next_lsn = leader_node.delta_source->get_current_lsn().next();
        if (next_lsn.value == 1) {
            next_lsn = LSN::first();
        }

        // Create record
        LogRecord record;
        record.lsn = next_lsn;
        record.type = LogRecordType::Put;
        record.key = key;
        record.value = value;
        record.tx_id = TxId{next_lsn.value, 1};
        record.checksum = 0;

        // Add to delta source
        leader_node.delta_source->add_entry(std::move(record));

        // Trigger replication
        auto result = leader_node.manager->replicate(next_lsn);
        (void)result;  // Ignore result - caller can verify via wait_for_commit

        return next_lsn;
    }

    /// @brief Write a key-value pair to the leader (string version)
    ///
    /// @param key The key string
    /// @param value The value string
    /// @return The LSN of the written record
    [[nodiscard]] LSN write_to_leader(std::string_view key, std::string_view value) {
        std::vector<std::byte> key_bytes;
        key_bytes.reserve(key.size());
        for (char c : key) {
            key_bytes.push_back(static_cast<std::byte>(c));
        }

        std::vector<std::byte> value_bytes;
        value_bytes.reserve(value.size());
        for (char c : value) {
            value_bytes.push_back(static_cast<std::byte>(c));
        }

        return write_to_leader(key_bytes, value_bytes);
    }

    // ========================================================================
    // Verification
    // ========================================================================

    /// @brief Check if all running nodes have consistent state
    ///
    /// Verifies that all running nodes have the same:
    /// - Committed LSN
    /// - Applied LSN
    /// - Current leader
    ///
    /// @return true if all nodes are consistent
    [[nodiscard]] bool all_nodes_consistent() const {
        if (nodes_.empty()) {
            return true;
        }

        // Find first running node
        const TestNode* reference = nullptr;
        for (const auto& n : nodes_) {
            if (n.running && n.manager) {
                reference = &n;
                break;
            }
        }

        if (!reference) {
            return true;  // No running nodes
        }

        auto ref_stats = reference->manager->stats();
        auto ref_leader = reference->manager->current_leader();

        // Compare all other running nodes
        for (const auto& n : nodes_) {
            if (&n == reference || !n.running || !n.manager) {
                continue;
            }

            auto stats = n.manager->stats();

            // All nodes should agree on committed LSN
            if (stats.committed_lsn != ref_stats.committed_lsn) {
                return false;
            }

            // All nodes should agree on the leader
            if (n.manager->current_leader() != ref_leader) {
                return false;
            }
        }

        return true;
    }

    /// @brief Check if the cluster has exactly one leader
    [[nodiscard]] bool has_single_leader() const {
        int leader_count = 0;
        for (const auto& n : nodes_) {
            if (n.running && n.manager && n.manager->is_leader()) {
                leader_count++;
            }
        }
        return leader_count == 1;
    }

    /// @brief Get all nodes that think they are the leader
    [[nodiscard]] std::vector<const TestNode*> get_leaders() const {
        std::vector<const TestNode*> result;
        for (const auto& n : nodes_) {
            if (n.running && n.manager && n.manager->is_leader()) {
                result.push_back(&n);
            }
        }
        return result;
    }

    /// @brief Get the index of a node by its ID
    ///
    /// @param id The NodeId to find
    /// @return The index of the node, or nullopt if not found
    [[nodiscard]] std::optional<std::size_t> find_node_index(const NodeId& id) const {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (nodes_[i].id == id) {
                return i;
            }
        }
        return std::nullopt;
    }

private:
    TestCluster() = default;

    std::vector<TestNode> nodes_;
};

}  // namespace dotvm::core::state::replication::testing
