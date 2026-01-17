#pragma once

/// @file capability.hpp
/// @brief Capability-based security system core types for DotVM (SEC-001)
///
/// This header defines the Permission enum, CapabilityLimits struct,
/// Capability struct, and CapabilityHandle for implementing a hierarchical
/// capability-based security model.

#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>

namespace dotvm::core::capabilities {

// ============================================================================
// Permission Enum (Bitmask)
// ============================================================================

/// Permission flags for capability-based access control
///
/// Permissions are organized into categories:
/// - Memory (bits 0-3): Read, Write, Allocate, Deallocate
/// - Execution (bits 4-7): Execute, Call, Simd, Jit
/// - I/O (bits 8-11): IoRead, IoWrite, Network, Filesystem
/// - Security (bits 12-15): Derive, Revoke, Crypto, BypassCfi
///
/// Use bitmask operators (|, &, ^, ~) to combine permissions.
enum class Permission : std::uint32_t {
    None = 0,

    // ===== Memory Permissions (bits 0-3) =====

    /// Read from memory regions
    MemoryRead = 1U << 0,

    /// Write to memory regions
    MemoryWrite = 1U << 1,

    /// Allocate new memory
    MemoryAllocate = 1U << 2,

    /// Deallocate memory
    MemoryDeallocate = 1U << 3,

    // ===== Execution Permissions (bits 4-7) =====

    /// Execute bytecode instructions
    Execute = 1U << 4,

    /// Call functions
    Call = 1U << 5,

    /// Use SIMD operations
    Simd = 1U << 6,

    /// Use JIT compilation
    Jit = 1U << 7,

    // ===== I/O Permissions (bits 8-11) =====

    /// Read from I/O channels
    IoRead = 1U << 8,

    /// Write to I/O channels
    IoWrite = 1U << 9,

    /// Network operations
    Network = 1U << 10,

    /// Filesystem operations
    Filesystem = 1U << 11,

    // ===== Security Permissions (bits 12-15) =====

    /// Derive child capabilities
    Derive = 1U << 12,

    /// Revoke capabilities
    Revoke = 1U << 13,

    /// Cryptographic operations
    Crypto = 1U << 14,

    /// Bypass CFI checks (dangerous)
    BypassCfi = 1U << 15,

    // ===== Composite Permissions =====

    /// All memory permissions
    MemoryAll = MemoryRead | MemoryWrite | MemoryAllocate | MemoryDeallocate,

    /// Basic execution permissions
    ExecuteBasic = Execute | Call,

    /// All I/O permissions
    IoAll = IoRead | IoWrite | Network | Filesystem,

