#pragma once

/// @file security_context.hpp
/// @brief SEC-003 Security Context for per-Dot execution enforcement
///
/// This header defines the SecurityContext class that bridges SEC-001
/// (CapabilityLimits) and SEC-002 (PermissionSet) for per-Dot execution
/// with resource tracking and audit logging.
///
/// Performance Target: <100 microseconds for security check latency.
///
/// @note SEC-006 Enhanced Audit Logging
/// For enhanced audit logging with severity levels, query/export capabilities,
/// and async high-throughput logging, include the following headers:
/// - dotvm/core/security/audit_event.hpp - Enhanced AuditEvent with severity
/// - dotvm/core/security/audit_logger.hpp - Enhanced AuditLogger interface
/// - dotvm/core/security/async_audit_logger.hpp - High-throughput async logger
///
/// The types defined in this file (AuditEvent, AuditEventType, AuditLogger,
/// BufferedAuditLogger, CallbackAuditLogger) are maintained for backward
/// compatibility. New code should prefer the SEC-006 enhanced types.

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/core/capabilities/capability.hpp"
#include "dotvm/core/security/permission.hpp"

namespace dotvm::core::security {

// ============================================================================
// Forward Declarations
// ============================================================================

class AuditLogger;

// ============================================================================
// ResourceUsage Struct
// ============================================================================

/// @brief Tracks current resource usage for a Dot execution
///
/// All counters start at zero and are updated as the Dot executes.
/// Thread Safety: NOT thread-safe. Use one per Dot (single-threaded).
struct ResourceUsage {
    /// Current bytes allocated by this Dot
    std::uint64_t memory_allocated{0};

    /// Total number of allocations made
    std::uint32_t allocation_count{0};

    /// Total instructions executed
    std::uint64_t instructions_executed{0};

    /// Current call stack depth
    std::uint32_t current_stack_depth{0};

    /// Maximum stack depth reached (for diagnostics)
    std::uint32_t max_stack_depth_reached{0};

    /// Execution start time
    std::chrono::steady_clock::time_point start_time = {};

    /// Reset all counters to initial state
    constexpr void reset() noexcept {
        memory_allocated = 0;
        allocation_count = 0;
        instructions_executed = 0;
        current_stack_depth = 0;
        max_stack_depth_reached = 0;
        start_time = std::chrono::steady_clock::time_point{};
    }

    /// Equality comparison
    constexpr bool operator==(const ResourceUsage&) const noexcept = default;
};

// ============================================================================
// SecurityContextError Enum
// ============================================================================

/// @brief Error codes for security context operations
enum class SecurityContextError : std::uint8_t {
    /// Operation succeeded
    Success = 0,

    /// Required permission was denied
    PermissionDenied,

    /// Total memory limit exceeded
    MemoryLimitExceeded,

    /// Allocation count limit exceeded
    AllocationCountExceeded,

    /// Single allocation size limit exceeded
    AllocationSizeExceeded,

    /// Instruction execution limit exceeded
    InstructionLimitExceeded,

    /// Stack depth limit exceeded
    StackDepthExceeded,

    /// Execution time limit exceeded
    TimeLimitExceeded,

    /// Context is invalid (internal error)
    ContextInvalid,
};

/// @brief Convert SecurityContextError to human-readable string
[[nodiscard]] constexpr const char* to_string(SecurityContextError error) noexcept {
    switch (error) {
        case SecurityContextError::Success:
            return "Success";
        case SecurityContextError::PermissionDenied:
            return "PermissionDenied";
        case SecurityContextError::MemoryLimitExceeded:
            return "MemoryLimitExceeded";
        case SecurityContextError::AllocationCountExceeded:
            return "AllocationCountExceeded";
        case SecurityContextError::AllocationSizeExceeded:
            return "AllocationSizeExceeded";
        case SecurityContextError::InstructionLimitExceeded:
            return "InstructionLimitExceeded";
        case SecurityContextError::StackDepthExceeded:
            return "StackDepthExceeded";
        case SecurityContextError::TimeLimitExceeded:
            return "TimeLimitExceeded";
        case SecurityContextError::ContextInvalid:
            return "ContextInvalid";
    }
    return "Unknown";
}

// ============================================================================
// AuditEventType Enum
// ============================================================================

/// @brief Types of security audit events
///
/// Event types are grouped by category with reserved ranges for future
/// expansion. SEC-006 adds Dot lifecycle (30-39), capability lifecycle (40-49),
/// and resource violation (50-59) events.
enum class AuditEventType : std::uint8_t {
    // Permission events (0-9)
    PermissionGranted = 0,  ///< Permission check passed
    PermissionDenied = 1,   ///< Permission check failed

