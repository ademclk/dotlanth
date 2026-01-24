#pragma once

/// @file transaction_manager.hpp
/// @brief STATE-003 TransactionManager with OCC and deadlock detection
///
/// Provides high-level transaction management on top of StateBackend:
/// - Optimistic Concurrency Control (OCC) with automatic read_set tracking
/// - Write-write conflict detection (first-committer-wins)
/// - Snapshot and ReadCommitted isolation levels
/// - Timeout-based deadlock detection

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::state {

// ============================================================================
// Transaction State
// ============================================================================

/// @brief State of a managed transaction
enum class TransactionState : std::uint8_t {
    Active = 0,     ///< Transaction is active and can perform operations
    Committed = 1,  ///< Transaction has been committed successfully
    Aborted = 2,    ///< Transaction has been aborted/rolled back
};

/// @brief Convert transaction state to string
[[nodiscard]] constexpr const char* to_string(TransactionState state) noexcept {
    switch (state) {
        case TransactionState::Active:
            return "Active";
        case TransactionState::Committed:
            return "Committed";
        case TransactionState::Aborted:
            return "Aborted";
    }
    return "Unknown";
}

// ============================================================================
// Read/Write Set Entries
// ============================================================================

/// @brief Comparator for byte vectors (lexicographic ordering)
struct ByteVectorCompare {
    bool operator()(const std::vector<std::byte>& lhs,
                    const std::vector<std::byte>& rhs) const noexcept {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }
};

/// @brief Entry in the transaction's read set
struct ReadSetEntry {
    std::vector<std::byte> key;                           ///< The key that was read
    std::uint64_t version_at_read;                        ///< Version of the value at read time
    bool existed;                                         ///< Whether the key existed at read time
    std::optional<std::vector<std::byte>> value_at_read;  ///< Value at read time (for validation)
};

/// @brief Entry in the transaction's write set
struct WriteSetEntry {
    std::optional<std::vector<std::byte>> value;  ///< Value to write (nullopt = delete)
};

// ============================================================================
// Managed Transaction
// ============================================================================

/// @brief A transaction managed by TransactionManager
///
/// Contains all state needed for OCC validation:
/// - Read set for detecting phantom reads and lost updates
/// - Write set for buffering changes until commit
/// - Timing info for deadlock detection
struct ManagedTransaction {
    TxId id;                                           ///< Unique transaction identifier
    TransactionState state{TransactionState::Active};  ///< Current state
    std::uint64_t start_version;                       ///< Global version at transaction start
    TransactionIsolationLevel isolation_level;         ///< Isolation level for this transaction
    std::map<std::vector<std::byte>, ReadSetEntry, ByteVectorCompare> read_set;    ///< Keys read
    std::map<std::vector<std::byte>, WriteSetEntry, ByteVectorCompare> write_set;  ///< Keys written
    std::chrono::steady_clock::time_point start_time;  ///< When transaction started
    std::chrono::milliseconds timeout{0};              ///< Timeout (0 = use default)
};

// ============================================================================
// Configuration
// ============================================================================

/// @brief Configuration for TransactionManager
struct TransactionManagerConfig {
    std::size_t max_concurrent_transactions{1000};           ///< Max concurrent active transactions
    std::chrono::milliseconds default_timeout{5000};         ///< Default transaction timeout
    std::chrono::milliseconds deadlock_check_interval{100};  ///< Deadlock check frequency
    TransactionIsolationLevel default_isolation{TransactionIsolationLevel::Snapshot};

    /// @brief Create default configuration
    [[nodiscard]] static constexpr TransactionManagerConfig defaults() noexcept {
        return TransactionManagerConfig{};
    }

    /// @brief Validate configuration
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return max_concurrent_transactions > 0 && default_timeout.count() > 0 &&
               deadlock_check_interval.count() > 0;
    }
};

// ============================================================================
// TransactionManager
// ============================================================================

/// @brief Manages transactions with OCC and deadlock detection
///
/// TransactionManager wraps a StateBackend and provides:
/// - Automatic read_set tracking for OCC validation
/// - Write buffering in write_set until commit
/// - First-committer-wins conflict resolution
/// - Background deadlock detection via timeout
///
/// Thread Safety: Thread-safe. Multiple threads can use different transactions
/// concurrently. A single transaction should only be used from one thread.
///
/// @par Design: Composition over Inheritance
/// TransactionManager wraps StateBackend rather than extending InMemoryBackend.
/// This keeps STATE-002 unchanged and allows TransactionManager to work with
/// any backend implementation.
class TransactionManager {
public:
    using Key = StateBackend::Key;
    using Value = StateBackend::Value;
    template <typename T>
    using Result = ::dotvm::core::Result<T, StateBackendError>;

    // ========================================================================
    // Construction
    // ========================================================================

    /// @brief Create a TransactionManager wrapping a backend
    /// @param backend The backend to use for storage (takes ownership)
    /// @param config Configuration options
    explicit TransactionManager(
        std::unique_ptr<StateBackend> backend,
        TransactionManagerConfig config = TransactionManagerConfig::defaults());

    /// @brief Destructor - stops deadlock detector and aborts active transactions
    ~TransactionManager();

