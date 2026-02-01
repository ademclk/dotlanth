#pragma once

/// @file delta_publisher.hpp
/// @brief STATE-006 Delta streaming publisher (leader side)
///
/// DeltaPublisher streams WAL entries from the leader to followers.
/// It maintains per-follower state tracking acknowledged LSNs and
/// handles backpressure and retransmission.

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
// Delta Publisher Configuration
// ============================================================================

/// @brief Configuration for delta publishing
struct DeltaPublisherConfig {
    std::size_t batch_size{100};                  ///< Max entries per batch
    std::size_t max_batch_bytes{64 * 1024};       ///< Max bytes per batch (64KB)
    std::chrono::milliseconds batch_timeout{10};  ///< Max time to wait for batch fill
    std::chrono::milliseconds ack_timeout{5000};  ///< Timeout waiting for ACK
    std::size_t max_inflight_batches{10};         ///< Max batches awaiting ACK per follower
    bool verify_checksums{true};                  ///< Verify checksums before sending

    [[nodiscard]] static DeltaPublisherConfig defaults() noexcept { return DeltaPublisherConfig{}; }
};

// ============================================================================
// Follower State
// ============================================================================

/// @brief Tracking state for a single follower
struct FollowerDeltaState {
    NodeId follower_id;
    LSN acknowledged_lsn{0};          ///< Highest LSN acknowledged by follower
    LSN sent_lsn{0};                  ///< Highest LSN sent to follower
    std::size_t inflight_batches{0};  ///< Number of batches awaiting ACK
    std::chrono::steady_clock::time_point last_ack_time;
    std::uint64_t bytes_sent{0};
    std::uint64_t batches_sent{0};
    std::uint64_t retransmissions{0};
    bool is_caught_up{false};  ///< Follower is within batch_size of leader
};

// ============================================================================
// Delta Source Interface
// ============================================================================

/// @brief Interface for reading WAL entries for streaming
///
/// This interface allows DeltaPublisher to read entries from the WAL
/// without depending directly on the WriteAheadLog class.
class DeltaSource {
public:
    virtual ~DeltaSource() = default;

    /// @brief Get the current (highest) LSN in the log
    [[nodiscard]] virtual LSN current_lsn() const noexcept = 0;

    /// @brief Read entries from the log
    ///
    /// @param from_lsn Starting LSN (inclusive)
    /// @param max_entries Maximum number of entries to read
    /// @param max_bytes Maximum total bytes to read
    /// @return Vector of log records, or empty if from_lsn is past end
    [[nodiscard]] virtual std::vector<LogRecord> read_entries(LSN from_lsn, std::size_t max_entries,
                                                              std::size_t max_bytes) const = 0;

    /// @brief Check if an LSN is still available (not truncated)
    [[nodiscard]] virtual bool is_available(LSN lsn) const noexcept = 0;
};

// ============================================================================
// Delta Publisher
// ============================================================================

/// @brief Publishes WAL deltas to followers
///
/// The DeltaPublisher runs on the leader node and is responsible for:
/// - Tracking each follower's acknowledged LSN
/// - Batching WAL entries for efficient transmission
/// - Handling ACKs and retransmissions
/// - Managing backpressure when followers are slow
///
/// Thread Safety: All public methods are thread-safe.
class DeltaPublisher {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    /// @brief Callback invoked when a follower acknowledges entries
    using AckCallback = std::function<void(const NodeId& follower, LSN acked_lsn)>;

    /// @brief Create a delta publisher
    ///
    /// @param config Publisher configuration
    /// @param source Source for reading WAL entries
    /// @param transport Transport for sending messages
    DeltaPublisher(DeltaPublisherConfig config, DeltaSource& source, Transport& transport);

    ~DeltaPublisher();

    // Non-copyable, non-movable
    DeltaPublisher(const DeltaPublisher&) = delete;
    DeltaPublisher& operator=(const DeltaPublisher&) = delete;
    DeltaPublisher(DeltaPublisher&&) = delete;
    DeltaPublisher& operator=(DeltaPublisher&&) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Start publishing to followers
    [[nodiscard]] Result<void> start();

    /// @brief Stop publishing
    void stop();

    /// @brief Check if publisher is running
    [[nodiscard]] bool is_running() const noexcept;

    // ========================================================================
    // Follower Management
    // ========================================================================

    /// @brief Add a follower to receive deltas
    ///
    /// @param follower_id Follower's node ID
    /// @param start_lsn LSN to start streaming from (0 = from beginning)
    [[nodiscard]] Result<void> add_follower(const NodeId& follower_id,
                                            state::LSN start_lsn = state::LSN{0});

    /// @brief Remove a follower
    [[nodiscard]] Result<void> remove_follower(const NodeId& follower_id);

    /// @brief Get state for a specific follower
    [[nodiscard]] std::optional<FollowerDeltaState>
    get_follower_state(const NodeId& follower_id) const;

    /// @brief Get all follower states
    [[nodiscard]] std::vector<FollowerDeltaState> get_all_follower_states() const;

    // ========================================================================
    // Publishing
    // ========================================================================

    /// @brief Publish pending entries to all followers
    ///
    /// This reads new entries from the source and sends batches to
    /// followers that are ready (not backpressured).
    ///
    /// @return Number of batches sent
    [[nodiscard]] std::size_t publish();

    /// @brief Notify publisher that new entries are available
    ///
    /// Call this when new entries are written to the WAL.
    void notify_new_entries();

    // ========================================================================
    // Acknowledgments
    // ========================================================================

    /// @brief Handle an ACK from a follower
    ///
    /// @param follower_id Follower that sent the ACK
    /// @param acked_lsn Highest LSN acknowledged
    void handle_ack(const NodeId& follower_id, LSN acked_lsn);

    /// @brief Set callback for ACK events
    void set_ack_callback(AckCallback callback);

    // ========================================================================
    // Statistics
    // ========================================================================

    /// @brief Get total bytes sent across all followers
    [[nodiscard]] std::uint64_t total_bytes_sent() const noexcept;

    /// @brief Get total batches sent across all followers
    [[nodiscard]] std::uint64_t total_batches_sent() const noexcept;

    /// @brief Get minimum acknowledged LSN across all followers
    [[nodiscard]] LSN min_acknowledged_lsn() const;

    /// @brief Check if all followers are caught up
    [[nodiscard]] bool all_caught_up() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dotvm::core::state::replication
