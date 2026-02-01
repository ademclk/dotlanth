/// @file delta_subscriber.cpp
/// @brief Implementation of delta streaming subscriber (follower side)

#include "dotvm/core/state/replication/delta_subscriber.hpp"

#include <algorithm>
#include <queue>

namespace dotvm::core::state::replication {

// Bring state::LSN into scope
using LSN = state::LSN;
using LogRecord = state::LogRecord;

// ============================================================================
// DeltaSubscriber Implementation
// ============================================================================

struct DeltaSubscriber::Impl {
    DeltaSubscriberConfig config;
    DeltaSink& sink;
    Transport& transport;
    NodeId leader_id;

    mutable std::mutex mtx_;

    // Pending batches ordered by base_lsn for reordering
    std::priority_queue<DeltaBatch, std::vector<DeltaBatch>,
                        std::function<bool(const DeltaBatch&, const DeltaBatch&)>>
        pending_batches_;

    ApplyCallback apply_callback_;
    VerificationFailureCallback verification_failure_callback_;

    DeltaSubscriberStats stats_;

    std::atomic<bool> running_{false};

    Impl(DeltaSubscriberConfig cfg, DeltaSink& snk, Transport& trans, NodeId leader)
        : config(std::move(cfg)),
          sink(snk),
          transport(trans),
          leader_id(std::move(leader)),
          pending_batches_([](const DeltaBatch& a, const DeltaBatch& b) {
              // Min-heap: lower start_lsn has higher priority
              return a.start_lsn > b.start_lsn;
          }) {
        stats_.applied_lsn = sink.applied_lsn();
    }

    Result<void> start() {
        if (running_.exchange(true)) {
            return {};  // Already running
        }
        return {};
    }

    void stop() {
        running_.store(false);
    }

    Result<void> receive_batch(const DeltaBatch& batch) {
        if (!running_.load()) {
            return ReplicationError::ShuttingDown;
        }

        std::lock_guard lock(mtx_);

        // Check if queue is full (backpressure)
        if (pending_batches_.size() >= config.max_pending_batches) {
            return ReplicationError::BackpressureExceeded;
        }

        // Verify checksums if enabled
        if (config.verify_checksums) {
            for (const auto& entry : batch.entries) {
                if (entry.checksum != 0) {
                    // TODO: Verify checksum
                    // For now, trust the checksum
                }
            }
        }

        // Update stats
        stats_.batches_received++;
        for (const auto& entry : batch.entries) {
            stats_.bytes_received += entry.key.size() + entry.value.size();
            if (entry.lsn > stats_.received_lsn) {
                stats_.received_lsn = entry.lsn;
            }
        }

        // Add to pending queue
        pending_batches_.push(batch);

        return {};
    }

    std::size_t process_pending() {
        if (!running_.load()) {
            return 0;
        }

        std::lock_guard lock(mtx_);
        std::size_t applied = 0;

        while (!pending_batches_.empty()) {
            const auto& batch = pending_batches_.top();

            // Check if this batch is next in sequence
            // Expected start_lsn is applied_lsn + 1 (next after what's applied)
            LSN expected_start = stats_.applied_lsn.next();
            if (batch.start_lsn != expected_start) {
                // Gap detected - need to wait for missing batch
                // Check if beyond reorder window
                if (batch.start_lsn.value > stats_.applied_lsn.value + config.reorder_window) {
                    // Too far ahead, likely missing data
                    break;
                }
                // Within reorder window, check if we have the next one
                stats_.reordered_batches++;
                break;
            }

            // Extract batch (need to copy since top() returns const ref)
            DeltaBatch current_batch = batch;
            pending_batches_.pop();

            // Apply the batch
            auto apply_result = sink.apply_batch(current_batch.entries);
            if (apply_result.is_err()) {
                // Failed to apply - this is serious
                // Re-queue the batch? For now, just continue
                continue;
            }

            // Update applied LSN
            LSN new_applied_lsn = stats_.applied_lsn;
            for (const auto& entry : current_batch.entries) {
                if (entry.lsn > new_applied_lsn) {
                    new_applied_lsn = entry.lsn;
                }
            }
            stats_.applied_lsn = new_applied_lsn;
            stats_.batches_applied++;
            stats_.last_batch_time = std::chrono::steady_clock::now();

            // Verify MPT root if enabled
            // Note: DeltaBatch doesn't have mpt_root_after field currently
            // This verification is disabled until the field is added
            if (config.verify_mpt_root) {
                // TODO: Add MPT root verification when DeltaBatch gains mpt_root_after field
            }

            // Send ACK to leader
            send_ack(new_applied_lsn);

            // Invoke callback
            if (apply_callback_) {
                apply_callback_(new_applied_lsn, current_batch.entries.size());
            }

            applied++;
        }

        return applied;
    }

