/// @file raft_state.cpp
/// @brief Implementation of Raft state management

#include "dotvm/core/state/replication/raft_state.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// RaftStateManager Implementation
// ============================================================================

RaftStateManager::RaftStateManager(NodeId local_id)
    : local_id_(local_id), last_leader_contact_(std::chrono::steady_clock::now()) {
    // Add self as voting member
    membership_.voting_members.insert(local_id_);
}

// ========================================================================
// Role Management
// ========================================================================

RaftRole RaftStateManager::role() const noexcept {
    std::shared_lock lock(mtx_);
    return role_;
}

void RaftStateManager::become_follower(Term term) {
    std::unique_lock lock(mtx_);
    role_ = RaftRole::Follower;
    if (term > persistent_.current_term) {
        persistent_.current_term = term;
        persistent_.voted_for = std::nullopt;
    }
}

void RaftStateManager::become_candidate() {
    std::unique_lock lock(mtx_);
    role_ = RaftRole::Candidate;
    persistent_.current_term = persistent_.current_term.next();
    persistent_.voted_for = local_id_;  // Vote for self
    current_leader_ = std::nullopt;
}

void RaftStateManager::become_leader() {
    std::unique_lock lock(mtx_);
    role_ = RaftRole::Leader;
    current_leader_ = local_id_;
}

// ========================================================================
// Term Management
// ========================================================================

Term RaftStateManager::current_term() const noexcept {
    std::shared_lock lock(mtx_);
    return persistent_.current_term;
}

bool RaftStateManager::update_term(Term new_term) {
    std::unique_lock lock(mtx_);
    if (new_term > persistent_.current_term) {
        persistent_.current_term = new_term;
        persistent_.voted_for = std::nullopt;
        role_ = RaftRole::Follower;
        return true;
    }
    return false;
}

// ========================================================================
// Vote Management
// ========================================================================

std::optional<NodeId> RaftStateManager::voted_for() const {
    std::shared_lock lock(mtx_);
    return persistent_.voted_for;
}

RaftStateManager::Result<void> RaftStateManager::record_vote(const NodeId& candidate) {
    std::unique_lock lock(mtx_);

    // Can only vote once per term
    if (persistent_.voted_for.has_value()) {
        if (persistent_.voted_for.value() == candidate) {
            return {};  // Already voted for this candidate
        }
        return ReplicationError::AlreadyVoted;
    }

    persistent_.voted_for = candidate;
    return {};
}

void RaftStateManager::clear_vote() {
    std::unique_lock lock(mtx_);
    persistent_.voted_for = std::nullopt;
}

// ========================================================================
// Log State
// ========================================================================

LogIndex RaftStateManager::last_log_index() const noexcept {
    std::shared_lock lock(mtx_);
    return persistent_.last_log_index;
}

Term RaftStateManager::last_log_term() const noexcept {
    std::shared_lock lock(mtx_);
    return persistent_.last_log_term;
}

void RaftStateManager::update_last_log(LogIndex index, Term term) {
    std::unique_lock lock(mtx_);
    persistent_.last_log_index = index;
    persistent_.last_log_term = term;
}

// ========================================================================
// Commit State
// ========================================================================

LogIndex RaftStateManager::commit_index() const noexcept {
    std::shared_lock lock(mtx_);
    return volatile_.commit_index;
}

LogIndex RaftStateManager::last_applied() const noexcept {
    std::shared_lock lock(mtx_);
    return volatile_.last_applied;
}

bool RaftStateManager::update_commit_index(LogIndex new_index) {
    std::unique_lock lock(mtx_);
    if (new_index > volatile_.commit_index) {
        volatile_.commit_index = new_index;
        return true;
    }
    return false;
}

bool RaftStateManager::update_last_applied(LogIndex new_index) {
    std::unique_lock lock(mtx_);
    if (new_index > volatile_.last_applied) {
        volatile_.last_applied = new_index;
        return true;
    }
    return false;
}

// ========================================================================
// Leader State
// ========================================================================

std::optional<LogIndex> RaftStateManager::get_next_index(const NodeId& peer) const {
    std::shared_lock lock(mtx_);
    auto it = leader_state_.next_index.find(peer);
    if (it == leader_state_.next_index.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<LogIndex> RaftStateManager::get_match_index(const NodeId& peer) const {
    std::shared_lock lock(mtx_);
    auto it = leader_state_.match_index.find(peer);
    if (it == leader_state_.match_index.end()) {
        return std::nullopt;
    }
    return it->second;
}

void RaftStateManager::set_next_index(const NodeId& peer, LogIndex index) {
    std::unique_lock lock(mtx_);
    leader_state_.next_index[peer] = index;
}

void RaftStateManager::set_match_index(const NodeId& peer, LogIndex index) {
    std::unique_lock lock(mtx_);
    leader_state_.match_index[peer] = index;
}

void RaftStateManager::initialize_leader_state(const std::vector<NodeId>& peers) {
    std::unique_lock lock(mtx_);
    leader_state_.initialize(peers, persistent_.last_log_index);
}

// ========================================================================
// Cluster Membership
// ========================================================================

ClusterMembership RaftStateManager::membership() const {
    std::shared_lock lock(mtx_);
    return membership_;
}

void RaftStateManager::add_voting_member(const NodeId& id) {
    std::unique_lock lock(mtx_);
    membership_.non_voting_members.erase(id);
    membership_.voting_members.insert(id);
}

void RaftStateManager::add_non_voting_member(const NodeId& id) {
    std::unique_lock lock(mtx_);
    if (!membership_.voting_members.contains(id)) {
        membership_.non_voting_members.insert(id);
    }
}

void RaftStateManager::remove_member(const NodeId& id) {
    std::unique_lock lock(mtx_);
    membership_.voting_members.erase(id);
    membership_.non_voting_members.erase(id);
    leader_state_.next_index.erase(id);
    leader_state_.match_index.erase(id);
}

std::vector<NodeId> RaftStateManager::peers() const {
    std::shared_lock lock(mtx_);
    std::vector<NodeId> result;
    for (const auto& id : membership_.voting_members) {
        if (id != local_id_) {
            result.push_back(id);
        }
    }
    for (const auto& id : membership_.non_voting_members) {
        if (id != local_id_) {
            result.push_back(id);
        }
    }
    return result;
}

// ========================================================================
// Leader Tracking
// ========================================================================

std::optional<NodeId> RaftStateManager::current_leader() const {
    std::shared_lock lock(mtx_);
    return current_leader_;
}

void RaftStateManager::set_current_leader(const NodeId& leader) {
    std::unique_lock lock(mtx_);
    current_leader_ = leader;
}

void RaftStateManager::clear_current_leader() {
    std::unique_lock lock(mtx_);
    current_leader_ = std::nullopt;
}

// ========================================================================
// Election Timing
// ========================================================================

void RaftStateManager::touch_leader_contact() {
    std::unique_lock lock(mtx_);
    last_leader_contact_ = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::duration RaftStateManager::time_since_leader_contact() const {
    std::shared_lock lock(mtx_);
    return std::chrono::steady_clock::now() - last_leader_contact_;
}

void RaftStateManager::touch_node_contact(const NodeId& /*node*/) {
    // For now, just update leader contact time
    // In future, could track per-node contact times
    touch_leader_contact();
}

}  // namespace dotvm::core::state::replication
