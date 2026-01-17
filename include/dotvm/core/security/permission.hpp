#pragma once

/// @file permission.hpp
/// @brief SEC-002 Permission Model for Dot-level access control
///
/// This header defines the Permission enum and PermissionSet class for
/// controlling what operations a Dot entity (actor/agent) can perform.
/// This is separate from SEC-001 (capability-based security) which handles
/// resource management with hierarchy, limits, and expiration.

#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace dotvm::core::security {

// ============================================================================
// Permission Enum (SEC-002)
// ============================================================================

/// @brief Bitfield enum for Dot-level permissions
///
/// Each permission is a single bit that can be combined using bitwise
/// operators. The permission set controls what operations a Dot entity
/// is authorized to perform.
///
/// @note Uses explicit bit positions as specified in SEC-002 requirements.
enum class Permission : std::uint32_t {
    /// No permissions granted
    None = 0,

    /// Execute bytecode instructions
    Execute = 1U << 0,

    /// Read from memory regions
    ReadMemory = 1U << 1,

    /// Write to memory regions
    WriteMemory = 1U << 2,

    /// Allocate new memory
    Allocate = 1U << 3,

    /// Read VM state (registers, flags, etc.)
    ReadState = 1U << 4,

    /// Modify VM state
    WriteState = 1U << 5,

    /// Spawn child Dot entities
    SpawnDot = 1U << 6,

    /// Send inter-Dot messages
    SendMessage = 1U << 7,

    /// Perform cryptographic operations
    Crypto = 1U << 8,

    /// Access system calls
    SystemCall = 1U << 9,

    /// Debug operations (breakpoints, stepping, etc.)
    Debug = 1U << 10,

    /// All permissions granted
    Full = 0xFFFF'FFFFU
};

// ============================================================================
// Bitwise Operators for Permission
// ============================================================================

/// @brief Combine two permission sets (union)
[[nodiscard]] constexpr Permission operator|(Permission lhs, Permission rhs) noexcept {
    return static_cast<Permission>(static_cast<std::uint32_t>(lhs) |
                                   static_cast<std::uint32_t>(rhs));
}

/// @brief Intersect two permission sets
[[nodiscard]] constexpr Permission operator&(Permission lhs, Permission rhs) noexcept {
    return static_cast<Permission>(static_cast<std::uint32_t>(lhs) &
                                   static_cast<std::uint32_t>(rhs));
}

/// @brief Symmetric difference of two permission sets
[[nodiscard]] constexpr Permission operator^(Permission lhs, Permission rhs) noexcept {
    return static_cast<Permission>(static_cast<std::uint32_t>(lhs) ^
                                   static_cast<std::uint32_t>(rhs));
}

/// @brief Complement of a permission set
[[nodiscard]] constexpr Permission operator~(Permission perm) noexcept {
    return static_cast<Permission>(~static_cast<std::uint32_t>(perm));
}

