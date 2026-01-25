#pragma once

/// @file postgres_backend.hpp
/// @brief STATE-006 PostgreSQL StateBackend implementation
///
/// Provides a PostgreSQL-backed implementation of StateBackend using libpq
/// with connection pooling, prepared statements, and PostgreSQL-native transactions.

#include <chrono>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {

// ============================================================================
// PostgreSQL Backend Configuration
// ============================================================================

/// @brief Configuration for PostgreSQL state backend
///
/// Extends StateBackendConfig with PostgreSQL-specific settings including
/// connection parameters, SSL options, and connection pool configuration.
struct PostgresBackendConfig {
    // ========================================================================
    // Connection Settings
    // ========================================================================

    std::string host{"localhost"};  ///< PostgreSQL server hostname
    std::uint16_t port{5432};       ///< PostgreSQL server port
    std::string dbname{"dotvm"};    ///< Database name
    std::string user{"dotvm"};      ///< Database user
    std::string password{};         ///< Database password (empty = use .pgpass or trust)

    // ========================================================================
    // SSL Settings
    // ========================================================================

    /// @brief SSL connection mode
    enum class SslMode : std::uint8_t {
        Disable,     ///< Disable SSL entirely
        Allow,       ///< Try non-SSL, then SSL if that fails
        Prefer,      ///< Try SSL, then non-SSL if that fails (default)
        Require,     ///< Require SSL (no verification)
        VerifyCA,    ///< Require SSL and verify server certificate CA
        VerifyFull,  ///< Require SSL, verify CA, and verify hostname
    };

    SslMode ssl_mode{SslMode::Prefer};  ///< SSL connection mode

    // ========================================================================
    // Connection Pool Settings
    // ========================================================================

    std::size_t pool_min_connections{2};                 ///< Minimum connections to keep open
    std::size_t pool_max_connections{10};                ///< Maximum connections allowed
    std::chrono::milliseconds connection_timeout{5000};  ///< Connection acquire timeout

    // ========================================================================
    // Inherited StateBackend Settings
    // ========================================================================

    std::size_t max_key_size{1024};           ///< Maximum key size (default 1KB)
    std::size_t max_value_size{1024 * 1024};  ///< Maximum value size (default 1MB)
    TransactionIsolationLevel isolation_level{TransactionIsolationLevel::Snapshot};
    bool enable_transactions{true};  ///< Enable transaction support

    // ========================================================================
    // Batch Tuning
    // ========================================================================

    bool use_copy_for_batch{true};  ///< Use COPY for batch inserts (faster for >10 items)

    // ========================================================================
    // Methods
    // ========================================================================

    /// @brief Build a libpq connection string from config
    /// @return Connection string suitable for PQconnectdb()
    [[nodiscard]] std::string build_connection_string() const;

    /// @brief Create default configuration
    [[nodiscard]] static constexpr PostgresBackendConfig defaults() noexcept {
        return PostgresBackendConfig{};
    }

    /// @brief Validate configuration
    [[nodiscard]] bool is_valid() const noexcept;
};

// ============================================================================
// PostgreSQL Backend Implementation
// ============================================================================

#ifdef DOTVM_POSTGRESQL_ENABLED

// Forward declarations
class ConnectionPool;

/// @brief PostgreSQL implementation of StateBackend
///
/// Uses libpq for database access with:
/// - Connection pooling for concurrent access
/// - Prepared statements for efficient queries
/// - PostgreSQL transactions with isolation level mapping
/// - COPY protocol for efficient batch operations
///
/// Thread Safety: Thread-safe via connection pool and internal locking.
class PostgresBackend final : public StateBackend {
public:
    /// @brief Factory method to create a PostgresBackend
    ///
    /// @param config Backend configuration
    /// @return The backend, or InvalidConfig if config is invalid
    [[nodiscard]] static Result<std::unique_ptr<PostgresBackend>>
    create(PostgresBackendConfig config = PostgresBackendConfig::defaults());

    ~PostgresBackend() override;

    // Non-copyable, non-movable
    PostgresBackend(const PostgresBackend&) = delete;
    PostgresBackend& operator=(const PostgresBackend&) = delete;
    PostgresBackend(PostgresBackend&&) = delete;
    PostgresBackend& operator=(PostgresBackend&&) = delete;

    // ========================================================================
    // CRUD Operations
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
    // Transactions
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

private:
    friend class TxHandle;  // Allow TxHandle to call release
    explicit PostgresBackend(PostgresBackendConfig config);

    PostgresBackendConfig pg_config_;
    StateBackendConfig base_config_;  // Holds inherited config for config() return
    std::unique_ptr<ConnectionPool> pool_;

    // Transaction management
    struct PgTransaction;
    mutable std::unordered_map<std::uint64_t, PgTransaction> transactions_;
    mutable std::shared_mutex tx_mtx_;
};

#endif  // DOTVM_POSTGRESQL_ENABLED

}  // namespace dotvm::core::state
