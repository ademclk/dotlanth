#pragma once

/// @file replication_manager.hpp
/// @brief STATE-006 Central orchestrator for state replication
///
/// ReplicationManager ties together all replication components:
/// - Raft consensus for leader election and membership
/// - Delta streaming for WAL entry replication
/// - Snapshot transfer for lagging followers
///
/// It provides a unified interface for cluster management and
/// ensures consistency between the consensus and data planes.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/replication/delta_publisher.hpp"
#include "dotvm/core/state/replication/delta_subscriber.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/raft_state.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"
#include "dotvm/core/state/replication/snapshot_receiver.hpp"
#include "dotvm/core/state/replication/snapshot_sender.hpp"
#include "dotvm/core/state/replication/transport.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Replication Configuration
// ============================================================================

/// @brief Configuration for the ReplicationManager
///
/// This struct consolidates all configuration for the replication subsystem,
/// including node identity, cluster membership, timeouts, and sub-component
/// configurations.
struct ReplicationConfig {
    NodeId local_node_id;                   ///< This node's ID
    std::vector<NodeId> initial_peers;      ///< Initial cluster members
    std::string raft_log_path{":memory:"};  ///< Path for Raft log storage

    /// @brief Consistency mode for replication
    ///
    /// Controls how the leader handles write acknowledgment:
    /// - Strong: Wait for majority ACK before returning (linearizable)
    /// - Eventual: Return immediately, replicate asynchronously
    enum class ConsistencyMode : std::uint8_t {
        Strong,    ///< Wait for majority ACK before returning
        Eventual,  ///< Return immediately, async replication
    };
    ConsistencyMode consistency{ConsistencyMode::Strong};

    // Raft timeouts (per Raft paper recommendations)
    std::chrono::milliseconds election_timeout_min{150};  ///< Min election timeout
    std::chrono::milliseconds election_timeout_max{300};  ///< Max election timeout
    std::chrono::milliseconds heartbeat_interval{50};     ///< Heartbeat interval

    // Sub-component configurations
    DeltaPublisherConfig delta_publisher;      ///< Delta streaming publisher config
    DeltaSubscriberConfig delta_subscriber;    ///< Delta streaming subscriber config
    SnapshotSenderConfig snapshot_sender;      ///< Snapshot transfer sender config
    SnapshotReceiverConfig snapshot_receiver;  ///< Snapshot transfer receiver config

    /// @brief Create default configuration for a given node ID
    ///
    /// @param local_id This node's unique identifier
    /// @return Configuration with sensible defaults
    [[nodiscard]] static ReplicationConfig defaults(NodeId local_id) {
        ReplicationConfig config;
        config.local_node_id = local_id;
        config.initial_peers = {};
        config.raft_log_path = ":memory:";
        config.consistency = ConsistencyMode::Strong;
        config.election_timeout_min = std::chrono::milliseconds{150};
        config.election_timeout_max = std::chrono::milliseconds{300};
        config.heartbeat_interval = std::chrono::milliseconds{50};
        config.delta_publisher = DeltaPublisherConfig::defaults();
        config.delta_subscriber = DeltaSubscriberConfig::defaults();
        config.snapshot_sender = SnapshotSenderConfig::defaults();
        config.snapshot_receiver = SnapshotReceiverConfig::defaults();
        return config;
    }
};

// ============================================================================
// Replication Statistics
// ============================================================================

/// @brief Statistics for monitoring replication health
///
/// Provides a snapshot of the current replication state including
/// Raft consensus state, replication progress, and follower tracking.
struct ReplicationStats {
    // Raft state
    RaftRole role;                         ///< Current node role (Leader/Follower/Candidate)
    Term current_term;                     ///< Current Raft term
    std::optional<NodeId> current_leader;  ///< Known leader (if any)
    LogIndex commit_index;                 ///< Highest committed log index

    // Replication progress
    LSN applied_lsn;              ///< Highest LSN applied to state machine
    LSN committed_lsn;            ///< Highest LSN committed (majority ack'd)
    std::size_t pending_entries;  ///< Entries pending replication

    // Follower tracking (populated when leader)
    std::vector<FollowerDeltaState> follower_states;  ///< Per-follower replication state

