/// @file replication_manager.cpp
/// @brief STATE-006 ReplicationManager implementation
///
/// The ReplicationManager orchestrates all replication components:
/// - RaftStateManager for consensus state
/// - RaftLog (InMemoryRaftLog) for Raft entries
/// - DeltaPublisher (when leader) for streaming to followers
/// - DeltaSubscriber (when follower) for receiving from leader
/// - SnapshotSender (when leader) for initial sync
/// - SnapshotReceiver (when follower) for initial sync

#include "dotvm/core/state/replication/replication_manager.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>

#include "dotvm/core/state/replication/message_serializer.hpp"
#include "dotvm/core/state/replication/raft_log.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Constants
// ============================================================================

/// @brief Threshold for switching from delta streaming to snapshot transfer
constexpr std::uint64_t SNAPSHOT_THRESHOLD = 1000;

// ============================================================================
// Implementation Structure
// ============================================================================

struct ReplicationManager::Impl {
    // Configuration
    ReplicationConfig config;

    // State manager
    RaftStateManager state_manager;

    // Raft log storage
    std::unique_ptr<RaftLog> raft_log;

    // Sub-components
    std::unique_ptr<DeltaPublisher> delta_publisher;
    std::unique_ptr<DeltaSubscriber> delta_subscriber;
    std::unique_ptr<SnapshotSender> snapshot_sender;
    std::unique_ptr<SnapshotReceiver> snapshot_receiver;

    // External references
    DeltaSource& delta_source;
    DeltaSink& delta_sink;
    SnapshotSource& snapshot_source;
    SnapshotSink& snapshot_sink;
    Transport& transport;

    // Runtime state
    std::atomic<bool> running{false};
    std::atomic<bool> started{false};
    mutable std::mutex mtx;

    // Election timing
    std::chrono::steady_clock::time_point last_heartbeat_sent;
    std::chrono::steady_clock::time_point election_deadline;
    std::mt19937 rng{std::random_device{}()};

    // Callbacks
    LeaderChangeCallback leader_change_callback;
    CommitCallback commit_callback;

    // Commit tracking
    std::map<LSN, std::promise<Result<void>>> commit_promises;
    std::mutex commit_mtx;
    std::condition_variable commit_cv;
    LSN last_committed_lsn{0};

    // Vote tracking for elections
    std::set<NodeId> votes_received;
    std::size_t votes_count{0};

    // Constructor
    Impl(ReplicationConfig cfg, DeltaSource& ds, DeltaSink& dk, SnapshotSource& ss,
         SnapshotSink& ssk, Transport& t)
        : config(std::move(cfg)),
          state_manager(config.local_node_id),
          delta_source(ds),
          delta_sink(dk),
          snapshot_source(ss),
          snapshot_sink(ssk),
          transport(t) {
        // Add self to membership
        state_manager.add_voting_member(config.local_node_id);

        // Add initial peers
        for (const auto& peer : config.initial_peers) {
            state_manager.add_voting_member(peer);
        }

        reset_election_timer();
    }

    // ========================================================================
    // Timer Management
    // ========================================================================

    void reset_election_timer() {
        std::uniform_int_distribution<int> dist(
            static_cast<int>(config.election_timeout_min.count()),
            static_cast<int>(config.election_timeout_max.count()));

        auto timeout = std::chrono::milliseconds{dist(rng)};
        election_deadline = std::chrono::steady_clock::now() + timeout;
    }

    [[nodiscard]] std::chrono::milliseconds random_election_timeout() {
        std::uniform_int_distribution<int> dist(
            static_cast<int>(config.election_timeout_min.count()),
            static_cast<int>(config.election_timeout_max.count()));
        return std::chrono::milliseconds{dist(rng)};
    }

    [[nodiscard]] bool election_timeout_expired() const {
        return std::chrono::steady_clock::now() >= election_deadline;
    }

    [[nodiscard]] bool heartbeat_due() const {
        return std::chrono::steady_clock::now() >= last_heartbeat_sent + config.heartbeat_interval;
    }