    // Resource events (10-19)
    AllocationAttempt = 2,    ///< Memory allocation attempted
    AllocationDenied = 3,     ///< Allocation exceeded limits
    DeallocationAttempt = 4,  ///< Memory deallocation attempted
    InstructionLimitHit = 5,  ///< Instruction count exceeded
    StackDepthLimitHit = 6,   ///< Stack depth exceeded
    TimeLimitHit = 7,         ///< Execution time exceeded

    // Context lifecycle (8-11)
    ContextCreated = 8,    ///< SecurityContext instantiated
    ContextDestroyed = 9,  ///< SecurityContext destroyed
    ContextReset = 10,     ///< SecurityContext usage reset

    // Opcode authorization (SEC-005)
    OpcodeDenied = 20,  ///< Opcode permission denied

    // SEC-006: Dot lifecycle (30-39)
    DotStarted = 30,    ///< Dot execution started
    DotCompleted = 31,  ///< Dot execution completed normally
    DotFailed = 32,     ///< Dot execution failed with error

    // SEC-006: Capability lifecycle (40-49)
    CapabilityCreated = 40,  ///< New capability created
    CapabilityRevoked = 41,  ///< Capability revoked/invalidated

    // SEC-006: Resource violations (50-59)
    MemoryLimitExceeded = 50,  ///< Memory limit exceeded (explicit)
    SecurityViolation = 51,    ///< Generic security violation
};

/// @brief Convert AuditEventType to human-readable string
[[nodiscard]] constexpr const char* to_string(AuditEventType type) noexcept {
    switch (type) {
        case AuditEventType::PermissionGranted:
            return "PermissionGranted";
        case AuditEventType::PermissionDenied:
            return "PermissionDenied";
        case AuditEventType::AllocationAttempt:
            return "AllocationAttempt";
        case AuditEventType::AllocationDenied:
            return "AllocationDenied";
        case AuditEventType::DeallocationAttempt:
            return "DeallocationAttempt";
        case AuditEventType::InstructionLimitHit:
            return "InstructionLimitHit";
        case AuditEventType::StackDepthLimitHit:
            return "StackDepthLimitHit";
        case AuditEventType::TimeLimitHit:
            return "TimeLimitHit";
        case AuditEventType::ContextCreated:
            return "ContextCreated";
        case AuditEventType::ContextDestroyed:
            return "ContextDestroyed";
        case AuditEventType::ContextReset:
            return "ContextReset";
        case AuditEventType::OpcodeDenied:
            return "OpcodeDenied";
        // SEC-006: Dot lifecycle
        case AuditEventType::DotStarted:
            return "DotStarted";
        case AuditEventType::DotCompleted:
            return "DotCompleted";
        case AuditEventType::DotFailed:
            return "DotFailed";
        // SEC-006: Capability lifecycle
        case AuditEventType::CapabilityCreated:
            return "CapabilityCreated";
        case AuditEventType::CapabilityRevoked:
            return "CapabilityRevoked";
        // SEC-006: Resource violations
        case AuditEventType::MemoryLimitExceeded:
            return "MemoryLimitExceeded";
        case AuditEventType::SecurityViolation:
            return "SecurityViolation";
    }
    return "Unknown";
}

// ============================================================================
// AuditEvent Struct
// ============================================================================

/// @brief A security audit event record
struct AuditEvent {
    /// Type of event
    AuditEventType type{AuditEventType::ContextCreated};

    /// Timestamp when event occurred
    std::chrono::steady_clock::time_point timestamp = {};

    /// Associated permission (for permission events)
    Permission permission{Permission::None};

    /// Associated value (size for allocations, count for instructions)
    std::uint64_t value{0};

    /// Optional context string
    std::string_view context = {};

    /// Create an event with current timestamp
    [[nodiscard]] static AuditEvent now(AuditEventType type, Permission perm = Permission::None,
                                        std::uint64_t value = 0,
                                        std::string_view ctx = "") noexcept {
        return AuditEvent{
            .type = type,
            .timestamp = std::chrono::steady_clock::now(),
            .permission = perm,
            .value = value,
            .context = ctx,
        };
    }
};

// ============================================================================
// AuditLogger Interface
// ============================================================================

/// @brief Abstract interface for security audit logging
///
/// Implementations can store events in memory, write to files, or forward
/// to external systems. The interface is designed for minimal overhead
/// when logging is disabled.
class AuditLogger {
public:
    AuditLogger() = default;
    virtual ~AuditLogger() = default;
    AuditLogger(const AuditLogger&) = default;
    AuditLogger& operator=(const AuditLogger&) = default;
    AuditLogger(AuditLogger&&) = default;
    AuditLogger& operator=(AuditLogger&&) = default;

