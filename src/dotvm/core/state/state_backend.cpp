/// @file state_backend.cpp
/// @brief STATE-001 StateBackend factory and TxHandle implementation

#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {

// Forward declaration of InMemoryBackend
class InMemoryBackend;

// ============================================================================
// TxHandle Implementation
// ============================================================================

TxHandle::~TxHandle() noexcept {
    if (backend_ != nullptr) {
        // Auto-rollback uncommitted transaction
        // We can't do much if rollback fails in destructor
        (void)backend_->rollback(TxHandle{backend_, tx_id_});
        backend_ = nullptr;
    }
}

TxHandle::TxHandle(TxHandle&& other) noexcept : backend_{other.backend_}, tx_id_{other.tx_id_} {
    other.backend_ = nullptr;
    other.tx_id_ = {};
}

TxHandle& TxHandle::operator=(TxHandle&& other) noexcept {
    if (this != &other) {
        // Rollback current transaction if active
        if (backend_ != nullptr) {
            (void)backend_->rollback(TxHandle{backend_, tx_id_});
        }

        backend_ = other.backend_;
        tx_id_ = other.tx_id_;
        other.backend_ = nullptr;
        other.tx_id_ = {};
    }
    return *this;
}

}  // namespace dotvm::core::state
