/// @file in_memory_backend.cpp
/// @brief STATE-002 MVCC In-memory StateBackend implementation
///
/// Implements Multi-Version Concurrency Control (MVCC) with:
/// - Snapshot isolation for concurrent transactions
/// - Write-write conflict detection (first-committer-wins)
/// - Garbage collection of old versions
/// - Thread-safe operations with shared_mutex

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {

namespace {

/// @brief Comparator for byte vectors (lexicographic ordering)
struct ByteVectorCompare {
    bool operator()(const std::vector<std::byte>& lhs,
                    const std::vector<std::byte>& rhs) const noexcept {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }
};

/// @brief Check if key starts with prefix
[[nodiscard]] bool starts_with(const std::vector<std::byte>& key,
                               std::span<const std::byte> prefix) noexcept {
    if (prefix.empty()) {
        return true;
    }
    if (key.size() < prefix.size()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), key.begin());
}

// ============================================================================
// MVCC Data Structures
// ============================================================================

/// @brief A single version of a value
struct VersionEntry {
    std::uint64_t version;         ///< Version number (commit timestamp)
    std::vector<std::byte> value;  ///< The value data
    bool deleted;                  ///< True if this version represents a deletion
};

/// @brief Versioned value holding multiple versions
struct VersionedValue {
    std::vector<VersionEntry> versions;  ///< Versions in ascending order by version
    mutable std::shared_mutex mtx;       ///< Per-key lock for concurrent access

    /// @brief Get the version visible at the given snapshot
    /// @param snapshot The snapshot version to read at
    /// @return Pointer to visible version entry, or nullptr if not visible
    [[nodiscard]] const VersionEntry* get_visible(std::uint64_t snapshot) const noexcept {
        // Binary search for the largest version <= snapshot
        if (versions.empty()) {
            return nullptr;
        }

        // Find the first version > snapshot
        auto it = std::upper_bound(
            versions.begin(), versions.end(), snapshot,
            [](std::uint64_t snap, const VersionEntry& entry) { return snap < entry.version; });

        // The version we want is the one before that
        if (it == versions.begin()) {
            return nullptr;  // All versions are > snapshot
        }
        --it;

        return &(*it);
    }

    /// @brief Add a new version
    /// @param ver Version number
    /// @param val Value data
    /// @param del True if this is a deletion marker
    void add_version(std::uint64_t ver, std::vector<std::byte> val, bool del = false) {
        versions.push_back(VersionEntry{ver, std::move(val), del});
    }

    /// @brief Garbage collect versions older than min_version
    /// @param min_version Minimum version to keep
    /// @return Number of bytes freed
    [[nodiscard]] std::size_t gc(std::uint64_t min_version) {
        std::size_t freed = 0;
        auto it = versions.begin();
        while (it != versions.end() && versions.size() > 1) {
            if (it->version < min_version) {
                // Check if next version is also < min_version or doesn't exist
                auto next = it + 1;
                if (next != versions.end() && next->version <= min_version) {
                    freed += it->value.size();
                    it = versions.erase(it);
                    continue;
                }
            }
            ++it;
        }
        return freed;
    }

    /// @brief Get the latest version entry
    [[nodiscard]] const VersionEntry* latest() const noexcept {
        if (versions.empty()) {
            return nullptr;
        }
        return &versions.back();
    }
};

/// @brief MVCC Transaction state
struct MvccTransaction {
    TxId id;
    std::uint64_t snapshot_version;  ///< Snapshot version at transaction start
    bool active{true};
    /// Write set: key -> optional<value> (nullopt = delete)
    std::map<std::vector<std::byte>, std::optional<std::vector<std::byte>>, ByteVectorCompare>
        write_set;
};

}  // namespace

// ============================================================================
// InMemoryBackend MVCC Implementation
// ============================================================================

/// @brief In-memory MVCC implementation of StateBackend
///
/// Uses Multi-Version Concurrency Control for transaction isolation:
/// - Each write creates a new version instead of overwriting
/// - Transactions read from a consistent snapshot
/// - Write-write conflicts detected at commit time (first-committer-wins)
///
/// Thread Safety: Thread-safe via shared_mutex for read/write separation.
class InMemoryBackend final : public StateBackend {
public:
    explicit InMemoryBackend(StateBackendConfig config) noexcept : config_{std::move(config)} {}