    /// @brief Log a security event
    ///
    /// @param event The event to log
    /// @note Must be noexcept for use in security-critical paths
    virtual void log(const AuditEvent& event) noexcept = 0;

    /// @brief Check if logging is enabled
    ///
    /// @return true if log() will actually record events
    [[nodiscard]] virtual bool is_enabled() const noexcept = 0;
};

// ============================================================================
// NullAuditLogger
// ============================================================================

/// @brief No-op audit logger (default)
///
/// Use this when audit logging is not needed. All operations are no-ops.
class NullAuditLogger final : public AuditLogger {
public:
    void log(const AuditEvent& /*event*/) noexcept override {}

    [[nodiscard]] bool is_enabled() const noexcept override { return false; }

    /// @brief Get singleton instance
    [[nodiscard]] static NullAuditLogger& instance() noexcept {
        static NullAuditLogger instance;
        return instance;
    }
};

// ============================================================================
// BufferedAuditLogger
// ============================================================================

/// @brief Ring buffer audit logger for deferred inspection
///
/// Stores events in a bounded buffer. When the buffer is full, oldest
/// events are overwritten. Thread Safety: NOT thread-safe.
class BufferedAuditLogger final : public AuditLogger {
public:
    /// @brief Construct with specified capacity
    ///
    /// @param capacity Maximum number of events to store
    explicit BufferedAuditLogger(std::size_t capacity = 1024) noexcept;

    void log(const AuditEvent& event) noexcept override;

    [[nodiscard]] bool is_enabled() const noexcept override { return true; }

    /// @brief Get all logged events
    ///
    /// @return Span of events in chronological order
    [[nodiscard]] std::span<const AuditEvent> events() const noexcept;

    /// @brief Get number of events stored
    [[nodiscard]] std::size_t size() const noexcept;

    /// @brief Get buffer capacity
    [[nodiscard]] std::size_t capacity() const noexcept;

    /// @brief Clear all events
    void clear() noexcept;

private:
    std::vector<AuditEvent> events_;
    std::size_t capacity_;
    std::size_t write_index_{0};
    bool wrapped_{false};
};

// ============================================================================
// CallbackAuditLogger
// ============================================================================

/// @brief Callback-based audit logger
///
/// Invokes a user-provided callback for each event. Useful for real-time
/// monitoring or forwarding to external systems.
class CallbackAuditLogger final : public AuditLogger {
public:
    /// Callback function type
    using Callback = void (*)(const AuditEvent& event, void* user_data);

    /// @brief Construct with callback
    ///
    /// @param callback Function to call for each event (nullptr disables)
    /// @param user_data User data passed to callback
    explicit CallbackAuditLogger(Callback callback, void* user_data = nullptr) noexcept;

    void log(const AuditEvent& event) noexcept override;

    [[nodiscard]] bool is_enabled() const noexcept override;

private:
    Callback callback_;
    void* user_data_;
};

// ============================================================================
// SecurityContext Class
// ============================================================================

/// @brief Security context for per-Dot execution enforcement
///
/// SecurityContext is created per Dot execution and tracks resource usage
/// against capability limits. It provides fast permission checking using
/// the SEC-002 permission model and enforces SEC-001 capability limits.
///
/// @par Performance Target
/// <100 microseconds for security check latency. Hot-path methods
/// (`can()`, `on_instruction()`) are inlined for maximum performance.
///
/// @par Thread Safety
/// NOT thread-safe. Use one SecurityContext per Dot. Dots are
/// single-threaded entities.
///
/// @par Usage Example
/// @code
/// // Create context with sandbox limits and read-write permissions
/// SecurityContext ctx(
///     capabilities::CapabilityLimits::sandbox(),
///     PermissionSet::read_write()
/// );
///
/// // Check permission before operation
/// if (ctx.can(Permission::WriteMemory)) {
///     // Track allocation
///     if (auto err = ctx.on_allocate(1024); err != SecurityContextError::Success) {
///         // Handle allocation limit exceeded
///     }
/// }
///
/// // Track instruction execution in hot loop
/// while (ctx.on_instruction()) {
///     // Execute bytecode...
/// }
/// @endcode
class SecurityContext {
public:
    // ========== Construction ==========

