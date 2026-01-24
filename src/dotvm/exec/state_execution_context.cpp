/// @file state_execution_context.cpp
/// @brief STATE-004 StateExecutionContext implementation

#include "dotvm/exec/state_execution_context.hpp"

#include <algorithm>

namespace dotvm::exec {

// ============================================================================
// Lifecycle
// ============================================================================

StateExecutionContext::~StateExecutionContext() noexcept {
    // Auto-rollback all active transactions on destruction
    rollback_all();
}

void StateExecutionContext::enable(core::state::TransactionManager* tx_mgr) noexcept {
    // If already enabled with different manager, rollback first
    if (tx_mgr_ != nullptr && tx_mgr_ != tx_mgr) {
        rollback_all();
    }
    tx_mgr_ = tx_mgr;
}

void StateExecutionContext::disable() noexcept {
    rollback_all();
    tx_mgr_ = nullptr;
}

// ============================================================================
// Transaction Lifecycle
// ============================================================================

std::uint64_t StateExecutionContext::begin_transaction(std::uint8_t isolation) noexcept {
    if (!tx_mgr_) {
        return 0;  // Not enabled
    }

    // Map isolation byte to enum
    auto iso_level = isolation == 1 ? core::state::TransactionIsolationLevel::ReadCommitted
                                    : core::state::TransactionIsolationLevel::Snapshot;

    // Begin transaction in the manager
    auto result = tx_mgr_->begin(iso_level);
    if (!result) {
        return 0;  // Failed to begin (too many transactions, etc.)
    }

    // Generate a unique handle for this VM execution
    std::uint64_t handle = generate_handle();

    // Store the mapping
    active_transactions_[handle] = result.value();

    return handle;
}

StateExecError StateExecutionContext::commit(std::uint64_t handle) noexcept {
    if (!tx_mgr_) {
        return StateExecError::NotEnabled;
    }

    auto* tx = get_transaction(handle);
    if (!tx) {
        return StateExecError::InvalidHandle;
    }

    // Attempt commit
    auto result = tx_mgr_->commit(*tx);
    if (!result) {
        // Check error type
        auto err = result.error();
        if (err == core::state::StateBackendError::TransactionConflict ||
            err == core::state::StateBackendError::ReadSetValidationFailed) {
            // Transaction remains active for retry
            return StateExecError::TransactionConflict;
        }
        // Other errors - transaction may be in undefined state
        remove_entry(handle);
        return StateExecError::BackendError;
    }

    // Success - remove from tracking
    remove_entry(handle);
    return StateExecError::Success;
}

StateExecError StateExecutionContext::rollback(std::uint64_t handle) noexcept {
    if (!tx_mgr_) {
        return StateExecError::NotEnabled;
    }

    auto* tx = get_transaction(handle);
    if (!tx) {
        return StateExecError::InvalidHandle;
    }

    // Perform rollback
    auto result = tx_mgr_->rollback(*tx);

    // Remove from tracking regardless of result
    remove_entry(handle);

    if (!result) {
        return StateExecError::BackendError;
    }

    return StateExecError::Success;
}

void StateExecutionContext::rollback_all() noexcept {
    if (!tx_mgr_) {
        active_transactions_.clear();
        return;
    }

    // Collect handles to avoid iterator invalidation
    std::vector<std::uint64_t> handles;
    handles.reserve(active_transactions_.size());
    for (const auto& [handle, tx] : active_transactions_) {
        handles.push_back(handle);
    }

    // Rollback each transaction
    for (auto handle : handles) {
        auto* tx = get_transaction(handle);
        if (tx && tx->state == core::state::TransactionState::Active) {
            (void)tx_mgr_->rollback(*tx);
        }
    }

    active_transactions_.clear();
}

// ============================================================================
// State Operations
// ============================================================================

StateExecError StateExecutionContext::get(std::uint64_t handle, Key key,
                                          std::vector<std::byte>& out_value) noexcept {
    if (!tx_mgr_) {
        return StateExecError::NotEnabled;
    }

    if (handle == 0) {
        // Direct backend access (no transaction)
        auto result = tx_mgr_->backend().get(key);
        if (!result) {
            if (result.error() == core::state::StateBackendError::KeyNotFound) {
                return StateExecError::KeyNotFound;
            }
            return StateExecError::BackendError;
        }
        out_value = std::move(result).value();
        return StateExecError::Success;
    }

    // Transactional access
    auto* tx = get_transaction(handle);
    if (!tx) {
        return StateExecError::InvalidHandle;
    }

    auto result = tx_mgr_->get(*tx, key);
    if (!result) {
        if (result.error() == core::state::StateBackendError::KeyNotFound) {
            return StateExecError::KeyNotFound;
        }
        return StateExecError::BackendError;
    }
    out_value = std::move(result).value();
    return StateExecError::Success;
}

StateExecError StateExecutionContext::put(std::uint64_t handle, Key key, Value value) noexcept {
    if (!tx_mgr_) {
        return StateExecError::NotEnabled;
    }

    if (handle == 0) {
        // Direct backend access (no transaction)
        auto result = tx_mgr_->backend().put(key, value);
        if (!result) {
            return StateExecError::BackendError;
        }
        return StateExecError::Success;
    }

    // Transactional access
    auto* tx = get_transaction(handle);
    if (!tx) {
        return StateExecError::InvalidHandle;
    }

    auto result = tx_mgr_->put(*tx, key, value);
    if (!result) {
        return StateExecError::BackendError;
    }
    return StateExecError::Success;
}

StateExecError StateExecutionContext::remove(std::uint64_t handle, Key key) noexcept {
    if (!tx_mgr_) {
        return StateExecError::NotEnabled;
    }

    if (handle == 0) {
        // Direct backend access (no transaction)
        auto result = tx_mgr_->backend().remove(key);
        if (!result) {
            if (result.error() == core::state::StateBackendError::KeyNotFound) {
                return StateExecError::KeyNotFound;
            }
            return StateExecError::BackendError;
        }
        return StateExecError::Success;
    }

    // Transactional access
    auto* tx = get_transaction(handle);
    if (!tx) {
        return StateExecError::InvalidHandle;
    }

    auto result = tx_mgr_->remove(*tx, key);
    if (!result) {
        if (result.error() == core::state::StateBackendError::KeyNotFound) {
            return StateExecError::KeyNotFound;
        }
        return StateExecError::BackendError;
    }
    return StateExecError::Success;
}

StateExecError StateExecutionContext::exists(std::uint64_t handle, Key key,
                                             bool& out_exists) noexcept {
    if (!tx_mgr_) {
        return StateExecError::NotEnabled;
    }

    if (handle == 0) {
        // Direct backend access (no transaction)
        out_exists = tx_mgr_->backend().exists(key);
        return StateExecError::Success;
    }

    // Transactional access
    auto* tx = get_transaction(handle);
    if (!tx) {
        return StateExecError::InvalidHandle;
    }

    out_exists = tx_mgr_->exists(*tx, key);
    return StateExecError::Success;
}

// ============================================================================
// Statistics
// ============================================================================

std::size_t StateExecutionContext::active_transaction_count() const noexcept {
    return active_transactions_.size();
}

bool StateExecutionContext::is_valid_handle(std::uint64_t handle) const noexcept {
    if (handle == 0) {
        return true;  // 0 is always valid (means no transaction)
    }
    return active_transactions_.find(handle) != active_transactions_.end();
}

// ============================================================================
// Private Helpers
// ============================================================================

std::uint64_t StateExecutionContext::generate_handle() noexcept {
    // Monotonically increasing, never reused within a session
    return next_handle_.fetch_add(1, std::memory_order_relaxed);
}

core::state::ManagedTransaction* StateExecutionContext::get_transaction(
    std::uint64_t handle) noexcept {
    auto it = active_transactions_.find(handle);
    if (it == active_transactions_.end()) {
        return nullptr;
    }
    return it->second;
}

void StateExecutionContext::remove_entry(std::uint64_t handle) noexcept {
    active_transactions_.erase(handle);
}

}  // namespace dotvm::exec