    // ========================================================================
    // CRUD Operations
    // ========================================================================

    [[nodiscard]] Result<std::vector<std::byte>> get(Key key) const override {
        auto key_vec = std::vector<std::byte>(key.begin(), key.end());

        // Check active transaction's write set first (no lock needed - thread-local)
        if (active_tx_) {
            auto& tx = transactions_.at(active_tx_->id);
            auto it = tx.write_set.find(key_vec);
            if (it != tx.write_set.end()) {
                if (it->second.has_value()) {
                    return it->second.value();
                }
                return StateBackendError::KeyNotFound;  // Deleted in transaction
            }
        }

        // Read from storage with shared lock
        std::shared_lock lock(storage_mtx_);

        auto it = storage_.find(key_vec);
        if (it == storage_.end()) {
            return StateBackendError::KeyNotFound;
        }

        // Get snapshot version
        std::uint64_t read_version = active_tx_ ? transactions_.at(active_tx_->id).snapshot_version
                                                : global_version_.load(std::memory_order_acquire);

        // Read at snapshot version
        std::shared_lock key_lock(it->second.mtx);
        const VersionEntry* entry = it->second.get_visible(read_version);
        if (entry == nullptr || entry->deleted) {
            return StateBackendError::KeyNotFound;
        }

        return entry->value;
    }

    [[nodiscard]] Result<void> put(Key key, Value value) override {
        // Validate key
        if (key.empty()) {
            return StateBackendError::InvalidKey;
        }
        if (key.size() > config_.max_key_size) {
            return StateBackendError::KeyTooLarge;
        }
        if (value.size() > config_.max_value_size) {
            return StateBackendError::ValueTooLarge;
        }

        auto key_vec = std::vector<std::byte>(key.begin(), key.end());
        auto value_vec = std::vector<std::byte>(value.begin(), value.end());

        // If transaction active, buffer in write set
        if (active_tx_) {
            auto& tx = transactions_.at(active_tx_->id);
            tx.write_set[key_vec] = value_vec;
            return {};
        }

        // Direct write with exclusive lock
        std::unique_lock lock(storage_mtx_);
        std::uint64_t version = global_version_.fetch_add(1, std::memory_order_acq_rel) + 1;

        auto [it, inserted] = storage_.try_emplace(key_vec);
        std::unique_lock key_lock(it->second.mtx);

        if (inserted) {
            storage_bytes_.fetch_add(key_vec.size() + value_vec.size(), std::memory_order_relaxed);
        } else {
            // Update storage bytes for value change
            const VersionEntry* latest = it->second.latest();
            if (latest && !latest->deleted) {
                storage_bytes_.fetch_sub(latest->value.size(), std::memory_order_relaxed);
            }
            storage_bytes_.fetch_add(value_vec.size(), std::memory_order_relaxed);
        }

        it->second.add_version(version, std::move(value_vec), false);

        return {};
    }

    [[nodiscard]] Result<void> remove(Key key) override {
        auto key_vec = std::vector<std::byte>(key.begin(), key.end());

        // If transaction active, buffer deletion
        if (active_tx_) {
            auto& tx = transactions_.at(active_tx_->id);

            // Check if key exists (in write set or storage)
            bool exists_in_write_set =
                tx.write_set.count(key_vec) > 0 && tx.write_set.at(key_vec).has_value();

            bool exists_in_storage = false;
            {
                std::shared_lock lock(storage_mtx_);
                auto it = storage_.find(key_vec);
                if (it != storage_.end()) {
                    std::shared_lock key_lock(it->second.mtx);
                    const VersionEntry* entry = it->second.get_visible(tx.snapshot_version);
                    exists_in_storage = (entry != nullptr && !entry->deleted);
                }
            }

            if (!exists_in_write_set && !exists_in_storage) {
                return StateBackendError::KeyNotFound;
            }

            tx.write_set[key_vec] = std::nullopt;  // Mark as deleted
            return {};
        }

        // Direct removal with exclusive lock
        std::unique_lock lock(storage_mtx_);

        auto it = storage_.find(key_vec);
        if (it == storage_.end()) {
            return StateBackendError::KeyNotFound;
        }

        std::unique_lock key_lock(it->second.mtx);
        const VersionEntry* latest = it->second.latest();
        if (latest == nullptr || latest->deleted) {
            return StateBackendError::KeyNotFound;
        }

        std::uint64_t version = global_version_.fetch_add(1, std::memory_order_acq_rel) + 1;

        // Update storage bytes
        storage_bytes_.fetch_sub(key_vec.size() + latest->value.size(), std::memory_order_relaxed);

        it->second.add_version(version, {}, true);  // Add deletion marker

        return {};
    }