    /// @brief Construct from CapabilityLimits and PermissionSet
    ///
    /// @param limits Resource limits to enforce (copied)
    /// @param permissions SEC-002 permission set (copied)
    /// @param logger Optional audit logger (nullptr for no logging)
    SecurityContext(capabilities::CapabilityLimits limits, PermissionSet permissions,
                    AuditLogger* logger = nullptr) noexcept;

    /// @brief Construct from a Capability (extracts limits)
    ///
    /// @param capability The capability to extract limits from
    /// @param permissions SEC-002 permission set for this Dot
    /// @param logger Optional audit logger
    SecurityContext(const capabilities::Capability& capability, PermissionSet permissions,
                    AuditLogger* logger = nullptr) noexcept;

    // Non-copyable, movable
    SecurityContext(const SecurityContext&) = delete;
    SecurityContext& operator=(const SecurityContext&) = delete;
    SecurityContext(SecurityContext&&) noexcept = default;
    SecurityContext& operator=(SecurityContext&&) noexcept = default;

    ~SecurityContext();

    // ========== Permission Checking (SEC-002) ==========

    /// @brief Check if a permission is granted (non-throwing)
    ///
    /// @param perm The permission to check
    /// @return true if permission is granted
    ///
    /// @par Performance
    /// Inline bitmask operation, <10ns typical.
    [[nodiscard]] bool can(Permission perm) const noexcept {
        return permissions_.has_permission(perm);
    }

    /// @brief Require a permission (non-throwing)
    ///
    /// @param perm The permission required
    /// @param context Optional context for audit logging
    /// @return Success or PermissionDenied
    ///
    /// @par Side Effects
    /// - Increments internal permission check counter
    /// - Logs to audit logger if configured
    [[nodiscard]] SecurityContextError require(Permission perm,
                                               std::string_view context = "") noexcept;

    /// @brief Require a permission (throwing version)
    ///
    /// @param perm The permission required
    /// @param context Optional context for error messages
    /// @throws PermissionDeniedException if permission denied
    ///
    /// @note Use for non-hot paths where exceptions are acceptable.
    void require_or_throw(Permission perm, std::string_view context = "") const;

    // ========== Resource Limit Checking ==========

    /// @brief Check if an allocation of given size is allowed
    ///
    /// Checks against:
    /// - max_allocation_size (single allocation limit)
    /// - max_memory (total memory limit)
    /// - max_allocations (allocation count limit)
    ///
    /// @param size Bytes to allocate
    /// @return true if allocation is within limits
    [[nodiscard]] bool can_allocate(std::size_t size) const noexcept;

    /// @brief Check if another instruction can be executed (hot path)
    ///
    /// @return true if within instruction limit
    ///
    /// @par Performance
    /// Inline check, samples limit every INSTRUCTION_CHECK_INTERVAL.
    [[nodiscard]] bool can_execute_instruction() const noexcept {
        if (limits_.max_instructions == 0) {
            return true;
        }
        return usage_.instructions_executed < limits_.max_instructions;
    }

    /// @brief Check if stack can grow by one level
    ///
    /// @return true if within stack depth limit
    [[nodiscard]] bool can_push_stack() const noexcept {
        if (limits_.max_stack_depth == 0) {
            return true;
        }
        return usage_.current_stack_depth < limits_.max_stack_depth;
    }

    /// @brief Check if execution time is within limits
    ///
    /// @return true if within time limit
    ///
    /// @note Reads system clock - use sparingly in hot paths.
    [[nodiscard]] bool check_time_limit() const noexcept;

    // ========== Resource Usage Tracking ==========

    /// @brief Record a memory allocation
    ///
    /// @param size Bytes allocated
    /// @return Success or error if limits exceeded
    ///
    /// @par Side Effects
    /// - Updates memory_allocated and allocation_count
    /// - Logs AllocationAttempt to audit logger
    [[nodiscard]] SecurityContextError on_allocate(std::size_t size) noexcept;

    /// @brief Record a memory deallocation
    ///
    /// @param size Bytes deallocated
    ///
    /// @par Side Effects
    /// - Decrements memory_allocated (saturates at 0)
    /// - Logs DeallocationAttempt to audit logger
    void on_deallocate(std::size_t size) noexcept;