/// @brief Compound union assignment
constexpr Permission& operator|=(Permission& lhs, Permission rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// @brief Compound intersection assignment
constexpr Permission& operator&=(Permission& lhs, Permission rhs) noexcept {
    lhs = lhs & rhs;
    return lhs;
}

/// @brief Compound symmetric difference assignment
constexpr Permission& operator^=(Permission& lhs, Permission rhs) noexcept {
    lhs = lhs ^ rhs;
    return lhs;
}

// ============================================================================
// Combo Permissions (SEC-002 Requirements)
// ============================================================================

/// @brief Read-only permission combination
///
/// Allows execution, reading memory, and reading VM state.
/// Does not allow modification of any kind.
inline constexpr Permission kReadOnly =
    Permission::Execute | Permission::ReadMemory | Permission::ReadState;

/// @brief Read-write permission combination
///
/// Includes all read-only permissions plus write access to memory and state.
inline constexpr Permission kReadWrite =
    kReadOnly | Permission::WriteMemory | Permission::WriteState;

// ============================================================================
// Permission Helper Functions
// ============================================================================

/// @brief Check if a permission set contains all required permissions
///
/// @param perms The permission set to check
/// @param required The permission(s) that must be present
/// @return true if all required permissions are present
[[nodiscard]] constexpr bool has_permission(Permission perms, Permission required) noexcept {
    return (perms & required) == required;
}

/// @brief Check if child permissions are a subset of parent permissions
///
/// @param parent The parent permission set
/// @param child The child permission set to validate
/// @return true if child is a subset of parent
[[nodiscard]] constexpr bool is_subset(Permission parent, Permission child) noexcept {
    return (child & parent) == child;
}

/// @brief Convert a permission to human-readable string
///
/// @param perm The permission to convert
/// @return String representation of the permission
[[nodiscard]] std::string to_string(Permission perm);

// ============================================================================
// PermissionDeniedException
// ============================================================================

/// @brief Exception thrown when a required permission is not present
///
/// This is a host-level exception (C++ exception) thrown by
/// PermissionSet::require() when the requested permissions are not available.
/// It captures the required and actual permissions, along with context and
/// source location for debugging.
///
/// Thread Safety: Immutable after construction.
class PermissionDeniedException : public std::exception {
public:
    /// @brief Construct a permission denied exception
    ///
    /// @param required The permission(s) that were required
    /// @param actual The permission(s) that were actually present
    /// @param context Optional context string (operation name)
    /// @param location Source location where the exception was thrown
    PermissionDeniedException(Permission required, Permission actual, std::string_view context = "",
                              std::source_location location = std::source_location::current());

    /// @brief Get the required permission(s)
    [[nodiscard]] Permission required() const noexcept { return required_; }

    /// @brief Get the actual permission(s) that were present
    [[nodiscard]] Permission actual() const noexcept { return actual_; }

    /// @brief Get the missing permission(s) (required but not present)
    [[nodiscard]] Permission missing() const noexcept {
        return static_cast<Permission>(static_cast<std::uint32_t>(required_) &
                                       ~static_cast<std::uint32_t>(actual_));
    }

    /// @brief Get the context string
    [[nodiscard]] std::string_view context() const noexcept { return context_; }

    /// @brief Get the source location where the exception was thrown
    [[nodiscard]] const std::source_location& location() const noexcept { return location_; }

    /// @brief Get exception message
    [[nodiscard]] const char* what() const noexcept override { return message_.c_str(); }

private:
    Permission required_;
    Permission actual_;
    std::string context_;
    std::source_location location_;
    std::string message_;

    /// @brief Build the exception message
    void build_message();
};

// ============================================================================
// PermissionSet Class
// ============================================================================

/// @brief A set of permissions with grant/revoke/require semantics
///
/// PermissionSet wraps the Permission enum and provides a convenient
/// interface for permission management. It is designed for use in contexts
/// where permission checking is needed for Dot entities.
///
/// Thread Safety: NOT thread-safe. Use external synchronization or create
/// per-thread instances.
///
/// @code
/// PermissionSet perms;
/// perms.grant(Permission::Execute)
///      .grant(Permission::ReadMemory);
///
/// if (perms.has_permission(Permission::Execute)) {
///     // Execute bytecode
/// }
///
/// // Throws PermissionDeniedException if WriteMemory not present
/// perms.require(Permission::WriteMemory, "store_operation");
/// @endcode
class PermissionSet {
public:
    // ========== Construction ==========

    /// @brief Construct with no permissions
    constexpr PermissionSet() noexcept = default;

    /// @brief Construct with initial permissions
    ///
    /// @param perms Initial permission set
    constexpr explicit PermissionSet(Permission perms) noexcept : permissions_(perms) {}

    /// @brief Construct from raw bits (for interop)
    ///
    /// @param bits Raw permission bits
    [[nodiscard]] static constexpr PermissionSet from_bits(std::uint32_t bits) noexcept {
        return PermissionSet{static_cast<Permission>(bits)};
    }

    // ========== Preset Constructors ==========

    /// @brief Create a read-only permission set
    [[nodiscard]] static constexpr PermissionSet read_only() noexcept {
        return PermissionSet{kReadOnly};
    }

    /// @brief Create a read-write permission set
    [[nodiscard]] static constexpr PermissionSet read_write() noexcept {
        return PermissionSet{kReadWrite};
    }

    /// @brief Create a full permission set (all permissions)
    [[nodiscard]] static constexpr PermissionSet full() noexcept {
        return PermissionSet{Permission::Full};
    }

    /// @brief Create an empty permission set (no permissions)
    [[nodiscard]] static constexpr PermissionSet none() noexcept {
        return PermissionSet{Permission::None};
    }

    // ========== Query Methods ==========

    /// @brief Check if the set contains a specific permission
    ///
    /// @param perm The permission(s) to check
    /// @return true if all requested permissions are present
    [[nodiscard]] constexpr bool has_permission(Permission perm) const noexcept {
        return security::has_permission(permissions_, perm);
    }

    /// @brief Check if the set contains any of the specified permissions
    ///
    /// @param perm The permission(s) to check
    /// @return true if any of the requested permissions are present
    [[nodiscard]] constexpr bool has_any(Permission perm) const noexcept {
        return (permissions_ & perm) != Permission::None;
    }

    /// @brief Check if the set is empty (no permissions)
    [[nodiscard]] constexpr bool empty() const noexcept { return permissions_ == Permission::None; }

    /// @brief Get the raw permission value
    [[nodiscard]] constexpr Permission value() const noexcept { return permissions_; }

    /// @brief Get the raw bits
    [[nodiscard]] constexpr std::uint32_t bits() const noexcept {
        return static_cast<std::uint32_t>(permissions_);
    }

    // ========== Requirement Methods ==========

    /// @brief Require that the set contains specific permissions
    ///
    /// @param perm The permission(s) required
    /// @param context Optional context for error messages
    /// @throws PermissionDeniedException if permissions are not present
    void require(Permission perm, std::string_view context = "",
                 std::source_location location = std::source_location::current()) const;

    /// @brief Check if the set satisfies requirements (non-throwing)
    ///
    /// @param perm The permission(s) required
    /// @return true if all required permissions are present
    [[nodiscard]] constexpr bool satisfies(Permission perm) const noexcept {
        return has_permission(perm);
    }

    // ========== Modification Methods ==========

    /// @brief Grant additional permissions
    ///
    /// @param perm The permission(s) to grant
    /// @return Reference to this for chaining
    constexpr PermissionSet& grant(Permission perm) noexcept {
        permissions_ |= perm;
        return *this;
    }

    /// @brief Revoke permissions
    ///
    /// @param perm The permission(s) to revoke
    /// @return Reference to this for chaining
    constexpr PermissionSet& revoke(Permission perm) noexcept {
        permissions_ = static_cast<Permission>(static_cast<std::uint32_t>(permissions_) &
                                               ~static_cast<std::uint32_t>(perm));
        return *this;
    }

    /// @brief Clear all permissions
    ///
    /// @return Reference to this for chaining
    constexpr PermissionSet& clear() noexcept {
        permissions_ = Permission::None;
        return *this;
    }

    /// @brief Set permissions to specific value
    ///
    /// @param perm The permission set to assign
    /// @return Reference to this for chaining
    constexpr PermissionSet& set(Permission perm) noexcept {
        permissions_ = perm;
        return *this;
    }

    // ========== Set Operations ==========

    /// @brief Compute the union of two permission sets
    [[nodiscard]] constexpr PermissionSet operator|(PermissionSet other) const noexcept {
        return PermissionSet{permissions_ | other.permissions_};
    }

    /// @brief Compute the intersection of two permission sets
    [[nodiscard]] constexpr PermissionSet operator&(PermissionSet other) const noexcept {
        return PermissionSet{permissions_ & other.permissions_};
    }

    /// @brief Compute the symmetric difference of two permission sets
    [[nodiscard]] constexpr PermissionSet operator^(PermissionSet other) const noexcept {
        return PermissionSet{permissions_ ^ other.permissions_};
    }

    /// @brief Compute the complement of this permission set
    [[nodiscard]] constexpr PermissionSet operator~() const noexcept {
        return PermissionSet{~permissions_};
    }

    /// @brief Union with another set
    constexpr PermissionSet& operator|=(PermissionSet other) noexcept {
        permissions_ |= other.permissions_;
        return *this;
    }

    /// @brief Intersection with another set
    constexpr PermissionSet& operator&=(PermissionSet other) noexcept {
        permissions_ &= other.permissions_;
        return *this;
    }

    /// @brief Symmetric difference with another set
    constexpr PermissionSet& operator^=(PermissionSet other) noexcept {
        permissions_ ^= other.permissions_;
        return *this;
    }

    // ========== Comparison ==========

    /// @brief Equality comparison
    [[nodiscard]] constexpr bool operator==(const PermissionSet&) const noexcept = default;

    /// @brief Check if this set is a subset of another
    [[nodiscard]] constexpr bool is_subset_of(PermissionSet other) const noexcept {
        return is_subset(other.permissions_, permissions_);
    }

    /// @brief Check if this set is a superset of another
    [[nodiscard]] constexpr bool is_superset_of(PermissionSet other) const noexcept {
        return is_subset(permissions_, other.permissions_);
    }

    // ========== String Conversion ==========

    /// @brief Convert to human-readable string
    [[nodiscard]] std::string to_string() const { return security::to_string(permissions_); }

private:
    Permission permissions_{Permission::None};
};

}  // namespace dotvm::core::security