    // Timing information
    std::chrono::steady_clock::time_point last_heartbeat;     ///< Last heartbeat sent/received
    std::chrono::steady_clock::time_point last_state_change;  ///< Last role/term change
};

// ============================================================================
// Replication Manager
// ============================================================================

/// @brief Central orchestrator for state replication
///
/// ReplicationManager is the main entry point for cluster replication. It
/// coordinates between:
/// - Raft consensus (leader election, log replication, membership)
/// - Delta streaming (efficient WAL entry propagation)
/// - Snapshot transfer (bulk state transfer for lagging followers)
///
/// The manager handles role transitions (follower/candidate/leader) and
/// ensures the appropriate sub-components are activated based on role.
///
/// ## Usage Pattern
///
/// ```cpp
/// // Create configuration
/// auto config = ReplicationConfig::defaults(local_id);
/// config.initial_peers = {peer1, peer2, peer3};
///
/// // Create manager
/// auto result = ReplicationManager::create(
///     config, delta_source, delta_sink, snapshot_source, snapshot_sink, transport);
/// auto manager = std::move(result.value());
///
/// // Start replication
/// manager->start();
///
/// // Main loop
/// while (running) {
///     manager->tick();  // Process replication events
///
///     if (manager->is_leader()) {
///         // Replicate new entries
///         manager->replicate(new_lsn);
///
///         // For strong consistency, wait for commit
///         auto future = manager->wait_for_commit(new_lsn);
///         future.get();  // Blocks until majority ACK
///     }
/// }
///
/// manager->stop();
/// ```
///
/// ## Thread Safety
///
/// All public methods are thread-safe. The manager uses internal locking
/// to protect state transitions and coordinate sub-components.
///
/// ## Error Handling
///
/// Operations return Result<T, ReplicationError> for explicit error handling.
/// Common errors include:
/// - NotLeader: Operation requires leadership
/// - NoLeader: No leader elected
/// - ShuttingDown: Manager is stopping
/// - QuorumNotReached: Cannot reach majority
class ReplicationManager {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    // ========================================================================
    // Factory
    // ========================================================================

    /// @brief Create and wire up all replication components
    ///
    /// Factory method that creates the ReplicationManager and all its
    /// sub-components (delta publisher/subscriber, snapshot sender/receiver).
    /// The components are wired together and ready to start.
    ///
    /// @param config Replication configuration
    /// @param delta_source Source for reading WAL entries (leader side)
    /// @param delta_sink Sink for applying received entries (follower side)
    /// @param snapshot_source Source for reading snapshot data (leader side)
    /// @param snapshot_sink Sink for writing snapshot data (follower side)
    /// @param transport Transport for cluster communication
    /// @return Configured ReplicationManager, or error
    [[nodiscard]] static Result<std::unique_ptr<ReplicationManager>>
    create(ReplicationConfig config, DeltaSource& delta_source, DeltaSink& delta_sink,
           SnapshotSource& snapshot_source, SnapshotSink& snapshot_sink, Transport& transport);

    /// @brief Destructor
    ~ReplicationManager();

    // Non-copyable, non-movable (pImpl with references to external objects)
    ReplicationManager(const ReplicationManager&) = delete;
    ReplicationManager& operator=(const ReplicationManager&) = delete;
    ReplicationManager(ReplicationManager&&) = delete;
    ReplicationManager& operator=(ReplicationManager&&) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Start the replication manager
    ///
    /// Initializes Raft state, starts sub-components, and begins
    /// participating in cluster consensus.
    ///
    /// @return Success, or error if start failed
    [[nodiscard]] Result<void> start();

    /// @brief Stop the replication manager
    ///
    /// Gracefully shuts down all sub-components and stops participating
    /// in cluster consensus. Pending operations may be cancelled.
    void stop();

    /// @brief Check if the manager is running
    [[nodiscard]] bool is_running() const noexcept;

    // ========================================================================
    // Leadership
    // ========================================================================

    /// @brief Check if this node is the cluster leader
    [[nodiscard]] bool is_leader() const noexcept;

    /// @brief Get the current leader (if known)
    ///
    /// @return Leader's NodeId, or nullopt if no leader is known
    [[nodiscard]] std::optional<NodeId> current_leader() const noexcept;