    // Non-copyable, non-movable
    TransactionManager(const TransactionManager&) = delete;
    TransactionManager& operator=(const TransactionManager&) = delete;
    TransactionManager(TransactionManager&&) = delete;
    TransactionManager& operator=(TransactionManager&&) = delete;

    // ========================================================================
    // Transaction Lifecycle
    // ========================================================================

    /// @brief Begin a new transaction
    /// @param isolation Isolation level (uses config default if not specified)
    /// @param timeout Transaction timeout (uses config default if not specified)
    /// @return Pointer to the transaction, or error if max transactions exceeded
    [[nodiscard]] Result<ManagedTransaction*>
    begin(std::optional<TransactionIsolationLevel> isolation = std::nullopt,
          std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /// @brief Commit a transaction
    ///
    /// Performs OCC validation:
    /// 1. Check write-write conflicts (first-committer-wins)
    /// 2. Validate read_set versions unchanged
    /// 3. Apply write_set atomically to backend
    ///
    /// @param tx The transaction to commit (must be Active)
    /// @return Success, or error (TransactionConflict, ReadSetValidationFailed, etc.)
    [[nodiscard]] Result<void> commit(ManagedTransaction& tx);

    /// @brief Rollback (abort) a transaction
    /// @param tx The transaction to rollback (must be Active)
    /// @return Success, or error if transaction not active
    [[nodiscard]] Result<void> rollback(ManagedTransaction& tx);

    // ========================================================================
    // Transactional Operations
    // ========================================================================

    /// @brief Get a value within a transaction
    ///
    /// Automatically adds the key to the read_set for OCC validation.
    /// Checks write_set first for read-your-writes semantics.
    ///
    /// @param tx The transaction context
    /// @param key The key to read
    /// @return The value, or KeyNotFound
    [[nodiscard]] Result<std::vector<std::byte>> get(ManagedTransaction& tx, Key key);

    /// @brief Put a value within a transaction
    ///
    /// Buffers the write in write_set until commit.
    ///
    /// @param tx The transaction context
    /// @param key The key to write
    /// @param value The value to write
    /// @return Success, or error
    [[nodiscard]] Result<void> put(ManagedTransaction& tx, Key key, Value value);

    /// @brief Remove a key within a transaction
    ///
    /// Marks the key for deletion in write_set.
    ///
    /// @param tx The transaction context
    /// @param key The key to remove
    /// @return Success, or error
    [[nodiscard]] Result<void> remove(ManagedTransaction& tx, Key key);

    /// @brief Check if a key exists within a transaction
    ///
    /// Automatically adds the key to the read_set for OCC validation.
    ///
    /// @param tx The transaction context
    /// @param key The key to check
    /// @return true if key exists
    [[nodiscard]] bool exists(ManagedTransaction& tx, Key key);

    // ========================================================================
    // Statistics & Configuration
    // ========================================================================

    /// @brief Get the number of active transactions
    [[nodiscard]] std::size_t active_transaction_count() const noexcept;

    /// @brief Get the configuration
    [[nodiscard]] const TransactionManagerConfig& config() const noexcept;

    /// @brief Get the underlying backend
    [[nodiscard]] StateBackend& backend() noexcept;

    /// @brief Get the underlying backend (const)
    [[nodiscard]] const StateBackend& backend() const noexcept;

private:
    // ========================================================================
    // Internal Helpers
    // ========================================================================

    /// @brief Validate that a transaction is active
    [[nodiscard]] Result<void> validate_transaction(const ManagedTransaction& tx) const;

    /// @brief Get the current global version
    [[nodiscard]] std::uint64_t get_current_version() const noexcept;

    /// @brief Check for write-write conflicts
    [[nodiscard]] Result<void> check_write_conflicts(const ManagedTransaction& tx);

    /// @brief Validate the read_set for OCC
    [[nodiscard]] Result<void> validate_read_set(const ManagedTransaction& tx);

    /// @brief Apply the write_set to the backend
    [[nodiscard]] Result<void> apply_write_set(ManagedTransaction& tx);

    /// @brief Background thread function for deadlock detection
    void deadlock_detector_loop();

    /// @brief Check and handle deadlocks (aborts oldest timed-out transaction)
    void check_deadlocks();

    /// @brief Generate a new transaction ID
    [[nodiscard]] TxId generate_tx_id();

    /// @brief Clean up a transaction (called after commit/rollback/abort)
    void cleanup_transaction(std::uint64_t tx_id);

    // ========================================================================
    // Members
    // ========================================================================

    std::unique_ptr<StateBackend> backend_;
    TransactionManagerConfig config_;

    // Transaction storage
    std::unordered_map<std::uint64_t, ManagedTransaction> transactions_;
    mutable std::shared_mutex transactions_mtx_;

    // Active transaction tracking
    std::set<std::uint64_t> active_transactions_;
    mutable std::mutex active_mtx_;

    // Version and ID generation
    std::atomic<std::uint64_t> global_version_{0};
    std::atomic<std::uint64_t> next_tx_id_{1};
    std::atomic<std::uint32_t> tx_generation_{1};

    // Deadlock detection
    std::jthread deadlock_detector_;
    std::atomic<bool> shutdown_{false};
};

}  // namespace dotvm::core::state
