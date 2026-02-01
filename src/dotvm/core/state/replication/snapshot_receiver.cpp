/// @file snapshot_receiver.cpp
/// @brief Implementation of SnapshotReceiver for bulk snapshot transfers

#include "dotvm/core/state/replication/snapshot_receiver.hpp"

#include <algorithm>
#include <map>
#include <mutex>

#include "dotvm/core/state/replication/message_serializer.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// CRC32 Helper
// ============================================================================

namespace {

/// @brief Calculate CRC32 checksum for data
[[nodiscard]] std::uint32_t calculate_crc32(std::span<const std::byte> data) {
    // Simple additive checksum matching the test implementation
    std::uint32_t checksum = 0;
    for (auto b : data) {
        checksum += static_cast<std::uint32_t>(b);
    }
    return checksum;
}

}  // namespace

// ============================================================================
// Pending Chunk
// ============================================================================

struct PendingChunk {
    std::uint32_t index;
    std::vector<std::byte> data;
    std::size_t offset;
};

// ============================================================================
// Implementation
// ============================================================================

struct SnapshotReceiver::Impl {
    SnapshotReceiverConfig config;
    SnapshotSink& sink;
    Transport& transport;
    std::atomic<bool> running{false};

    mutable std::mutex mtx;
    ReceiveState state;
    std::map<std::uint32_t, PendingChunk> pending_chunks;  // Out-of-order buffer
    std::uint32_t next_expected_chunk{0};
    SnapshotCompleteCallback complete_callback;

    Impl(SnapshotReceiverConfig cfg, SnapshotSink& s, Transport& t)
        : config(std::move(cfg)), sink(s), transport(t) {}

    std::size_t process_pending_internal();
    Result<void> finalize_receive();
    void send_ack(const SnapshotChunk& chunk, bool success);
};

SnapshotReceiver::SnapshotReceiver(SnapshotReceiverConfig config, SnapshotSink& sink,
                                   Transport& transport)
    : impl_(std::make_unique<Impl>(std::move(config), sink, transport)) {}

