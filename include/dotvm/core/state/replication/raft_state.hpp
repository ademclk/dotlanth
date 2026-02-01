#pragma once

/// @file raft_state.hpp
/// @brief STATE-006 Raft consensus state management
///
/// Defines the persistent and volatile state for Raft consensus nodes.
/// This implementation follows the Raft paper specification for state
/// management with some additions for cluster membership.

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Raft Node Role
// ============================================================================

/// @brief Role of a Raft node in the cluster
enum class RaftRole : std::uint8_t {
    Follower = 0,   ///< Following a leader, responds to RPCs
    Candidate = 1,  ///< Seeking votes to become leader
    Leader = 2,     ///< Replicating log to followers
};

/// @brief Convert role to string
[[nodiscard]] constexpr std::string_view to_string(RaftRole role) noexcept {
    switch (role) {
        case RaftRole::Follower:
            return "Follower";
        case RaftRole::Candidate:
            return "Candidate";
        case RaftRole::Leader:
            return "Leader";
    }
    return "Unknown";
}

// RaftCommandType and RaftLogEntry are defined in message_types.hpp

// ============================================================================
// Persistent State
// ============================================================================

/// @brief Persistent state that must survive crashes
///
/// This state is written to stable storage before responding to RPCs.
/// Per the Raft paper: currentTerm, votedFor, and log[] are persisted.
struct PersistentState {
    Term current_term{0};              ///< Latest term server has seen
    std::optional<NodeId> voted_for;   ///< CandidateId that received vote in current term
    LogIndex last_log_index{0};        ///< Index of last log entry
    Term last_log_term{0};             ///< Term of last log entry
};

// ============================================================================
// Volatile State
// ============================================================================

/// @brief Volatile state on all servers
///
/// This state is rebuilt after crashes from the log.
struct VolatileState {
    LogIndex commit_index{0};  ///< Index of highest log entry known to be committed
    LogIndex last_applied{0};  ///< Index of highest log entry applied to state machine
};

/// @brief Volatile state on leaders
///
/// Reinitialized after election.
struct LeaderState {
    /// @brief Next log index to send to each server
    std::map<NodeId, LogIndex> next_index;

    /// @brief Highest log index known to be replicated on each server
    std::map<NodeId, LogIndex> match_index;

    /// @brief Initialize leader state for a new term
    void initialize(const std::vector<NodeId>& peers, LogIndex last_log_index) {
        next_index.clear();
        match_index.clear();
        for (const auto& peer : peers) {
            next_index[peer] = LogIndex{last_log_index.value + 1};
            match_index[peer] = LogIndex{0};
        }
    }
};

// ============================================================================
// Cluster Configuration
// ============================================================================

/// @brief Cluster membership configuration
struct ClusterMembership {
    std::set<NodeId> voting_members;      ///< Nodes that can vote
    std::set<NodeId> non_voting_members;  ///< Learners (receive data, don't vote)

    /// @brief Get total cluster size (voting + non-voting)
    [[nodiscard]] std::size_t total_size() const noexcept {
        return voting_members.size() + non_voting_members.size();
    }

    /// @brief Get quorum size for voting
    [[nodiscard]] std::size_t quorum_size() const noexcept {
        return (voting_members.size() / 2) + 1;
    }

    /// @brief Check if node is a voting member
    [[nodiscard]] bool is_voting_member(const NodeId& id) const {
        return voting_members.contains(id);
    }

    /// @brief Check if node is any kind of member
    [[nodiscard]] bool is_member(const NodeId& id) const {
        return voting_members.contains(id) || non_voting_members.contains(id);
    }
};

// ============================================================================
// Raft State Manager
// ============================================================================

/// @brief Manages all Raft state with thread-safe access
///
/// This class provides thread-safe access to both persistent and volatile
/// Raft state. It's designed to be the single source of truth for the
/// RaftNode implementation.
///
/// Thread Safety: All public methods are thread-safe.
class RaftStateManager {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    /// @brief Create a new state manager for a node
    explicit RaftStateManager(NodeId local_id);

    ~RaftStateManager() = default;

    // Non-copyable, non-movable
    RaftStateManager(const RaftStateManager&) = delete;
    RaftStateManager& operator=(const RaftStateManager&) = delete;
    RaftStateManager(RaftStateManager&&) = delete;
    RaftStateManager& operator=(RaftStateManager&&) = delete;

    // ========================================================================
    // Identity
    // ========================================================================

    /// @brief Get this node's ID
    [[nodiscard]] NodeId local_id() const noexcept { return local_id_; }

