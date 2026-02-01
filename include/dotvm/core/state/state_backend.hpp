#pragma once

/// @file state_backend.hpp
/// @brief STATE-001 StateBackend interface for key-value storage with transactions
///
/// Provides a pure virtual interface for pluggable state storage backends.
/// The default implementation is an in-memory backend suitable for testing
/// and single-process use cases.

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/state_backend_fwd.hpp"

namespace dotvm::core::state {

// ============================================================================
// Error Enum
// ============================================================================

/// @brief Error codes for state backend operations
///
/// Error codes are grouped by category for easier debugging:
/// - 1-15:  Key/Value errors
/// - 16-31: Transaction errors
/// - 32-47: Backend errors
/// - 48-63: Iteration errors
/// - 64-79: Configuration errors
enum class StateBackendError : std::uint8_t {
    // Key/Value errors (1-15)
    KeyNotFound = 1,    ///< Key does not exist in storage
    KeyTooLarge = 2,    ///< Key exceeds max_key_size
    ValueTooLarge = 3,  ///< Value exceeds max_value_size
    InvalidKey = 4,     ///< Key is invalid (e.g., empty)

    // Transaction errors (16-31)
    TransactionNotActive = 16,     ///< No active transaction
    TransactionConflict = 17,      ///< Conflicting concurrent modification
    InvalidTransaction = 18,       ///< Transaction handle is invalid
    DeadlockDetected = 19,         ///< Transaction aborted due to deadlock timeout
    TooManyTransactions = 20,      ///< Max concurrent transactions exceeded
    TransactionTimeout = 21,       ///< Transaction exceeded timeout limit
    ReadSetValidationFailed = 22,  ///< Read set version changed during OCC validation

    // Backend errors (32-47)
    StorageFull = 32,    ///< Storage capacity exceeded
    BackendClosed = 33,  ///< Backend has been closed

    // Iteration errors (48-63)
    IterationAborted = 48,  ///< Iteration was aborted by callback
    InvalidPrefix = 49,     ///< Invalid prefix for iteration

    // Configuration errors (64-79)
    InvalidConfig = 64,         ///< Invalid configuration
    UnsupportedOperation = 65,  ///< Operation not supported by this backend

