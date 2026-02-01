#pragma once

/// @file delta_subscriber.hpp
/// @brief STATE-006 Delta streaming subscriber (follower side)
///
/// DeltaSubscriber receives WAL deltas from the leader, applies them
/// to the local state, and verifies consistency via MPT root hash.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"
#include "dotvm/core/state/replication/transport.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Delta Subscriber Configuration
// ============================================================================

/// @brief Configuration for delta subscription
struct DeltaSubscriberConfig {
    std::size_t max_pending_batches{100};           ///< Max batches to queue before backpressure
    std::chrono::milliseconds apply_timeout{1000};  ///< Timeout for applying a batch
    bool verify_mpt_root{true};                     ///< Verify MPT root after each batch
    bool verify_checksums{true};                    ///< Verify entry checksums
    std::size_t reorder_window{10};                 ///< Max out-of-order batches to buffer

    [[nodiscard]] static DeltaSubscriberConfig defaults() noexcept {
        return DeltaSubscriberConfig{};
    }
};

// ============================================================================
// Delta Sink Interface
// ============================================================================

/// @brief Interface for applying WAL entries to local state
///
/// This interface allows DeltaSubscriber to apply entries without
/// depending directly on StateBackend or WriteAheadLog classes.
class DeltaSink {
public:
    virtual ~DeltaSink() = default;

    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    /// @brief Get the current applied LSN
    [[nodiscard]] virtual LSN applied_lsn() const noexcept = 0;

    /// @brief Apply a batch of log records
    ///
    /// Records must be applied in LSN order. The sink is responsible
    /// for persisting to local WAL and updating state.
    ///
    /// @param records Records to apply
    /// @return Success or error
    [[nodiscard]] virtual Result<void> apply_batch(const std::vector<LogRecord>& records) = 0;

    /// @brief Get current MPT root hash for verification
    [[nodiscard]] virtual MptHash mpt_root() const = 0;
};

// ============================================================================
// Subscriber Statistics
// ============================================================================

/// @brief Statistics for delta subscription
struct DeltaSubscriberStats {
    LSN applied_lsn{0};   ///< Highest applied LSN
    LSN received_lsn{0};  ///< Highest received LSN
    std::uint64_t batches_received{0};
    std::uint64_t batches_applied{0};
    std::uint64_t bytes_received{0};
    std::uint64_t verification_failures{0};
    std::uint64_t reordered_batches{0};
    std::chrono::steady_clock::time_point last_batch_time;
};

// ============================================================================
// Delta Subscriber
// ============================================================================

/// @brief Subscribes to WAL deltas from the leader
///
/// The DeltaSubscriber runs on follower nodes and is responsible for:
/// - Receiving delta batches from the leader
/// - Buffering and reordering out-of-order batches
/// - Applying batches to local state in LSN order
/// - Verifying MPT root hash after each batch
/// - Sending ACKs back to the leader
///
/// Thread Safety: All public methods are thread-safe.
class DeltaSubscriber {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    /// @brief Callback invoked when batches are applied
    using ApplyCallback = std::function<void(LSN applied_lsn, std::size_t entries_count)>;

    /// @brief Callback invoked when verification fails
    using VerificationFailureCallback =
        std::function<void(LSN lsn, const MptHash& expected, const MptHash& actual)>;

    /// @brief Create a delta subscriber
    ///
    /// @param config Subscriber configuration
    /// @param sink Sink for applying entries
    /// @param transport Transport for sending ACKs
    /// @param leader_id ID of the leader node
    DeltaSubscriber(DeltaSubscriberConfig config, DeltaSink& sink, Transport& transport,
                    NodeId leader_id);

    ~DeltaSubscriber();

    // Non-copyable, non-movable
    DeltaSubscriber(const DeltaSubscriber&) = delete;
    DeltaSubscriber& operator=(const DeltaSubscriber&) = delete;
    DeltaSubscriber(DeltaSubscriber&&) = delete;
    DeltaSubscriber& operator=(DeltaSubscriber&&) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Start receiving deltas
    [[nodiscard]] Result<void> start();

    /// @brief Stop receiving deltas
    void stop();

    /// @brief Check if subscriber is running
    [[nodiscard]] bool is_running() const noexcept;

    // ========================================================================
    // Delta Reception
    // ========================================================================

    /// @brief Handle an incoming delta batch
    ///
    /// This is called when a DeltaBatch message is received from the leader.
    /// The batch is queued and will be applied when all prior batches have
    /// been applied.
    ///
    /// @param batch The delta batch
    /// @return Success or error (e.g., queue full)
    [[nodiscard]] Result<void> receive_batch(const DeltaBatch& batch);

    /// @brief Process pending batches
    ///
    /// Applies queued batches that are ready (in LSN order).
    /// Call this periodically or after receiving batches.
    ///
    /// @return Number of batches applied
    [[nodiscard]] std::size_t process_pending();

    // ========================================================================
    // State
    // ========================================================================

    /// @brief Get current applied LSN
    [[nodiscard]] LSN applied_lsn() const noexcept;

    /// @brief Get number of pending batches
    [[nodiscard]] std::size_t pending_count() const noexcept;

    /// @brief Check if subscriber is caught up (no pending batches)
    [[nodiscard]] bool is_caught_up() const noexcept;

    /// @brief Get statistics
    [[nodiscard]] DeltaSubscriberStats stats() const;

    // ========================================================================
    // Leader Management
    // ========================================================================

    /// @brief Update leader ID (on leader change)
    void set_leader(const NodeId& leader_id);

    /// @brief Get current leader ID
    [[nodiscard]] NodeId leader_id() const;

    // ========================================================================
    // Callbacks
    // ========================================================================

    /// @brief Set callback for batch application events
    void set_apply_callback(ApplyCallback callback);

    /// @brief Set callback for verification failure events
    void set_verification_failure_callback(VerificationFailureCallback callback);

    // ========================================================================
    // Recovery
    // ========================================================================

    /// @brief Request retransmission from a specific LSN
    ///
    /// Call this after detecting a gap or verification failure.
    [[nodiscard]] Result<void> request_retransmit(LSN from_lsn);

    /// @brief Clear all pending batches
    ///
    /// Use this before requesting a snapshot or after leader change.
    void clear_pending();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dotvm::core::state::replication