    // ========================================================================
    // Role Management
    // ========================================================================

    /// @brief Get current role
    [[nodiscard]] RaftRole role() const noexcept;

    /// @brief Transition to follower role
    void become_follower(Term term);

    /// @brief Transition to candidate role
    void become_candidate();

    /// @brief Transition to leader role
    void become_leader();

    // ========================================================================
    // Term Management
    // ========================================================================

    /// @brief Get current term
    [[nodiscard]] Term current_term() const noexcept;

    /// @brief Update term if greater than current
    ///
    /// If the new term is greater, transitions to follower and clears votedFor.
    /// @return true if term was updated
    bool update_term(Term new_term);

    // ========================================================================
    // Vote Management
    // ========================================================================

    /// @brief Get the node we voted for in current term (if any)
    [[nodiscard]] std::optional<NodeId> voted_for() const;

    /// @brief Record a vote for a candidate
    ///
    /// @return Success if vote was recorded, error if already voted for someone else
    [[nodiscard]] Result<void> record_vote(const NodeId& candidate);

    /// @brief Clear the vote (on term change)
    void clear_vote();

    // ========================================================================
    // Log State
    // ========================================================================

    /// @brief Get index of last log entry
    [[nodiscard]] LogIndex last_log_index() const noexcept;

    /// @brief Get term of last log entry
    [[nodiscard]] Term last_log_term() const noexcept;

    /// @brief Update last log entry info
    void update_last_log(LogIndex index, Term term);

    // ========================================================================
    // Commit State
    // ========================================================================

    /// @brief Get commit index
    [[nodiscard]] LogIndex commit_index() const noexcept;

    /// @brief Get last applied index
    [[nodiscard]] LogIndex last_applied() const noexcept;

    /// @brief Update commit index (must be monotonically increasing)
    ///
    /// @return true if index was updated
    bool update_commit_index(LogIndex new_index);

    /// @brief Update last applied index (must be monotonically increasing)
    ///
    /// @return true if index was updated
    bool update_last_applied(LogIndex new_index);

    // ========================================================================
    // Leader State
    // ========================================================================

    /// @brief Get next index for a follower
    [[nodiscard]] std::optional<LogIndex> get_next_index(const NodeId& peer) const;

    /// @brief Get match index for a follower
    [[nodiscard]] std::optional<LogIndex> get_match_index(const NodeId& peer) const;

    /// @brief Update next index for a follower
    void set_next_index(const NodeId& peer, LogIndex index);

    /// @brief Update match index for a follower
    void set_match_index(const NodeId& peer, LogIndex index);

    /// @brief Initialize leader state for a new term
    void initialize_leader_state(const std::vector<NodeId>& peers);

    // ========================================================================
    // Cluster Membership
    // ========================================================================

    /// @brief Get current cluster membership
    [[nodiscard]] ClusterMembership membership() const;

    /// @brief Add a voting member
    void add_voting_member(const NodeId& id);

    /// @brief Add a non-voting member (learner)
    void add_non_voting_member(const NodeId& id);

    /// @brief Remove a member
    void remove_member(const NodeId& id);

    /// @brief Get list of all peers (excluding self)
    [[nodiscard]] std::vector<NodeId> peers() const;

    // ========================================================================
    // Leader Tracking
    // ========================================================================

    /// @brief Get the current leader (if known)
    [[nodiscard]] std::optional<NodeId> current_leader() const;

    /// @brief Set the current leader
    void set_current_leader(const NodeId& leader);

    /// @brief Clear the current leader (on election timeout)
    void clear_current_leader();

    // ========================================================================
    // Election Timing
    // ========================================================================

    /// @brief Record that we received communication from leader
    void touch_leader_contact();

    /// @brief Get time since last leader contact
    [[nodiscard]] std::chrono::steady_clock::duration time_since_leader_contact() const;

    /// @brief Record that we received communication from a node
    void touch_node_contact(const NodeId& node);

private:
    const NodeId local_id_;

    mutable std::shared_mutex mtx_;

    // Role
    RaftRole role_{RaftRole::Follower};

    // Persistent state
    PersistentState persistent_;

    // Volatile state
    VolatileState volatile_;

    // Leader-specific state
    LeaderState leader_state_;

    // Cluster membership
    ClusterMembership membership_;

    // Current leader (if known)
    std::optional<NodeId> current_leader_;

    // Timing
    std::chrono::steady_clock::time_point last_leader_contact_;
};

}  // namespace dotvm::core::state::replication