    // ========================================================================
    // State Transitions
    // ========================================================================

    void become_leader() {
        auto peers = state_manager.peers();
        state_manager.become_leader();
        state_manager.set_current_leader(config.local_node_id);

        // Initialize leader state
        state_manager.initialize_leader_state(peers);

        // Stop follower components
        if (delta_subscriber && delta_subscriber->is_running()) {
            delta_subscriber->stop();
        }
        if (snapshot_receiver && snapshot_receiver->is_running()) {
            snapshot_receiver->stop();
        }

        // Start leader components
        if (delta_publisher) {
            auto result = delta_publisher->start();
            (void)result;

            // Add all peers as followers
            for (const auto& peer : peers) {
                auto add_result = delta_publisher->add_follower(peer, delta_source.current_lsn());
                (void)add_result;
            }
        }
        if (snapshot_sender) {
            auto result = snapshot_sender->start();
            (void)result;
        }

        last_heartbeat_sent = std::chrono::steady_clock::now();

        // Send initial heartbeat
        send_heartbeat();

        // Notify callback
        if (leader_change_callback) {
            leader_change_callback(config.local_node_id);
        }
    }

    void become_follower(Term term) {
        auto was_leader = state_manager.role() == RaftRole::Leader;
        state_manager.become_follower(term);
        reset_election_timer();

        // Stop leader components
        if (delta_publisher && delta_publisher->is_running()) {
            delta_publisher->stop();
        }
        if (snapshot_sender && snapshot_sender->is_running()) {
            snapshot_sender->stop();
        }

        // Start follower components if leader is known
        auto leader = state_manager.current_leader();
        if (leader.has_value() && delta_subscriber) {
            delta_subscriber->set_leader(*leader);
            auto result = delta_subscriber->start();
            (void)result;
        }

        if (was_leader && leader_change_callback) {
            leader_change_callback(state_manager.current_leader());
        }
    }

    void step_down_to_follower() {
        state_manager.become_follower(state_manager.current_term());
        state_manager.clear_current_leader();
        reset_election_timer();

        // Stop leader components
        if (delta_publisher && delta_publisher->is_running()) {
            delta_publisher->stop();
        }
        if (snapshot_sender && snapshot_sender->is_running()) {
            snapshot_sender->stop();
        }

        if (leader_change_callback) {
            leader_change_callback(std::nullopt);
        }
    }

    void start_election() {
        state_manager.become_candidate();
        votes_received.clear();
        votes_received.insert(config.local_node_id);  // Vote for self
        votes_count = 1;

        // If single node cluster, immediately become leader
        auto membership = state_manager.membership();
        if (membership.quorum_size() == 1) {
            become_leader();
            return;
        }

        // Send RequestVote to all peers
        send_request_vote();

        reset_election_timer();
    }

    // ========================================================================
    // Message Sending
    // ========================================================================

    void send_request_vote() {
        RequestVote request;
        request.term = state_manager.current_term();
        request.candidate_id = config.local_node_id;
        request.last_log_index = state_manager.last_log_index();
        request.last_log_term = state_manager.last_log_term();

        auto serialize_result = MessageSerializer::serialize(request);
        if (serialize_result.is_err()) {
            return;
        }

        transport.broadcast(StreamType::Raft, serialize_result.value());
    }

    void send_heartbeat() {
        AppendEntries heartbeat;
        heartbeat.term = state_manager.current_term();
        heartbeat.leader_id = config.local_node_id;
        heartbeat.prev_log_index = state_manager.last_log_index();
        heartbeat.prev_log_term = state_manager.last_log_term();
        heartbeat.entries = {};  // Empty for heartbeat
        heartbeat.leader_commit = state_manager.commit_index();

        auto serialize_result = MessageSerializer::serialize(heartbeat);
        if (serialize_result.is_err()) {
            return;
        }

        transport.broadcast(StreamType::Raft, serialize_result.value());
        last_heartbeat_sent = std::chrono::steady_clock::now();
    }

