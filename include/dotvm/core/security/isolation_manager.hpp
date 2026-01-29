#pragma once

/// @file isolation_manager.hpp
/// @brief SEC-007 Isolation Manager for DotVM multi-Dot execution
///
/// This header defines the IsolationManager class responsible for:
/// - Sandbox lifecycle management (create/destroy)
/// - Boundary enforcement between Dots
/// - Handle grants for controlled memory sharing
/// - Syscall validation in Strict isolation mode
///
/// The IsolationManager implements per-Dot HandleTable namespacing for
/// memory isolation, with explicit grants for parent-to-child sharing.

#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dotvm/core/memory.hpp"
#include "dotvm/core/security/isolation_level.hpp"
#include "dotvm/core/security/syscall_whitelist.hpp"

namespace dotvm::core::security {

// ============================================================================
// Type Aliases
// ============================================================================

/// @brief Unique identifier for a Dot execution context
///
/// DotId 0 is reserved for the root/parent context.
using DotId = std::uint64_t;

/// @brief Invalid/null DotId constant
inline constexpr DotId INVALID_DOT_ID = 0;

// ============================================================================
// AccessType Enum
// ============================================================================

/// @brief Type of memory access for boundary enforcement
enum class AccessType : std::uint8_t {
    Read = 0,   ///< Read-only access
    Write = 1,  ///< Write access
};

/// @brief Convert AccessType to human-readable string
[[nodiscard]] constexpr std::string_view to_string(AccessType type) noexcept {
    switch (type) {
        case AccessType::Read:
            return "Read";
        case AccessType::Write:
            return "Write";
    }
    return "Unknown";
}

// ============================================================================
// IsolationError Enum
// ============================================================================

/// @brief Error codes for isolation operations
enum class IsolationError : std::uint8_t {
    /// Operation succeeded
    Success = 0,

    /// Dot ID not found in sandbox registry
    DotNotFound,

    /// Dot ID already exists
    DotAlreadyExists,

    /// Parent Dot not found for child creation
    ParentNotFound,

    /// Cross-Dot access denied (no valid grant)
    AccessDenied,

    /// Grant not found for the specified handle
    GrantNotFound,

    /// Grant has been revoked
    GrantRevoked,

    /// Syscall not in whitelist
    SyscallDenied,

    /// Handle not owned by the granting Dot
    HandleNotOwned,

    /// Invalid parent-child relationship
    InvalidRelationship,

    /// Cannot destroy sandbox with active children
    HasActiveChildren,

    /// Network access denied by isolation level
    NetworkDenied,

    /// Filesystem access denied by isolation level
    FilesystemDenied,

    /// Internal error (should not occur)
    InternalError,
};

/// @brief Convert IsolationError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(IsolationError error) noexcept {
    switch (error) {
        case IsolationError::Success:
            return "Success";
        case IsolationError::DotNotFound:
            return "DotNotFound";
        case IsolationError::DotAlreadyExists:
            return "DotAlreadyExists";
        case IsolationError::ParentNotFound:
            return "ParentNotFound";
        case IsolationError::AccessDenied:
            return "AccessDenied";
        case IsolationError::GrantNotFound:
            return "GrantNotFound";
        case IsolationError::GrantRevoked:
            return "GrantRevoked";
        case IsolationError::SyscallDenied:
            return "SyscallDenied";
        case IsolationError::HandleNotOwned:
            return "HandleNotOwned";
        case IsolationError::InvalidRelationship:
            return "InvalidRelationship";
        case IsolationError::HasActiveChildren:
            return "HasActiveChildren";
        case IsolationError::NetworkDenied:
            return "NetworkDenied";
        case IsolationError::FilesystemDenied:
            return "FilesystemDenied";
        case IsolationError::InternalError:
            return "InternalError";
    }
    return "Unknown";
}

// ============================================================================
// HandleGrant Struct
// ============================================================================

/// @brief Record of a handle grant from parent to child Dot
///
/// HandleGrants enable controlled memory sharing between isolated Dots.
/// The parent Dot grants access to a specific handle in its namespace,
/// creating a mapping to the child's namespace with specified permissions.
struct HandleGrant {
    /// Handle in the parent's namespace (source)
    Handle source_handle;

    /// Handle in the child's namespace (granted)
    Handle granted_handle;

    /// Parent Dot that owns the source handle
    DotId parent_dot;

    /// Child Dot receiving the grant
    DotId child_dot;

    /// Whether read access is granted
    bool can_read;

    /// Whether write access is granted
    bool can_write;

    /// Whether this grant has been revoked
    bool revoked;

    /// @brief Check if access type is allowed by this grant
    [[nodiscard]] constexpr bool allows(AccessType type) const noexcept {
        if (revoked) {
            return false;
        }
        switch (type) {
            case AccessType::Read:
                return can_read;
            case AccessType::Write:
                return can_write;
        }
        return false;
    }

