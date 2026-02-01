#pragma once

/// @file snapshot_sender.hpp
/// @brief STATE-006 Snapshot transfer sender (leader side)
///
/// SnapshotSender runs on the leader and sends consistent snapshots to new
/// followers who are too far behind for delta streaming. It chunks the snapshot
/// data and manages concurrent transfers to multiple followers.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"
#include "dotvm/core/state/replication/transport.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Snapshot Sender Configuration
// ============================================================================

/// @brief Configuration for snapshot transfer
struct SnapshotSenderConfig {
    std::size_t chunk_size{64 * 1024};                  ///< Size of each chunk (64KB)
    std::size_t max_concurrent_transfers{2};            ///< Max parallel snapshot transfers
    std::chrono::milliseconds transfer_timeout{30000};  ///< Timeout for entire transfer (30s)
    bool verify_chunks{true};                           ///< Compute CRC32 for each chunk

    [[nodiscard]] static SnapshotSenderConfig defaults() noexcept { return SnapshotSenderConfig{}; }
};

// ============================================================================
// Transfer Status
// ============================================================================

/// @brief Status of a snapshot transfer
enum class TransferStatus : std::uint8_t {
    Pending = 0,     ///< Transfer initiated but not started
    InProgress = 1,  ///< Transfer is actively sending chunks
    Completed = 2,   ///< Transfer completed successfully
    Failed = 3,      ///< Transfer failed (transport or source error)
    Cancelled = 4,   ///< Transfer was cancelled
};

/// @brief Convert transfer status to string
[[nodiscard]] constexpr std::string_view to_string(TransferStatus status) noexcept {
    switch (status) {
        case TransferStatus::Pending:
            return "Pending";
        case TransferStatus::InProgress:
            return "InProgress";
        case TransferStatus::Completed:
            return "Completed";
        case TransferStatus::Failed:
            return "Failed";
        case TransferStatus::Cancelled:
            return "Cancelled";
    }
    return "Unknown";
}

// ============================================================================
// Transfer State
// ============================================================================

/// @brief Tracking state for a single snapshot transfer
struct TransferState {
    NodeId follower_id;                                ///< Target follower receiving the snapshot
    state::LSN snapshot_lsn;                           ///< LSN at which snapshot was taken
    std::size_t total_size{0};                         ///< Total snapshot size in bytes
    std::size_t bytes_sent{0};                         ///< Bytes successfully sent
    std::uint32_t chunks_sent{0};                      ///< Number of chunks sent
    std::uint32_t total_chunks{0};                     ///< Total number of chunks
    std::chrono::steady_clock::time_point start_time;  ///< When transfer was initiated
    std::chrono::steady_clock::time_point last_activity_time;  ///< Last chunk sent
    TransferStatus status{TransferStatus::Pending};            ///< Current status
};

// ============================================================================
// Snapshot Source Interface
// ============================================================================

/// @brief Interface for reading snapshot data
///
/// This interface allows SnapshotSender to read snapshot data without
/// depending directly on the SnapshotManager class.
class SnapshotSource {
public:
    virtual ~SnapshotSource() = default;

    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    /// @brief Get the LSN of the current snapshot
    [[nodiscard]] virtual state::LSN snapshot_lsn() const = 0;

    /// @brief Get total size of the snapshot in bytes
    [[nodiscard]] virtual std::size_t total_size() const = 0;

    /// @brief Read a chunk at the given offset
    ///
    /// @param offset Starting byte offset in the snapshot
    /// @param size Maximum number of bytes to read
    /// @return Chunk data, or error if read failed
    [[nodiscard]] virtual Result<std::vector<std::byte>> read_chunk(std::size_t offset,
                                                                    std::size_t size) const = 0;

    /// @brief Get MPT root hash of the snapshot
    [[nodiscard]] virtual MptHash mpt_root() const = 0;
};

// ============================================================================
// Snapshot Sender
// ============================================================================

/// @brief Sends snapshots to followers
///
/// The SnapshotSender runs on the leader node and is responsible for:
/// - Chunking snapshot data for efficient transmission
/// - Managing concurrent transfers to multiple followers
/// - Tracking transfer progress and handling failures
/// - Computing checksums for data integrity verification
///
/// Thread Safety: All public methods are thread-safe.
class SnapshotSender {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    /// @brief Callback invoked when a transfer completes (success or failure)
    using TransferCompleteCallback = std::function<void(const NodeId& follower, bool success)>;

    /// @brief Create a snapshot sender
    ///
    /// @param config Sender configuration
    /// @param source Source for reading snapshot data
    /// @param transport Transport for sending messages
    SnapshotSender(SnapshotSenderConfig config, SnapshotSource& source, Transport& transport);

    ~SnapshotSender();

    // Non-copyable, non-movable
    SnapshotSender(const SnapshotSender&) = delete;
    SnapshotSender& operator=(const SnapshotSender&) = delete;
    SnapshotSender(SnapshotSender&&) = delete;
    SnapshotSender& operator=(SnapshotSender&&) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Start the snapshot sender
    [[nodiscard]] Result<void> start();

    /// @brief Stop the snapshot sender
    void stop();

    /// @brief Check if sender is running
    [[nodiscard]] bool is_running() const noexcept;

    // ========================================================================
    // Transfer Management
    // ========================================================================

    /// @brief Initiate a snapshot transfer to a follower
    ///
    /// Creates a new transfer for the given follower. The transfer will begin
    /// sending chunks when process_transfers() is called.
    ///
    /// @param follower_id Target follower's node ID
    /// @return Success, or error if transfer cannot be initiated
    [[nodiscard]] Result<void> initiate_transfer(const NodeId& follower_id);

    /// @brief Cancel an in-progress transfer
    ///
    /// @param follower_id Follower whose transfer to cancel
    /// @return Success, or NodeNotFound if no transfer exists
    [[nodiscard]] Result<void> cancel_transfer(const NodeId& follower_id);

    /// @brief Process active transfers
    ///
    /// Sends the next chunk for each active transfer. Call this periodically
    /// to make progress on snapshot transfers.
    ///
    /// @return Number of chunks sent
    [[nodiscard]] std::size_t process_transfers();

    // ========================================================================
    // Transfer State Queries
    // ========================================================================

    /// @brief Get state for a specific transfer
    ///
    /// @param follower_id Follower's node ID
    /// @return Transfer state if found
    [[nodiscard]] std::optional<TransferState> get_transfer_state(const NodeId& follower_id) const;

    /// @brief Get all active (non-completed) transfers
    [[nodiscard]] std::vector<TransferState> get_active_transfers() const;

    // ========================================================================
    // Callbacks
    // ========================================================================

    /// @brief Set callback for transfer completion events
    void set_transfer_complete_callback(TransferCompleteCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dotvm::core::state::replication