    void send_ack(LSN acked_lsn) {
        DeltaAck ack;
        ack.node_id = NodeId::null();  // TODO: Set to local node ID
        ack.acked_lsn = acked_lsn;
        ack.success = true;

        // Serialize and send via transport
        std::vector<std::byte> serialized(sizeof(DeltaAck));
        auto result = transport.send(leader_id, StreamType::Delta, serialized);
        (void)result;  // Ignore send failures for ACKs
    }

    Result<void> request_retransmit(LSN from_lsn) {
        DeltaRequest request;
        request.node_id = NodeId::null();  // TODO: Set to local node ID
        request.start_lsn = from_lsn;

        std::vector<std::byte> serialized(sizeof(DeltaRequest));
        auto result = transport.send(leader_id, StreamType::Delta, serialized);
        if (result.is_err()) {
            return result.error();
        }
        return {};
    }

    void clear_pending() {
        std::lock_guard lock(mtx_);
        while (!pending_batches_.empty()) {
            pending_batches_.pop();
        }
    }
};

DeltaSubscriber::DeltaSubscriber(DeltaSubscriberConfig config, DeltaSink& sink,
                                 Transport& transport, NodeId leader_id)
    : impl_(std::make_unique<Impl>(std::move(config), sink, transport,
                                   std::move(leader_id))) {}

DeltaSubscriber::~DeltaSubscriber() {
    stop();
}

DeltaSubscriber::Result<void> DeltaSubscriber::start() {
    return impl_->start();
}

void DeltaSubscriber::stop() {
    impl_->stop();
}

bool DeltaSubscriber::is_running() const noexcept {
    return impl_->running_.load();
}

DeltaSubscriber::Result<void> DeltaSubscriber::receive_batch(const DeltaBatch& batch) {
    return impl_->receive_batch(batch);
}

std::size_t DeltaSubscriber::process_pending() {
    return impl_->process_pending();
}

LSN DeltaSubscriber::applied_lsn() const noexcept {
    std::lock_guard lock(impl_->mtx_);
    return impl_->stats_.applied_lsn;
}

std::size_t DeltaSubscriber::pending_count() const noexcept {
    std::lock_guard lock(impl_->mtx_);
    return impl_->pending_batches_.size();
}

bool DeltaSubscriber::is_caught_up() const noexcept {
    std::lock_guard lock(impl_->mtx_);
    return impl_->pending_batches_.empty();
}

DeltaSubscriberStats DeltaSubscriber::stats() const {
    std::lock_guard lock(impl_->mtx_);
    return impl_->stats_;
}

void DeltaSubscriber::set_leader(const NodeId& leader_id) {
    std::lock_guard lock(impl_->mtx_);
    impl_->leader_id = leader_id;
}

NodeId DeltaSubscriber::leader_id() const {
    std::lock_guard lock(impl_->mtx_);
    return impl_->leader_id;
}

void DeltaSubscriber::set_apply_callback(ApplyCallback callback) {
    std::lock_guard lock(impl_->mtx_);
    impl_->apply_callback_ = std::move(callback);
}

void DeltaSubscriber::set_verification_failure_callback(
    VerificationFailureCallback callback) {
    std::lock_guard lock(impl_->mtx_);
    impl_->verification_failure_callback_ = std::move(callback);
}

DeltaSubscriber::Result<void> DeltaSubscriber::request_retransmit(LSN from_lsn) {
    return impl_->request_retransmit(from_lsn);
}

void DeltaSubscriber::clear_pending() {
    impl_->clear_pending();
}

}  // namespace dotvm::core::state::replication
