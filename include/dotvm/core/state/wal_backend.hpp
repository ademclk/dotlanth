#pragma once

/// @file wal_backend.hpp
/// @brief STATE-007 WAL-enabled StateBackend decorator
///
/// WalBackend wraps any StateBackend with Write-Ahead Logging for durability.

#include <memory>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/state_backend.hpp"
#include "dotvm/core/state/write_ahead_log.hpp"

namespace dotvm::core::state {

// ============================================================================
// WalBackend Configuration
// ============================================================================

/// @brief Configuration for WalBackend
struct WalBackendConfig {
    WalConfig wal_config;  ///< WAL configuration

    /// @brief Create default configuration
    [[nodiscard]] static WalBackendConfig defaults() noexcept {
        return WalBackendConfig{.wal_config = WalConfig::defaults()};
    }

    /// @brief Validate configuration
    [[nodiscard]] bool is_valid() const noexcept { return wal_config.is_valid(); }
};

// ============================================================================
// WalBackend
// ============================================================================

/// @brief StateBackend decorator that adds Write-Ahead Logging
///
/// WalBackend wraps any StateBackend with WAL for durability:
/// - All mutations (put, remove) are first logged to the WAL
/// - Transaction commits are logged and synced
/// - Recovery replays the WAL to restore state
///
/// Thread Safety: Thread-safe (inherits from inner backend + WAL locking).
class WalBackend final : public StateBackend {
public:
    /// @brief Create a new WalBackend with a fresh WAL
    ///
    /// @param inner The underlying StateBackend to wrap
    /// @param config WAL configuration
    /// @return The created WalBackend, or error
    [[nodiscard]] static ::dotvm::core::Result<std::unique_ptr<WalBackend>, StateBackendError>
    create(std::unique_ptr<StateBackend> inner, WalBackendConfig config);

    /// @brief Open an existing WalBackend (for recovery)
    ///
    /// @param inner Fresh StateBackend (empty) to replay into
    /// @param wal_dir Path to existing WAL directory
    /// @return The opened WalBackend, or error
    [[nodiscard]] static ::dotvm::core::Result<std::unique_ptr<WalBackend>, StateBackendError>
    open(std::unique_ptr<StateBackend> inner, const std::filesystem::path& wal_dir);

    ~WalBackend() override = default;

    // ========================================================================
    // CRUD Operations (with WAL)
    // ========================================================================

    [[nodiscard]] Result<std::vector<std::byte>> get(Key key) const override;
    [[nodiscard]] Result<void> put(Key key, Value value) override;
    [[nodiscard]] Result<void> remove(Key key) override;
    [[nodiscard]] bool exists(Key key) const noexcept override;

    // ========================================================================
    // Iteration
    // ========================================================================

    [[nodiscard]] Result<void> iterate(Key prefix, const IterateCallback& callback) const override;

    // ========================================================================
    // Batch Operations
    // ========================================================================

    [[nodiscard]] Result<void> batch(std::span<const BatchOp> ops) override;

    // ========================================================================
    // Transactions (with WAL)
    // ========================================================================

    [[nodiscard]] Result<TxHandle> begin_transaction() override;
    [[nodiscard]] Result<void> commit(TxHandle tx) override;
    [[nodiscard]] Result<void> rollback(TxHandle tx) override;

    // ========================================================================
    // Statistics & Configuration
    // ========================================================================

    [[nodiscard]] std::size_t key_count() const noexcept override;
    [[nodiscard]] std::size_t storage_bytes() const noexcept override;
    [[nodiscard]] const StateBackendConfig& config() const noexcept override;
    [[nodiscard]] bool supports_transactions() const noexcept override;
    void clear() noexcept override;

    // ========================================================================
    // WAL-Specific Operations
    // ========================================================================

    /// @brief Sync the WAL to disk
    [[nodiscard]] ::dotvm::core::Result<void, WalError> sync_wal();

    /// @brief Recover state from WAL
    ///
    /// Replays all records from the WAL into the inner backend.
    [[nodiscard]] ::dotvm::core::Result<void, WalError> recover();

    /// @brief Create a checkpoint
    [[nodiscard]] ::dotvm::core::Result<CheckpointInfo, WalError> checkpoint();

    /// @brief Get the current WAL LSN
    [[nodiscard]] LSN current_lsn() const noexcept;

private:
    WalBackend(std::unique_ptr<StateBackend> inner, std::unique_ptr<WriteAheadLog> wal);

    std::unique_ptr<StateBackend> inner_;
    std::unique_ptr<WriteAheadLog> wal_;
    std::optional<TxId> active_tx_;  ///< Current transaction (for WAL logging)
};

}  // namespace dotvm::core::state
