/// @file wal_backend.cpp
/// @brief STATE-007 WalBackend decorator implementation
///
/// Implements WAL-enabled state backend by decorating an inner backend.

#include "dotvm/core/state/wal_backend.hpp"

namespace dotvm::core::state {

// ============================================================================
// Construction
// ============================================================================

WalBackend::WalBackend(std::unique_ptr<StateBackend> inner, std::unique_ptr<WriteAheadLog> wal)
    : inner_{std::move(inner)}, wal_{std::move(wal)} {}

// ============================================================================
// Factory Methods
// ============================================================================

::dotvm::core::Result<std::unique_ptr<WalBackend>, StateBackendError>
WalBackend::create(std::unique_ptr<StateBackend> inner, WalBackendConfig config) {
    if (!inner) {
        return StateBackendError::InvalidConfig;
    }

    if (!config.is_valid()) {
        return StateBackendError::InvalidConfig;
    }

    auto wal_result = WriteAheadLog::create(config.wal_config);
    if (wal_result.is_err()) {
        return StateBackendError::InvalidConfig;
    }

    return std::unique_ptr<WalBackend>(
        new WalBackend(std::move(inner), std::move(wal_result.value())));
}

::dotvm::core::Result<std::unique_ptr<WalBackend>, StateBackendError>
WalBackend::open(std::unique_ptr<StateBackend> inner, const std::filesystem::path& wal_dir) {
    if (!inner) {
        return StateBackendError::InvalidConfig;
    }

    auto wal_result = WriteAheadLog::open(wal_dir);
    if (wal_result.is_err()) {
        return StateBackendError::InvalidConfig;
    }

    return std::unique_ptr<WalBackend>(
        new WalBackend(std::move(inner), std::move(wal_result.value())));
}

// ============================================================================
// CRUD Operations
// ============================================================================

WalBackend::Result<std::vector<std::byte>> WalBackend::get(Key key) const {
    return inner_->get(key);
}

WalBackend::Result<void> WalBackend::put(Key key, Value value) {
    // Determine transaction ID
    TxId tx_id = active_tx_.value_or(TxId{0, 0});

    // Log to WAL first
    auto wal_result = wal_->append(LogRecordType::Put, std::span<const std::byte>(key),
                                   std::span<const std::byte>(value), tx_id);
    if (wal_result.is_err()) {
        // Convert WAL error to backend error
        return StateBackendError::StorageFull;  // Generic error
    }

    // Then apply to inner backend
    return inner_->put(key, value);
}

WalBackend::Result<void> WalBackend::remove(Key key) {
    TxId tx_id = active_tx_.value_or(TxId{0, 0});

    // Log to WAL first
    auto wal_result =
        wal_->append(LogRecordType::Delete, std::span<const std::byte>(key), {}, tx_id);
    if (wal_result.is_err()) {
        return StateBackendError::StorageFull;
    }

    return inner_->remove(key);
}

bool WalBackend::exists(Key key) const noexcept {
    return inner_->exists(key);
}

// ============================================================================
// Iteration
// ============================================================================

WalBackend::Result<void> WalBackend::iterate(Key prefix, const IterateCallback& callback) const {
    return inner_->iterate(prefix, callback);
}

// ============================================================================
// Batch Operations
// ============================================================================

WalBackend::Result<void> WalBackend::batch(std::span<const BatchOp> ops) {
    TxId tx_id = active_tx_.value_or(TxId{0, 0});

    // Log each operation to WAL
    for (const auto& op : ops) {
        if (op.type == BatchOpType::Put) {
            auto wal_result = wal_->append(LogRecordType::Put, op.key, op.value, tx_id);
            if (wal_result.is_err()) {
                return StateBackendError::StorageFull;
            }
        } else {
            auto wal_result = wal_->append(LogRecordType::Delete, op.key, {}, tx_id);
            if (wal_result.is_err()) {
                return StateBackendError::StorageFull;
            }
        }
    }

    return inner_->batch(ops);
}

// ============================================================================
// Transactions
// ============================================================================

WalBackend::Result<TxHandle> WalBackend::begin_transaction() {
    auto result = inner_->begin_transaction();
    if (result.is_err()) {
        return result.error();
    }

    TxId tx_id = result.value().id();
    active_tx_ = tx_id;

    // Log transaction begin
    auto wal_result = wal_->append(LogRecordType::TxBegin, {}, {}, tx_id);
    if (wal_result.is_err()) {
        // Rollback the inner transaction
        (void)inner_->rollback(std::move(result.value()));
        active_tx_.reset();
        return StateBackendError::StorageFull;
    }

    return result;
}

WalBackend::Result<void> WalBackend::commit(TxHandle tx) {
    if (!tx.is_valid()) {
        return StateBackendError::InvalidTransaction;
    }

    TxId tx_id = tx.id();

    // Log commit
    auto wal_result = wal_->append(LogRecordType::TxCommit, {}, {}, tx_id);
    if (wal_result.is_err()) {
        return StateBackendError::StorageFull;
    }

    // Sync WAL (EveryCommit policy ensures durability)
    auto sync_result = wal_->sync();
    if (sync_result.is_err()) {
        return StateBackendError::StorageFull;
    }

    active_tx_.reset();
    return inner_->commit(std::move(tx));
}

WalBackend::Result<void> WalBackend::rollback(TxHandle tx) {
    if (!tx.is_valid()) {
        return StateBackendError::InvalidTransaction;
    }

    TxId tx_id = tx.id();

    // Log abort
    auto wal_result = wal_->append(LogRecordType::TxAbort, {}, {}, tx_id);
    if (wal_result.is_err()) {
        // Still try to rollback inner transaction
    }

    active_tx_.reset();
    return inner_->rollback(std::move(tx));
}

// ============================================================================
// Statistics & Configuration
// ============================================================================

std::size_t WalBackend::key_count() const noexcept {
    return inner_->key_count();
}

std::size_t WalBackend::storage_bytes() const noexcept {
    return inner_->storage_bytes();
}

const StateBackendConfig& WalBackend::config() const noexcept {
    return inner_->config();
}

bool WalBackend::supports_transactions() const noexcept {
    return inner_->supports_transactions();
}

void WalBackend::clear() noexcept {
    inner_->clear();
    // Note: Does not clear WAL - call checkpoint and truncate for that
}

// ============================================================================
// WAL-Specific Operations
// ============================================================================

::dotvm::core::Result<void, WalError> WalBackend::sync_wal() {
    return wal_->sync();
}

::dotvm::core::Result<void, WalError> WalBackend::recover() {
    auto records_result = wal_->recover();
    if (records_result.is_err()) {
        return records_result.error();
    }

    auto& records = records_result.value();

    // Track active transactions for replay
    std::unordered_map<std::uint64_t, bool> tx_committed;  // tx_id -> committed?

    // First pass: identify committed transactions
    for (const auto& record : records) {
        if (record.type == LogRecordType::TxCommit) {
            tx_committed[record.tx_id.id] = true;
        } else if (record.type == LogRecordType::TxAbort) {
            tx_committed[record.tx_id.id] = false;
        }
    }

    // Second pass: replay committed operations
    for (const auto& record : records) {
        // Skip transaction markers
        if (record.type == LogRecordType::TxBegin || record.type == LogRecordType::TxCommit ||
            record.type == LogRecordType::TxAbort || record.type == LogRecordType::Checkpoint) {
            continue;
        }

        // Skip if transaction was aborted (if tx_id != 0)
        if (record.tx_id.id != 0) {
            auto it = tx_committed.find(record.tx_id.id);
            if (it == tx_committed.end() || !it->second) {
                continue;  // Transaction not committed or aborted
            }
        }

        // Replay the operation
        if (record.type == LogRecordType::Put) {
            auto result = inner_->put(record.key, record.value);
            if (result.is_err()) {
                return WalError::RecoveryFailed;
            }
        } else if (record.type == LogRecordType::Delete) {
            auto result = inner_->remove(record.key);
            // Ignore KeyNotFound during recovery
            if (result.is_err() && result.error() != StateBackendError::KeyNotFound) {
                return WalError::RecoveryFailed;
            }
        }
    }

    return {};
}

::dotvm::core::Result<CheckpointInfo, WalError> WalBackend::checkpoint() {
    return wal_->checkpoint();
}

LSN WalBackend::current_lsn() const noexcept {
    return wal_->current_lsn();
}

}  // namespace dotvm::core::state