    void send_vote_response(const NodeId& to, bool granted) {
        RequestVoteResponse response;
        response.term = state_manager.current_term();
        response.vote_granted = granted;

        auto serialize_result = MessageSerializer::serialize(response);
        if (serialize_result.is_err()) {
            return;
        }

        auto send_result = transport.send(to, StreamType::Raft, serialize_result.value());
        (void)send_result;
    }

    void send_append_entries_response(const NodeId& to, bool success, LogIndex match_index) {
        AppendEntriesResponse response;
        response.term = state_manager.current_term();
        response.success = success;
        response.match_index = match_index;

        auto serialize_result = MessageSerializer::serialize(response);
        if (serialize_result.is_err()) {
            return;
        }

        auto send_result = transport.send(to, StreamType::Raft, serialize_result.value());
        (void)send_result;
    }

    void send_append_entries(const NodeId& peer) {
        auto next_index = state_manager.get_next_index(peer);
        if (!next_index.has_value()) {
            return;
        }

        // Get entries from the log
        LogIndex start_index = *next_index;
        auto entries = raft_log->get_range(start_index, LogIndex{start_index.value + 100});

        // Get previous log info
        LogIndex prev_log_index = start_index.prev();
        Term prev_log_term{0};
        if (prev_log_index.is_valid()) {
            auto term = raft_log->term_at(prev_log_index);
            if (term.has_value()) {
                prev_log_term = *term;
            }
        }

        AppendEntries request;
        request.term = state_manager.current_term();
        request.leader_id = config.local_node_id;
        request.prev_log_index = prev_log_index;
        request.prev_log_term = prev_log_term;
        request.entries = std::move(entries);
        request.leader_commit = state_manager.commit_index();

        auto serialize_result = MessageSerializer::serialize(request);
        if (serialize_result.is_err()) {
            return;
        }

        auto send_result = transport.send(peer, StreamType::Raft, serialize_result.value());
        (void)send_result;
    }

    // ========================================================================
    // Message Handlers
    // ========================================================================

    void handle_request_vote(const NodeId& from, const RequestVote& request) {
        // If request term is higher, update term and become follower
        if (request.term > state_manager.current_term()) {
            become_follower(request.term);
        }

        bool vote_granted = false;

        // Only grant vote if:
        // 1. Request term >= current term
        // 2. Haven't voted for someone else this term
        // 3. Candidate's log is at least as up-to-date as ours
        if (request.term >= state_manager.current_term()) {
            auto voted_for = state_manager.voted_for();
            bool can_vote = !voted_for.has_value() || voted_for.value() == request.candidate_id;

            // Log up-to-date check (Section 5.4.1 of Raft paper)
            bool log_ok = false;
            Term our_last_term = state_manager.last_log_term();
            LogIndex our_last_index = state_manager.last_log_index();

            if (request.last_log_term > our_last_term) {
                log_ok = true;
            } else if (request.last_log_term == our_last_term &&
                       request.last_log_index >= our_last_index) {
                log_ok = true;
            }

            if (can_vote && log_ok) {
                auto vote_result = state_manager.record_vote(request.candidate_id);
                if (vote_result.is_ok()) {
                    vote_granted = true;
                    reset_election_timer();
                }
            }
        }

        send_vote_response(from, vote_granted);
    }

    void handle_request_vote_response(const NodeId& from, const RequestVoteResponse& response) {
        // Ignore if not a candidate
        if (state_manager.role() != RaftRole::Candidate) {
            return;
        }

        // If response term is higher, become follower
        if (response.term > state_manager.current_term()) {
            become_follower(response.term);
            return;
        }

        // Ignore old term responses
        if (response.term < state_manager.current_term()) {
            return;
        }

        if (response.vote_granted) {
            votes_received.insert(from);
            votes_count = votes_received.size();

            // Check if we have majority
            auto membership = state_manager.membership();
            if (votes_count >= membership.quorum_size()) {
                become_leader();
            }
        }
    }