    /// All permissions (use with caution)
    All = 0xFFFF'FFFFU
};

// ============================================================================
// Permission Bitmask Operators
// ============================================================================

/// Bitwise OR for combining permissions
[[nodiscard]] constexpr Permission operator|(Permission lhs, Permission rhs) noexcept {
    return static_cast<Permission>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

/// Bitwise AND for permission intersection
[[nodiscard]] constexpr Permission operator&(Permission lhs, Permission rhs) noexcept {
    return static_cast<Permission>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

/// Bitwise XOR for permission difference
[[nodiscard]] constexpr Permission operator^(Permission lhs, Permission rhs) noexcept {
    return static_cast<Permission>(
        static_cast<std::uint32_t>(lhs) ^ static_cast<std::uint32_t>(rhs));
}

/// Bitwise NOT for permission complement
[[nodiscard]] constexpr Permission operator~(Permission perm) noexcept {
    return static_cast<Permission>(~static_cast<std::uint32_t>(perm));
}

/// Compound OR assignment
constexpr Permission& operator|=(Permission& lhs, Permission rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// Compound AND assignment
constexpr Permission& operator&=(Permission& lhs, Permission rhs) noexcept {
    lhs = lhs & rhs;
    return lhs;
}

/// Compound XOR assignment
constexpr Permission& operator^=(Permission& lhs, Permission rhs) noexcept {
    lhs = lhs ^ rhs;
    return lhs;
}

// ============================================================================
// Permission Helper Functions
// ============================================================================

/// Check if a permission set contains a specific permission
///
/// @param perms The permission set to check
/// @param required The permission(s) required
/// @return true if all required permissions are present
[[nodiscard]] constexpr bool has_permission(Permission perms, Permission required) noexcept {
    return (perms & required) == required;
}

/// Check if child permissions are a subset of parent permissions
///
/// @param parent The parent permission set
/// @param child The child permission set to validate
/// @return true if child is a subset of parent
[[nodiscard]] constexpr bool is_subset(Permission parent, Permission child) noexcept {
    // Child must not have any permissions that parent doesn't have
    return (child & ~parent) == Permission::None;
}

/// Convert Permission to human-readable string
///
/// @param perm The permission to convert
/// @return String representation (may be composite like "MemoryRead|Execute")
[[nodiscard]] inline std::string to_string(Permission perm) {
    if (perm == Permission::None) {
        return "None";
    }
    if (perm == Permission::All) {
        return "All";
    }

    std::string result;
    auto append = [&result](const char* name) {
        if (!result.empty()) {
            result += "|";
        }
        result += name;
    };

    auto value = static_cast<std::uint32_t>(perm);

    if ((value & static_cast<std::uint32_t>(Permission::MemoryRead)) != 0) {
        append("MemoryRead");
    }
    if ((value & static_cast<std::uint32_t>(Permission::MemoryWrite)) != 0) {
        append("MemoryWrite");
    }
    if ((value & static_cast<std::uint32_t>(Permission::MemoryAllocate)) != 0) {
        append("MemoryAllocate");
    }
    if ((value & static_cast<std::uint32_t>(Permission::MemoryDeallocate)) != 0) {
        append("MemoryDeallocate");
    }
    if ((value & static_cast<std::uint32_t>(Permission::Execute)) != 0) {
        append("Execute");
    }
    if ((value & static_cast<std::uint32_t>(Permission::Call)) != 0) {
        append("Call");
    }
    if ((value & static_cast<std::uint32_t>(Permission::Simd)) != 0) {
        append("Simd");
    }
    if ((value & static_cast<std::uint32_t>(Permission::Jit)) != 0) {
        append("Jit");
    }
    if ((value & static_cast<std::uint32_t>(Permission::IoRead)) != 0) {
        append("IoRead");
    }
    if ((value & static_cast<std::uint32_t>(Permission::IoWrite)) != 0) {
        append("IoWrite");
    }
    if ((value & static_cast<std::uint32_t>(Permission::Network)) != 0) {
        append("Network");
    }
    if ((value & static_cast<std::uint32_t>(Permission::Filesystem)) != 0) {
        append("Filesystem");
    }
    if ((value & static_cast<std::uint32_t>(Permission::Derive)) != 0) {
        append("Derive");
    }
    if ((value & static_cast<std::uint32_t>(Permission::Revoke)) != 0) {
        append("Revoke");
    }
    if ((value & static_cast<std::uint32_t>(Permission::Crypto)) != 0) {
        append("Crypto");
    }
    if ((value & static_cast<std::uint32_t>(Permission::BypassCfi)) != 0) {
        append("BypassCfi");
    }

    return result.empty() ? "Unknown" : result;
}

// ============================================================================
// CapabilityLimits Struct
// ============================================================================

/// Resource limits for capability-based sandboxing
///
/// Defines resource constraints that can be enforced during execution.
/// Child capabilities must have limits that are within their parent's limits.
///
/// Thread Safety: Immutable after construction.
struct CapabilityLimits {
    /// Maximum memory that can be allocated (bytes, 0 = unlimited)
    std::uint64_t max_memory = 0;

    /// Maximum instructions that can be executed (0 = unlimited)
    std::uint64_t max_instructions = 0;

    /// Maximum call stack depth (0 = unlimited)
    std::uint32_t max_stack_depth = 0;

    /// Maximum number of memory allocations (0 = unlimited)
    std::uint32_t max_allocations = 0;

    /// Maximum size of a single allocation (bytes, 0 = unlimited)
    std::uint64_t max_allocation_size = 0;

    /// Maximum execution time (milliseconds, 0 = unlimited)
    std::uint32_t max_execution_time_ms = 0;

    // ========== Factory Methods ==========

    /// Create unlimited limits (no restrictions)
    [[nodiscard]] static constexpr CapabilityLimits unlimited() noexcept {
        return CapabilityLimits{};
    }

    /// Create limits for untrusted code (most restrictive)
    ///
    /// - 1MB memory
    /// - 100,000 instructions
    /// - 64 stack depth
    /// - 100 allocations
    /// - 64KB max allocation size
    /// - 1 second timeout
    [[nodiscard]] static constexpr CapabilityLimits untrusted() noexcept {
        return CapabilityLimits{
            .max_memory = 1ULL * 1024 * 1024,           // 1MB
            .max_instructions = 100'000,
            .max_stack_depth = 64,
            .max_allocations = 100,
            .max_allocation_size = 64ULL * 1024,        // 64KB
            .max_execution_time_ms = 1000               // 1 second
        };
    }

    /// Create limits for sandboxed code (moderate restrictions)
    ///
    /// - 16MB memory
    /// - 1,000,000 instructions
    /// - 256 stack depth
    /// - 1,000 allocations
    /// - 1MB max allocation size
    /// - 10 second timeout
    [[nodiscard]] static constexpr CapabilityLimits sandbox() noexcept {
        return CapabilityLimits{
            .max_memory = 16ULL * 1024 * 1024,          // 16MB
            .max_instructions = 1'000'000,
            .max_stack_depth = 256,
            .max_allocations = 1'000,
            .max_allocation_size = 1ULL * 1024 * 1024,  // 1MB
            .max_execution_time_ms = 10'000             // 10 seconds
        };
    }

    /// Create limits for trusted code (minimal restrictions)
    ///
    /// - 256MB memory
    /// - 100,000,000 instructions
    /// - 4096 stack depth
    /// - 100,000 allocations
    /// - 64MB max allocation size
    /// - 5 minute timeout
    [[nodiscard]] static constexpr CapabilityLimits trusted() noexcept {
        return CapabilityLimits{
            .max_memory = 256ULL * 1024 * 1024,         // 256MB
            .max_instructions = 100'000'000,
            .max_stack_depth = 4096,
            .max_allocations = 100'000,
            .max_allocation_size = 64ULL * 1024 * 1024, // 64MB
            .max_execution_time_ms = 300'000            // 5 minutes
        };
    }

    // ========== Query Methods ==========

    /// Check if any limits are set
    [[nodiscard]] constexpr bool has_limits() const noexcept {
        return max_memory > 0 || max_instructions > 0 ||
               max_stack_depth > 0 || max_allocations > 0 ||
               max_allocation_size > 0 || max_execution_time_ms > 0;
    }

    /// Check if these limits are within another set of limits
    ///
    /// A limit of 0 means "unlimited", so:
    /// - If parent has 0 (unlimited), child can have any value
    /// - If parent has N, child must have 0 < child <= N or child == 0 (inherit unlimited)
    ///
    /// @param parent The parent limits to check against
    /// @return true if this limit set is within the parent's limits
    [[nodiscard]] constexpr bool is_within(const CapabilityLimits& parent) const noexcept {
        auto within = [](std::uint64_t child, std::uint64_t parent_val) {
            if (parent_val == 0) return true;  // Parent unlimited
            if (child == 0) return false;      // Child unlimited but parent limited
            return child <= parent_val;
        };

        auto within32 = [](std::uint32_t child, std::uint32_t parent_val) {
            if (parent_val == 0) return true;
            if (child == 0) return false;
            return child <= parent_val;
        };

        return within(max_memory, parent.max_memory) &&
               within(max_instructions, parent.max_instructions) &&
               within32(max_stack_depth, parent.max_stack_depth) &&
               within32(max_allocations, parent.max_allocations) &&
               within(max_allocation_size, parent.max_allocation_size) &&
               within32(max_execution_time_ms, parent.max_execution_time_ms);
    }

    /// Equality comparison
    constexpr bool operator==(const CapabilityLimits&) const noexcept = default;
};

// ============================================================================
// Time Point Type
// ============================================================================

/// Time point type for capability expiration
using TimePoint = std::chrono::system_clock::time_point;

/// Sentinel value indicating no expiration
inline const TimePoint NO_EXPIRATION = TimePoint::max();

// ============================================================================
// Capability Struct
// ============================================================================

/// A capability representing a set of permissions with associated limits
///
/// Capabilities form a hierarchy where child capabilities can only have
/// a subset of their parent's permissions and must have limits within
/// their parent's limits.
///
/// Thread Safety: Immutable after construction (except is_active which
/// is managed by CapabilityManager).
struct Capability {
    /// Unique identifier for this capability
    std::uint64_t id{0};

    /// Human-readable name for debugging/auditing
    std::string name;

    /// Permission bitmask
    Permission permissions{Permission::None};

    /// Resource limits
    CapabilityLimits limits{};

    /// Expiration time (NO_EXPIRATION for no expiry)
    TimePoint expires_at{NO_EXPIRATION};

    /// Parent capability ID (0 = root capability)
    std::uint64_t granted_by{0};

    /// Generation counter for use-after-revoke detection
    std::uint32_t generation{1};

    /// Whether this capability is currently active
    bool is_active{true};

    // ========== Query Methods ==========

    /// Check if capability is currently valid
    ///
    /// A capability is valid if:
    /// - It is active (not revoked)
    /// - It has not expired
    [[nodiscard]] bool is_valid() const noexcept {
        if (!is_active) return false;
        if (expires_at == NO_EXPIRATION) return true;
        return std::chrono::system_clock::now() < expires_at;
    }

    /// Check if this is a root capability (no parent)
    [[nodiscard]] bool is_root() const noexcept {
        return granted_by == 0;
    }

    /// Check if capability has a specific permission
    [[nodiscard]] bool has(Permission perm) const noexcept {
        return has_permission(permissions, perm);
    }

    /// Check if derived permissions would be valid
    ///
    /// @param child_perms Permissions for the child capability
    /// @return true if child_perms is a valid subset
    [[nodiscard]] bool can_derive(Permission child_perms) const noexcept {
        // Must have Derive permission to create children
        if (!has(Permission::Derive)) return false;
        // Child must be a subset of our permissions
        return is_subset(permissions, child_perms);
    }

    /// Check if derived limits would be valid
    ///
    /// @param child_limits Limits for the child capability
    /// @return true if child_limits is within our limits
    [[nodiscard]] bool can_derive_limits(const CapabilityLimits& child_limits) const noexcept {
        return child_limits.is_within(limits);
    }

    /// Equality comparison (by ID only)
    [[nodiscard]] bool operator==(const Capability& other) const noexcept {
        return id == other.id;
    }
};

// ============================================================================
// CapabilityHandle
// ============================================================================

/// Lightweight handle for referencing a capability
///
/// Similar to memory handles, includes a generation counter to detect
/// use-after-revoke scenarios.
struct CapabilityHandle {
    /// Capability ID
    std::uint64_t id{0};

    /// Generation counter (must match capability's generation)
    std::uint32_t generation{0};

    /// Check if this is a null handle
    [[nodiscard]] constexpr bool is_null() const noexcept {
        return id == 0;
    }

    /// Create a null handle
    [[nodiscard]] static constexpr CapabilityHandle null() noexcept {
        return CapabilityHandle{};
    }

    /// Equality comparison
    [[nodiscard]] constexpr bool operator==(const CapabilityHandle&) const noexcept = default;
};

}  // namespace dotvm::core::capabilities
