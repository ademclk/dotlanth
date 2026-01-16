#pragma once

/// @file capability_manager.hpp
/// @brief Capability manager for hierarchical permission management (SEC-001)
///
/// This header defines the CapabilityManager class which manages capability
/// lifecycle including creation, derivation, revocation, and validation.

#include <dotvm/core/capabilities/capability.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declaration
namespace dotvm::core {
struct SecurityStats;
}

namespace dotvm::core::capabilities {

// ============================================================================
// Capability Error Codes
// ============================================================================

/// Error codes for capability operations
///
/// These codes indicate why a capability operation failed.
enum class CapabilityError : std::uint8_t {
    /// Operation succeeded
    Success = 0,

    /// Handle is invalid (null or unknown ID)
    InvalidHandle,

    /// Capability has expired
    Expired,

    /// Capability has been revoked
    Revoked,

    /// Operation requires permission that capability doesn't have
    InsufficientPermission,

    /// Child permissions are not a subset of parent permissions
    PermissionNotSubset,

    /// Child limits exceed parent limits
    LimitsNotWithin,

    /// Parent doesn't have Derive permission
    DerivationNotAllowed,

    /// Capability was already revoked
    AlreadyRevoked,

    /// Parent capability is invalid
    InvalidParent,

    /// Generation counter mismatch (use-after-revoke)
    GenerationMismatch
};

/// Convert CapabilityError to string
[[nodiscard]] constexpr std::string_view to_string(CapabilityError err) noexcept {
    switch (err) {
        case CapabilityError::Success: return "Success";
        case CapabilityError::InvalidHandle: return "InvalidHandle";
        case CapabilityError::Expired: return "Expired";
        case CapabilityError::Revoked: return "Revoked";
        case CapabilityError::InsufficientPermission: return "InsufficientPermission";
        case CapabilityError::PermissionNotSubset: return "PermissionNotSubset";
        case CapabilityError::LimitsNotWithin: return "LimitsNotWithin";
        case CapabilityError::DerivationNotAllowed: return "DerivationNotAllowed";
        case CapabilityError::AlreadyRevoked: return "AlreadyRevoked";
        case CapabilityError::InvalidParent: return "InvalidParent";
        case CapabilityError::GenerationMismatch: return "GenerationMismatch";
    }
    return "Unknown";
}

// ============================================================================
// CapabilityManager Class
// ============================================================================

/// Manages capability lifecycle and validation
///
/// The CapabilityManager is responsible for:
/// - Creating root capabilities
/// - Deriving child capabilities from parents
/// - Revoking capabilities (including cascading to children)
/// - Validating capability handles
/// - Permission and limit checking
///
/// Thread Safety: All public methods are thread-safe using reader-writer locks.
/// Multiple readers can access simultaneously; writers have exclusive access.
///
/// @note CapabilityManager is non-copyable but movable.
class CapabilityManager {
public:
    /// Result type for operations that may fail
    template<typename T>
    using Result = std::expected<T, CapabilityError>;

    // ========== Constructors ==========

    /// Construct a CapabilityManager without security stats
    CapabilityManager() noexcept = default;

    /// Construct a CapabilityManager with security stats
    ///
    /// @param stats Pointer to SecurityStats for auditing (may be nullptr)
    explicit CapabilityManager(SecurityStats* stats) noexcept;

    // Non-copyable
    CapabilityManager(const CapabilityManager&) = delete;
    CapabilityManager& operator=(const CapabilityManager&) = delete;

    // Movable
    CapabilityManager(CapabilityManager&& other) noexcept;
    CapabilityManager& operator=(CapabilityManager&& other) noexcept;

    ~CapabilityManager() = default;

    // ========== Root Capability Creation ==========

    /// Create a root capability with full control
    ///
    /// Root capabilities have no parent and serve as the origin of
    /// capability hierarchies. They should be created sparingly.
    ///
    /// @param name Human-readable name for auditing
    /// @param perms Permission set for this capability
    /// @param limits Resource limits
    /// @param expires Expiration time (default: no expiration)
    /// @return Handle to the new capability
    [[nodiscard]] CapabilityHandle create_root(
        std::string name,
        Permission perms,
        CapabilityLimits limits,
        TimePoint expires = NO_EXPIRATION) noexcept;

    // ========== Capability Derivation ==========

    /// Derive a child capability from a parent
    ///
    /// Child capabilities must:
    /// - Have permissions that are a subset of parent's permissions
    /// - Have limits that are within parent's limits
    /// - Parent must have Derive permission
    ///
    /// @param parent Handle to the parent capability
    /// @param name Human-readable name for auditing
    /// @param perms Permission set (must be subset of parent)
    /// @param limits Resource limits (must be within parent)
    /// @param expires Expiration time (must not exceed parent's expiration)
    /// @return Handle to the new capability, or error
    [[nodiscard]] Result<CapabilityHandle> derive(
        CapabilityHandle parent,
        std::string name,
        Permission perms,
        CapabilityLimits limits,
        TimePoint expires = NO_EXPIRATION) noexcept;

    // ========== Capability Revocation ==========

