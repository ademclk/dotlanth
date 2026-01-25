/// @file postgres_backend.cpp
/// @brief STATE-006 PostgreSQL StateBackend implementation

#include "dotvm/core/state/postgres_backend.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#ifdef DOTVM_POSTGRESQL_ENABLED
    #include <libpq-fe.h>
#endif

namespace dotvm::core::state {

// ============================================================================
// PostgresBackendConfig Implementation
// ============================================================================

std::string PostgresBackendConfig::build_connection_string() const {
    std::string result;

    // Host
    result += "host=";
    result += host;

    // Port
    result += " port=";
    result += std::to_string(port);

    // Database name
    result += " dbname=";
    result += dbname;

    // User
    result += " user=";
    result += user;

    // Password (only if non-empty)
    if (!password.empty()) {
        result += " password=";
        // Escape single quotes by doubling them (libpq convention)
        for (char c : password) {
            if (c == '\'') {
                result += "''";
            } else if (c == '\\') {
                result += "\\\\";
            } else {
                result += c;
            }
        }
    }

    // SSL mode
    result += " sslmode=";
    switch (ssl_mode) {
        case SslMode::Disable:
            result += "disable";
            break;
        case SslMode::Allow:
            result += "allow";
            break;
        case SslMode::Prefer:
            result += "prefer";
            break;
        case SslMode::Require:
            result += "require";
            break;
        case SslMode::VerifyCA:
            result += "verify-ca";
            break;
        case SslMode::VerifyFull:
            result += "verify-full";
            break;
    }

    return result;
}

bool PostgresBackendConfig::is_valid() const noexcept {
    // Connection settings validation
    if (host.empty()) {
        return false;
    }
    if (port == 0) {
        return false;
    }
    if (dbname.empty()) {
        return false;
    }
    if (user.empty()) {
        return false;
    }

    // Pool settings validation
    if (pool_max_connections == 0) {
        return false;
    }
    if (pool_min_connections > pool_max_connections) {
        return false;
    }

    // Timeout validation
    if (connection_timeout.count() == 0) {
        return false;
    }

    // Inherited StateBackendConfig validation
    if (max_key_size == 0) {
        return false;
    }
    if (max_value_size == 0) {
        return false;
    }

    return true;
}

// ============================================================================
// PostgresBackend Implementation (only when PostgreSQL enabled)
// ============================================================================

#ifdef DOTVM_POSTGRESQL_ENABLED

namespace {

// ============================================================================
// RAII Wrappers
// ============================================================================

/// @brief RAII wrapper for PGresult
class PgResultGuard {
public:
    explicit PgResultGuard(PGresult* result) noexcept : result_{result} {}
    ~PgResultGuard() {
        if (result_) {
            PQclear(result_);
        }
    }

    PgResultGuard(const PgResultGuard&) = delete;
    PgResultGuard& operator=(const PgResultGuard&) = delete;
    PgResultGuard(PgResultGuard&& other) noexcept : result_{other.result_} {
        other.result_ = nullptr;
    }
    PgResultGuard& operator=(PgResultGuard&& other) noexcept {
        if (this != &other) {
            if (result_)
                PQclear(result_);
            result_ = other.result_;
            other.result_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] PGresult* get() const noexcept { return result_; }
    [[nodiscard]] PGresult* release() noexcept {
        auto* r = result_;
        result_ = nullptr;
        return r;
    }

private:
    PGresult* result_;
};

/// @brief Map PostgreSQL error to StateBackendError
[[nodiscard]] StateBackendError map_pq_error(const PGresult* result) noexcept {
    if (!result) {
        return StateBackendError::BackendClosed;
    }

    const char* sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);
    if (!sqlstate) {
        return StateBackendError::BackendClosed;
    }

    // SQLSTATE codes: https://www.postgresql.org/docs/current/errcodes-appendix.html
    std::string_view state{sqlstate, 5};

    // Serialization failures
    if (state == "40001") {  // serialization_failure
        return StateBackendError::TransactionConflict;
    }
    if (state == "40P01") {  // deadlock_detected
        return StateBackendError::DeadlockDetected;
    }

    // Insufficient resources
    if (state.starts_with("53")) {
        return StateBackendError::StorageFull;
    }

    // Connection exceptions
    if (state.starts_with("08")) {
        return StateBackendError::BackendClosed;
    }