SnapshotReceiver::~SnapshotReceiver() {
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

SnapshotReceiver::Result<void> SnapshotReceiver::start() {
    bool expected = false;
    if (!impl_->running.compare_exchange_strong(expected, true)) {
        // Already running
        return {};
    }
    return {};
}

void SnapshotReceiver::stop() {
    impl_->running.store(false);

    std::lock_guard lock(impl_->mtx);
    if (impl_->state.status == ReceiveStatus::Receiving) {
        impl_->sink.abort_snapshot();
        if (impl_->complete_callback) {
            impl_->complete_callback(impl_->state.snapshot_lsn, false);
        }
    }
    impl_->state = ReceiveState{};
    impl_->pending_chunks.clear();
    impl_->next_expected_chunk = 0;
}

bool SnapshotReceiver::is_running() const noexcept {
    return impl_->running.load();
}

// ============================================================================
// Snapshot Request
// ============================================================================

SnapshotReceiver::Result<void> SnapshotReceiver::request_snapshot(NodeId leader_id) {
    if (!impl_->running.load()) {
        return ReplicationError::ShuttingDown;
    }

    std::lock_guard lock(impl_->mtx);

    // Build request message
    SnapshotRequest request;
    request.node_id = impl_->transport.local_id();
    request.from_lsn = LSN::invalid();  // Request full snapshot

    // Serialize and send
    auto serialize_result = MessageSerializer::serialize(request);
    if (serialize_result.is_err()) {
        return ReplicationError::SerializationFailed;
    }
    return impl_->transport.send(leader_id, StreamType::Snapshot, serialize_result.value());
}

// ============================================================================
// Chunk Reception
// ============================================================================

SnapshotReceiver::Result<void> SnapshotReceiver::receive_chunk(const SnapshotChunk& chunk) {
    if (!impl_->running.load()) {
        return ReplicationError::ShuttingDown;
    }

    std::lock_guard lock(impl_->mtx);

    // Verify checksum if enabled
    if (impl_->config.verify_chunks && !chunk.data.empty()) {
        auto calculated = calculate_crc32(chunk.data);
        if (calculated != chunk.checksum) {
            impl_->send_ack(chunk, false);
            return ReplicationError::ChecksumMismatch;
        }
    }

    // Check if this is a new snapshot or continuation
    if (impl_->state.status == ReceiveStatus::Idle ||
        impl_->state.snapshot_lsn != chunk.snapshot_lsn) {
        // Starting new snapshot - abort old one if any
        if (impl_->state.status == ReceiveStatus::Receiving) {
            impl_->sink.abort_snapshot();
        }

        // Begin new snapshot
        auto begin_result = impl_->sink.begin_snapshot(chunk.snapshot_lsn, chunk.total_bytes);
        if (begin_result.is_err()) {
            impl_->state.status = ReceiveStatus::Failed;
            if (impl_->complete_callback) {
                impl_->complete_callback(chunk.snapshot_lsn, false);
            }
            return begin_result.error();
        }

        // Reset state for new snapshot
        impl_->state = ReceiveState{};
        impl_->state.leader_id = chunk.sender_id;
        impl_->state.snapshot_lsn = chunk.snapshot_lsn;
        impl_->state.total_size = chunk.total_bytes;
        impl_->state.total_chunks = chunk.total_chunks;
        impl_->state.start_time = std::chrono::steady_clock::now();
        impl_->state.status = ReceiveStatus::Receiving;
        impl_->pending_chunks.clear();
        impl_->next_expected_chunk = 0;
    }

    impl_->state.last_chunk_time = std::chrono::steady_clock::now();

    // Calculate offset for this chunk
    std::size_t chunk_offset = 0;
    if (chunk.total_chunks > 0) {
        std::size_t regular_chunk_size =
            (chunk.total_bytes + chunk.total_chunks - 1) / chunk.total_chunks;
        chunk_offset = static_cast<std::size_t>(chunk.chunk_index) * regular_chunk_size;
    }

    if (chunk.chunk_index == impl_->next_expected_chunk) {
        // In-order chunk - apply directly
        auto write_result = impl_->sink.write_chunk(chunk_offset, chunk.data);
        if (write_result.is_err()) {
            impl_->state.status = ReceiveStatus::Failed;
            impl_->sink.abort_snapshot();
            if (impl_->complete_callback) {
                impl_->complete_callback(impl_->state.snapshot_lsn, false);
            }
            return write_result.error();
        }

        impl_->state.bytes_received += chunk.data.size();
        impl_->state.chunks_received++;
        impl_->next_expected_chunk++;

        // Send ACK
        impl_->send_ack(chunk, true);

        // Process any pending chunks that are now in order
        (void)impl_->process_pending_internal();

        // Check if complete
        if (chunk.is_last || (impl_->state.total_chunks > 0 &&
                              impl_->state.chunks_received >= impl_->state.total_chunks)) {
            return impl_->finalize_receive();
        }
    } else if (chunk.chunk_index > impl_->next_expected_chunk) {
        // Out-of-order - buffer if within window
        std::size_t gap = chunk.chunk_index - impl_->next_expected_chunk;
        if (gap <= impl_->config.max_out_of_order_chunks) {
            PendingChunk pending;
            pending.index = chunk.chunk_index;
            pending.data = std::vector<std::byte>(chunk.data.begin(), chunk.data.end());
            pending.offset = chunk_offset;
            impl_->pending_chunks[chunk.chunk_index] = std::move(pending);

            // Still send ACK for the chunk
            impl_->send_ack(chunk, true);
        }
        // else: chunk too far ahead, will be retransmitted
    }
    // else: duplicate chunk, ignore

    return {};
}

void SnapshotReceiver::Impl::send_ack(const SnapshotChunk& chunk, bool success) {
    SnapshotAck ack;
    ack.node_id = transport.local_id();
    ack.chunk_index = chunk.chunk_index;
    ack.success = success;
    if (!success) {
        ack.error_msg = "Checksum mismatch";
    }

    auto serialize_result = MessageSerializer::serialize(ack);
    if (serialize_result.is_ok()) {
        (void)transport.send(chunk.sender_id, StreamType::Snapshot, serialize_result.value());
    }
}

SnapshotReceiver::Result<void> SnapshotReceiver::Impl::finalize_receive() {
    state.status = ReceiveStatus::Finalizing;

    // Verify MPT root if enabled
    auto finalize_result = sink.finalize_snapshot();
    if (finalize_result.is_err()) {
        state.status = ReceiveStatus::Failed;
        sink.abort_snapshot();
        if (complete_callback) {
            complete_callback(state.snapshot_lsn, false);
        }
        return finalize_result.error();
    }

    // MPT root verification
    if (config.verify_final_hash && !state.expected_mpt_root.is_zero()) {
        auto actual_root = sink.mpt_root();
        if (actual_root != state.expected_mpt_root) {
            state.status = ReceiveStatus::Failed;
            if (complete_callback) {
                complete_callback(state.snapshot_lsn, false);
            }
            return ReplicationError::VerificationFailed;
        }
    }

    state.status = ReceiveStatus::Complete;

    if (complete_callback) {
        complete_callback(state.snapshot_lsn, true);
    }

    return {};
}

std::size_t SnapshotReceiver::Impl::process_pending_internal() {
    std::size_t processed = 0;

    while (pending_chunks.contains(next_expected_chunk)) {
        auto& pending = pending_chunks[next_expected_chunk];

        auto write_result = sink.write_chunk(pending.offset, pending.data);
        if (write_result.is_err()) {
            break;
        }

        state.bytes_received += pending.data.size();
        state.chunks_received++;

        pending_chunks.erase(next_expected_chunk);
        next_expected_chunk++;
        ++processed;
    }

    return processed;
}

// ============================================================================
// State Queries
// ============================================================================

bool SnapshotReceiver::is_receiving() const noexcept {
    std::lock_guard lock(impl_->mtx);
    return impl_->state.status == ReceiveStatus::Receiving ||
           impl_->state.status == ReceiveStatus::Finalizing;
}

ReceiveState SnapshotReceiver::receive_state() const {
    std::lock_guard lock(impl_->mtx);
    return impl_->state;
}

// ============================================================================
// Abort
// ============================================================================

void SnapshotReceiver::abort_receive() {
    std::lock_guard lock(impl_->mtx);

    if (impl_->state.status == ReceiveStatus::Receiving ||
        impl_->state.status == ReceiveStatus::Finalizing) {
        impl_->sink.abort_snapshot();
        if (impl_->complete_callback) {
            impl_->complete_callback(impl_->state.snapshot_lsn, false);
        }
    }
    impl_->state = ReceiveState{};
    impl_->pending_chunks.clear();
    impl_->next_expected_chunk = 0;
}

// ============================================================================
// Callbacks
// ============================================================================

void SnapshotReceiver::set_complete_callback(SnapshotCompleteCallback callback) {
    std::lock_guard lock(impl_->mtx);
    impl_->complete_callback = std::move(callback);
}

}  // namespace dotvm::core::state::replication