    /// Revoke a capability and all its descendants
    ///
    /// Revocation is recursive: all capabilities derived from the revoked
    /// capability are also revoked. This is irreversible.
    ///
    /// @param handle Handle to the capability to revoke
    /// @return Success or error code
    [[nodiscard]] CapabilityError revoke(CapabilityHandle handle) noexcept;

    // ========== Capability Validation ==========

    /// Check if a capability handle is valid
    ///
    /// A capability is valid if:
    /// - The handle references an existing capability
    /// - The generation matches (not use-after-revoke)
    /// - The capability is active (not revoked)
    /// - The capability has not expired
    ///
    /// @param handle Handle to check
    /// @return true if valid
    [[nodiscard]] bool is_valid(CapabilityHandle handle) const noexcept;

    /// Get a capability by handle (read-only)
    ///
    /// @param handle Handle to look up
    /// @return Pointer to capability, or nullptr if invalid
    [[nodiscard]] const Capability* get(CapabilityHandle handle) const noexcept;

    /// Check if a capability has required permissions
    ///
    /// @param handle Handle to check
    /// @param required Permissions required
    /// @return true if capability has all required permissions
    [[nodiscard]] bool check_permission(
        CapabilityHandle handle,
        Permission required) const noexcept;

    /// Check if operation is within capability limits
    ///
    /// @param handle Handle to check
    /// @param memory Memory usage to check (0 to skip)
    /// @param instructions Instructions executed (0 to skip)
    /// @return true if within limits
    [[nodiscard]] bool check_limits(
        CapabilityHandle handle,
        std::uint64_t memory,
        std::uint64_t instructions) const noexcept;

    // ========== Preset Capabilities ==========

    /// Create an untrusted capability (most restrictive)
    ///
    /// Permissions: Execute, MemoryRead, MemoryWrite
    /// Limits: untrusted preset (1MB, 100K instructions, 64 stack)
    ///
    /// @param name Optional name (default: "untrusted")
    /// @return Handle to the new capability
    [[nodiscard]] CapabilityHandle create_untrusted(
        std::string name = "untrusted") noexcept;

    /// Create a sandbox capability (moderate restrictions)
    ///
    /// Permissions: ExecuteBasic, MemoryAll, Derive
    /// Limits: sandbox preset (16MB, 1M instructions, 256 stack)
    ///
    /// @param name Optional name (default: "sandbox")
    /// @return Handle to the new capability
    [[nodiscard]] CapabilityHandle create_sandbox(
        std::string name = "sandbox") noexcept;

    /// Create a trusted capability (minimal restrictions)
    ///
    /// Permissions: All except BypassCfi
    /// Limits: trusted preset (256MB, 100M instructions, 4096 stack)
    ///
    /// @param name Optional name (default: "trusted")
    /// @return Handle to the new capability
    [[nodiscard]] CapabilityHandle create_trusted(
        std::string name = "trusted") noexcept;

    // ========== Statistics ==========

    /// Get count of active (non-revoked) capabilities
    [[nodiscard]] std::size_t active_count() const noexcept;

    /// Get total number of capabilities created
    [[nodiscard]] std::uint64_t total_created() const noexcept;

    /// Get total number of capabilities revoked
    [[nodiscard]] std::uint64_t total_revoked() const noexcept;

    // ========== Advanced Operations ==========

    /// Find all capabilities derived from a parent
    ///
    /// @param parent Parent capability handle
    /// @return Vector of child handles (may be empty)
    [[nodiscard]] std::vector<CapabilityHandle> get_children(
        CapabilityHandle parent) const noexcept;

    /// Set the security stats object
    ///
    /// @param stats Pointer to SecurityStats (may be nullptr)
    void set_security_stats(SecurityStats* stats) noexcept;

private:
    /// Generate the next capability ID
    [[nodiscard]] std::uint64_t next_id() noexcept;

    /// Internal: validate handle and return capability
    [[nodiscard]] const Capability* get_internal(CapabilityHandle handle) const noexcept;

    /// Internal: revoke a capability and its children (called with lock held)
    void revoke_recursive(std::uint64_t id) noexcept;

    /// Record capability creation in stats
    void record_creation() noexcept;

    /// Record capability derivation in stats
    void record_derivation() noexcept;

    /// Record capability revocation in stats
    void record_revocation() noexcept;

    /// Record permission violation in stats
    void record_permission_violation() noexcept;

    /// Record limit violation in stats
    void record_limit_violation() noexcept;

    // ========== Member Variables ==========

    /// Reader-writer lock for thread safety
    mutable std::shared_mutex mutex_;

    /// Capability storage (ID -> Capability)
    std::unordered_map<std::uint64_t, Capability> capabilities_;

    /// Parent-to-children mapping for efficient revocation
    std::unordered_map<std::uint64_t, std::unordered_set<std::uint64_t>> children_;

    /// Next capability ID (atomic for lock-free ID generation)
    std::atomic<std::uint64_t> next_id_{1};

    /// Total capabilities revoked
    std::atomic<std::uint64_t> total_revoked_{0};

    /// Optional security stats for auditing
    SecurityStats* stats_{nullptr};
};

}  // namespace dotvm::core::capabilities
