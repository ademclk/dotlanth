#pragma once

/// @file snapshot_manager.hpp
/// @brief STATE-009 SnapshotManager for point-in-time consistent reads
///
/// Provides snapshot support leveraging MVCC infrastructure:
/// - COW semantics via version capture
/// - Reference counting for GC coordination
/// - Optional periodic backup thread
/// - Max concurrent snapshots enforcement

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {

// ============================================================================
// Error Codes (128-143 range)
// ============================================================================

/// @brief Error codes for snapshot operations
///
/// Error codes are in the 128-143 range to avoid collision with StateBackendError.
enum class SnapshotError : std::uint8_t {
    SnapshotNotFound = 128,         ///< Snapshot ID does not exist
    SnapshotAlreadyReleased = 129,  ///< Snapshot has already been released
    InvalidSnapshotId = 130,        ///< Snapshot ID is invalid
    KeyNotFound = 131,              ///< Key not found in snapshot
    TooManySnapshots = 132,         ///< Max concurrent snapshots exceeded
    IterationAborted = 133,         ///< Iteration was aborted by callback
    BackendError = 134,             ///< Underlying backend error
    PeriodicSnapshotFailed = 136,   ///< Periodic snapshot creation failed
};

/// @brief Convert error to human-readable string
[[nodiscard]] constexpr const char* to_string(SnapshotError error) noexcept {
    switch (error) {
        case SnapshotError::SnapshotNotFound:
            return "SnapshotNotFound";
        case SnapshotError::SnapshotAlreadyReleased:
            return "SnapshotAlreadyReleased";
        case SnapshotError::InvalidSnapshotId:
            return "InvalidSnapshotId";
        case SnapshotError::KeyNotFound:
            return "KeyNotFound";
        case SnapshotError::TooManySnapshots:
            return "TooManySnapshots";
        case SnapshotError::IterationAborted:
            return "IterationAborted";
        case SnapshotError::BackendError:
            return "BackendError";
        case SnapshotError::PeriodicSnapshotFailed:
            return "PeriodicSnapshotFailed";
    }
    return "Unknown";
}

/// @brief Check if an error is recoverable
[[nodiscard]] constexpr bool is_recoverable(SnapshotError error) noexcept {
    switch (error) {
        case SnapshotError::KeyNotFound:
        case SnapshotError::TooManySnapshots:
        case SnapshotError::IterationAborted:
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Snapshot ID
// ============================================================================

/// @brief Unique identifier for a snapshot
///
/// The combination of id and generation prevents ABA problems when
/// snapshot IDs are reused internally.
struct SnapshotId {
    std::uint64_t id{0};          ///< Unique snapshot ID
    std::uint32_t generation{0};  ///< Generation for reuse detection

    [[nodiscard]] constexpr bool operator==(const SnapshotId&) const noexcept = default;
};

// ============================================================================
// Configuration
// ============================================================================

/// @brief Configuration for SnapshotManager
struct SnapshotManagerConfig {
    std::size_t max_concurrent_snapshots{10};  ///< Max active snapshots (default 10)
    bool enable_periodic_snapshots{false};     ///< Enable background periodic snapshots
    std::chrono::seconds periodic_interval{
        3600};  ///< Interval between periodic snapshots (default 1 hour)
    std::filesystem::path backup_directory;  ///< Directory for periodic backups (optional)

    /// @brief Create default configuration
    [[nodiscard]] static constexpr SnapshotManagerConfig defaults() noexcept {
        return SnapshotManagerConfig{};
    }

    /// @brief Validate configuration
    [[nodiscard]] constexpr bool is_valid() const noexcept { return max_concurrent_snapshots > 0; }
};

// ============================================================================
// Snapshot Metadata (Internal)
// ============================================================================

namespace detail {

/// @brief Internal metadata for an active snapshot
struct SnapshotMetadata {
    SnapshotId id;                                  ///< Snapshot identifier
    std::uint64_t captured_version;                 ///< MVCC version at snapshot creation
    std::chrono::steady_clock::time_point created;  ///< When snapshot was created
    std::atomic<std::size_t> ref_count{1};          ///< Reference count for GC coordination
};

}  // namespace detail

// ============================================================================
// SnapshotManager
// ============================================================================

/// @brief Manages point-in-time consistent snapshots
///
/// SnapshotManager wraps a StateBackend and provides:
/// - Point-in-time consistent reads via MVCC version capture
/// - Reference counting to coordinate with GC
/// - Limit on concurrent snapshots
/// - Optional background periodic snapshot creation
///
/// Thread Safety: Thread-safe. Multiple threads can create/release snapshots
/// and read from the same snapshot concurrently.
///
/// @par Design: Composition over Inheritance
/// SnapshotManager wraps StateBackend rather than extending it.
/// This keeps existing backends unchanged and allows SnapshotManager
/// to work with any backend implementation that supports MVCC versioning.
class SnapshotManager {
public:
    using Key = StateBackend::Key;
    using Value = StateBackend::Value;
    using IterateCallback = StateBackend::IterateCallback;

    template <typename T>
    using Result = ::dotvm::core::Result<T, SnapshotError>;

    // ========================================================================
    // Construction
    // ========================================================================

    /// @brief Create a SnapshotManager wrapping a backend
    /// @param backend The backend to use for storage (takes ownership)
    /// @param config Configuration options
    explicit SnapshotManager(std::unique_ptr<StateBackend> backend,
                             SnapshotManagerConfig config = SnapshotManagerConfig::defaults());

    /// @brief Destructor - stops periodic thread and releases resources
    ~SnapshotManager();

    // Non-copyable, non-movable
    SnapshotManager(const SnapshotManager&) = delete;
    SnapshotManager& operator=(const SnapshotManager&) = delete;
    SnapshotManager(SnapshotManager&&) = delete;
    SnapshotManager& operator=(SnapshotManager&&) = delete;

    // ========================================================================
    // Snapshot Lifecycle
    // ========================================================================

    /// @brief Create a new snapshot capturing current state
    ///
    /// The snapshot captures the current MVCC version, allowing
    /// consistent reads at that point in time.
    ///
    /// @return Snapshot ID, or TooManySnapshots if limit exceeded
    [[nodiscard]] Result<SnapshotId> create_snapshot();

    /// @brief Release a snapshot
    ///
    /// Decrements the reference count. When the count reaches zero,
    /// the snapshot is removed and GC can proceed for versions
    /// older than the next oldest snapshot.
    ///
    /// @param id The snapshot to release
    /// @return Success, or SnapshotNotFound if not found
    [[nodiscard]] Result<void> release_snapshot(SnapshotId id);

    // ========================================================================
    // Snapshot Operations
    // ========================================================================

    /// @brief Get a value at the snapshot's point in time
    ///
    /// @param id The snapshot to read from
    /// @param key The key to look up
    /// @return The value at snapshot time, or KeyNotFound/SnapshotNotFound
    [[nodiscard]] Result<std::vector<std::byte>> get(SnapshotId id, Key key) const;

    /// @brief Iterate over keys at the snapshot's point in time
    ///
    /// @param id The snapshot to iterate
    /// @param prefix Key prefix to filter (empty = all keys)
    /// @param callback Called for each matching key-value pair.
    ///                 Return false to stop iteration early.
    /// @return Success, or error code
    [[nodiscard]] Result<void> iterate(SnapshotId id, Key prefix,
                                       const IterateCallback& callback) const;

    // ========================================================================
    // GC Coordination
    // ========================================================================

    /// @brief Get the minimum version held by any active snapshot
    ///
    /// Returns std::numeric_limits<uint64_t>::max() if no snapshots are active.
    /// The MVCC GC should not prune versions >= this value.
    ///
    /// @return Minimum snapshot version, or max uint64 if none active
    [[nodiscard]] std::uint64_t get_min_active_snapshot_version() const noexcept;

    // ========================================================================
    // Statistics & Configuration
    // ========================================================================

    /// @brief Get the number of active snapshots
    [[nodiscard]] std::size_t active_snapshot_count() const noexcept;

    /// @brief Get the configuration
    [[nodiscard]] const SnapshotManagerConfig& config() const noexcept;

    /// @brief Get the underlying backend
    [[nodiscard]] StateBackend& backend() noexcept;

    /// @brief Get the underlying backend (const)
    [[nodiscard]] const StateBackend& backend() const noexcept;

private:
    // ========================================================================
    // Internal Helpers
    // ========================================================================

    /// @brief Find snapshot metadata by ID
    /// @return Pointer to metadata, or nullptr if not found
    [[nodiscard]] detail::SnapshotMetadata* find_snapshot(SnapshotId id) const noexcept;

    /// @brief Background thread function for periodic snapshots
    void periodic_snapshot_loop();

    /// @brief Generate a new snapshot ID
    [[nodiscard]] SnapshotId generate_snapshot_id() noexcept;

    /// @brief Update backend's min snapshot version for GC coordination
    void update_backend_min_version();

    // ========================================================================
    // Members
    // ========================================================================

    std::unique_ptr<StateBackend> backend_;
    SnapshotManagerConfig config_;

    // Snapshot storage
    // Using map for easier iteration; unique_ptr for stable pointers with atomic ref_count
    std::unordered_map<std::uint64_t, std::unique_ptr<detail::SnapshotMetadata>> snapshots_;
    mutable std::shared_mutex snapshots_mtx_;

    // ID generation (cache-line aligned to avoid false sharing)
    alignas(64) std::atomic<std::uint64_t> next_id_{1};
    alignas(64) std::atomic<std::uint32_t> generation_{1};

    // Periodic snapshot thread
    std::thread periodic_thread_;
    std::atomic<bool> shutdown_{false};
};

}  // namespace dotvm::core::state