    // Unique violation (for our use case, treat as conflict)
    if (state == "23505") {
        return StateBackendError::TransactionConflict;
    }

    return StateBackendError::BackendClosed;
}

/// @brief Prepared statement names
constexpr const char* STMT_GET = "dotvm_get";
constexpr const char* STMT_PUT = "dotvm_put";
constexpr const char* STMT_DELETE = "dotvm_delete";
constexpr const char* STMT_EXISTS = "dotvm_exists";
constexpr const char* STMT_ITERATE = "dotvm_iterate";
constexpr const char* STMT_ITERATE_ALL = "dotvm_iterate_all";
constexpr const char* STMT_COUNT = "dotvm_count";
constexpr const char* STMT_SIZE = "dotvm_size";

/// @brief Schema initialization SQL
constexpr const char* SCHEMA_SQL = R"(
CREATE TABLE IF NOT EXISTS kv_store (
    key BYTEA PRIMARY KEY,
    value BYTEA NOT NULL,
    version BIGINT NOT NULL DEFAULT 1,
    deleted BOOLEAN NOT NULL DEFAULT FALSE
);

CREATE SEQUENCE IF NOT EXISTS kv_version_seq START 1;

CREATE INDEX IF NOT EXISTS idx_kv_store_key_prefix ON kv_store USING btree (key);
)";

/// @brief Prepare all statements on a connection
[[nodiscard]] bool prepare_statements(PGconn* conn) noexcept {
    // GET
    auto* r1 =
        PQprepare(conn, STMT_GET, "SELECT value FROM kv_store WHERE key = $1 AND deleted = FALSE",
                  1, nullptr);
    PgResultGuard g1{r1};
    if (PQresultStatus(r1) != PGRES_COMMAND_OK)
        return false;

    // PUT (UPSERT)
    auto* r2 = PQprepare(conn, STMT_PUT,
                         "INSERT INTO kv_store (key, value, version, deleted) "
                         "VALUES ($1, $2, nextval('kv_version_seq'), FALSE) "
                         "ON CONFLICT (key) DO UPDATE SET value = $2, "
                         "version = nextval('kv_version_seq'), deleted = FALSE",
                         2, nullptr);
    PgResultGuard g2{r2};
    if (PQresultStatus(r2) != PGRES_COMMAND_OK)
        return false;

    // DELETE (soft delete)
    auto* r3 = PQprepare(conn, STMT_DELETE,
                         "UPDATE kv_store SET deleted = TRUE, "
                         "version = nextval('kv_version_seq') "
                         "WHERE key = $1 AND deleted = FALSE",
                         1, nullptr);
    PgResultGuard g3{r3};
    if (PQresultStatus(r3) != PGRES_COMMAND_OK)
        return false;

    // EXISTS
    auto* r4 =
        PQprepare(conn, STMT_EXISTS,
                  "SELECT 1 FROM kv_store WHERE key = $1 AND deleted = FALSE LIMIT 1", 1, nullptr);
    PgResultGuard g4{r4};
    if (PQresultStatus(r4) != PGRES_COMMAND_OK)
        return false;

    // ITERATE with prefix
    auto* r5 = PQprepare(conn, STMT_ITERATE,
                         "SELECT key, value FROM kv_store "
                         "WHERE key >= $1 AND key < ($1 || E'\\\\xFF')::bytea "
                         "AND deleted = FALSE ORDER BY key",
                         1, nullptr);
    PgResultGuard g5{r5};
    if (PQresultStatus(r5) != PGRES_COMMAND_OK)
        return false;

    // ITERATE all
    auto* r6 = PQprepare(conn, STMT_ITERATE_ALL,
                         "SELECT key, value FROM kv_store "
                         "WHERE deleted = FALSE ORDER BY key",
                         0, nullptr);
    PgResultGuard g6{r6};
    if (PQresultStatus(r6) != PGRES_COMMAND_OK)
        return false;

    // COUNT
    auto* r7 = PQprepare(conn, STMT_COUNT, "SELECT COUNT(*) FROM kv_store WHERE deleted = FALSE", 0,
                         nullptr);
    PgResultGuard g7{r7};
    if (PQresultStatus(r7) != PGRES_COMMAND_OK)
        return false;

    // SIZE
    auto* r8 = PQprepare(conn, STMT_SIZE,
                         "SELECT COALESCE(SUM(length(key) + length(value)), 0) "
                         "FROM kv_store WHERE deleted = FALSE",
                         0, nullptr);
    PgResultGuard g8{r8};
    if (PQresultStatus(r8) != PGRES_COMMAND_OK)
        return false;

    return true;
}

}  // namespace