    [[nodiscard]] bool exists(Key key) const noexcept override {
        auto key_vec = std::vector<std::byte>(key.begin(), key.end());

        // Check active transaction's write set first
        if (active_tx_) {
            auto& tx = transactions_.at(active_tx_->id);
            auto it = tx.write_set.find(key_vec);
            if (it != tx.write_set.end()) {
                return it->second.has_value();
            }
        }

        // Read from storage
        std::shared_lock lock(storage_mtx_);

        auto it = storage_.find(key_vec);
        if (it == storage_.end()) {
            return false;
        }

        std::uint64_t read_version = active_tx_ ? transactions_.at(active_tx_->id).snapshot_version
                                                : global_version_.load(std::memory_order_acquire);

        std::shared_lock key_lock(it->second.mtx);
        const VersionEntry* entry = it->second.get_visible(read_version);
        return entry != nullptr && !entry->deleted;
    }

    // ========================================================================
    // Iteration
    // ========================================================================

    [[nodiscard]] Result<void> iterate(Key prefix, const IterateCallback& callback) const override {
        auto prefix_vec = std::vector<std::byte>(prefix.begin(), prefix.end());

        // Build snapshot of matching keys
        std::map<std::vector<std::byte>, std::vector<std::byte>, ByteVectorCompare> snapshot;

        std::uint64_t read_version;
        const MvccTransaction* tx_ptr = nullptr;

        if (active_tx_) {
            tx_ptr = &transactions_.at(active_tx_->id);
            read_version = tx_ptr->snapshot_version;
        } else {
            read_version = global_version_.load(std::memory_order_acquire);
        }

        // Capture snapshot from storage under shared lock
        {
            std::shared_lock lock(storage_mtx_);

            auto it = storage_.lower_bound(prefix_vec);
            while (it != storage_.end()) {
                if (!starts_with(it->first, prefix_vec)) {
                    break;
                }

                std::shared_lock key_lock(it->second.mtx);
                const VersionEntry* entry = it->second.get_visible(read_version);
                if (entry != nullptr && !entry->deleted) {
                    snapshot[it->first] = entry->value;
                }
                ++it;
            }
        }

        // Merge uncommitted writes from write set
        if (tx_ptr != nullptr) {
            for (const auto& [key, value_opt] : tx_ptr->write_set) {
                if (!starts_with(key, prefix_vec)) {
                    continue;
                }

                if (value_opt.has_value()) {
                    snapshot[key] = value_opt.value();  // Add or override
                } else {
                    snapshot.erase(key);  // Remove deleted key
                }
            }
        }

        // Invoke callback on snapshot (no locks held)
        for (const auto& [key, value] : snapshot) {
            Key key_span{key};
            Value value_span{value};
            if (!callback(key_span, value_span)) {
                break;
            }
        }

        return {};
    }

    // ========================================================================
    // Batch Operations
    // ========================================================================