    // Import/Export errors (80-95)
    InvalidMagic = 80,          ///< File does not start with expected magic bytes
    InvalidVersion = 81,        ///< Unsupported file format version
    ChecksumMismatch = 82,      ///< CRC32 checksum verification failed
    TruncatedData = 83,         ///< Unexpected end of data
    InvalidChunkHeader = 84,    ///< Malformed chunk header
    InvalidRecordFormat = 85,   ///< Malformed record within chunk
    ChunkSequenceError = 86,    ///< Chunk sequence number out of order
    ImportAborted = 87,         ///< Import was aborted (partial import possible)
    ExportAborted = 88,         ///< Export was aborted by callback
    TooManyErrors = 89,         ///< Import stopped due to excessive errors
};

/// @brief Convert error to human-readable string
[[nodiscard]] constexpr std::string_view to_string(StateBackendError error) noexcept {
    switch (error) {
        case StateBackendError::KeyNotFound:
            return "KeyNotFound";
        case StateBackendError::KeyTooLarge:
            return "KeyTooLarge";
        case StateBackendError::ValueTooLarge:
            return "ValueTooLarge";
        case StateBackendError::InvalidKey:
            return "InvalidKey";
        case StateBackendError::TransactionNotActive:
            return "TransactionNotActive";
        case StateBackendError::TransactionConflict:
            return "TransactionConflict";
        case StateBackendError::InvalidTransaction:
            return "InvalidTransaction";
        case StateBackendError::DeadlockDetected:
            return "DeadlockDetected";
        case StateBackendError::TooManyTransactions:
            return "TooManyTransactions";
        case StateBackendError::TransactionTimeout:
            return "TransactionTimeout";
        case StateBackendError::ReadSetValidationFailed:
            return "ReadSetValidationFailed";
        case StateBackendError::StorageFull:
            return "StorageFull";
        case StateBackendError::BackendClosed:
            return "BackendClosed";
        case StateBackendError::IterationAborted:
            return "IterationAborted";
        case StateBackendError::InvalidPrefix:
            return "InvalidPrefix";
        case StateBackendError::InvalidConfig:
            return "InvalidConfig";
        case StateBackendError::UnsupportedOperation:
            return "UnsupportedOperation";
        case StateBackendError::InvalidMagic:
            return "InvalidMagic";
        case StateBackendError::InvalidVersion:
            return "InvalidVersion";
        case StateBackendError::ChecksumMismatch:
            return "ChecksumMismatch";
        case StateBackendError::TruncatedData:
            return "TruncatedData";
        case StateBackendError::InvalidChunkHeader:
            return "InvalidChunkHeader";
        case StateBackendError::InvalidRecordFormat:
            return "InvalidRecordFormat";
        case StateBackendError::ChunkSequenceError:
            return "ChunkSequenceError";
        case StateBackendError::ImportAborted:
            return "ImportAborted";
        case StateBackendError::ExportAborted:
            return "ExportAborted";
        case StateBackendError::TooManyErrors:
            return "TooManyErrors";
    }
    return "Unknown";
}

/// @brief Check if an error is recoverable (can be retried or handled gracefully)
[[nodiscard]] constexpr bool is_recoverable(StateBackendError error) noexcept {
    switch (error) {
        case StateBackendError::KeyNotFound:
        case StateBackendError::TransactionConflict:
        case StateBackendError::IterationAborted:
        case StateBackendError::DeadlockDetected:
        case StateBackendError::TransactionTimeout:
        case StateBackendError::ReadSetValidationFailed:
        case StateBackendError::ImportAborted:  // Can resume from checkpoint
        case StateBackendError::ExportAborted:  // Can retry export
            return true;
        default:
            return false;
    }
}

// ============================================================================
// Transaction Isolation Level
// ============================================================================

/// @brief Transaction isolation level
enum class TransactionIsolationLevel : std::uint8_t {
    ReadCommitted = 0,  ///< See committed writes from other transactions
    Snapshot = 1,       ///< Consistent snapshot at transaction start
};

/// @brief Convert isolation level to string
[[nodiscard]] constexpr std::string_view to_string(TransactionIsolationLevel level) noexcept {
    switch (level) {
        case TransactionIsolationLevel::ReadCommitted:
            return "ReadCommitted";
        case TransactionIsolationLevel::Snapshot:
            return "Snapshot";
    }
    return "Unknown";
}

// ============================================================================
// Configuration
// ============================================================================

/// @brief Configuration for state backend
struct StateBackendConfig {
    std::size_t max_key_size{1024};           ///< Maximum key size (default 1KB)
    std::size_t max_value_size{1024 * 1024};  ///< Maximum value size (default 1MB)
    TransactionIsolationLevel isolation_level{TransactionIsolationLevel::ReadCommitted};
    bool enable_transactions{true};      ///< Enable transaction support
    std::size_t initial_capacity{1024};  ///< Initial storage capacity hint

    /// @brief Create default configuration
    [[nodiscard]] static constexpr StateBackendConfig defaults() noexcept {
        return StateBackendConfig{};
    }

    /// @brief Validate configuration
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return max_key_size > 0 && max_value_size > 0 && initial_capacity > 0;
    }
};

// ============================================================================
// Transaction ID
// ============================================================================

/// @brief Unique identifier for a transaction
struct TxId {
    std::uint64_t id{0};          ///< Unique transaction ID
    std::uint32_t generation{0};  ///< Generation for reuse detection

    [[nodiscard]] constexpr bool operator==(const TxId&) const noexcept = default;
};

// ============================================================================
// Batch Operation
// ============================================================================

/// @brief Type of batch operation
enum class BatchOpType : std::uint8_t {
    Put = 0,     ///< Store a key-value pair
    Remove = 1,  ///< Remove a key
};

/// @brief Single operation in a batch
struct BatchOp {
    BatchOpType type;                  ///< Operation type
    std::span<const std::byte> key;    ///< Key to operate on
    std::span<const std::byte> value;  ///< Value (ignored for Remove)
};

// ============================================================================
// Transaction Handle (RAII)
// ============================================================================