    void handle_append_entries(const NodeId& from, const AppendEntries& request) {
        // If request term is higher, update and become follower
        if (request.term > state_manager.current_term()) {
            become_follower(request.term);
        }

        // Reject if term is too old
        if (request.term < state_manager.current_term()) {
            send_append_entries_response(from, false, LogIndex::invalid());
            return;
        }

        // Valid leader - reset election timer
        reset_election_timer();
        state_manager.set_current_leader(request.leader_id);
        state_manager.touch_leader_contact();

        // If we're a candidate, step down
        if (state_manager.role() == RaftRole::Candidate) {
            become_follower(request.term);
        }

        // Update delta subscriber with leader
        if (delta_subscriber) {
            delta_subscriber->set_leader(request.leader_id);
            if (!delta_subscriber->is_running()) {
                auto result = delta_subscriber->start();
                (void)result;
            }
        }

        // Check log consistency
        bool log_ok = true;
        if (request.prev_log_index.is_valid()) {
            auto term_at_prev = raft_log->term_at(request.prev_log_index);
            if (!term_at_prev.has_value() || term_at_prev.value() != request.prev_log_term) {
                log_ok = false;
            }
        }

        if (!log_ok) {
            send_append_entries_response(from, false, LogIndex::invalid());
            return;
        }

        // Append new entries
        LogIndex match_index = request.prev_log_index;
        for (const auto& entry : request.entries) {
            // Check for conflicting entry
            auto existing = raft_log->get(entry.index);
            if (existing.has_value() && existing->term != entry.term) {
                // Conflict - truncate from this point
                auto truncate_result = raft_log->truncate(entry.index);
                (void)truncate_result;
            }

            // Append if not already present
            if (!existing.has_value()) {
                auto append_result = raft_log->append(entry);
                if (append_result.is_err()) {
                    break;
                }
                state_manager.update_last_log(entry.index, entry.term);
            }
            match_index = entry.index;
        }

        // Update commit index
        if (request.leader_commit > state_manager.commit_index()) {
            LogIndex new_commit =
                LogIndex{std::min(request.leader_commit.value, match_index.value)};
            state_manager.update_commit_index(new_commit);
        }

        send_append_entries_response(from, true, match_index);
    }

    void handle_append_entries_response(const NodeId& from, const AppendEntriesResponse& response) {
        // Ignore if not leader
        if (state_manager.role() != RaftRole::Leader) {
            return;
        }

        // If response term is higher, become follower
        if (response.term > state_manager.current_term()) {
            become_follower(response.term);
            return;
        }

        // Ignore old term responses
        if (response.term < state_manager.current_term()) {
            return;
        }

        if (response.success) {
            // Update match_index and next_index for follower
            state_manager.set_match_index(from, response.match_index);
            state_manager.set_next_index(from, response.match_index.next());

            // Check if we can advance commit index
            advance_commit_index();
        } else {
            // Decrement next_index and retry
            auto next_index = state_manager.get_next_index(from);
            if (next_index.has_value() && next_index->value > 1) {
                state_manager.set_next_index(from, next_index->prev());
                send_append_entries(from);
            }
        }
    }

    void handle_delta_batch(const NodeId& from, const DeltaBatch& batch) {
        if (state_manager.role() != RaftRole::Follower) {
            return;
        }

        if (delta_subscriber) {
            auto result = delta_subscriber->receive_batch(batch);
            (void)result;
        }
    }

    void handle_delta_ack(const NodeId& from, const DeltaAck& ack) {
        if (state_manager.role() != RaftRole::Leader) {
            return;
        }

        if (delta_publisher) {
            delta_publisher->handle_ack(from, ack.acked_lsn);
        }

        // Check if commit waiters can be resolved
        resolve_commit_waiters();
    }

    void handle_snapshot_chunk(const NodeId& from, const SnapshotChunk& chunk) {
        if (state_manager.role() != RaftRole::Follower) {
            return;
        }

        if (snapshot_receiver) {
            auto result = snapshot_receiver->receive_chunk(chunk);
            (void)result;
        }
    }