    [[nodiscard]] Result<void> batch(std::span<const BatchOp> ops) override {
        if (ops.empty()) {
            return {};
        }

        std::unique_lock lock(storage_mtx_);

        // Validate all operations first
        for (const auto& op : ops) {
            if (op.type == BatchOpType::Put) {
                if (op.key.empty()) {
                    return StateBackendError::InvalidKey;
                }
                if (op.key.size() > config_.max_key_size) {
                    return StateBackendError::KeyTooLarge;
                }
                if (op.value.size() > config_.max_value_size) {
                    return StateBackendError::ValueTooLarge;
                }
            } else if (op.type == BatchOpType::Remove) {
                auto key_vec = std::vector<std::byte>(op.key.begin(), op.key.end());
                auto it = storage_.find(key_vec);
                if (it == storage_.end()) {
                    return StateBackendError::KeyNotFound;
                }
                std::shared_lock key_lock(it->second.mtx);
                const VersionEntry* latest = it->second.latest();
                if (latest == nullptr || latest->deleted) {
                    return StateBackendError::KeyNotFound;
                }
            }
        }

        // Apply all operations atomically
        std::uint64_t version = global_version_.fetch_add(1, std::memory_order_acq_rel) + 1;

        for (const auto& op : ops) {
            auto key_vec = std::vector<std::byte>(op.key.begin(), op.key.end());

            if (op.type == BatchOpType::Put) {
                auto value_vec = std::vector<std::byte>(op.value.begin(), op.value.end());

                auto [it, inserted] = storage_.try_emplace(key_vec);
                std::unique_lock key_lock(it->second.mtx);

                if (inserted) {
                    storage_bytes_.fetch_add(key_vec.size() + value_vec.size(),
                                             std::memory_order_relaxed);
                } else {
                    const VersionEntry* latest = it->second.latest();
                    if (latest && !latest->deleted) {
                        storage_bytes_.fetch_sub(latest->value.size(), std::memory_order_relaxed);
                    }
                    storage_bytes_.fetch_add(value_vec.size(), std::memory_order_relaxed);
                }

                it->second.add_version(version, std::move(value_vec), false);
            } else {
                auto it = storage_.find(key_vec);
                if (it != storage_.end()) {
                    std::unique_lock key_lock(it->second.mtx);
                    const VersionEntry* latest = it->second.latest();
                    if (latest && !latest->deleted) {
                        storage_bytes_.fetch_sub(key_vec.size() + latest->value.size(),
                                                 std::memory_order_relaxed);
                    }
                    it->second.add_version(version, {}, true);
                }
            }
        }

        return {};
    }

    // ========================================================================
    // Transactions
    // ========================================================================

    [[nodiscard]] Result<TxHandle> begin_transaction() override {
        if (!config_.enable_transactions) {
            return StateBackendError::UnsupportedOperation;
        }

        TxId id{.id = next_tx_id_++, .generation = tx_generation_};

        // Capture snapshot version at transaction start
        std::uint64_t snapshot = global_version_.load(std::memory_order_acquire);

        MvccTransaction tx{.id = id, .snapshot_version = snapshot, .active = true, .write_set = {}};

        transactions_[id.id] = std::move(tx);
        active_tx_ = id;

        return TxHandle{this, id};
    }

    [[nodiscard]] Result<void> commit(TxHandle tx) override {
        if (!tx.is_valid()) {
            return StateBackendError::InvalidTransaction;
        }

        auto tx_it = transactions_.find(tx.id().id);
        if (tx_it == transactions_.end() || !tx_it->second.active ||
            tx_it->second.id.generation != tx.id().generation) {
            return StateBackendError::InvalidTransaction;
        }

        auto& mvcc_tx = tx_it->second;

        // Acquire exclusive lock for commit
        std::unique_lock lock(storage_mtx_);

        // Allocate commit version atomically
        std::uint64_t commit_version = global_version_.fetch_add(1, std::memory_order_acq_rel) + 1;

        // Conflict detection: check if any key in write_set was modified since snapshot
        for (const auto& [key, value_opt] : mvcc_tx.write_set) {
            auto it = storage_.find(key);
            if (it != storage_.end()) {
                std::shared_lock key_lock(it->second.mtx);
                const VersionEntry* latest = it->second.latest();
                if (latest != nullptr && latest->version > mvcc_tx.snapshot_version) {
                    // Conflict: key was modified after our snapshot
                    transactions_.erase(tx_it);
                    if (active_tx_ && active_tx_->id == tx.id().id) {
                        active_tx_.reset();
                    }
                    tx.release();
                    return StateBackendError::TransactionConflict;
                }
            }
        }

        // No conflicts - apply all write set entries
        for (auto& [key, value_opt] : mvcc_tx.write_set) {
            if (value_opt.has_value()) {
                // Put
                auto [it, inserted] = storage_.try_emplace(key);
                std::unique_lock key_lock(it->second.mtx);

                if (inserted) {
                    storage_bytes_.fetch_add(key.size() + value_opt->size(),
                                             std::memory_order_relaxed);
                } else {
                    const VersionEntry* latest = it->second.latest();
                    if (latest && !latest->deleted) {
                        storage_bytes_.fetch_sub(latest->value.size(), std::memory_order_relaxed);
                    }
                    storage_bytes_.fetch_add(value_opt->size(), std::memory_order_relaxed);
                }

                it->second.add_version(commit_version, std::move(*value_opt), false);
            } else {
                // Remove
                auto it = storage_.find(key);
                if (it != storage_.end()) {
                    std::unique_lock key_lock(it->second.mtx);
                    const VersionEntry* latest = it->second.latest();
                    if (latest && !latest->deleted) {
                        storage_bytes_.fetch_sub(key.size() + latest->value.size(),
                                                 std::memory_order_relaxed);
                    }
                    it->second.add_version(commit_version, {}, true);
                }
            }
        }

        // Clean up transaction
        transactions_.erase(tx_it);
        if (active_tx_ && active_tx_->id == tx.id().id) {
            active_tx_.reset();
        }

        // Trigger garbage collection
        maybe_gc();

        tx.release();
        return {};
    }