/// @brief RAII wrapper for transaction lifetime
///
/// TxHandle provides automatic rollback if the transaction is not explicitly
/// committed. This ensures transactions are never left dangling.
///
/// Thread Safety: NOT thread-safe. Each transaction should be used from
/// a single thread.
class TxHandle {
public:
    /// @brief Default constructor creates an invalid handle
    TxHandle() noexcept = default;

    /// @brief Destructor auto-rollbacks uncommitted transactions
    ~TxHandle() noexcept;

    /// @brief Move constructor
    TxHandle(TxHandle&& other) noexcept;

    /// @brief Move assignment
    TxHandle& operator=(TxHandle&& other) noexcept;

    // Non-copyable
    TxHandle(const TxHandle&) = delete;
    TxHandle& operator=(const TxHandle&) = delete;

    /// @brief Get the transaction ID
    [[nodiscard]] TxId id() const noexcept { return tx_id_; }

    /// @brief Check if this handle is valid
    [[nodiscard]] bool is_valid() const noexcept { return backend_ != nullptr; }

private:
    friend class StateBackend;
    friend class InMemoryBackend;

    /// @brief Private constructor for StateBackend to create handles
    TxHandle(StateBackend* backend, TxId id) noexcept : backend_{backend}, tx_id_{id} {}

    /// @brief Release ownership without rollback (called by commit)
    void release() noexcept {
        backend_ = nullptr;
        tx_id_ = {};
    }

    StateBackend* backend_{nullptr};
    TxId tx_id_{};
};

// ============================================================================
// StateBackend Interface
// ============================================================================

/// @brief Abstract interface for key-value storage with transaction support
///
/// StateBackend provides a pluggable storage layer for DotVM state management.
/// Implementations can range from simple in-memory maps to persistent databases.
///
/// Thread Safety: Depends on implementation. See specific backend classes.
///
/// @par Design Decisions
/// - Uses std::span<const std::byte> for keys/values for zero-copy efficiency
/// - Returns owned vectors from get() to avoid lifetime issues
/// - Transaction support is optional via config
/// - Batch operations provide atomicity guarantees
class StateBackend {
public:
    virtual ~StateBackend() = default;

    // Non-copyable, non-movable (interface class)
    StateBackend(const StateBackend&) = delete;
    StateBackend& operator=(const StateBackend&) = delete;
    StateBackend(StateBackend&&) = delete;
    StateBackend& operator=(StateBackend&&) = delete;

protected:
    StateBackend() = default;

public:
    // Type aliases for convenience
    using Key = std::span<const std::byte>;
    using Value = std::span<const std::byte>;
    using IterateCallback = std::function<bool(Key key, Value value)>;

    template <typename T>
    using Result = ::dotvm::core::Result<T, StateBackendError>;

    // ========================================================================
    // CRUD Operations
    // ========================================================================

    /// @brief Get the value for a key
    ///
    /// @param key The key to look up
    /// @return The value bytes, or KeyNotFound if not present
    [[nodiscard]] virtual Result<std::vector<std::byte>> get(Key key) const = 0;

    /// @brief Store a key-value pair
    ///
    /// @param key The key (must not be empty, must not exceed max_key_size)
    /// @param value The value (must not exceed max_value_size)
    /// @return Success, or error code
    [[nodiscard]] virtual Result<void> put(Key key, Value value) = 0;

    /// @brief Remove a key
    ///
    /// @param key The key to remove
    /// @return Success, or KeyNotFound if not present
    [[nodiscard]] virtual Result<void> remove(Key key) = 0;

    /// @brief Check if a key exists
    ///
    /// @param key The key to check
    /// @return true if the key exists
    [[nodiscard]] virtual bool exists(Key key) const noexcept = 0;

    // ========================================================================
    // Iteration
    // ========================================================================

    /// @brief Iterate over keys with a prefix
    ///
    /// @param prefix Key prefix to filter (empty = all keys)
    /// @param callback Called for each matching key-value pair.
    ///                 Return false to stop iteration early.
    /// @return Success, or error code
    [[nodiscard]] virtual Result<void> iterate(Key prefix,
                                               const IterateCallback& callback) const = 0;

    // ========================================================================
    // Batch Operations
    // ========================================================================

