#pragma once

/// @file audit_event.hpp
/// @brief SEC-006 Enhanced Audit Event types and structures
///
/// This header defines the enhanced audit event structure with severity levels,
/// Dot/capability identification, and extensible metadata for the DotVM
/// security module.
///
/// @note This header provides enhanced versions of audit types for SEC-006.
/// The base AuditEventType enum is defined in security_context.hpp and
/// re-exported here for convenience.

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "dotvm/core/security/security_context.hpp"

namespace dotvm::core::security {

// ============================================================================
// AuditSeverity Enum (SEC-006)
// ============================================================================

/// @brief Severity levels for audit events
///
/// Severity levels follow standard logging conventions and can be used
/// for filtering and alerting.
enum class AuditSeverity : std::uint8_t {
    Debug = 0,    ///< Verbose debugging information
    Info = 1,     ///< Normal operational events
    Warning = 2,  ///< Potential issues worth noting
    Error = 3,    ///< Operation failures
    Critical = 4  ///< Security-critical events requiring immediate attention
};

/// @brief Convert AuditSeverity to human-readable string
[[nodiscard]] constexpr const char* to_string(AuditSeverity severity) noexcept {
    switch (severity) {
        case AuditSeverity::Debug:
            return "Debug";
        case AuditSeverity::Info:
            return "Info";
        case AuditSeverity::Warning:
            return "Warning";
        case AuditSeverity::Error:
            return "Error";
        case AuditSeverity::Critical:
            return "Critical";
    }
    return "Unknown";
}

// ============================================================================
// Helper: Get default severity for an event type
// ============================================================================

/// @brief Get the default severity for an event type
[[nodiscard]] constexpr AuditSeverity default_severity(AuditEventType type) noexcept {
    switch (type) {
        // Critical severity
        case AuditEventType::SecurityViolation:
            return AuditSeverity::Critical;

        // Error severity
        case AuditEventType::PermissionDenied:
        case AuditEventType::AllocationDenied:
        case AuditEventType::InstructionLimitHit:
        case AuditEventType::StackDepthLimitHit:
        case AuditEventType::TimeLimitHit:
        case AuditEventType::OpcodeDenied:
        case AuditEventType::DotFailed:
        case AuditEventType::MemoryLimitExceeded:
            return AuditSeverity::Error;

        // Warning severity
        case AuditEventType::CapabilityRevoked:
            return AuditSeverity::Warning;

        // Info severity (default)
        case AuditEventType::PermissionGranted:
        case AuditEventType::AllocationAttempt:
        case AuditEventType::DeallocationAttempt:
        case AuditEventType::ContextCreated:
        case AuditEventType::ContextDestroyed:
        case AuditEventType::ContextReset:
        case AuditEventType::DotStarted:
        case AuditEventType::DotCompleted:
        case AuditEventType::CapabilityCreated:
            return AuditSeverity::Info;
    }
    return AuditSeverity::Info;
}

// ============================================================================
// Identifier Types (SEC-006)
// ============================================================================

/// @brief Identifier for a Dot entity (0 = no associated Dot)
using DotId = std::uint64_t;

/// @brief Identifier for a capability (0 = no associated capability)
using CapabilityId = std::uint64_t;

}  // namespace dotvm::core::security

// ============================================================================
// SEC-006 Enhanced AuditEvent (in separate namespace to avoid collision)
// ============================================================================

namespace dotvm::core::security::audit {

using dotvm::core::security::AuditEventType;
using dotvm::core::security::AuditSeverity;
using dotvm::core::security::CapabilityId;
using dotvm::core::security::DotId;
using dotvm::core::security::Permission;

/// @brief A security audit event record (enhanced for SEC-006)
///
/// This struct captures all relevant information about a security event,
/// including context for correlation and extensible metadata.
///
/// Thread Safety: Individual events are immutable after construction.
/// This struct owns all string data (uses std::string, not string_view)
/// to ensure safety with async logging.
///
/// @par Usage Example
/// @code
/// auto event = AuditEvent::now(AuditEventType::DotStarted)
///     .with_dot(dot_id)
///     .with_message("Starting execution")
///     .with_metadata("bytecode_size", "1024");
/// @endcode
struct AuditEvent {
    // === Core fields (backward compatible) ===

    /// Type of event
    AuditEventType type{AuditEventType::ContextCreated};

    /// Timestamp when event occurred
    std::chrono::steady_clock::time_point timestamp = {};

    /// Associated permission (for permission events)
    Permission permission{Permission::None};

    /// Associated value (size for allocations, count for instructions, etc.)
    std::uint64_t value{0};

    // === New fields (SEC-006) ===

    /// Severity level for filtering and alerting
    AuditSeverity severity{AuditSeverity::Info};

    /// Associated Dot identifier (0 = no associated Dot)
    DotId dot_id{0};

    /// Associated capability identifier (0 = no associated capability)
    CapabilityId capability_id{0};

