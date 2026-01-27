/// @file snapshot_manager.cpp
/// @brief STATE-009 SnapshotManager implementation
///
/// Provides point-in-time consistent reads leveraging MVCC infrastructure:
/// - COW semantics via version capture
/// - Reference counting for GC coordination
/// - Optional periodic backup thread
/// - Max concurrent snapshots enforcement

#include "dotvm/core/state/snapshot_manager.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>

namespace dotvm::core::state {

// ============================================================================
// Construction / Destruction
// ============================================================================

SnapshotManager::SnapshotManager(std::unique_ptr<StateBackend> backend,
                                 SnapshotManagerConfig config)
    : backend_{std::move(backend)}, config_{std::move(config)} {
    // Start periodic snapshot thread if enabled
    if (config_.enable_periodic_snapshots && !config_.backup_directory.empty()) {
        periodic_thread_ = std::thread([this] { periodic_snapshot_loop(); });
    }
}

SnapshotManager::~SnapshotManager() {
    // Signal shutdown and wait for periodic thread
    shutdown_.store(true, std::memory_order_release);

    if (periodic_thread_.joinable()) {
        periodic_thread_.join();
    }

    // Clear all snapshots (release references)
    {
        std::unique_lock lock(snapshots_mtx_);
        snapshots_.clear();
    }

    // Reset min snapshot version so GC can proceed
    backend_->set_min_snapshot_version(std::numeric_limits<std::uint64_t>::max());
}

// ============================================================================
// Snapshot Lifecycle
// ============================================================================

SnapshotManager::Result<SnapshotId> SnapshotManager::create_snapshot() {
    std::unique_lock lock(snapshots_mtx_);

    // Check limit
    if (snapshots_.size() >= config_.max_concurrent_snapshots) {
        return SnapshotError::TooManySnapshots;
    }

    // Generate new ID and capture version atomically
    SnapshotId id = generate_snapshot_id();
    std::uint64_t version = backend_->current_version();

    // Create metadata
    auto metadata = std::make_unique<detail::SnapshotMetadata>();
    metadata->id = id;
    metadata->captured_version = version;
    metadata->created = std::chrono::steady_clock::now();
    metadata->ref_count.store(1, std::memory_order_relaxed);

    // Insert into map
    snapshots_[id.id] = std::move(metadata);

    // Update backend's min snapshot version for GC coordination
    update_backend_min_version();

    return id;
}

SnapshotManager::Result<void> SnapshotManager::release_snapshot(SnapshotId id) {
    std::unique_lock lock(snapshots_mtx_);

    auto it = snapshots_.find(id.id);
    if (it == snapshots_.end()) {
        return SnapshotError::SnapshotNotFound;
    }

    // Verify generation matches (prevents ABA issues)
    if (it->second->id.generation != id.generation) {
        return SnapshotError::SnapshotNotFound;
    }

    // Decrement ref count
    std::size_t prev_count = it->second->ref_count.fetch_sub(1, std::memory_order_acq_rel);
    if (prev_count == 1) {
        // Last reference - remove snapshot
        snapshots_.erase(it);
    }

    // Update backend's min snapshot version for GC coordination
    update_backend_min_version();

    return {};
}

// ============================================================================
// Snapshot Operations
// ============================================================================

SnapshotManager::Result<std::vector<std::byte>> SnapshotManager::get(SnapshotId id, Key key) const {
    std::uint64_t version = 0;

    {
        std::shared_lock lock(snapshots_mtx_);

        auto* metadata = find_snapshot(id);
        if (metadata == nullptr) {
            return SnapshotError::SnapshotNotFound;
        }

        version = metadata->captured_version;
    }

    // Read from backend at the captured version
    auto result = backend_->get_at_version(key, version);
    if (result.is_err()) {
        if (result.error() == StateBackendError::KeyNotFound) {
            return SnapshotError::KeyNotFound;
        }
        return SnapshotError::BackendError;
    }

    return result.value();
}

SnapshotManager::Result<void> SnapshotManager::iterate(SnapshotId id, Key prefix,
                                                       const IterateCallback& callback) const {
    std::uint64_t version = 0;

    {
        std::shared_lock lock(snapshots_mtx_);

        auto* metadata = find_snapshot(id);
        if (metadata == nullptr) {
            return SnapshotError::SnapshotNotFound;
        }

        version = metadata->captured_version;
    }

    // Iterate backend at the captured version
    auto result = backend_->iterate_at_version(prefix, version, callback);
    if (result.is_err()) {
        if (result.error() == StateBackendError::IterationAborted) {
            return SnapshotError::IterationAborted;
        }
        return SnapshotError::BackendError;
    }

    return {};
}

// ============================================================================
// GC Coordination
// ============================================================================

std::uint64_t SnapshotManager::get_min_active_snapshot_version() const noexcept {
    std::shared_lock lock(snapshots_mtx_);

    if (snapshots_.empty()) {
        return std::numeric_limits<std::uint64_t>::max();
    }

    std::uint64_t min_version = std::numeric_limits<std::uint64_t>::max();
    for (const auto& [id, metadata] : snapshots_) {
        if (metadata->captured_version < min_version) {
            min_version = metadata->captured_version;
        }
    }

    return min_version;
}

// ============================================================================
// Statistics & Configuration
// ============================================================================

std::size_t SnapshotManager::active_snapshot_count() const noexcept {
    std::shared_lock lock(snapshots_mtx_);
    return snapshots_.size();
}

const SnapshotManagerConfig& SnapshotManager::config() const noexcept {
    return config_;
}

StateBackend& SnapshotManager::backend() noexcept {
    return *backend_;
}

const StateBackend& SnapshotManager::backend() const noexcept {
    return *backend_;
}

// ============================================================================
// Internal Helpers
// ============================================================================

detail::SnapshotMetadata* SnapshotManager::find_snapshot(SnapshotId id) const noexcept {
    auto it = snapshots_.find(id.id);
    if (it == snapshots_.end()) {
        return nullptr;
    }

    // Verify generation matches
    if (it->second->id.generation != id.generation) {
        return nullptr;
    }

    return it->second.get();
}

void SnapshotManager::periodic_snapshot_loop() {
    while (!shutdown_.load(std::memory_order_acquire)) {
        // Sleep in small increments to check shutdown flag
        for (int i = 0; i < 10 && !shutdown_.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(config_.periodic_interval / 10);
        }

        if (shutdown_.load(std::memory_order_acquire)) {
            break;
        }

        // Create periodic snapshot (implementation would include backup to file)
        // For now, just create and immediately release to demonstrate the pattern
        auto result = create_snapshot();
        if (result.is_ok()) {
            // In a real implementation, we would serialize state to backup_directory here
            (void)release_snapshot(result.value());
        }
    }
}

SnapshotId SnapshotManager::generate_snapshot_id() noexcept {
    std::uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
    std::uint32_t gen = generation_.load(std::memory_order_relaxed);
    return SnapshotId{.id = id, .generation = gen};
}

void SnapshotManager::update_backend_min_version() {
    // Calculate minimum version across all active snapshots
    std::uint64_t min_version = std::numeric_limits<std::uint64_t>::max();
    for (const auto& [id, metadata] : snapshots_) {
        if (metadata->captured_version < min_version) {
            min_version = metadata->captured_version;
        }
    }

    // Notify backend for GC coordination
    backend_->set_min_snapshot_version(min_version);
}

}  // namespace dotvm::core::state