    void handle_snapshot_request(const NodeId& from, const SnapshotRequest& request) {
        if (state_manager.role() != RaftRole::Leader) {
            return;
        }

        if (snapshot_sender) {
            auto result = snapshot_sender->initiate_transfer(from);
            (void)result;
        }
    }

    // ========================================================================
    // Commit Index Management
    // ========================================================================

    void advance_commit_index() {
        // Find the highest index N such that a majority of match_index[i] >= N
        // and log[N].term == currentTerm

        auto peers = state_manager.peers();
        std::vector<LogIndex> indices;
        indices.push_back(state_manager.last_log_index());  // Include leader

        for (const auto& peer : peers) {
            auto match = state_manager.get_match_index(peer);
            if (match.has_value()) {
                indices.push_back(*match);
            }
        }

        std::sort(indices.begin(), indices.end(),
                  [](const LogIndex& a, const LogIndex& b) { return a.value > b.value; });

        // Find median (majority must have this index or higher)
        std::size_t majority_pos = indices.size() / 2;
        if (majority_pos < indices.size()) {
            LogIndex commit_candidate = indices[majority_pos];

            // Only commit if the entry is from current term
            auto term_at_candidate = raft_log->term_at(commit_candidate);
            if (term_at_candidate.has_value() &&
                *term_at_candidate == state_manager.current_term() &&
                commit_candidate > state_manager.commit_index()) {
                state_manager.update_commit_index(commit_candidate);

                // Notify commit callback
                notify_commit(delta_source.current_lsn());
            }
        }
    }