    /// @brief Voluntarily step down as leader
    ///
    /// Transitions to follower role and triggers a new election.
    /// No-op if not currently the leader.
    ///
    /// @return Success, or NotLeader if not currently leader
    [[nodiscard]] Result<void> step_down();

    // ========================================================================
    // Replication (Leader Only)
    // ========================================================================

    /// @brief Trigger replication of a new entry
    ///
    /// Notifies the replication system that a new WAL entry is available
    /// at the given LSN. The entry will be read from the DeltaSource and
    /// streamed to followers.
    ///
    /// In Strong consistency mode, this triggers immediate replication.
    /// In Eventual mode, replication may be batched.
    ///
    /// @param lsn LSN of the new entry to replicate
    /// @return Success, or NotLeader if not the leader
    [[nodiscard]] Result<void> replicate(LSN lsn);

    /// @brief Wait for an entry to be committed (majority acknowledged)
    ///
    /// Returns a future that completes when the entry at the given LSN
    /// has been acknowledged by a majority of the cluster.
    ///
    /// @param lsn LSN to wait for
    /// @return Future that resolves when committed, or with error
    [[nodiscard]] std::future<Result<void>> wait_for_commit(LSN lsn);

    // ========================================================================
    // Membership Management
    // ========================================================================

    /// @brief Add a new peer to the cluster
    ///
    /// Initiates a configuration change to add a new node. The change
    /// must be committed through Raft consensus before taking effect.
    ///
    /// @param peer Node ID of the peer to add
    /// @return Success if change initiated, or error
    [[nodiscard]] Result<void> add_peer(const NodeId& peer);

    /// @brief Remove a peer from the cluster
    ///
    /// Initiates a configuration change to remove a node. The change
    /// must be committed through Raft consensus before taking effect.
    ///
    /// @param peer Node ID of the peer to remove
    /// @return Success if change initiated, or error
    [[nodiscard]] Result<void> remove_peer(const NodeId& peer);

    /// @brief Get list of current cluster peers
    ///
    /// @return Vector of peer NodeIds (excluding local node)
    [[nodiscard]] std::vector<NodeId> peers() const;

    // ========================================================================
    // Statistics
    // ========================================================================

    /// @brief Get current replication statistics
    ///
    /// Returns a snapshot of the current replication state including
    /// Raft consensus state, replication progress, and follower tracking.
    ///
    /// @return Current statistics
    [[nodiscard]] ReplicationStats stats() const;

    // ========================================================================
    // Event Callbacks
    // ========================================================================

    /// @brief Callback type for leader change events
    ///
    /// @param new_leader The new leader's NodeId, or nullopt if no leader
    using LeaderChangeCallback = std::function<void(std::optional<NodeId> new_leader)>;

    /// @brief Callback type for commit events
    ///
    /// @param committed_lsn The newly committed LSN
    using CommitCallback = std::function<void(LSN committed_lsn)>;

    /// @brief Set callback for leader change events
    ///
    /// The callback is invoked when the cluster leader changes. This
    /// includes transitions to/from no-leader state during elections.
    ///
    /// @param callback Function to call on leader change
    void set_leader_change_callback(LeaderChangeCallback callback);

    /// @brief Set callback for commit events
    ///
    /// The callback is invoked when entries are committed (majority ACK'd).
    /// This is useful for triggering downstream operations.
    ///
    /// @param callback Function to call on commit
    void set_commit_callback(CommitCallback callback);

    // ========================================================================
    // Processing
    // ========================================================================

    /// @brief Run one iteration of the replication loop
    ///
    /// Processes pending events including:
    /// - Raft timer checks (election timeout, heartbeat)
    /// - Message handling from transport
    /// - Delta batch processing
    /// - Snapshot transfer progress
    ///
    /// Call this periodically (e.g., every 10ms) or in a dedicated thread.
    /// For event-driven operation, use with poll/select on transport FD.
    void tick();

private:
    /// @brief Implementation details (pImpl idiom for ABI stability)
    struct Impl;

    /// @brief Private constructor (use create() factory method)
    explicit ReplicationManager(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace dotvm::core::state::replication