    /// Human-readable message describing the event
    std::string message = "";

    /// Extensible key-value metadata pairs
    std::vector<std::pair<std::string, std::string>> metadata = {};

    // === Factory Methods ===

    /// @brief Create an event with current timestamp
    ///
    /// @param type The event type
    /// @param sev Optional severity (defaults to type's default severity)
    /// @param perm Optional associated permission
    /// @param val Optional associated value
    /// @return A new AuditEvent with current timestamp
    [[nodiscard]] static AuditEvent now(AuditEventType type,
                                        AuditSeverity sev = AuditSeverity::Info,
                                        Permission perm = Permission::None,
                                        std::uint64_t val = 0) noexcept {
        return AuditEvent{
            .type = type,
            .timestamp = std::chrono::steady_clock::now(),
            .permission = perm,
            .value = val,
            .severity = sev,
        };
    }

    /// @brief Create an event with current timestamp using type's default
    /// severity
    ///
    /// @param type The event type
    /// @param perm Optional associated permission
    /// @param val Optional associated value
    /// @return A new AuditEvent with current timestamp and default severity
    [[nodiscard]] static AuditEvent now_default(AuditEventType type,
                                                Permission perm = Permission::None,
                                                std::uint64_t val = 0) noexcept {
        return AuditEvent{
            .type = type,
            .timestamp = std::chrono::steady_clock::now(),
            .permission = perm,
            .value = val,
            .severity = default_severity(type),
        };
    }

    /// @brief Create an event for a specific Dot
    ///
    /// @param dot The Dot identifier
    /// @param type The event type
    /// @param sev Optional severity
    /// @param msg Optional message
    /// @return A new AuditEvent with Dot context
    [[nodiscard]] static AuditEvent for_dot(DotId dot, AuditEventType type, AuditSeverity sev,
                                            std::string msg = "") noexcept {
        return AuditEvent{
            .type = type,
            .timestamp = std::chrono::steady_clock::now(),
            .permission = Permission::None,
            .value = 0,
            .severity = sev,
            .dot_id = dot,
            .capability_id = 0,
            .message = std::move(msg),
        };
    }

    /// @brief Create an event for a capability operation
    ///
    /// @param cap_id The capability identifier
    /// @param type The event type
    /// @param sev Optional severity
    /// @param msg Optional message
    /// @return A new AuditEvent with capability context
    [[nodiscard]] static AuditEvent for_capability(CapabilityId cap_id, AuditEventType type,
                                                   AuditSeverity sev = AuditSeverity::Info,
                                                   std::string msg = "") noexcept {
        return AuditEvent{
            .type = type,
            .timestamp = std::chrono::steady_clock::now(),
            .permission = Permission::None,
            .value = 0,
            .severity = sev,
            .dot_id = 0,
            .capability_id = cap_id,
            .message = std::move(msg),
        };
    }

    // === Fluent API for building events ===

    /// @brief Add a key-value metadata pair
    ///
    /// @param key The metadata key
    /// @param val The metadata value
    /// @return Reference to this event for chaining
    AuditEvent& with_metadata(std::string key, std::string val) {
        metadata.emplace_back(std::move(key), std::move(val));
        return *this;
    }

    /// @brief Set the event message
    ///
    /// @param msg The message string
    /// @return Reference to this event for chaining
    AuditEvent& with_message(std::string msg) {
        message = std::move(msg);
        return *this;
    }

    /// @brief Set the Dot identifier
    ///
    /// @param dot The Dot ID
    /// @return Reference to this event for chaining
    AuditEvent& with_dot(DotId dot) noexcept {
        dot_id = dot;
        return *this;
    }

    /// @brief Set the capability identifier
    ///
    /// @param cap The capability ID
    /// @return Reference to this event for chaining
    AuditEvent& with_capability(CapabilityId cap) noexcept {
        capability_id = cap;
        return *this;
    }

    /// @brief Set the permission
    ///
    /// @param perm The permission
    /// @return Reference to this event for chaining
    AuditEvent& with_permission(Permission perm) noexcept {
        permission = perm;
        return *this;
    }

    /// @brief Set the value
    ///
    /// @param val The value
    /// @return Reference to this event for chaining
    AuditEvent& with_value(std::uint64_t val) noexcept {
        value = val;
        return *this;
    }

    /// @brief Set the severity
    ///
    /// @param sev The severity level
    /// @return Reference to this event for chaining
    AuditEvent& with_severity(AuditSeverity sev) noexcept {
        severity = sev;
        return *this;
    }
};

}  // namespace dotvm::core::security::audit

// Re-export in main namespace for convenience
namespace dotvm::core::security {
/// @brief Enhanced AuditEvent for SEC-006 (alias to audit::AuditEvent)
/// @note For backward compatibility, the original AuditEvent remains in
/// security_context.hpp. Use this type for new SEC-006 code.
using EnhancedAuditEvent = audit::AuditEvent;
}  // namespace dotvm::core::security