    void resolve_commit_waiters() {
        if (delta_publisher) {
            LSN min_ack = delta_publisher->min_acknowledged_lsn();
            std::lock_guard lock(commit_mtx);
            auto it = commit_promises.begin();
            while (it != commit_promises.end()) {
                if (it->first <= min_ack) {
                    it->second.set_value({});  // Success
                    it = commit_promises.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    void notify_commit(LSN lsn) {
        std::lock_guard lock(commit_mtx);

        if (lsn > last_committed_lsn) {
            last_committed_lsn = lsn;

            // Fulfill any waiting promises
            auto it = commit_promises.begin();
            while (it != commit_promises.end()) {
                if (it->first <= lsn) {
                    it->second.set_value({});
                    it = commit_promises.erase(it);
                } else {
                    ++it;
                }
            }

            // Invoke callback
            if (commit_callback) {
                commit_callback(lsn);
            }
        }

        commit_cv.notify_all();
    }

    // ========================================================================
    // Tick Processing
    // ========================================================================

    void tick_leader() {
        // Send heartbeats if due
        if (heartbeat_due()) {
            send_heartbeat();
        }

        // Publish delta batches
        if (delta_publisher && delta_publisher->is_running()) {
            (void)delta_publisher->publish();
        }

        // Process snapshot transfers
        if (snapshot_sender && snapshot_sender->is_running()) {
            (void)snapshot_sender->process_transfers();
        }

        // Check for far-behind followers that need snapshots
        check_followers_need_snapshot();
    }

    void tick_follower() {
        // Check election timeout
        if (election_timeout_expired()) {
            start_election();
            return;
        }

        // Process pending deltas
        if (delta_subscriber && delta_subscriber->is_running()) {
            (void)delta_subscriber->process_pending();
        }
    }

    void tick_candidate() {
        // Check election timeout (start new election)
        if (election_timeout_expired()) {
            start_election();  // Re-run election with new term
        }
    }

    void check_followers_need_snapshot() {
        // Check if any follower is too far behind and needs a snapshot
        if (!delta_publisher || !snapshot_sender) {
            return;
        }

        LSN current_lsn = delta_source.current_lsn();
        auto follower_states = delta_publisher->get_all_follower_states();

        for (const auto& fstate : follower_states) {
            // If follower is more than SNAPSHOT_THRESHOLD behind
            std::uint64_t lag = current_lsn.value > fstate.acknowledged_lsn.value
                                    ? current_lsn.value - fstate.acknowledged_lsn.value
                                    : 0;

            if (lag > SNAPSHOT_THRESHOLD) {
                // Check if snapshot transfer is not already in progress
                auto transfer_state = snapshot_sender->get_transfer_state(fstate.follower_id);
                if (!transfer_state.has_value() ||
                    (transfer_state->status != TransferStatus::Pending &&
                     transfer_state->status != TransferStatus::InProgress)) {
                    // Initiate snapshot transfer
                    auto result = snapshot_sender->initiate_transfer(fstate.follower_id);
                    (void)result;
                }
            }
        }
    }

    // ========================================================================
    // Transport Message Handling
    // ========================================================================

    void setup_message_handler() {
        transport.set_message_callback(
            [this](const NodeId& from, StreamType /*stream*/, std::span<const std::byte> data) {
                // Deserialize and dispatch message
                auto deserialize_result = MessageSerializer::deserialize(data);
                if (deserialize_result.is_err()) {
                    return;
                }

                const auto& message = deserialize_result.value();

                std::lock_guard lock(mtx);

                // Dispatch to appropriate handler
                std::visit(
                    [this, &from](const auto& msg) {
                        using T = std::decay_t<decltype(msg)>;

                        if constexpr (std::same_as<T, RequestVote>) {
                            handle_request_vote(from, msg);
                        } else if constexpr (std::same_as<T, RequestVoteResponse>) {
                            handle_request_vote_response(from, msg);
                        } else if constexpr (std::same_as<T, AppendEntries>) {
                            handle_append_entries(from, msg);
                        } else if constexpr (std::same_as<T, AppendEntriesResponse>) {
                            handle_append_entries_response(from, msg);
                        } else if constexpr (std::same_as<T, DeltaBatch>) {
                            handle_delta_batch(from, msg);
                        } else if constexpr (std::same_as<T, DeltaAck>) {
                            handle_delta_ack(from, msg);
                        } else if constexpr (std::same_as<T, SnapshotChunk>) {
                            handle_snapshot_chunk(from, msg);
                        } else if constexpr (std::same_as<T, SnapshotRequest>) {
                            handle_snapshot_request(from, msg);
                        }
                        // Other message types can be added as needed
                    },
                    message);
            });
    }
};

// ============================================================================
// Factory
// ============================================================================

ReplicationManager::Result<std::unique_ptr<ReplicationManager>>
ReplicationManager::create(ReplicationConfig config, DeltaSource& delta_source,
                           DeltaSink& delta_sink, SnapshotSource& snapshot_source,
                           SnapshotSink& snapshot_sink, Transport& transport) {
    // Validate configuration
    if (config.local_node_id.is_null()) {
        return ReplicationError::InvalidNodeId;
    }

    auto impl = std::make_unique<Impl>(std::move(config), delta_source, delta_sink, snapshot_source,
                                       snapshot_sink, transport);

    // Create Raft log
    RaftLogConfig log_cfg;
    log_cfg.storage_path = impl->config.raft_log_path;
    auto log_result = create_raft_log(log_cfg);
    if (log_result.is_err()) {
        return log_result.error();
    }
    impl->raft_log = std::move(log_result.value());

    // Create delta publisher (used when leader)
    impl->delta_publisher =
        std::make_unique<DeltaPublisher>(impl->config.delta_publisher, delta_source, transport);

    // Create delta subscriber (used when follower)
    impl->delta_subscriber = std::make_unique<DeltaSubscriber>(
        impl->config.delta_subscriber, delta_sink, transport, NodeId::null());

    // Create snapshot sender (used when leader)
    impl->snapshot_sender =
        std::make_unique<SnapshotSender>(impl->config.snapshot_sender, snapshot_source, transport);

    // Create snapshot receiver (used when follower)
    impl->snapshot_receiver = std::make_unique<SnapshotReceiver>(impl->config.snapshot_receiver,
                                                                 snapshot_sink, transport);

    // Set up message handler for transport
    impl->setup_message_handler();

    return std::unique_ptr<ReplicationManager>(new ReplicationManager(std::move(impl)));
}

ReplicationManager::ReplicationManager(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ReplicationManager::~ReplicationManager() {
    if (is_running()) {
        stop();
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

ReplicationManager::Result<void> ReplicationManager::start() {
    std::lock_guard lock(impl_->mtx);

    if (impl_->running.load()) {
        return {};  // Already running
    }

    // Start transport if not running
    if (!impl_->transport.is_running()) {
        auto result = impl_->transport.start(impl_->config.local_node_id);
        if (result.is_err()) {
            return result.error();
        }
    }

    // For single-node cluster, become leader immediately
    auto membership = impl_->state_manager.membership();
    if (membership.voting_members.size() == 1 &&
        membership.is_voting_member(impl_->config.local_node_id)) {
        impl_->become_leader();

        // Start delta publisher
        if (impl_->delta_publisher) {
            auto result = impl_->delta_publisher->start();
            if (result.is_err()) {
                return result.error();
            }
        }
    }

    impl_->running.store(true);
    impl_->started.store(true);

    return {};
}

void ReplicationManager::stop() {
    std::lock_guard lock(impl_->mtx);

    if (!impl_->running.load()) {
        return;
    }

    impl_->running.store(false);

    // Stop sub-components
    if (impl_->delta_publisher && impl_->delta_publisher->is_running()) {
        impl_->delta_publisher->stop();
    }

    if (impl_->delta_subscriber && impl_->delta_subscriber->is_running()) {
        impl_->delta_subscriber->stop();
    }

    if (impl_->snapshot_sender && impl_->snapshot_sender->is_running()) {
        impl_->snapshot_sender->stop();
    }

    if (impl_->snapshot_receiver && impl_->snapshot_receiver->is_running()) {
        impl_->snapshot_receiver->stop();
    }

    // Cancel pending commit promises
    {
        std::lock_guard commit_lock(impl_->commit_mtx);
        for (auto& [lsn, promise] : impl_->commit_promises) {
            promise.set_value(ReplicationError::ShuttingDown);
        }
        impl_->commit_promises.clear();
    }
}

bool ReplicationManager::is_running() const noexcept {
    return impl_->running.load();
}

// ============================================================================
// Leadership
// ============================================================================

bool ReplicationManager::is_leader() const noexcept {
    return impl_->state_manager.role() == RaftRole::Leader;
}

std::optional<NodeId> ReplicationManager::current_leader() const noexcept {
    return impl_->state_manager.current_leader();
}

ReplicationManager::Result<void> ReplicationManager::step_down() {
    std::lock_guard lock(impl_->mtx);

    if (impl_->state_manager.role() != RaftRole::Leader) {
        return ReplicationError::NotLeader;
    }

    impl_->step_down_to_follower();

    // Stop leader-side components
    if (impl_->delta_publisher && impl_->delta_publisher->is_running()) {
        impl_->delta_publisher->stop();
    }

    if (impl_->snapshot_sender && impl_->snapshot_sender->is_running()) {
        impl_->snapshot_sender->stop();
    }

    return {};
}

// ============================================================================
// Replication
// ============================================================================

ReplicationManager::Result<void> ReplicationManager::replicate(LSN lsn) {
    std::lock_guard lock(impl_->mtx);

    if (impl_->state_manager.role() != RaftRole::Leader) {
        return ReplicationError::NotLeader;
    }

    if (!impl_->running.load()) {
        return ReplicationError::ShuttingDown;
    }

    // Notify delta publisher of new entries
    if (impl_->delta_publisher && impl_->delta_publisher->is_running()) {
        impl_->delta_publisher->notify_new_entries();
        (void)impl_->delta_publisher->publish();
    }

    // For single-node cluster, commit immediately
    auto membership = impl_->state_manager.membership();
    if (membership.voting_members.size() == 1) {
        impl_->notify_commit(lsn);
    }

    return {};
}

std::future<ReplicationManager::Result<void>> ReplicationManager::wait_for_commit(LSN lsn) {
    std::lock_guard lock(impl_->commit_mtx);

    // If already committed, return immediately
    if (lsn <= impl_->last_committed_lsn) {
        std::promise<Result<void>> p;
        p.set_value({});
        return p.get_future();
    }

    // Create a promise and store it
    std::promise<Result<void>> promise;
    auto future = promise.get_future();
    impl_->commit_promises.emplace(lsn, std::move(promise));

    return future;
}

// ============================================================================
// Membership Management
// ============================================================================

ReplicationManager::Result<void> ReplicationManager::add_peer(const NodeId& peer) {
    std::lock_guard lock(impl_->mtx);

    if (impl_->state_manager.membership().is_member(peer)) {
        return ReplicationError::NodeAlreadyExists;
    }

    impl_->state_manager.add_voting_member(peer);

    // If leader, add follower to delta publisher
    if (impl_->state_manager.role() == RaftRole::Leader && impl_->delta_publisher) {
        auto result = impl_->delta_publisher->add_follower(peer, LSN{0});
        if (result.is_err()) {
            // Rollback
            impl_->state_manager.remove_member(peer);
            return result.error();
        }
    }

    return {};
}

ReplicationManager::Result<void> ReplicationManager::remove_peer(const NodeId& peer) {
    std::lock_guard lock(impl_->mtx);

    if (!impl_->state_manager.membership().is_member(peer)) {
        return ReplicationError::NodeNotFound;
    }

    impl_->state_manager.remove_member(peer);

    // If leader, remove follower from delta publisher
    if (impl_->state_manager.role() == RaftRole::Leader && impl_->delta_publisher) {
        (void)impl_->delta_publisher->remove_follower(peer);
    }

    return {};
}

std::vector<NodeId> ReplicationManager::peers() const {
    return impl_->state_manager.peers();
}

// ============================================================================
// Statistics
// ============================================================================

ReplicationStats ReplicationManager::stats() const {
    std::lock_guard lock(impl_->mtx);

    ReplicationStats s;
    s.role = impl_->state_manager.role();
    s.current_term = impl_->state_manager.current_term();
    s.current_leader = impl_->state_manager.current_leader();
    s.commit_index = impl_->state_manager.commit_index();
    s.applied_lsn = impl_->delta_sink.applied_lsn();
    s.committed_lsn = impl_->last_committed_lsn;
    s.pending_entries = 0;

    // Populate follower states if leader
    if (impl_->state_manager.role() == RaftRole::Leader && impl_->delta_publisher) {
        s.follower_states = impl_->delta_publisher->get_all_follower_states();
    }

    s.last_heartbeat = impl_->last_heartbeat_sent;
    s.last_state_change = std::chrono::steady_clock::now();  // Simplified

    return s;
}

// ============================================================================
// Event Callbacks
// ============================================================================

void ReplicationManager::set_leader_change_callback(LeaderChangeCallback callback) {
    std::lock_guard lock(impl_->mtx);
    impl_->leader_change_callback = std::move(callback);
}

void ReplicationManager::set_commit_callback(CommitCallback callback) {
    std::lock_guard lock(impl_->mtx);
    impl_->commit_callback = std::move(callback);
}

// ============================================================================
// Processing
// ============================================================================

void ReplicationManager::tick() {
    std::lock_guard lock(impl_->mtx);

    if (!impl_->running.load()) {
        return;
    }

    auto role = impl_->state_manager.role();

    switch (role) {
        case RaftRole::Follower:
            impl_->tick_follower();
            break;

        case RaftRole::Candidate:
            impl_->tick_candidate();
            break;

        case RaftRole::Leader:
            impl_->tick_leader();
            break;
    }
}

}  // namespace dotvm::core::state::replication
