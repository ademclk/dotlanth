/// @file security_context.cpp
/// @brief SEC-003 Security Context implementation

#include "dotvm/core/security/security_context.hpp"

namespace dotvm::core::security {

// ============================================================================
// BufferedAuditLogger Implementation
// ============================================================================

BufferedAuditLogger::BufferedAuditLogger(std::size_t capacity) noexcept
    : capacity_(capacity > 0 ? capacity : 1) {
    events_.reserve(capacity_);
}

void BufferedAuditLogger::log(const AuditEvent& event) noexcept {
    if (events_.size() < capacity_) {
        events_.push_back(event);
    } else {
        // Ring buffer: overwrite oldest
        events_[write_index_] = event;
        write_index_ = (write_index_ + 1) % capacity_;
        wrapped_ = true;
    }
}

std::span<const AuditEvent> BufferedAuditLogger::events() const noexcept {
    return std::span<const AuditEvent>(events_);
}

std::size_t BufferedAuditLogger::size() const noexcept {
    return events_.size();
}

std::size_t BufferedAuditLogger::capacity() const noexcept {
    return capacity_;
}

void BufferedAuditLogger::clear() noexcept {
    events_.clear();
    write_index_ = 0;
    wrapped_ = false;
}

// ============================================================================
// CallbackAuditLogger Implementation
// ============================================================================

CallbackAuditLogger::CallbackAuditLogger(Callback callback,
                                         void* user_data) noexcept
    : callback_(callback), user_data_(user_data) {}

void CallbackAuditLogger::log(const AuditEvent& event) noexcept {
    if (callback_ != nullptr) {
        callback_(event, user_data_);
    }
}

bool CallbackAuditLogger::is_enabled() const noexcept {
    return callback_ != nullptr;
}

// ============================================================================
// SecurityContext Implementation
// ============================================================================

SecurityContext::SecurityContext(capabilities::CapabilityLimits limits,
                                 PermissionSet permissions,
                                 AuditLogger* logger) noexcept
    : limits_(limits), permissions_(permissions), logger_(logger) {
    usage_.start_time = std::chrono::steady_clock::now();

    if (logger_ != nullptr && logger_->is_enabled()) {
        log_event(AuditEventType::ContextCreated);
    }
}

SecurityContext::SecurityContext(const capabilities::Capability& capability,
                                 PermissionSet permissions,
                                 AuditLogger* logger) noexcept
    : limits_(capability.limits), permissions_(permissions), logger_(logger) {
    usage_.start_time = std::chrono::steady_clock::now();

    if (logger_ != nullptr && logger_->is_enabled()) {
        log_event(AuditEventType::ContextCreated);
    }
}

SecurityContext::~SecurityContext() {
    if (logger_ != nullptr && logger_->is_enabled()) {
        log_event(AuditEventType::ContextDestroyed,
                  usage_.instructions_executed);
    }
}

// ========== Permission Checking ==========

SecurityContextError SecurityContext::require(Permission perm,
                                               std::string_view context) noexcept {
    ++permission_checks_;

    if (permissions_.has_permission(perm)) {
        if (logger_ != nullptr && logger_->is_enabled()) {
            log_event(AuditEvent::now(AuditEventType::PermissionGranted, perm,
                                      0, context));
        }
        return SecurityContextError::Success;
    }

    ++permission_denials_;
    if (logger_ != nullptr && logger_->is_enabled()) {
        log_event(
            AuditEvent::now(AuditEventType::PermissionDenied, perm, 0, context));
    }
    return SecurityContextError::PermissionDenied;
}

void SecurityContext::require_or_throw(Permission perm,
                                       std::string_view context) const {
    permissions_.require(perm, context);
}

// ========== Resource Limit Checking ==========

bool SecurityContext::can_allocate(std::size_t size) const noexcept {
    // Check single allocation size limit
    if (limits_.max_allocation_size > 0 && size > limits_.max_allocation_size) {
        return false;
    }

    // Check total memory limit
    if (limits_.max_memory > 0 &&
        usage_.memory_allocated + size > limits_.max_memory) {
        return false;
    }

    // Check allocation count limit
    if (limits_.max_allocations > 0 &&
        usage_.allocation_count >= limits_.max_allocations) {
        return false;
    }

    return true;
}

bool SecurityContext::check_time_limit() const noexcept {
    if (limits_.max_execution_time_ms == 0) {
        return true;
    }

    auto elapsed = std::chrono::steady_clock::now() - usage_.start_time;
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    return static_cast<std::uint64_t>(elapsed_ms) < limits_.max_execution_time_ms;
}

// ========== Resource Usage Tracking ==========

SecurityContextError SecurityContext::on_allocate(std::size_t size) noexcept {
    // Check single allocation size limit
    if (limits_.max_allocation_size > 0 && size > limits_.max_allocation_size) {
        if (logger_ != nullptr && logger_->is_enabled()) {
            log_event(AuditEventType::AllocationDenied, size,
                      "allocation_size_exceeded");
        }
        return SecurityContextError::AllocationSizeExceeded;
    }

    // Check total memory limit
    if (limits_.max_memory > 0 &&
        usage_.memory_allocated + size > limits_.max_memory) {
        if (logger_ != nullptr && logger_->is_enabled()) {
            log_event(AuditEventType::AllocationDenied, size,
                      "memory_limit_exceeded");
        }
        return SecurityContextError::MemoryLimitExceeded;
    }

    // Check allocation count limit
    if (limits_.max_allocations > 0 &&
        usage_.allocation_count >= limits_.max_allocations) {
        if (logger_ != nullptr && logger_->is_enabled()) {
            log_event(AuditEventType::AllocationDenied, size,
                      "allocation_count_exceeded");
        }
        return SecurityContextError::AllocationCountExceeded;
    }

    // Update usage
    usage_.memory_allocated += size;
    ++usage_.allocation_count;

    if (logger_ != nullptr && logger_->is_enabled()) {
        log_event(AuditEventType::AllocationAttempt, size);
    }

    return SecurityContextError::Success;
}

void SecurityContext::on_deallocate(std::size_t size) noexcept {
    // Saturating subtraction
    if (size >= usage_.memory_allocated) {
        usage_.memory_allocated = 0;
    } else {
        usage_.memory_allocated -= size;
    }

    if (logger_ != nullptr && logger_->is_enabled()) {
        log_event(AuditEventType::DeallocationAttempt, size);
    }
}

bool SecurityContext::on_stack_push() noexcept {
    if (limits_.max_stack_depth > 0 &&
        usage_.current_stack_depth >= limits_.max_stack_depth) {
        if (logger_ != nullptr && logger_->is_enabled()) {
            log_event(AuditEventType::StackDepthLimitHit,
                      usage_.current_stack_depth);
        }
        return false;
    }

    ++usage_.current_stack_depth;

    // Track maximum depth reached
    if (usage_.current_stack_depth > usage_.max_stack_depth_reached) {
        usage_.max_stack_depth_reached = usage_.current_stack_depth;
    }

    return true;
}

void SecurityContext::on_stack_pop() noexcept {
    if (usage_.current_stack_depth > 0) {
        --usage_.current_stack_depth;
    }
}

// ========== Cold Path Checks ==========

bool SecurityContext::check_instruction_limit_cold() noexcept {
    if (usage_.instructions_executed >= limits_.max_instructions) {
        if (logger_ != nullptr && logger_->is_enabled()) {
            log_event(AuditEventType::InstructionLimitHit,
                      usage_.instructions_executed);
        }
        return false;
    }

    // Also check time limit periodically
    if ((usage_.instructions_executed & (TIME_CHECK_INTERVAL - 1)) == 0) {
        return check_time_limit_cold();
    }

    return true;
}

bool SecurityContext::check_time_limit_cold() noexcept {
    if (!check_time_limit()) {
        if (logger_ != nullptr && logger_->is_enabled()) {
            log_event(AuditEventType::TimeLimitHit, elapsed_ms());
        }
        return false;
    }
    return true;
}

// ========== Audit Logging ==========

void SecurityContext::log_event(const AuditEvent& event) noexcept {
    if (logger_ != nullptr) {
        logger_->log(event);
    }
}

void SecurityContext::log_event(AuditEventType type,
                                 std::uint64_t value,
                                 std::string_view context) noexcept {
    if (logger_ != nullptr && logger_->is_enabled()) {
        logger_->log(AuditEvent::now(type, Permission::None, value, context));
    }
}

// ========== State Access ==========

std::uint64_t SecurityContext::elapsed_ms() const noexcept {
    auto elapsed = std::chrono::steady_clock::now() - usage_.start_time;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

SecurityContext::Stats SecurityContext::stats() const noexcept {
    return Stats{
        .instructions_executed = usage_.instructions_executed,
        .memory_allocated = usage_.memory_allocated,
        .allocation_count = usage_.allocation_count,
        .max_stack_depth = usage_.max_stack_depth_reached,
        .elapsed_ms = elapsed_ms(),
        .permission_checks = permission_checks_,
        .permission_denials = permission_denials_,
    };
}

void SecurityContext::reset_usage() noexcept {
    usage_.reset();
    usage_.start_time = std::chrono::steady_clock::now();
    permission_checks_ = 0;
    permission_denials_ = 0;

    if (logger_ != nullptr && logger_->is_enabled()) {
        log_event(AuditEventType::ContextReset);
    }
}

}  // namespace dotvm::core::security
