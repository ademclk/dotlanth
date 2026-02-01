/// @file delta_publisher.cpp
/// @brief Implementation of delta streaming publisher (leader side)

#include "dotvm/core/state/replication/delta_publisher.hpp"

#include <algorithm>
#include <limits>
#include <map>

namespace dotvm::core::state::replication {

// Bring state::LSN into scope
using LSN = state::LSN;
using LogRecord = state::LogRecord;

// ============================================================================
// DeltaPublisher Implementation
// ============================================================================

struct DeltaPublisher::Impl {
    DeltaPublisherConfig config;
    DeltaSource& source;
    Transport& transport;

    mutable std::mutex mtx_;
    std::map<NodeId, FollowerDeltaState> followers_;
    AckCallback ack_callback_;

    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> total_bytes_sent_{0};
    std::atomic<std::uint64_t> total_batches_sent_{0};

    Impl(DeltaPublisherConfig cfg, DeltaSource& src, Transport& trans)
        : config(std::move(cfg)), source(src), transport(trans) {}

    Result<void> start() {
        if (running_.exchange(true)) {
            return {};  // Already running
        }
        return {};
    }

    void stop() {
        running_.store(false);
    }

    Result<void> add_follower(const NodeId& follower_id, LSN start_lsn) {
        std::lock_guard lock(mtx_);

        if (followers_.contains(follower_id)) {
            return ReplicationError::NodeAlreadyExists;
        }

        FollowerDeltaState state;
        state.follower_id = follower_id;
        state.acknowledged_lsn = start_lsn;
        state.sent_lsn = start_lsn;
        state.last_ack_time = std::chrono::steady_clock::now();

        followers_.emplace(follower_id, std::move(state));
        return {};
    }

    Result<void> remove_follower(const NodeId& follower_id) {
        std::lock_guard lock(mtx_);

        auto it = followers_.find(follower_id);
        if (it == followers_.end()) {
            return ReplicationError::NodeNotFound;
        }

        followers_.erase(it);
        return {};
    }