    /// @brief Equality comparison
    [[nodiscard]] constexpr bool operator==(const HandleGrant& other) const noexcept {
        return source_handle == other.source_handle && granted_handle == other.granted_handle &&
               parent_dot == other.parent_dot && child_dot == other.child_dot &&
               can_read == other.can_read && can_write == other.can_write &&
               revoked == other.revoked;
    }
};

// ============================================================================
// Sandbox Struct
// ============================================================================

/// @brief Sandbox configuration for a single Dot
///
/// Contains all isolation state for a Dot including its HandleTable,
/// syscall whitelist, parent relationship, and active grants.
struct Sandbox {
    /// Unique identifier for this Dot
    DotId dot_id;

    /// Isolation level for this sandbox
    IsolationLevel level;

    /// Per-Dot HandleTable (nullptr for IsolationLevel::None)
    ///
    /// When isolation is enabled (Basic or Strict), each Dot gets its
    /// own HandleTable namespace, preventing direct cross-Dot access.
    std::unique_ptr<HandleTable> handle_table;

    /// Syscall whitelist (used in Strict mode)
    SyscallWhitelist syscall_whitelist;

    /// Parent Dot ID (0 for root/orphan)
    DotId parent_id;

    /// List of child Dot IDs
    std::vector<DotId> children;

    /// Active grants from parent to this Dot
    std::vector<HandleGrant> incoming_grants;

    /// Active grants from this Dot to children
    std::vector<HandleGrant> outgoing_grants;

    /// @brief Check if this is a root sandbox (no parent)
    [[nodiscard]] constexpr bool is_root() const noexcept { return parent_id == INVALID_DOT_ID; }

    /// @brief Check if memory isolation is active
    [[nodiscard]] bool has_isolated_memory() const noexcept { return handle_table != nullptr; }
};

// ============================================================================
// IsolationManager Class
// ============================================================================

/// @brief Manager for Dot isolation boundaries and handle grants
///
/// IsolationManager maintains sandboxes for each Dot and enforces
/// isolation boundaries based on the configured isolation level.
/// It supports:
/// - Per-Dot HandleTable namespacing (Basic and Strict levels)
/// - Explicit handle grants for controlled memory sharing
/// - Syscall whitelist enforcement (Strict level)
/// - Network/Filesystem capability denial (Strict level)
///
/// Thread Safety: NOT thread-safe. Use one instance per VM or
/// protect with external synchronization.
///
/// @par Usage Example
/// @code
/// IsolationManager manager;
///
/// // Create parent sandbox
/// manager.create_sandbox(1, IsolationLevel::Basic);
///
/// // Create child sandbox
/// manager.create_sandbox(2, IsolationLevel::Basic, 1);
///
/// // Grant handle access from parent to child
/// auto granted = manager.grant_handle(1, 2, parent_handle, true, false);
///
/// // Enforce boundary on cross-Dot access
/// auto result = manager.enforce_boundary(2, 1, granted.value(), AccessType::Read);
/// @endcode
class IsolationManager {
public:
    /// @brief Result type for operations that can fail
    template <typename T>
    using Result = std::expected<T, IsolationError>;

    // ========== Construction ==========

    /// @brief Default constructor
    IsolationManager() noexcept = default;

    // Non-copyable, movable
    IsolationManager(const IsolationManager&) = delete;
    IsolationManager& operator=(const IsolationManager&) = delete;
    IsolationManager(IsolationManager&&) noexcept = default;
    IsolationManager& operator=(IsolationManager&&) noexcept = default;

    ~IsolationManager() = default;

    // ========== Sandbox Lifecycle ==========

    /// @brief Create a new sandbox for a Dot
    ///
    /// @param dot_id Unique identifier for the new Dot
    /// @param level Isolation level to apply
    /// @param parent Parent Dot ID (0 for root)
    /// @return Success or error code
    ///
    /// @par Side Effects
    /// - Creates HandleTable if level >= Basic
    /// - Registers parent-child relationship if parent specified
    /// - Initializes syscall whitelist based on level
    [[nodiscard]] Result<void> create_sandbox(DotId dot_id, IsolationLevel level,
                                              DotId parent = INVALID_DOT_ID) noexcept;

    /// @brief Destroy a sandbox and release its resources
    ///
    /// @param dot_id The Dot to destroy
    /// @return Success or error code
    ///
    /// @note Fails if the Dot has active children. Children must be
    /// destroyed first to maintain hierarchy integrity.
    [[nodiscard]] IsolationError destroy_sandbox(DotId dot_id) noexcept;

    // ========== Boundary Enforcement ==========

    /// @brief Enforce isolation boundary on cross-Dot access
    ///
    /// Validates that source_dot can access target_dot's handle with
    /// the specified access type. For same-Dot access, always succeeds
    /// (after handle validation). For cross-Dot access, requires a
    /// valid, non-revoked grant.
    ///
    /// @param source_dot Dot attempting the access
    /// @param target_dot Dot owning the target handle
    /// @param handle Handle being accessed
    /// @param access Type of access (Read or Write)
    /// @return Success or access denial error
    [[nodiscard]] IsolationError enforce_boundary(DotId source_dot, DotId target_dot, Handle handle,
                                                  AccessType access) const noexcept;

