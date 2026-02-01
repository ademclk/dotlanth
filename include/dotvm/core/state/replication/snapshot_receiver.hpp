#pragma once

/// @file snapshot_receiver.hpp
/// @brief STATE-006 Snapshot transfer receiver (follower side)
///
/// SnapshotReceiver handles receiving bulk snapshots from the leader,
/// verifying integrity via CRC32 checksums, reassembling chunks,
/// and finalizing the snapshot with MPT root verification.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/log_record.hpp"
#include "dotvm/core/state/mpt_types.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"
#include "dotvm/core/state/replication/transport.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Snapshot Receiver Configuration
// ============================================================================

/// @brief Configuration for snapshot receiving
struct SnapshotReceiverConfig {
    std::chrono::seconds receive_timeout{30};  ///< Timeout for entire receive operation
    std::chrono::seconds chunk_timeout{5};     ///< Timeout between chunks
    bool verify_chunks{true};                  ///< Verify CRC32 of each chunk
    bool verify_final_hash{true};              ///< Verify MPT root after applying
    std::size_t max_out_of_order_chunks{10};   ///< Max chunks to buffer if out of order

    [[nodiscard]] static SnapshotReceiverConfig defaults() noexcept {
        return SnapshotReceiverConfig{};
    }
};

// ============================================================================
// Receive Status
// ============================================================================

/// @brief Status of snapshot receive operation
enum class ReceiveStatus : std::uint8_t {
    Idle = 0,       ///< Not currently receiving
    Receiving = 1,  ///< Receiving chunks
    Finalizing = 2, ///< Finalizing snapshot
    Complete = 3,   ///< Snapshot complete
    Failed = 4,     ///< Snapshot receive failed
};

/// @brief Convert receive status to string
[[nodiscard]] constexpr std::string_view to_string(ReceiveStatus status) noexcept {
    switch (status) {
        case ReceiveStatus::Idle:
            return "Idle";
        case ReceiveStatus::Receiving:
            return "Receiving";
        case ReceiveStatus::Finalizing:
            return "Finalizing";
        case ReceiveStatus::Complete:
            return "Complete";
        case ReceiveStatus::Failed:
            return "Failed";
    }
    return "Unknown";
}

// ============================================================================
// Receive State
// ============================================================================

/// @brief State of an ongoing snapshot receive
struct ReceiveState {
    NodeId leader_id;                          ///< Leader sending the snapshot
    LSN snapshot_lsn{LSN::invalid()};          ///< LSN of the snapshot being received
    std::size_t total_size{0};                 ///< Total snapshot size in bytes
    std::size_t bytes_received{0};             ///< Bytes received so far
    std::uint32_t chunks_received{0};          ///< Number of chunks received
    std::uint32_t total_chunks{0};             ///< Total number of chunks expected
    MptHash expected_mpt_root{};               ///< Expected MPT root after applying
    std::chrono::steady_clock::time_point start_time{};       ///< When reception started
    std::chrono::steady_clock::time_point last_chunk_time{};  ///< When last chunk arrived
    ReceiveStatus status{ReceiveStatus::Idle}; ///< Current status
};

// ============================================================================
// Snapshot Sink Interface
// ============================================================================

/// @brief Interface for writing snapshot data
///
/// This interface allows SnapshotReceiver to write snapshot data without
/// depending directly on the SnapshotManager class.
class SnapshotSink {
public:
    virtual ~SnapshotSink() = default;

    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    /// @brief Begin receiving a new snapshot
    ///
    /// @param lsn The snapshot's LSN
    /// @param total_size Total expected size in bytes
    /// @return Success or error
    [[nodiscard]] virtual Result<void> begin_snapshot(LSN lsn, std::size_t total_size) = 0;

    /// @brief Write a chunk of snapshot data
    ///
    /// @param offset Byte offset where chunk belongs
    /// @param data Chunk data
    /// @return Success or error
    [[nodiscard]] virtual Result<void> write_chunk(std::size_t offset,
                                                    std::span<const std::byte> data) = 0;

    /// @brief Finalize the snapshot
    ///
    /// Called when all chunks have been received and verified.
    /// The sink should make the snapshot active.
    ///
    /// @return Success or error
    [[nodiscard]] virtual Result<void> finalize_snapshot() = 0;

    /// @brief Abort the current snapshot receive
    ///
    /// Called when the transfer fails or times out.
    /// The sink should clean up any partial data.
    virtual void abort_snapshot() = 0;

    /// @brief Get the current MPT root hash
    ///
    /// Used for verification after snapshot is applied.
    [[nodiscard]] virtual MptHash mpt_root() const = 0;
};

// ============================================================================
// Snapshot Receiver
// ============================================================================

/// @brief Receives snapshots from the leader via chunked transfer
///
/// The SnapshotReceiver runs on follower nodes and is responsible for:
/// - Requesting snapshots when too far behind for delta streaming
/// - Receiving and reassembling chunked snapshot data
/// - Verifying CRC32 checksums for each chunk
/// - Handling out-of-order chunks within a window
/// - Verifying MPT root hash after completion
/// - Sending ACKs back to the sender
///
/// Thread Safety: All public methods are thread-safe.
class SnapshotReceiver {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    /// @brief Callback invoked when snapshot receive completes
    /// @param lsn The snapshot LSN
    /// @param success True if receive completed successfully
    using SnapshotCompleteCallback = std::function<void(LSN lsn, bool success)>;

    /// @brief Create a snapshot receiver
    ///
    /// @param config Receiver configuration
    /// @param sink Sink for writing snapshot data
    /// @param transport Transport for sending ACKs and requests
    SnapshotReceiver(SnapshotReceiverConfig config, SnapshotSink& sink, Transport& transport);

    ~SnapshotReceiver();

    // Non-copyable, non-movable
    SnapshotReceiver(const SnapshotReceiver&) = delete;
    SnapshotReceiver& operator=(const SnapshotReceiver&) = delete;
    SnapshotReceiver(SnapshotReceiver&&) = delete;
    SnapshotReceiver& operator=(SnapshotReceiver&&) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Start the snapshot receiver
    [[nodiscard]] Result<void> start();

    /// @brief Stop the snapshot receiver
    void stop();

    /// @brief Check if receiver is running
    [[nodiscard]] bool is_running() const noexcept;

    // ========================================================================
    // Snapshot Request
    // ========================================================================

    /// @brief Request a snapshot from the leader
    ///
    /// @param leader_id ID of the leader to request from
    /// @return Success or error
    [[nodiscard]] Result<void> request_snapshot(NodeId leader_id);

    // ========================================================================
    // Chunk Reception
    // ========================================================================

    /// @brief Handle an incoming snapshot chunk
    ///
    /// @param chunk The snapshot chunk
    /// @return Success or error (e.g., checksum mismatch)
    [[nodiscard]] Result<void> receive_chunk(const SnapshotChunk& chunk);

    // ========================================================================
    // State Queries
    // ========================================================================

    /// @brief Check if a receive is in progress
    [[nodiscard]] bool is_receiving() const noexcept;

    /// @brief Get current receive state
    [[nodiscard]] ReceiveState receive_state() const;

    // ========================================================================
    // Abort
    // ========================================================================

    /// @brief Abort the current snapshot receive
    ///
    /// Use this on timeout or leader change.
    void abort_receive();

    // ========================================================================
    // Callbacks
    // ========================================================================

    /// @brief Set callback for completion events
    void set_complete_callback(SnapshotCompleteCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dotvm::core::state::replication
