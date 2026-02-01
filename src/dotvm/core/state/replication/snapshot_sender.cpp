/// @file snapshot_sender.cpp
/// @brief Implementation of SnapshotSender for bulk snapshot transfers

#include "dotvm/core/state/replication/snapshot_sender.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <span>

#include "dotvm/core/state/replication/message_serializer.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// CRC32 Helper
// ============================================================================

namespace {

/// @brief Calculate CRC32 checksum for data
[[nodiscard]] std::uint32_t calculate_crc32(std::span<const std::byte> data) {
    // CRC32 polynomial (IEEE 802.3)
    constexpr std::uint32_t POLYNOMIAL = 0xEDB88320;

    std::uint32_t crc = 0xFFFFFFFF;
    for (const auto byte : data) {
        crc ^= static_cast<std::uint32_t>(byte);
        for (int i = 0; i < 8; ++i) {
            if (crc & 1) {
                crc = (crc >> 1) ^ POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

}  // namespace

// ============================================================================
// Internal Transfer Data
// ============================================================================

struct TransferData {
    TransferState state;
    std::size_t next_offset{0};
};

// ============================================================================
// Implementation
// ============================================================================

struct SnapshotSender::Impl {
    SnapshotSenderConfig config;
    SnapshotSource& source;
    Transport& transport;
    std::atomic<bool> running{false};

    mutable std::mutex mtx;
    std::map<NodeId, TransferData> transfers;
    TransferCompleteCallback complete_callback;

    Impl(SnapshotSenderConfig cfg, SnapshotSource& src, Transport& t)
        : config(std::move(cfg)), source(src), transport(t) {}
};

SnapshotSender::SnapshotSender(SnapshotSenderConfig config, SnapshotSource& source,
                                Transport& transport)
    : impl_(std::make_unique<Impl>(std::move(config), source, transport)) {}

SnapshotSender::~SnapshotSender() {
    stop();
}

// ============================================================================
// Lifecycle
// ============================================================================

SnapshotSender::Result<void> SnapshotSender::start() {
    bool expected = false;
    if (!impl_->running.compare_exchange_strong(expected, true)) {
        // Already running
        return {};
    }
    return {};
}

void SnapshotSender::stop() {
    impl_->running.store(false);

    std::lock_guard lock(impl_->mtx);
    // Mark all active transfers as cancelled
    for (auto& [id, transfer] : impl_->transfers) {
        if (transfer.state.status == TransferStatus::Pending ||
            transfer.state.status == TransferStatus::InProgress) {
            transfer.state.status = TransferStatus::Cancelled;
            if (impl_->complete_callback) {
                impl_->complete_callback(id, false);
            }
        }
    }
    impl_->transfers.clear();
}

bool SnapshotSender::is_running() const noexcept {
    return impl_->running.load();
}

// ============================================================================
// Transfer Management
// ============================================================================

SnapshotSender::Result<void> SnapshotSender::initiate_transfer(const NodeId& follower_id) {
    if (!impl_->running.load()) {
        return ReplicationError::ShuttingDown;
    }

    std::lock_guard lock(impl_->mtx);

    // Check if transfer already exists for this follower
    if (impl_->transfers.contains(follower_id)) {
        return ReplicationError::NodeAlreadyExists;
    }

    // Count active transfers
    std::size_t active_count = 0;
    for (const auto& [id, transfer] : impl_->transfers) {
        if (transfer.state.status == TransferStatus::Pending ||
            transfer.state.status == TransferStatus::InProgress) {
            ++active_count;
        }
    }

    if (active_count >= impl_->config.max_concurrent_transfers) {
        return ReplicationError::BackpressureExceeded;
    }

    // Create transfer state
    TransferData data;
    data.state.follower_id = follower_id;
    data.state.snapshot_lsn = impl_->source.snapshot_lsn();
    data.state.total_size = impl_->source.total_size();

    // Handle empty snapshots
    if (data.state.total_size == 0) {
        data.state.total_chunks = 0;
    } else {
        data.state.total_chunks = static_cast<std::uint32_t>(
            (data.state.total_size + impl_->config.chunk_size - 1) / impl_->config.chunk_size);
    }

    data.state.start_time = std::chrono::steady_clock::now();
    data.state.last_activity_time = data.state.start_time;
    data.state.status = TransferStatus::Pending;
    data.next_offset = 0;

    impl_->transfers.emplace(follower_id, std::move(data));
    return {};
}

SnapshotSender::Result<void> SnapshotSender::cancel_transfer(const NodeId& follower_id) {
    std::lock_guard lock(impl_->mtx);

    auto it = impl_->transfers.find(follower_id);
    if (it == impl_->transfers.end()) {
        return ReplicationError::NodeNotFound;
    }

    if (it->second.state.status == TransferStatus::Pending ||
        it->second.state.status == TransferStatus::InProgress) {
        it->second.state.status = TransferStatus::Cancelled;
        if (impl_->complete_callback) {
            impl_->complete_callback(follower_id, false);
        }
    }

    return {};
}

std::size_t SnapshotSender::process_transfers() {
    if (!impl_->running.load()) {
        return 0;
    }

    std::lock_guard lock(impl_->mtx);
    std::size_t chunks_sent = 0;
    auto now = std::chrono::steady_clock::now();

    for (auto& [follower_id, transfer] : impl_->transfers) {
        // Skip completed/failed/cancelled transfers
        if (transfer.state.status != TransferStatus::Pending &&
            transfer.state.status != TransferStatus::InProgress) {
            continue;
        }

        // Mark as in progress if pending
        if (transfer.state.status == TransferStatus::Pending) {
            transfer.state.status = TransferStatus::InProgress;
        }

        // Check for timeout
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - transfer.state.start_time);
        if (elapsed > impl_->config.transfer_timeout) {
            transfer.state.status = TransferStatus::Failed;
            if (impl_->complete_callback) {
                impl_->complete_callback(follower_id, false);
            }
            continue;
        }

        // Handle empty snapshots - complete immediately
        if (transfer.state.total_size == 0) {
            transfer.state.status = TransferStatus::Completed;
            if (impl_->complete_callback) {
                impl_->complete_callback(follower_id, true);
            }
            continue;
        }

        // Send next chunk if we haven't reached the end
        if (transfer.next_offset < transfer.state.total_size) {
            std::size_t remaining = transfer.state.total_size - transfer.next_offset;
            std::size_t chunk_size = std::min(remaining, impl_->config.chunk_size);

            auto chunk_result = impl_->source.read_chunk(transfer.next_offset, chunk_size);
            if (chunk_result.is_err()) {
                transfer.state.status = TransferStatus::Failed;
                if (impl_->complete_callback) {
                    impl_->complete_callback(follower_id, false);
                }
                continue;
            }

            auto& chunk_data = chunk_result.value();

            // Build chunk message
            SnapshotChunk chunk;
            chunk.sender_id = impl_->transport.local_id();
            chunk.snapshot_lsn = transfer.state.snapshot_lsn;
            chunk.chunk_index = transfer.state.chunks_sent;
            chunk.total_chunks = transfer.state.total_chunks;
            chunk.total_bytes = transfer.state.total_size;
            chunk.data = std::move(chunk_data);
            chunk.is_last = (transfer.next_offset + chunk_size >= transfer.state.total_size);

            if (impl_->config.verify_chunks) {
                chunk.checksum = calculate_crc32(chunk.data);
            }

            // Serialize and send
            auto serialize_result = MessageSerializer::serialize(chunk);
            if (serialize_result.is_err()) {
                transfer.state.status = TransferStatus::Failed;
                if (impl_->complete_callback) {
                    impl_->complete_callback(follower_id, false);
                }
                continue;
            }

            auto send_result =
                impl_->transport.send(follower_id, StreamType::Snapshot, serialize_result.value());

            if (send_result.is_ok()) {
                transfer.next_offset += chunk_size;
                transfer.state.bytes_sent += chunk_size;
                transfer.state.chunks_sent++;
                transfer.state.last_activity_time = now;
                ++chunks_sent;

                // Check if transfer is complete
                if (transfer.next_offset >= transfer.state.total_size) {
                    transfer.state.status = TransferStatus::Completed;
                    if (impl_->complete_callback) {
                        impl_->complete_callback(follower_id, true);
                    }
                }
            } else {
                // Transport failure
                transfer.state.status = TransferStatus::Failed;
                if (impl_->complete_callback) {
                    impl_->complete_callback(follower_id, false);
                }
            }
        }
    }

    return chunks_sent;
}

// ============================================================================
// State Queries
// ============================================================================

std::optional<TransferState> SnapshotSender::get_transfer_state(const NodeId& follower_id) const {
    std::lock_guard lock(impl_->mtx);

    auto it = impl_->transfers.find(follower_id);
    if (it == impl_->transfers.end()) {
        return std::nullopt;
    }
    return it->second.state;
}

std::vector<TransferState> SnapshotSender::get_active_transfers() const {
    std::lock_guard lock(impl_->mtx);

    std::vector<TransferState> states;
    for (const auto& [id, transfer] : impl_->transfers) {
        if (transfer.state.status == TransferStatus::Pending ||
            transfer.state.status == TransferStatus::InProgress) {
            states.push_back(transfer.state);
        }
    }
    return states;
}

// ============================================================================
// Callbacks
// ============================================================================

void SnapshotSender::set_transfer_complete_callback(TransferCompleteCallback callback) {
    std::lock_guard lock(impl_->mtx);
    impl_->complete_callback = std::move(callback);
}

}  // namespace dotvm::core::state::replication