    /// @brief Execute a batch of operations atomically
    ///
    /// Either all operations succeed or none are applied.
    ///
    /// @param ops The operations to execute
    /// @return Success, or first error encountered (all ops rolled back)
    [[nodiscard]] virtual Result<void> batch(std::span<const BatchOp> ops) = 0;

    // ========================================================================
    // Transactions
    // ========================================================================

    /// @brief Begin a new transaction
    ///
    /// @return Transaction handle, or UnsupportedOperation if transactions disabled
    [[nodiscard]] virtual Result<TxHandle> begin_transaction() = 0;

    /// @brief Commit a transaction
    ///
    /// @param tx The transaction handle (moved, invalidated after call)
    /// @return Success, or error code
    [[nodiscard]] virtual Result<void> commit(TxHandle tx) = 0;

    /// @brief Rollback a transaction
    ///
    /// @param tx The transaction handle (moved, invalidated after call)
    /// @return Success, or error code
    [[nodiscard]] virtual Result<void> rollback(TxHandle tx) = 0;

    // ========================================================================
    // Statistics & Configuration
    // ========================================================================

    /// @brief Get the number of keys stored
    [[nodiscard]] virtual std::size_t key_count() const noexcept = 0;

    /// @brief Get approximate storage size in bytes
    [[nodiscard]] virtual std::size_t storage_bytes() const noexcept = 0;

    /// @brief Get the backend configuration
    [[nodiscard]] virtual const StateBackendConfig& config() const noexcept = 0;

    /// @brief Check if transactions are supported
    [[nodiscard]] virtual bool supports_transactions() const noexcept = 0;

    /// @brief Clear all data
    virtual void clear() noexcept = 0;

    // ========================================================================
    // MVCC Version Support (STATE-009)
    // ========================================================================

    /// @brief Get the current global MVCC version
    ///
    /// Returns 0 for backends without MVCC support.
    ///
    /// @return Current version number
    [[nodiscard]] virtual std::uint64_t current_version() const noexcept { return 0; }

    /// @brief Get a value at a specific MVCC version
    ///
    /// @param key The key to look up
    /// @param version The version to read at
    /// @return The value at that version, or KeyNotFound/UnsupportedOperation
    [[nodiscard]] virtual Result<std::vector<std::byte>>
    get_at_version(Key key, std::uint64_t version) const {
        (void)key;
        (void)version;
        return StateBackendError::UnsupportedOperation;
    }

    /// @brief Iterate over keys at a specific MVCC version
    ///
    /// @param prefix Key prefix to filter (empty = all keys)
    /// @param version The version to iterate at
    /// @param callback Called for each matching key-value pair
    /// @return Success, or error code
    [[nodiscard]] virtual Result<void> iterate_at_version(Key prefix, std::uint64_t version,
                                                          const IterateCallback& callback) const {
        (void)prefix;
        (void)version;
        (void)callback;
        return StateBackendError::UnsupportedOperation;
    }

    /// @brief Set the minimum version that must be retained for snapshots
    ///
    /// Called by SnapshotManager to coordinate GC. Versions >= this value
    /// should not be pruned.
    ///
    /// @param min_version Minimum version to retain
    virtual void set_min_snapshot_version(std::uint64_t min_version) noexcept { (void)min_version; }
};

// ============================================================================
// Factory Function
// ============================================================================

/// @brief Create a state backend with the given configuration
///
/// Currently creates an InMemoryBackend. Future versions may support
/// different backend types via configuration.
///
/// @param config Backend configuration (defaults used if not specified)
/// @return The created backend
[[nodiscard]] std::unique_ptr<StateBackend>
create_state_backend(const StateBackendConfig& config = StateBackendConfig::defaults());

}  // namespace dotvm::core::state

// ============================================================================
// std::formatter specializations
// ============================================================================

template <>
struct std::formatter<dotvm::core::state::StateBackendError> : std::formatter<std::string_view> {
    auto format(dotvm::core::state::StateBackendError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};

template <>
struct std::formatter<dotvm::core::state::TransactionIsolationLevel>
    : std::formatter<std::string_view> {
    auto format(dotvm::core::state::TransactionIsolationLevel e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