// ============================================================================
// Connection Pool Implementation
// ============================================================================

/// @brief Thread-safe connection pool with RAII connections
class ConnectionPool {
public:
    /// @brief RAII wrapper that returns connection to pool on destruction
    class PooledConnection {
    public:
        PooledConnection() noexcept = default;
        ~PooledConnection() { release(); }

        PooledConnection(const PooledConnection&) = delete;
        PooledConnection& operator=(const PooledConnection&) = delete;

        PooledConnection(PooledConnection&& other) noexcept
            : conn_{other.conn_}, pool_{other.pool_} {
            other.conn_ = nullptr;
            other.pool_ = nullptr;
        }

        PooledConnection& operator=(PooledConnection&& other) noexcept {
            if (this != &other) {
                release();
                conn_ = other.conn_;
                pool_ = other.pool_;
                other.conn_ = nullptr;
                other.pool_ = nullptr;
            }
            return *this;
        }

        [[nodiscard]] PGconn* raw() const noexcept { return conn_; }
        [[nodiscard]] bool valid() const noexcept { return conn_ != nullptr; }

    private:
        friend class ConnectionPool;
        PooledConnection(PGconn* conn, ConnectionPool* pool) noexcept : conn_{conn}, pool_{pool} {}

        void release() noexcept {
            if (conn_ && pool_) {
                pool_->return_connection(conn_);
            }
            conn_ = nullptr;
            pool_ = nullptr;
        }

        PGconn* conn_{nullptr};
        ConnectionPool* pool_{nullptr};
    };

    explicit ConnectionPool(const PostgresBackendConfig& config)
        : config_{config}, conn_string_{config.build_connection_string()} {}

    ~ConnectionPool() {
        std::unique_lock lock{mtx_};
        for (auto* conn : available_) {
            PQfinish(conn);
        }
        // In-use connections should have been returned
    }

    /// @brief Initialize the pool by creating min connections
    [[nodiscard]] bool initialize() {
        std::unique_lock lock{mtx_};
        for (std::size_t i = 0; i < config_.pool_min_connections; ++i) {
            auto* conn = create_connection();
            if (!conn) {
                return false;
            }
            available_.push_back(conn);
        }
        return true;
    }

    /// @brief Acquire a connection from the pool
    [[nodiscard]] std::optional<PooledConnection> acquire(std::chrono::milliseconds timeout) {
        std::unique_lock lock{mtx_};

        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (available_.empty()) {
            // Try to create a new connection if under max
            if (total_connections_ < config_.pool_max_connections) {
                auto* conn = create_connection_unlocked();
                if (conn) {
                    ++in_use_;
                    return PooledConnection{conn, this};
                }
            }

            // Wait for a connection to be returned
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                return std::nullopt;
            }
        }

        auto* conn = available_.front();
        available_.pop_front();

        // Health check
        if (PQstatus(conn) != CONNECTION_OK) {
            PQreset(conn);
            if (PQstatus(conn) != CONNECTION_OK) {
                PQfinish(conn);
                --total_connections_;
                // Try to create a new one
                conn = create_connection_unlocked();
                if (!conn) {
                    return std::nullopt;
                }
            }
        }

        ++in_use_;
        return PooledConnection{conn, this};
    }

    /// @brief Initialize schema (creates table and index)
    [[nodiscard]] bool initialize_schema() {
        auto conn = acquire(config_.connection_timeout);
        if (!conn || !conn->valid()) {
            return false;
        }

        PgResultGuard result{PQexec(conn->raw(), SCHEMA_SQL)};
        return PQresultStatus(result.get()) == PGRES_COMMAND_OK;
    }