    /// @brief Validate a syscall against the Dot's whitelist
    ///
    /// In Strict isolation mode, validates that the syscall is in
    /// the Dot's whitelist. In other modes, always succeeds.
    ///
    /// @param dot_id Dot attempting the syscall
    /// @param syscall Syscall ID to validate
    /// @return Success or SyscallDenied
    [[nodiscard]] IsolationError validate_syscall(DotId dot_id, SyscallId syscall) const noexcept;

    // ========== Handle Grants ==========

    /// @brief Grant handle access from parent to child Dot
    ///
    /// Creates a mapping from a handle in the parent's namespace to
    /// a new handle in the child's namespace with specified permissions.
    ///
    /// @param parent Parent Dot granting access
    /// @param child Child Dot receiving access
    /// @param handle Handle in parent's namespace to grant
    /// @param can_read Allow read access
    /// @param can_write Allow write access
    /// @return Granted handle in child's namespace, or error
    ///
    /// @par Requirements
    /// - Parent must own the handle
    /// - Child must be a direct child of parent
    /// - At least one of can_read/can_write must be true
    [[nodiscard]] Result<Handle> grant_handle(DotId parent, DotId child, Handle handle,
                                              bool can_read, bool can_write) noexcept;

    /// @brief Revoke a previously granted handle
    ///
    /// @param parent Parent Dot that created the grant
    /// @param child Child Dot with the grant
    /// @param handle Handle in child's namespace to revoke
    /// @return Success or error code
    [[nodiscard]] IsolationError revoke_handle(DotId parent, DotId child, Handle handle) noexcept;

    // ========== Capability Integration ==========

    /// @brief Check if Dot can access network
    ///
    /// @param dot_id Dot to check
    /// @return false if Strict isolation, true otherwise
    [[nodiscard]] bool can_access_network(DotId dot_id) const noexcept;

    /// @brief Check if Dot can access filesystem
    ///
    /// @param dot_id Dot to check
    /// @return false if Strict isolation, true otherwise
    [[nodiscard]] bool can_access_filesystem(DotId dot_id) const noexcept;

    // ========== Memory Manager Integration ==========

    /// @brief Get the HandleTable for a Dot
    ///
    /// @param dot_id Dot to query
    /// @return Pointer to HandleTable, or nullptr if not isolated
    [[nodiscard]] HandleTable* get_handle_table(DotId dot_id) noexcept;

    /// @brief Get the HandleTable for a Dot (const)
    [[nodiscard]] const HandleTable* get_handle_table(DotId dot_id) const noexcept;

    // ========== Query Methods ==========

    /// @brief Check if a Dot exists in the manager
    [[nodiscard]] bool has_sandbox(DotId dot_id) const noexcept;

    /// @brief Get the isolation level for a Dot
    [[nodiscard]] Result<IsolationLevel> get_isolation_level(DotId dot_id) const noexcept;

    /// @brief Get the parent Dot ID
    [[nodiscard]] Result<DotId> get_parent(DotId dot_id) const noexcept;

    /// @brief Get child Dot IDs
    [[nodiscard]] Result<std::vector<DotId>> get_children(DotId dot_id) const noexcept;

    /// @brief Get count of active sandboxes
    [[nodiscard]] std::size_t sandbox_count() const noexcept;

    /// @brief Get the syscall whitelist for a Dot
    [[nodiscard]] const SyscallWhitelist* get_syscall_whitelist(DotId dot_id) const noexcept;

    /// @brief Modify the syscall whitelist for a Dot
    [[nodiscard]] SyscallWhitelist* get_syscall_whitelist(DotId dot_id) noexcept;

private:
    /// @brief Find a grant from source to target for a handle
    [[nodiscard]] const HandleGrant* find_grant(DotId source_dot, DotId target_dot,
                                                Handle handle) const noexcept;

    /// @brief Find a mutable grant
    [[nodiscard]] HandleGrant* find_grant_mut(DotId source_dot, DotId target_dot,
                                              Handle handle) noexcept;

    /// @brief Get sandbox by ID (internal)
    [[nodiscard]] const Sandbox* get_sandbox(DotId dot_id) const noexcept;
    [[nodiscard]] Sandbox* get_sandbox_mut(DotId dot_id) noexcept;

    /// @brief Generate next handle for a sandbox
    [[nodiscard]] Handle generate_child_handle(Sandbox& sandbox) noexcept;

    std::unordered_map<DotId, Sandbox> sandboxes_;
    std::uint32_t next_handle_index_{1};  // Start at 1, 0 is invalid
};

}  // namespace dotvm::core::security

// ============================================================================
// std::formatter specializations
// ============================================================================

template <>
struct std::formatter<dotvm::core::security::AccessType> : std::formatter<std::string_view> {
    auto format(dotvm::core::security::AccessType e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};

template <>
struct std::formatter<dotvm::core::security::IsolationError> : std::formatter<std::string_view> {
    auto format(dotvm::core::security::IsolationError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