    /// @brief Record instruction execution (hot path)
    ///
    /// @return true if execution can continue, false if limit hit
    ///
    /// @par Performance
    /// Inline increment with sampled limit check every
    /// INSTRUCTION_CHECK_INTERVAL instructions. <5ns typical.
    [[nodiscard]] bool on_instruction() noexcept {
        ++usage_.instructions_executed;

        // Fast path: no limit
        if (limits_.max_instructions == 0) [[likely]] {
            return true;
        }

        // Periodic limit check
        if ((usage_.instructions_executed & (INSTRUCTION_CHECK_INTERVAL - 1)) == 0) [[unlikely]] {
            return check_instruction_limit_cold();
        }

        return true;
    }

    /// @brief Record stack push (function call)
    ///
    /// @return true if push succeeded, false if limit hit
    [[nodiscard]] bool on_stack_push() noexcept;

    /// @brief Record stack pop (function return)
    void on_stack_pop() noexcept;

    // ========== Audit Logging ==========

    /// @brief Log a security event
    ///
    /// @param event The event to log
    void log_event(const AuditEvent& event) noexcept;

    /// @brief Log a security event with type and value
    ///
    /// @param type Event type
    /// @param value Associated value (size, count, etc.)
    /// @param context Optional context string
    void log_event(AuditEventType type, std::uint64_t value = 0,
                   std::string_view context = "") noexcept;

    // ========== State Access ==========

    /// @brief Get current resource usage
    [[nodiscard]] const ResourceUsage& usage() const noexcept { return usage_; }

    /// @brief Get the configured limits
    [[nodiscard]] const capabilities::CapabilityLimits& limits() const noexcept { return limits_; }

    /// @brief Get the permission set
    [[nodiscard]] const PermissionSet& permissions() const noexcept { return permissions_; }

    /// @brief Get execution elapsed time in milliseconds
    [[nodiscard]] std::uint64_t elapsed_ms() const noexcept;

    // ========== Diagnostics ==========

    /// @brief Diagnostic statistics snapshot
    struct Stats {
        std::uint64_t instructions_executed;
        std::uint64_t memory_allocated;
        std::uint32_t allocation_count;
        std::uint32_t max_stack_depth;
        std::uint64_t elapsed_ms;
        std::size_t permission_checks;
        std::size_t permission_denials;
    };

    /// @brief Get a snapshot of security statistics
    [[nodiscard]] Stats stats() const noexcept;

    /// @brief Reset usage counters (keeps limits and permissions)
    void reset_usage() noexcept;

    // ========== Factory Methods ==========

    /// @brief Create a context with unlimited resources
    ///
    /// @param permissions SEC-002 permission set
    /// @param logger Optional audit logger
    [[nodiscard]] static SecurityContext unlimited(PermissionSet permissions,
                                                   AuditLogger* logger = nullptr) noexcept {
        return {capabilities::CapabilityLimits::unlimited(), permissions, logger};
    }

    /// @brief Create a context for untrusted code
    ///
    /// @param permissions SEC-002 permission set
    /// @param logger Optional audit logger
    [[nodiscard]] static SecurityContext untrusted(PermissionSet permissions,
                                                   AuditLogger* logger = nullptr) noexcept {
        return {capabilities::CapabilityLimits::untrusted(), permissions, logger};
    }

    /// @brief Create a context for sandboxed code
    ///
    /// @param permissions SEC-002 permission set
    /// @param logger Optional audit logger
    [[nodiscard]] static SecurityContext sandbox(PermissionSet permissions,
                                                 AuditLogger* logger = nullptr) noexcept {
        return {capabilities::CapabilityLimits::sandbox(), permissions, logger};
    }

    /// @brief Create a context for trusted code
    ///
    /// @param permissions SEC-002 permission set
    /// @param logger Optional audit logger
    [[nodiscard]] static SecurityContext trusted(PermissionSet permissions,
                                                 AuditLogger* logger = nullptr) noexcept {
        return {capabilities::CapabilityLimits::trusted(), permissions, logger};
    }

private:
    /// Cold path for instruction limit check
    [[nodiscard]] bool check_instruction_limit_cold() noexcept;

    /// Cold path for time limit check
    [[nodiscard]] bool check_time_limit_cold() noexcept;

    // Configuration (immutable after construction)
    capabilities::CapabilityLimits limits_;
    PermissionSet permissions_;
    AuditLogger* logger_;

    // Runtime state
    ResourceUsage usage_;

    // Diagnostics counters
    std::size_t permission_checks_{0};
    std::size_t permission_denials_{0};

    // Sampling configuration (power of 2 for fast modulo)
    static constexpr std::uint32_t INSTRUCTION_CHECK_INTERVAL = 1024;
    static constexpr std::uint32_t TIME_CHECK_INTERVAL = 4096;
};

}  // namespace dotvm::core::security