private:
    void return_connection(PGconn* conn) noexcept {
        std::unique_lock lock{mtx_};
        --in_use_;
        available_.push_back(conn);
        cv_.notify_one();
    }

    [[nodiscard]] PGconn* create_connection() {
        std::unique_lock lock{mtx_};
        return create_connection_unlocked();
    }

    [[nodiscard]] PGconn* create_connection_unlocked() {
        auto* conn = PQconnectdb(conn_string_.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            PQfinish(conn);
            return nullptr;
        }

        // Prepare statements
        if (!prepare_statements(conn)) {
            PQfinish(conn);
            return nullptr;
        }

        ++total_connections_;
        return conn;
    }

    PostgresBackendConfig config_;
    std::string conn_string_;
    std::deque<PGconn*> available_;
    std::size_t total_connections_{0};
    std::size_t in_use_{0};
    std::mutex mtx_;
    std::condition_variable cv_;
};

// ============================================================================
// Transaction State
// ============================================================================

struct PgTransaction {
    ConnectionPool::PooledConnection conn;
    TxId id;
    bool active{true};
};

// ============================================================================
// PostgresBackend Implementation
// ============================================================================

PostgresBackend::PostgresBackend(PostgresBackendConfig config) : pg_config_{std::move(config)} {
    // Initialize base_config_ from pg_config_
    base_config_.max_key_size = pg_config_.max_key_size;
    base_config_.max_value_size = pg_config_.max_value_size;
    base_config_.isolation_level = pg_config_.isolation_level;
    base_config_.enable_transactions = pg_config_.enable_transactions;
}

PostgresBackend::~PostgresBackend() = default;

Result<std::unique_ptr<PostgresBackend>> PostgresBackend::create(PostgresBackendConfig config) {
    if (!config.is_valid()) {
        return StateBackendError::InvalidConfig;
    }

    auto backend = std::unique_ptr<PostgresBackend>(new PostgresBackend(std::move(config)));

    // Create connection pool
    backend->pool_ = std::make_unique<ConnectionPool>(backend->pg_config_);
    if (!backend->pool_->initialize()) {
        return StateBackendError::BackendClosed;
    }

    // Initialize schema
    if (!backend->pool_->initialize_schema()) {
        return StateBackendError::BackendClosed;
    }

    return backend;
}

Result<std::vector<std::byte>> PostgresBackend::get(Key key) const {
    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return StateBackendError::BackendClosed;
    }

    const char* params[1] = {reinterpret_cast<const char*>(key.data())};
    int lengths[1] = {static_cast<int>(key.size())};
    int formats[1] = {1};  // Binary format

    PgResultGuard result{PQexecPrepared(conn->raw(), STMT_GET, 1, params, lengths, formats, 1)};

    if (PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
        return map_pq_error(result.get());
    }

    if (PQntuples(result.get()) == 0) {
        return StateBackendError::KeyNotFound;
    }

    // Get binary value
    const auto* value_ptr = reinterpret_cast<const std::byte*>(PQgetvalue(result.get(), 0, 0));
    int value_len = PQgetlength(result.get(), 0, 0);

    return std::vector<std::byte>(value_ptr, value_ptr + value_len);
}

Result<void> PostgresBackend::put(Key key, Value value) {
    // Validate
    if (key.empty()) {
        return StateBackendError::InvalidKey;
    }
    if (key.size() > pg_config_.max_key_size) {
        return StateBackendError::KeyTooLarge;
    }
    if (value.size() > pg_config_.max_value_size) {
        return StateBackendError::ValueTooLarge;
    }

    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return StateBackendError::BackendClosed;
    }

    const char* params[2] = {reinterpret_cast<const char*>(key.data()),
                             reinterpret_cast<const char*>(value.data())};
    int lengths[2] = {static_cast<int>(key.size()), static_cast<int>(value.size())};
    int formats[2] = {1, 1};  // Binary format

    PgResultGuard result{PQexecPrepared(conn->raw(), STMT_PUT, 2, params, lengths, formats, 0)};

    if (PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        return map_pq_error(result.get());
    }

    return {};
}

Result<void> PostgresBackend::remove(Key key) {
    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return StateBackendError::BackendClosed;
    }

    const char* params[1] = {reinterpret_cast<const char*>(key.data())};
    int lengths[1] = {static_cast<int>(key.size())};
    int formats[1] = {1};  // Binary format

    PgResultGuard result{PQexecPrepared(conn->raw(), STMT_DELETE, 1, params, lengths, formats, 0)};

    if (PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        return map_pq_error(result.get());
    }

    // Check if any rows were affected
    const char* affected = PQcmdTuples(result.get());
    if (affected && std::string_view{affected} == "0") {
        return StateBackendError::KeyNotFound;
    }

    return {};
}

