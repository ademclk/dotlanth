/// @file state_execution_context.hpp
/// @brief STATE-004 StateExecutionContext - VM-level state management bridge
///
/// StateExecutionContext bridges VM opcode execution with the TransactionManager,
/// providing multi-transaction support via explicit handles. Each execution context
/// can manage multiple concurrent transactions.

#pragma once

#include <atomic>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "dotvm/core/state/transaction_manager.hpp"

namespace dotvm::exec {

/// @brief Error codes for state execution operations
enum class StateExecError : std::uint8_t {
    Success = 0,           ///< Operation completed successfully
    NotEnabled = 1,        ///< State context not enabled
    InvalidHandle = 2,     ///< Invalid transaction handle
    KeyNotFound = 3,       ///< Key not found in state store
    TransactionConflict = 4,  ///< Transaction commit conflict (OCC)
    BackendError = 5,      ///< Backend operation failed
};

/// @brief Convert StateExecError to ExecResult
/// @param err The state execution error
/// @return Corresponding ExecResult value
constexpr std::uint8_t to_exec_result(StateExecError err) noexcept {
    switch (err) {
        case StateExecError::Success: return 0;  // ExecResult::Success
        case StateExecError::NotEnabled: return 17;  // ExecResult::StateNotEnabled
        case StateExecError::InvalidHandle: return 16;  // ExecResult::TransactionAborted
        case StateExecError::KeyNotFound: return 14;  // ExecResult::StateKeyNotFound
        case StateExecError::TransactionConflict: return 15;  // ExecResult::TransactionConflict
        case StateExecError::BackendError: return 6;  // ExecResult::Error
    }
    return 6;  // ExecResult::Error
}

/// @brief VM-level state execution context with multi-transaction support
///
/// This class provides the bridge between VM opcodes and the TransactionManager.
/// It manages multiple concurrent transactions via unique handles, enabling
/// complex transactional workflows in bytecode.
///
/// Handle semantics:
/// - Handle 0 means "no transaction" (direct backend access)
/// - Other handles reference active transactions
/// - Handles are monotonically increasing, never reused
///
/// Lifecycle:
/// - Call enable() with a TransactionManager before state operations
/// - Use begin_transaction() to start new transactions
/// - All active transactions are auto-rolled-back on destruction
class StateExecutionContext {
  public:
    using Key = std::span<const std::byte>;
    using Value = std::span<const std::byte>;

    /// @brief Default constructor - context is disabled
    StateExecutionContext() noexcept = default;

    /// @brief Destructor - auto-rollback all active transactions
    ~StateExecutionContext() noexcept;

    // Non-copyable, movable
    StateExecutionContext(const StateExecutionContext&) = delete;
    StateExecutionContext& operator=(const StateExecutionContext&) = delete;
    StateExecutionContext(StateExecutionContext&&) noexcept = default;
    StateExecutionContext& operator=(StateExecutionContext&&) noexcept = default;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Enable state operations with a transaction manager
    /// @param tx_mgr The transaction manager to use (must outlive this context)
    void enable(core::state::TransactionManager* tx_mgr) noexcept;

    /// @brief Disable state operations, rolling back all active transactions
    void disable() noexcept;

    /// @brief Check if state operations are enabled
    /// @return true if enabled
    [[nodiscard]] bool enabled() const noexcept { return tx_mgr_ != nullptr; }

    // ========================================================================
    // Transaction Lifecycle
    // ========================================================================

    /// @brief Begin a new transaction
    /// @param isolation Isolation level (0=Snapshot, 1=ReadCommitted)
    /// @return Transaction handle (0 on failure)
    [[nodiscard]] std::uint64_t begin_transaction(std::uint8_t isolation = 0) noexcept;

    /// @brief Commit a transaction
    /// @param handle Transaction handle from begin_transaction()
    /// @return Error code
    [[nodiscard]] StateExecError commit(std::uint64_t handle) noexcept;

    /// @brief Rollback a transaction
    /// @param handle Transaction handle from begin_transaction()
    /// @return Error code
    [[nodiscard]] StateExecError rollback(std::uint64_t handle) noexcept;

    /// @brief Rollback all active transactions
    void rollback_all() noexcept;

    // ========================================================================
    // State Operations
    // ========================================================================

    /// @brief Get a value from state
    /// @param handle Transaction handle (0 = no transaction)
    /// @param key Key to look up
    /// @param out_value Output buffer for value
    /// @return Error code
    [[nodiscard]] StateExecError get(std::uint64_t handle, Key key,
                                     std::vector<std::byte>& out_value) noexcept;

    /// @brief Put a value into state
    /// @param handle Transaction handle (0 = no transaction)
    /// @param key Key to write
    /// @param value Value to write
    /// @return Error code
    [[nodiscard]] StateExecError put(std::uint64_t handle, Key key, Value value) noexcept;

    /// @brief Remove a key from state
    /// @param handle Transaction handle (0 = no transaction)
    /// @param key Key to remove
    /// @return Error code
    [[nodiscard]] StateExecError remove(std::uint64_t handle, Key key) noexcept;

    /// @brief Check if a key exists in state
    /// @param handle Transaction handle (0 = no transaction)
    /// @param key Key to check
    /// @param out_exists Output flag
    /// @return Error code
    [[nodiscard]] StateExecError exists(std::uint64_t handle, Key key,
                                        bool& out_exists) noexcept;

    // ========================================================================
    // Statistics
    // ========================================================================

    /// @brief Get count of active transactions
    /// @return Number of active transactions
    [[nodiscard]] std::size_t active_transaction_count() const noexcept;

    /// @brief Check if a handle is valid
    /// @param handle Handle to check
    /// @return true if valid (including 0)
    [[nodiscard]] bool is_valid_handle(std::uint64_t handle) const noexcept;

  private:
    /// @brief Generate a unique transaction handle
    [[nodiscard]] std::uint64_t generate_handle() noexcept;

    /// @brief Get transaction by handle
    /// @param handle Transaction handle
    /// @return Pointer to managed transaction, or nullptr if invalid
    [[nodiscard]] core::state::ManagedTransaction* get_transaction(
        std::uint64_t handle) noexcept;

    /// @brief Remove a transaction entry
    /// @param handle Handle to remove
    void remove_entry(std::uint64_t handle) noexcept;

    core::state::TransactionManager* tx_mgr_{nullptr};
    std::unordered_map<std::uint64_t, core::state::ManagedTransaction*> active_transactions_;
    std::atomic<std::uint64_t> next_handle_{1};  // Start at 1, 0 means no-transaction
};

}  // namespace dotvm::exec