    [[nodiscard]] Result<void> rollback(TxHandle tx) override {
        if (!tx.is_valid()) {
            return StateBackendError::InvalidTransaction;
        }

        auto it = transactions_.find(tx.id().id);
        if (it == transactions_.end()) {
            tx.release();
            return {};
        }

        if (it->second.id.generation != tx.id().generation) {
            return StateBackendError::InvalidTransaction;
        }

        transactions_.erase(it);
        if (active_tx_ && active_tx_->id == tx.id().id) {
            active_tx_.reset();
        }

        tx.release();
        return {};
    }

    // ========================================================================
    // Statistics & Configuration
    // ========================================================================

    [[nodiscard]] std::size_t key_count() const noexcept override {
        std::shared_lock lock(storage_mtx_);

        std::uint64_t read_version = global_version_.load(std::memory_order_acquire);
        std::size_t count = 0;

        for (const auto& [key, versioned] : storage_) {
            std::shared_lock key_lock(versioned.mtx);
            const VersionEntry* latest = versioned.get_visible(read_version);
            if (latest != nullptr && !latest->deleted) {
                ++count;
            }
        }

        return count;
    }

    [[nodiscard]] std::size_t storage_bytes() const noexcept override {
        return storage_bytes_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] const StateBackendConfig& config() const noexcept override { return config_; }

    [[nodiscard]] bool supports_transactions() const noexcept override {
        return config_.enable_transactions;
    }

    void clear() noexcept override {
        std::unique_lock lock(storage_mtx_);
        storage_.clear();
        storage_bytes_.store(0, std::memory_order_relaxed);
        transactions_.clear();
        active_tx_.reset();
        global_version_.store(0, std::memory_order_release);
    }

private:
    /// @brief Update minimum active snapshot version
    [[nodiscard]] std::uint64_t get_min_active_snapshot() const noexcept {
        std::uint64_t min_snapshot = global_version_.load(std::memory_order_acquire);
        for (const auto& [id, tx] : transactions_) {
            if (tx.active && tx.snapshot_version < min_snapshot) {
                min_snapshot = tx.snapshot_version;
            }
        }
        return min_snapshot;
    }

    /// @brief Perform garbage collection if conditions are met
    void maybe_gc() {
        // Only GC if no active transactions
        if (!transactions_.empty()) {
            return;
        }

        std::uint64_t min_version = global_version_.load(std::memory_order_acquire);

        for (auto& [key, versioned] : storage_) {
            std::unique_lock key_lock(versioned.mtx);
            std::size_t freed = versioned.gc(min_version);
            if (freed > 0) {
                // Note: storage_bytes tracking for GC is approximate
                // since we don't track all version storage separately
            }
        }
    }

    StateBackendConfig config_;

    // MVCC versioned storage (sorted map for prefix iteration)
    mutable std::map<std::vector<std::byte>, VersionedValue, ByteVectorCompare> storage_;
    mutable std::shared_mutex storage_mtx_;  ///< Reader/writer lock for storage map

    std::atomic<std::size_t> storage_bytes_{0};
    std::atomic<std::uint64_t> global_version_{0};

    // Transaction management
    std::unordered_map<std::uint64_t, MvccTransaction> transactions_;
    std::optional<TxId> active_tx_;
    std::uint64_t next_tx_id_{1};
    std::uint32_t tx_generation_{1};
};

// ============================================================================
// Factory Function
// ============================================================================

std::unique_ptr<StateBackend> create_state_backend(const StateBackendConfig& config) {
    return std::make_unique<InMemoryBackend>(config);
}

}  // namespace dotvm::core::state