bool PostgresBackend::exists(Key key) const noexcept {
    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return false;
    }

    const char* params[1] = {reinterpret_cast<const char*>(key.data())};
    int lengths[1] = {static_cast<int>(key.size())};
    int formats[1] = {1};  // Binary format

    PgResultGuard result{PQexecPrepared(conn->raw(), STMT_EXISTS, 1, params, lengths, formats, 1)};

    if (PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
        return false;
    }

    return PQntuples(result.get()) > 0;
}

Result<void> PostgresBackend::iterate(Key prefix, const IterateCallback& callback) const {
    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return StateBackendError::BackendClosed;
    }

    PgResultGuard result{nullptr};

    if (prefix.empty()) {
        // Iterate all
        result = PgResultGuard{
            PQexecPrepared(conn->raw(), STMT_ITERATE_ALL, 0, nullptr, nullptr, nullptr, 1)};
    } else {
        // Iterate with prefix
        const char* params[1] = {reinterpret_cast<const char*>(prefix.data())};
        int lengths[1] = {static_cast<int>(prefix.size())};
        int formats[1] = {1};  // Binary format

        result = PgResultGuard{
            PQexecPrepared(conn->raw(), STMT_ITERATE, 1, params, lengths, formats, 1)};
    }

    if (PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
        return map_pq_error(result.get());
    }

    int nrows = PQntuples(result.get());
    for (int i = 0; i < nrows; ++i) {
        const auto* key_ptr = reinterpret_cast<const std::byte*>(PQgetvalue(result.get(), i, 0));
        int key_len = PQgetlength(result.get(), i, 0);

        const auto* val_ptr = reinterpret_cast<const std::byte*>(PQgetvalue(result.get(), i, 1));
        int val_len = PQgetlength(result.get(), i, 1);

        Key k{key_ptr, static_cast<std::size_t>(key_len)};
        Value v{val_ptr, static_cast<std::size_t>(val_len)};

        if (!callback(k, v)) {
            break;
        }
    }

    return {};
}

Result<void> PostgresBackend::batch(std::span<const BatchOp> ops) {
    if (ops.empty()) {
        return {};
    }

    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return StateBackendError::BackendClosed;
    }

    // Start transaction for atomicity
    PgResultGuard begin_result{PQexec(conn->raw(), "BEGIN")};
    if (PQresultStatus(begin_result.get()) != PGRES_COMMAND_OK) {
        return map_pq_error(begin_result.get());
    }

    // Validate and apply operations
    for (const auto& op : ops) {
        if (op.type == BatchOpType::Put) {
            if (op.key.empty()) {
                PQexec(conn->raw(), "ROLLBACK");
                return StateBackendError::InvalidKey;
            }
            if (op.key.size() > pg_config_.max_key_size) {
                PQexec(conn->raw(), "ROLLBACK");
                return StateBackendError::KeyTooLarge;
            }
            if (op.value.size() > pg_config_.max_value_size) {
                PQexec(conn->raw(), "ROLLBACK");
                return StateBackendError::ValueTooLarge;
            }

            const char* params[2] = {reinterpret_cast<const char*>(op.key.data()),
                                     reinterpret_cast<const char*>(op.value.data())};
            int lengths[2] = {static_cast<int>(op.key.size()), static_cast<int>(op.value.size())};
            int formats[2] = {1, 1};

            PgResultGuard result{
                PQexecPrepared(conn->raw(), STMT_PUT, 2, params, lengths, formats, 0)};

            if (PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
                PQexec(conn->raw(), "ROLLBACK");
                return map_pq_error(result.get());
            }
        } else {
            // Remove
            const char* params[1] = {reinterpret_cast<const char*>(op.key.data())};
            int lengths[1] = {static_cast<int>(op.key.size())};
            int formats[1] = {1};

            PgResultGuard result{
                PQexecPrepared(conn->raw(), STMT_DELETE, 1, params, lengths, formats, 0)};

            if (PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
                PQexec(conn->raw(), "ROLLBACK");
                return map_pq_error(result.get());
            }

            // Check if key existed
            const char* affected = PQcmdTuples(result.get());
            if (affected && std::string_view{affected} == "0") {
                PQexec(conn->raw(), "ROLLBACK");
                return StateBackendError::KeyNotFound;
            }
        }
    }

    // Commit transaction
    PgResultGuard commit_result{PQexec(conn->raw(), "COMMIT")};
    if (PQresultStatus(commit_result.get()) != PGRES_COMMAND_OK) {
        return map_pq_error(commit_result.get());
    }

    return {};
}

