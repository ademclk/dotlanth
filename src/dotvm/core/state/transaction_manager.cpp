/// @file transaction_manager.cpp
/// @brief STATE-003 TransactionManager implementation
///
/// Implements OCC-based transaction management with deadlock detection.

#include "dotvm/core/state/transaction_manager.hpp"

#include <algorithm>
#include <chrono>

namespace dotvm::core::state {

// ============================================================================
// Construction / Destruction
// ============================================================================

TransactionManager::TransactionManager(std::unique_ptr<StateBackend> backend,
                                       TransactionManagerConfig config)
    : backend_{std::move(backend)}, config_{std::move(config)} {
    // Initialize global version to 1 (0 means "never written")
    global_version_.store(1, std::memory_order_release);

    // Start deadlock detector thread
    deadlock_detector_ = std::thread([this]() {
        while (!shutdown_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(config_.deadlock_check_interval);
            if (!shutdown_.load(std::memory_order_acquire)) {
                check_deadlocks();
            }
        }
    });
}

TransactionManager::~TransactionManager() {
    // Signal shutdown
    shutdown_.store(true, std::memory_order_release);

    // Join the deadlock detector thread
    if (deadlock_detector_.joinable()) {
        deadlock_detector_.join();
    }

    // Abort any remaining active transactions
    std::unique_lock lock(transactions_mtx_);
    for (auto& [id, tx] : transactions_) {
        if (tx.state == TransactionState::Active) {
            tx.state = TransactionState::Aborted;
        }
    }
}

// ============================================================================
// Transaction Lifecycle
// ============================================================================

TransactionManager::Result<ManagedTransaction*>
TransactionManager::begin(std::optional<TransactionIsolationLevel> isolation,
                          std::optional<std::chrono::milliseconds> timeout) {
    // Check max concurrent transactions
    {
        std::lock_guard active_lock(active_mtx_);
        if (active_transactions_.size() >= config_.max_concurrent_transactions) {
            return StateBackendError::TooManyTransactions;
        }
    }

    // Generate new transaction ID
    TxId id = generate_tx_id();

    // Capture current global version
    std::uint64_t start_version = get_current_version();

    // Create the transaction
    ManagedTransaction tx;
    tx.id = id;
    tx.state = TransactionState::Active;
    tx.start_version = start_version;
    tx.isolation_level = isolation.value_or(config_.default_isolation);
    tx.start_time = std::chrono::steady_clock::now();
    tx.timeout = timeout.value_or(config_.default_timeout);

    // Store and track the transaction
    ManagedTransaction* tx_ptr = nullptr;
    {
        std::unique_lock lock(transactions_mtx_);
        auto [it, inserted] = transactions_.emplace(id.id, std::move(tx));
        tx_ptr = &it->second;
    }

    {
        std::lock_guard active_lock(active_mtx_);
        active_transactions_.insert(id.id);
    }

    return tx_ptr;
}

TransactionManager::Result<void> TransactionManager::commit(ManagedTransaction& tx) {
    // Validate transaction is active
    auto validate_result = validate_transaction(tx);
    if (!validate_result) {
        return validate_result.error();
    }

    // Acquire exclusive lock for commit
    std::unique_lock lock(transactions_mtx_);

    // Check write-write conflicts
    auto ww_result = check_write_conflicts(tx);
    if (!ww_result) {
        tx.state = TransactionState::Aborted;
        cleanup_transaction(tx.id.id);
        return ww_result.error();
    }

    // Validate read set (OCC validation)
    auto rs_result = validate_read_set(tx);
    if (!rs_result) {
        tx.state = TransactionState::Aborted;
        cleanup_transaction(tx.id.id);
        return rs_result.error();
    }

    // Apply write set
    auto apply_result = apply_write_set(tx);
    if (!apply_result) {
        tx.state = TransactionState::Aborted;
        cleanup_transaction(tx.id.id);
        return apply_result.error();
    }

    // Mark as committed
    tx.state = TransactionState::Committed;
    cleanup_transaction(tx.id.id);

    return {};
}

TransactionManager::Result<void> TransactionManager::rollback(ManagedTransaction& tx) {
    // Validate transaction is active
    auto validate_result = validate_transaction(tx);
    if (!validate_result) {
        return validate_result.error();
    }

    // Mark as aborted
    tx.state = TransactionState::Aborted;

    // Clean up
    {
        std::lock_guard active_lock(active_mtx_);
        active_transactions_.erase(tx.id.id);
    }

    return {};
}

// ============================================================================
// Transactional Operations
// ============================================================================

TransactionManager::Result<std::vector<std::byte>> TransactionManager::get(ManagedTransaction& tx,
                                                                           Key key) {
    // Validate transaction
    auto validate_result = validate_transaction(tx);
    if (!validate_result) {
        return validate_result.error();
    }

    auto key_vec = std::vector<std::byte>(key.begin(), key.end());

    // Check write_set first (read-your-writes)
    // NOTE: Do NOT add to read_set when reading from write_set. The value comes
    // from our own pending write, not the backend. Adding it to read_set with
    // incorrect `existed` status causes validation failures on commit.
    {
        auto it = tx.write_set.find(key_vec);
        if (it != tx.write_set.end()) {
            if (it->second.value.has_value()) {
                return it->second.value.value();
            }
            // Key marked for deletion in write_set
            return StateBackendError::KeyNotFound;
        }
    }

    // For Snapshot isolation: if we already read this key, return cached value
    // This ensures repeatable reads within the transaction
    if (tx.isolation_level == TransactionIsolationLevel::Snapshot) {
        auto rs_it = tx.read_set.find(key_vec);
        if (rs_it != tx.read_set.end()) {
            if (rs_it->second.existed && rs_it->second.value_at_read.has_value()) {
                return rs_it->second.value_at_read.value();
            }
            if (!rs_it->second.existed) {
                return StateBackendError::KeyNotFound;
            }
        }
    }

    // Get current version for tracking
    std::uint64_t read_version = get_current_version();

    // Read from backend
    auto result = backend_->get(key);

    // Track in read_set (only if not already tracked)
    if (tx.read_set.find(key_vec) == tx.read_set.end()) {
        ReadSetEntry entry;
        entry.key = key_vec;
        entry.version_at_read = read_version;
        entry.existed = result.is_ok();
        if (result.is_ok()) {
            entry.value_at_read = result.value();
        }
        tx.read_set[key_vec] = std::move(entry);
    }

    return result;
}

TransactionManager::Result<void> TransactionManager::put(ManagedTransaction& tx, Key key,
                                                         Value value) {
    // Validate transaction
    auto validate_result = validate_transaction(tx);
    if (!validate_result) {
        return validate_result.error();
    }

    // Validate key/value sizes against backend config
    if (key.empty()) {
        return StateBackendError::InvalidKey;
    }
    if (key.size() > backend_->config().max_key_size) {
        return StateBackendError::KeyTooLarge;
    }
    if (value.size() > backend_->config().max_value_size) {
        return StateBackendError::ValueTooLarge;
    }

    auto key_vec = std::vector<std::byte>(key.begin(), key.end());
    auto value_vec = std::vector<std::byte>(value.begin(), value.end());

    // Buffer in write_set
    WriteSetEntry entry;
    entry.value = std::move(value_vec);
    tx.write_set[key_vec] = std::move(entry);

    return {};
}

TransactionManager::Result<void> TransactionManager::remove(ManagedTransaction& tx, Key key) {
    // Validate transaction
    auto validate_result = validate_transaction(tx);
    if (!validate_result) {
        return validate_result.error();
    }

    auto key_vec = std::vector<std::byte>(key.begin(), key.end());

    // Mark for deletion in write_set (nullopt = delete)
    WriteSetEntry entry;
    entry.value = std::nullopt;
    tx.write_set[key_vec] = std::move(entry);

    return {};
}

bool TransactionManager::exists(ManagedTransaction& tx, Key key) {
    // Validate transaction
    if (tx.state != TransactionState::Active) {
        return false;
    }

    auto key_vec = std::vector<std::byte>(key.begin(), key.end());

    // Check write_set first
    {
        auto it = tx.write_set.find(key_vec);
        if (it != tx.write_set.end()) {
            bool exists_result = it->second.value.has_value();
            // Track in read_set (track what's in storage, not write_set)
            if (tx.read_set.find(key_vec) == tx.read_set.end()) {
                std::uint64_t read_version = get_current_version();
                auto backend_result = backend_->get(key);
                ReadSetEntry entry;
                entry.key = key_vec;
                entry.version_at_read = read_version;
                entry.existed = backend_result.is_ok();
                if (backend_result.is_ok()) {
                    entry.value_at_read = backend_result.value();
                }
                tx.read_set[key_vec] = std::move(entry);
            }
            return exists_result;
        }
    }

    // For Snapshot isolation: if we already read this key, return cached existence
    // This ensures repeatable reads within the transaction
    if (tx.isolation_level == TransactionIsolationLevel::Snapshot) {
        auto rs_it = tx.read_set.find(key_vec);
        if (rs_it != tx.read_set.end()) {
            return rs_it->second.existed;
        }
    }

    // Get current version for tracking
    std::uint64_t read_version = get_current_version();

    // Check backend
    bool exists_result = backend_->exists(key);

    // Track in read_set
    if (tx.read_set.find(key_vec) == tx.read_set.end()) {
        ReadSetEntry entry;
        entry.key = key_vec;
        entry.version_at_read = read_version;
        entry.existed = exists_result;
        if (exists_result) {
            auto get_result = backend_->get(key);
            if (get_result.is_ok()) {
                entry.value_at_read = get_result.value();
            }
        }
        tx.read_set[key_vec] = std::move(entry);
    }

    return exists_result;
}

// ============================================================================
// Statistics & Configuration
// ============================================================================

std::size_t TransactionManager::active_transaction_count() const noexcept {
    std::lock_guard lock(active_mtx_);
    return active_transactions_.size();
}

const TransactionManagerConfig& TransactionManager::config() const noexcept {
    return config_;
}

StateBackend& TransactionManager::backend() noexcept {
    return *backend_;
}

const StateBackend& TransactionManager::backend() const noexcept {
    return *backend_;
}

// ============================================================================
// Internal Helpers
// ============================================================================

TransactionManager::Result<void>
TransactionManager::validate_transaction(const ManagedTransaction& tx) const {
    if (tx.state != TransactionState::Active) {
        return StateBackendError::InvalidTransaction;
    }

    // Verify transaction exists and matches generation
    std::shared_lock lock(transactions_mtx_);
    auto it = transactions_.find(tx.id.id);
    if (it == transactions_.end()) {
        return StateBackendError::InvalidTransaction;
    }
    if (it->second.id.generation != tx.id.generation) {
        return StateBackendError::InvalidTransaction;
    }

    return {};
}

std::uint64_t TransactionManager::get_current_version() const noexcept {
    return global_version_.load(std::memory_order_acquire);
}

TransactionManager::Result<void>
TransactionManager::check_write_conflicts(const ManagedTransaction& tx) {
    // For each key in write_set, check if it was modified since our transaction started
    // This implements first-committer-wins for write-write conflicts

    for (const auto& [key, write_entry] : tx.write_set) {
        // If this key is also in our read_set, we'll detect value changes via validate_read_set
        // But for pure writes, we need to check if the version advanced

        // Get the current state of the key
        bool key_exists_now = backend_->exists(key);

        // Check if the global version advanced (meaning another transaction committed)
        // If so, we need to check if this specific key was affected
        if (get_current_version() > tx.start_version) {
            // The version advanced. Check if this key was written to.

            // If the key exists now and we're in our read_set with a different value
            // -> that's handled by validate_read_set

            // If the key exists now but we're NOT in our read_set (blind write scenario):
            // We need to check if the key existed at tx start and if it changed

            if (tx.read_set.count(key) == 0) {
                // This is a blind write. Check if key exists and compare values.
                // If a concurrent transaction created or modified this key, we have a conflict.

                // Strategy: Check the backend for current value. If the key didn't exist
                // when we started and now does, that's a conflict. If it existed and changed,
                // that's also a conflict.

                // Since we didn't read the key, we don't know what it was at tx.start_version.
                // We need to determine if another transaction wrote to this key.

                // Simple heuristic: if the key exists now and the version > start_version,
                // someone else may have written it. Since this is a blind write scenario
                // (no read), we treat any existing key modification as a potential conflict.

                // For correctness: check if key exists and was potentially modified
                // by another transaction after our snapshot.
                if (key_exists_now) {
                    // Key exists - another transaction may have written to it
                    // We return a conflict to be safe (first-committer-wins)
                    return StateBackendError::TransactionConflict;
                }
                // Key doesn't exist - safe to create it
            }
        }
    }

    return {};
}

TransactionManager::Result<void>
TransactionManager::validate_read_set(const ManagedTransaction& tx) {
    // For each key in read_set, verify the value hasn't changed
    for (const auto& [key, read_entry] : tx.read_set) {
        bool currently_exists = backend_->exists(key);

        // Check existence change (phantom reads)
        if (read_entry.existed != currently_exists) {
            return StateBackendError::ReadSetValidationFailed;
        }

        if (currently_exists && read_entry.existed) {
            // Key still exists - check if value changed
            auto current_result = backend_->get(key);
            if (!current_result.is_ok()) {
                // Key was deleted between exists() and get() - conflict
                return StateBackendError::ReadSetValidationFailed;
            }

            // Compare values
            if (read_entry.value_at_read.has_value()) {
                const auto& old_value = read_entry.value_at_read.value();
                const auto& new_value = current_result.value();
                if (old_value.size() != new_value.size() ||
                    !std::equal(old_value.begin(), old_value.end(), new_value.begin())) {
                    // Value changed - conflict
                    return StateBackendError::ReadSetValidationFailed;
                }
            }
        }
    }

    return {};
}

TransactionManager::Result<void> TransactionManager::apply_write_set(ManagedTransaction& tx) {
    if (tx.write_set.empty()) {
        // Nothing to apply
        global_version_.fetch_add(1, std::memory_order_acq_rel);
        return {};
    }

    // Build batch operations
    std::vector<BatchOp> ops;
    ops.reserve(tx.write_set.size());

    // We need to keep the key/value vectors alive during batch
    std::vector<std::vector<std::byte>> key_storage;
    std::vector<std::vector<std::byte>> value_storage;
    key_storage.reserve(tx.write_set.size());
    value_storage.reserve(tx.write_set.size());

    for (auto& [key, write_entry] : tx.write_set) {
        key_storage.push_back(key);

        if (write_entry.value.has_value()) {
            value_storage.push_back(std::move(*write_entry.value));
            BatchOp op;
            op.type = BatchOpType::Put;
            op.key = key_storage.back();
            op.value = value_storage.back();
            ops.push_back(op);
        } else {
            // Delete operation
            BatchOp op;
            op.type = BatchOpType::Remove;
            op.key = key_storage.back();
            op.value = {};
            ops.push_back(op);
        }
    }

    // Apply via backend's batch operation (atomic)
    auto batch_result = backend_->batch(ops);
    if (!batch_result) {
        // Check if it's a conflict-related error
        if (batch_result.error() == StateBackendError::KeyNotFound) {
            // This happens when trying to delete a non-existent key
            // For remove operations on keys that don't exist, this is expected
            // if the key was deleted by another transaction

            // Retry without the problematic removes
            std::vector<BatchOp> retry_ops;
            for (const auto& op : ops) {
                if (op.type == BatchOpType::Put) {
                    retry_ops.push_back(op);
                } else {
                    // Only include remove if key exists
                    if (backend_->exists(op.key)) {
                        retry_ops.push_back(op);
                    }
                }
            }

            if (!retry_ops.empty()) {
                auto retry_result = backend_->batch(retry_ops);
                if (!retry_result) {
                    return StateBackendError::TransactionConflict;
                }
            }
        } else {
            return StateBackendError::TransactionConflict;
        }
    }

    // Increment global version
    global_version_.fetch_add(1, std::memory_order_acq_rel);

    return {};
}

void TransactionManager::deadlock_detector_loop() {
    while (!shutdown_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(config_.deadlock_check_interval);
        if (!shutdown_.load(std::memory_order_acquire)) {
            check_deadlocks();
        }
    }
}

void TransactionManager::check_deadlocks() {
    auto now = std::chrono::steady_clock::now();

    std::unique_lock lock(transactions_mtx_);

    // Find timed-out transactions
    for (auto& [id, tx] : transactions_) {
        if (tx.state != TransactionState::Active) {
            continue;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - tx.start_time);
        if (elapsed > tx.timeout) {
            // Transaction has timed out - abort it
            tx.state = TransactionState::Aborted;

            // Remove from active set
            {
                std::lock_guard active_lock(active_mtx_);
                active_transactions_.erase(id);
            }
        }
    }
}

TxId TransactionManager::generate_tx_id() {
    TxId id;
    id.id = next_tx_id_.fetch_add(1, std::memory_order_relaxed);
    id.generation = tx_generation_.load(std::memory_order_relaxed);
    return id;
}

void TransactionManager::cleanup_transaction(std::uint64_t tx_id) {
    // Remove from active set (already holding transactions_mtx_)
    std::lock_guard active_lock(active_mtx_);
    active_transactions_.erase(tx_id);
}

}  // namespace dotvm::core::state