    std::optional<FollowerDeltaState> get_follower_state(const NodeId& follower_id) const {
        std::lock_guard lock(mtx_);

        auto it = followers_.find(follower_id);
        if (it == followers_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<FollowerDeltaState> get_all_follower_states() const {
        std::lock_guard lock(mtx_);

        std::vector<FollowerDeltaState> result;
        result.reserve(followers_.size());
        for (const auto& [id, state] : followers_) {
            result.push_back(state);
        }
        return result;
    }

    std::size_t publish() {
        if (!running_.load()) {
            return 0;
        }

        std::lock_guard lock(mtx_);
        std::size_t batches_sent = 0;

        for (auto& [follower_id, state] : followers_) {
            // Skip if too many inflight batches (backpressure)
            if (state.inflight_batches >= config.max_inflight_batches) {
                continue;
            }

            // Check if there are new entries to send
            LSN current_lsn = source.current_lsn();
            if (state.sent_lsn >= current_lsn) {
                state.is_caught_up = true;
                continue;
            }

            // Read entries starting from sent_lsn + 1
            auto entries = source.read_entries(
                state.sent_lsn.next(),
                config.batch_size,
                config.max_batch_bytes);

            if (entries.empty()) {
                continue;
            }

            // Build delta batch
            DeltaBatch batch;
            batch.start_lsn = state.sent_lsn.next();
            batch.entries = std::move(entries);

            // Calculate batch LSN range
            LSN batch_end_lsn = batch.start_lsn;
            for (const auto& entry : batch.entries) {
                if (entry.lsn > batch_end_lsn) {
                    batch_end_lsn = entry.lsn;
                }
            }
            batch.end_lsn = batch_end_lsn;

            // Calculate total bytes
            std::size_t batch_bytes = 0;
            for (const auto& entry : batch.entries) {
                batch_bytes += entry.key.size() + entry.value.size() + sizeof(LogRecord);
            }

            // Send batch via transport (serialize to bytes)
            // For now, just use the batch size as a placeholder
            // In production, this would use proper serialization
            std::vector<std::byte> serialized(sizeof(DeltaBatch));
            auto send_result = transport.send(follower_id, StreamType::Delta, serialized);
            if (send_result.is_err()) {
                // Failed to send, will retry next time
                continue;
            }

            // Update follower state
            state.sent_lsn = batch_end_lsn;
            state.inflight_batches++;
            state.batches_sent++;
            state.bytes_sent += batch_bytes;
            state.is_caught_up = (state.sent_lsn >= current_lsn);

            // Update totals
            total_bytes_sent_.fetch_add(batch_bytes);
            total_batches_sent_.fetch_add(1);

            batches_sent++;
        }

        return batches_sent;
    }

    void handle_ack(const NodeId& follower_id, LSN acked_lsn) {
        std::lock_guard lock(mtx_);

        auto it = followers_.find(follower_id);
        if (it == followers_.end()) {
            return;
        }

        auto& state = it->second;

        // Only update if ACK is for a higher LSN
        if (acked_lsn > state.acknowledged_lsn) {
            state.acknowledged_lsn = acked_lsn;
            state.last_ack_time = std::chrono::steady_clock::now();

            // Decrease inflight count (simplified - assumes one batch per ACK)
            if (state.inflight_batches > 0) {
                state.inflight_batches--;
            }
        }

        // Invoke callback outside lock would be better, but keep simple for now
        if (ack_callback_) {
            ack_callback_(follower_id, acked_lsn);
        }
    }

    LSN min_acknowledged_lsn() const {
        std::lock_guard lock(mtx_);

        if (followers_.empty()) {
            return source.current_lsn();
        }

        LSN min_lsn{std::numeric_limits<std::uint64_t>::max()};
        for (const auto& [id, state] : followers_) {
            if (state.acknowledged_lsn < min_lsn) {
                min_lsn = state.acknowledged_lsn;
            }
        }
        return min_lsn;
    }

    bool all_caught_up() const {
        std::lock_guard lock(mtx_);

        for (const auto& [id, state] : followers_) {
            if (!state.is_caught_up) {
                return false;
            }
        }
        return true;
    }
};

DeltaPublisher::DeltaPublisher(DeltaPublisherConfig config, DeltaSource& source,
                               Transport& transport)
    : impl_(std::make_unique<Impl>(std::move(config), source, transport)) {}

DeltaPublisher::~DeltaPublisher() {
    stop();
}

DeltaPublisher::Result<void> DeltaPublisher::start() {
    return impl_->start();
}

void DeltaPublisher::stop() {
    impl_->stop();
}

bool DeltaPublisher::is_running() const noexcept {
    return impl_->running_.load();
}

DeltaPublisher::Result<void> DeltaPublisher::add_follower(const NodeId& follower_id,
                                                           LSN start_lsn) {
    return impl_->add_follower(follower_id, start_lsn);
}

DeltaPublisher::Result<void> DeltaPublisher::remove_follower(const NodeId& follower_id) {
    return impl_->remove_follower(follower_id);
}

std::optional<FollowerDeltaState> DeltaPublisher::get_follower_state(
    const NodeId& follower_id) const {
    return impl_->get_follower_state(follower_id);
}

std::vector<FollowerDeltaState> DeltaPublisher::get_all_follower_states() const {
    return impl_->get_all_follower_states();
}

std::size_t DeltaPublisher::publish() {
    return impl_->publish();
}

void DeltaPublisher::notify_new_entries() {
    // Could trigger a condition variable to wake up a publishing thread
    // For now, callers should call publish() after this
}

void DeltaPublisher::handle_ack(const NodeId& follower_id, LSN acked_lsn) {
    impl_->handle_ack(follower_id, acked_lsn);
}

void DeltaPublisher::set_ack_callback(AckCallback callback) {
    std::lock_guard lock(impl_->mtx_);
    impl_->ack_callback_ = std::move(callback);
}

std::uint64_t DeltaPublisher::total_bytes_sent() const noexcept {
    return impl_->total_bytes_sent_.load();
}

std::uint64_t DeltaPublisher::total_batches_sent() const noexcept {
    return impl_->total_batches_sent_.load();
}

LSN DeltaPublisher::min_acknowledged_lsn() const {
    return impl_->min_acknowledged_lsn();
}

bool DeltaPublisher::all_caught_up() const {
    return impl_->all_caught_up();
}

}  // namespace dotvm::core::state::replication