Result<TxHandle> PostgresBackend::begin_transaction() {
    if (!pg_config_.enable_transactions) {
        return StateBackendError::UnsupportedOperation;
    }

    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return StateBackendError::BackendClosed;
    }

    // Determine isolation level
    const char* iso_level = (pg_config_.isolation_level == TransactionIsolationLevel::Snapshot)
                                ? "BEGIN ISOLATION LEVEL REPEATABLE READ"
                                : "BEGIN ISOLATION LEVEL READ COMMITTED";

    PgResultGuard result{PQexec(conn->raw(), iso_level)};
    if (PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        return map_pq_error(result.get());
    }

    // Create transaction ID
    static std::atomic<std::uint64_t> next_tx_id{1};
    TxId tx_id{.id = next_tx_id++, .generation = 1};

    // Store transaction state
    std::unique_lock lock{tx_mtx_};
    transactions_[tx_id.id] = PgTransaction{.conn = std::move(*conn), .id = tx_id, .active = true};

    return TxHandle{this, tx_id};
}

Result<void> PostgresBackend::commit(TxHandle tx) {
    if (!tx.is_valid()) {
        return StateBackendError::InvalidTransaction;
    }

    std::unique_lock lock{tx_mtx_};
    auto it = transactions_.find(tx.id().id);
    if (it == transactions_.end() || !it->second.active) {
        tx.release();
        return StateBackendError::InvalidTransaction;
    }

    auto& pg_tx = it->second;
    PgResultGuard result{PQexec(pg_tx.conn.raw(), "COMMIT")};

    pg_tx.active = false;
    transactions_.erase(it);

    if (PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
        auto err = map_pq_error(result.get());
        tx.release();
        return err;
    }

    tx.release();
    return {};
}

Result<void> PostgresBackend::rollback(TxHandle tx) {
    if (!tx.is_valid()) {
        return StateBackendError::InvalidTransaction;
    }

    std::unique_lock lock{tx_mtx_};
    auto it = transactions_.find(tx.id().id);
    if (it == transactions_.end()) {
        tx.release();
        return {};
    }

    auto& pg_tx = it->second;
    PgResultGuard result{PQexec(pg_tx.conn.raw(), "ROLLBACK")};

    pg_tx.active = false;
    transactions_.erase(it);
    tx.release();
    return {};
}

std::size_t PostgresBackend::key_count() const noexcept {
    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return 0;
    }

    PgResultGuard result{PQexecPrepared(conn->raw(), STMT_COUNT, 0, nullptr, nullptr, nullptr, 0)};

    if (PQresultStatus(result.get()) != PGRES_TUPLES_OK || PQntuples(result.get()) == 0) {
        return 0;
    }

    const char* count_str = PQgetvalue(result.get(), 0, 0);
    if (!count_str) {
        return 0;
    }

    return static_cast<std::size_t>(std::stoll(count_str));
}

std::size_t PostgresBackend::storage_bytes() const noexcept {
    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return 0;
    }

    PgResultGuard result{PQexecPrepared(conn->raw(), STMT_SIZE, 0, nullptr, nullptr, nullptr, 0)};

    if (PQresultStatus(result.get()) != PGRES_TUPLES_OK || PQntuples(result.get()) == 0) {
        return 0;
    }

    const char* size_str = PQgetvalue(result.get(), 0, 0);
    if (!size_str) {
        return 0;
    }

    return static_cast<std::size_t>(std::stoll(size_str));
}

const StateBackendConfig& PostgresBackend::config() const noexcept {
    return base_config_;
}

bool PostgresBackend::supports_transactions() const noexcept {
    return pg_config_.enable_transactions;
}

void PostgresBackend::clear() noexcept {
    auto conn = pool_->acquire(pg_config_.connection_timeout);
    if (!conn || !conn->valid()) {
        return;
    }

    // Truncate table for fast clear
    PQexec(conn->raw(), "TRUNCATE TABLE kv_store");
    // Reset sequence
    PQexec(conn->raw(), "ALTER SEQUENCE kv_version_seq RESTART WITH 1");
}

#endif  // DOTVM_POSTGRESQL_ENABLED

}  // namespace dotvm::core::state
