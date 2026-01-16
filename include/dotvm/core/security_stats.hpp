#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dotvm::core {

// ============================================================================
// Security Event Types
// ============================================================================

/// Security event types for audit logging and monitoring
///
/// These events represent security-relevant occurrences during VM execution
/// that may indicate attacks or resource exhaustion.
enum class SecurityEvent : std::uint8_t {
    GenerationWraparound = 0,   ///< Handle generation counter wrapped
    BoundsViolation = 1,        ///< Out-of-bounds memory access attempt
    InvalidHandleAccess = 2,    ///< Access to invalid/deallocated handle
    CfiViolation = 3,           ///< Control flow integrity violation
    AllocationLimitHit = 4,     ///< Allocation size limit exceeded
    HandleTableExhaustion = 5,  ///< No more handles available
    InstructionLimitHit = 6,    ///< Instruction execution limit exceeded
    MemoryLimitHit = 7          ///< Total memory limit exceeded
};

/// Returns a human-readable name for a security event
[[nodiscard]] constexpr const char* event_name(SecurityEvent e) noexcept {
    switch (e) {
        case SecurityEvent::GenerationWraparound:
            return "GenerationWraparound";
        case SecurityEvent::BoundsViolation:
            return "BoundsViolation";
        case SecurityEvent::InvalidHandleAccess:
            return "InvalidHandleAccess";
        case SecurityEvent::CfiViolation:
            return "CfiViolation";
        case SecurityEvent::AllocationLimitHit:
            return "AllocationLimitHit";
        case SecurityEvent::HandleTableExhaustion:
            return "HandleTableExhaustion";
        case SecurityEvent::InstructionLimitHit:
            return "InstructionLimitHit";
        case SecurityEvent::MemoryLimitHit:
            return "MemoryLimitHit";
    }
    return "Unknown";
}

/// Callback type for security event notifications
///
/// @param event The type of security event that occurred
/// @param context Optional context string (may be nullptr)
/// @param user_data User-provided data pointer
using SecurityEventCallback = void (*)(SecurityEvent event,
                                        const char* context,
                                        void* user_data);

/// Security event statistics for monitoring and auditing.
///
/// Tracks security-relevant events that occur during VM execution:
/// - Memory safety violations (bounds, use-after-free attempts)
/// - Control flow integrity violations
/// - Resource exhaustion events
///
/// Thread Safety: All counters are atomic for safe concurrent access.
/// Performance: Uses relaxed memory ordering for minimal overhead.
///
/// Note: SecurityStats is non-copyable and non-movable due to atomic members.
/// Embed it directly or use std::unique_ptr if indirection is needed.
struct SecurityStats {
    // Non-copyable, non-movable (contains std::atomic members)
    SecurityStats() = default;
    SecurityStats(const SecurityStats&) = delete;
    SecurityStats& operator=(const SecurityStats&) = delete;
    SecurityStats(SecurityStats&&) = delete;
    SecurityStats& operator=(SecurityStats&&) = delete;

    /// Number of times a handle generation counter wrapped from MAX to INITIAL.
    /// High values may indicate handle reuse attacks or resource exhaustion.
    std::atomic<std::size_t> generation_wraparounds{0};

    /// Number of attempted out-of-bounds memory accesses (read or write).
    std::atomic<std::size_t> bounds_violations{0};

    /// Number of attempted accesses to invalid/deallocated handles.
    std::atomic<std::size_t> invalid_handle_accesses{0};

    /// Number of control flow integrity violations detected.
    std::atomic<std::size_t> cfi_violations{0};

    /// Number of allocation failures due to size limits.
    std::atomic<std::size_t> allocation_limit_hits{0};

    /// Number of handle table exhaustion events.
    std::atomic<std::size_t> handle_table_exhaustions{0};

    /// Total successful allocations (for reference).
    std::atomic<std::size_t> total_allocations{0};

    /// Total successful deallocations (for reference).
    std::atomic<std::size_t> total_deallocations{0};

    /// Number of deallocation failures (OS-level failures).
    std::atomic<std::size_t> deallocation_failures{0};

    /// Number of invalid deallocation attempts (bad size).
    std::atomic<std::size_t> invalid_deallocations{0};

    // ========== Capability System Counters (SEC-001) ==========

    /// Number of capabilities created (root capabilities).
    std::atomic<std::size_t> capability_creations{0};

    /// Number of capabilities derived from parents.
    std::atomic<std::size_t> capability_derivations{0};

    /// Number of capabilities revoked.
    std::atomic<std::size_t> capability_revocations{0};

    /// Number of permission check failures.
    std::atomic<std::size_t> permission_violations{0};

    /// Number of resource limit violations.
    std::atomic<std::size_t> limit_violations{0};

    // ========== Increment Methods (relaxed ordering for performance) ==========

    void record_generation_wraparound() noexcept {
        generation_wraparounds.fetch_add(1, std::memory_order_relaxed);
    }

    void record_bounds_violation() noexcept {
        bounds_violations.fetch_add(1, std::memory_order_relaxed);
    }

    void record_invalid_handle_access() noexcept {
        invalid_handle_accesses.fetch_add(1, std::memory_order_relaxed);
    }

    void record_cfi_violation() noexcept {
        cfi_violations.fetch_add(1, std::memory_order_relaxed);
    }

    void record_allocation_limit_hit() noexcept {
        allocation_limit_hits.fetch_add(1, std::memory_order_relaxed);
    }

    void record_handle_table_exhaustion() noexcept {
        handle_table_exhaustions.fetch_add(1, std::memory_order_relaxed);
    }

    void record_allocation() noexcept {
        total_allocations.fetch_add(1, std::memory_order_relaxed);
    }

    void record_deallocation() noexcept {
        total_deallocations.fetch_add(1, std::memory_order_relaxed);
    }

    void record_deallocation_failure() noexcept {
        deallocation_failures.fetch_add(1, std::memory_order_relaxed);
    }

    void record_invalid_deallocation() noexcept {
        invalid_deallocations.fetch_add(1, std::memory_order_relaxed);
    }

    // ========== Capability System Recording Methods (SEC-001) ==========

    void record_capability_creation() noexcept {
        capability_creations.fetch_add(1, std::memory_order_relaxed);
    }

    void record_capability_derivation() noexcept {
        capability_derivations.fetch_add(1, std::memory_order_relaxed);
    }

    void record_capability_revocation() noexcept {
        capability_revocations.fetch_add(1, std::memory_order_relaxed);
    }

    void record_permission_violation() noexcept {
        permission_violations.fetch_add(1, std::memory_order_relaxed);
    }

    void record_limit_violation() noexcept {
        limit_violations.fetch_add(1, std::memory_order_relaxed);
    }

    // ========== Query Methods ==========

    /// Returns true if any security violation has been recorded.
    [[nodiscard]] bool has_violations() const noexcept {
        return bounds_violations.load(std::memory_order_relaxed) > 0 ||
               invalid_handle_accesses.load(std::memory_order_relaxed) > 0 ||
               cfi_violations.load(std::memory_order_relaxed) > 0 ||
               permission_violations.load(std::memory_order_relaxed) > 0;
    }

    /// Returns true if any resource exhaustion event has occurred.
    [[nodiscard]] bool has_exhaustion_events() const noexcept {
        return allocation_limit_hits.load(std::memory_order_relaxed) > 0 ||
               handle_table_exhaustions.load(std::memory_order_relaxed) > 0;
    }

    /// Returns total security violation count (saturates at SIZE_MAX on overflow).
    [[nodiscard]] std::size_t total_violations() const noexcept {
        std::size_t bv = bounds_violations.load(std::memory_order_relaxed);
        std::size_t iha = invalid_handle_accesses.load(std::memory_order_relaxed);
        std::size_t cfi = cfi_violations.load(std::memory_order_relaxed);

        // Saturating addition to prevent overflow
        std::size_t sum = bv;
        if (sum > SIZE_MAX - iha) return SIZE_MAX;
        sum += iha;
        if (sum > SIZE_MAX - cfi) return SIZE_MAX;
        sum += cfi;
        return sum;
    }

    // ========== Snapshot for Reporting ==========

    /// Non-atomic snapshot of all counters for reporting.
    struct Snapshot {
        std::size_t generation_wraparounds;
        std::size_t bounds_violations;
        std::size_t invalid_handle_accesses;
        std::size_t cfi_violations;
        std::size_t allocation_limit_hits;
        std::size_t handle_table_exhaustions;
        std::size_t total_allocations;
        std::size_t total_deallocations;
        std::size_t deallocation_failures;
        std::size_t invalid_deallocations;
        // Capability system counters (SEC-001)
        std::size_t capability_creations;
        std::size_t capability_derivations;
        std::size_t capability_revocations;
        std::size_t permission_violations;
        std::size_t limit_violations;
    };

    /// Takes a snapshot of all statistics.
    /// @note This snapshot is NOT atomically consistent across all counters.
    ///       Values are loaded individually, so concurrent updates may result
    ///       in a snapshot that doesn't represent a single point in time.
    ///       For accurate monitoring, call when no other threads are active.
    [[nodiscard]] Snapshot snapshot() const noexcept {
        return Snapshot{
            .generation_wraparounds = generation_wraparounds.load(std::memory_order_acquire),
            .bounds_violations = bounds_violations.load(std::memory_order_acquire),
            .invalid_handle_accesses = invalid_handle_accesses.load(std::memory_order_acquire),
            .cfi_violations = cfi_violations.load(std::memory_order_acquire),
            .allocation_limit_hits = allocation_limit_hits.load(std::memory_order_acquire),
            .handle_table_exhaustions = handle_table_exhaustions.load(std::memory_order_acquire),
            .total_allocations = total_allocations.load(std::memory_order_acquire),
            .total_deallocations = total_deallocations.load(std::memory_order_acquire),
            .deallocation_failures = deallocation_failures.load(std::memory_order_acquire),
            .invalid_deallocations = invalid_deallocations.load(std::memory_order_acquire),
            // Capability system counters (SEC-001)
            .capability_creations = capability_creations.load(std::memory_order_acquire),
            .capability_derivations = capability_derivations.load(std::memory_order_acquire),
            .capability_revocations = capability_revocations.load(std::memory_order_acquire),
            .permission_violations = permission_violations.load(std::memory_order_acquire),
            .limit_violations = limit_violations.load(std::memory_order_acquire)
        };
    }

    /// Resets all counters to zero.
    /// @warning NOT thread-safe for concurrent recording. Call only when no
    ///          other threads are recording events, or some events may be lost.
    void reset() noexcept {
        generation_wraparounds.store(0, std::memory_order_relaxed);
        bounds_violations.store(0, std::memory_order_relaxed);
        invalid_handle_accesses.store(0, std::memory_order_relaxed);
        cfi_violations.store(0, std::memory_order_relaxed);
        allocation_limit_hits.store(0, std::memory_order_relaxed);
        handle_table_exhaustions.store(0, std::memory_order_relaxed);
        total_allocations.store(0, std::memory_order_relaxed);
        total_deallocations.store(0, std::memory_order_relaxed);
        deallocation_failures.store(0, std::memory_order_relaxed);
        invalid_deallocations.store(0, std::memory_order_relaxed);
        // Capability system counters (SEC-001)
        capability_creations.store(0, std::memory_order_relaxed);
        capability_derivations.store(0, std::memory_order_relaxed);
        capability_revocations.store(0, std::memory_order_relaxed);
        permission_violations.store(0, std::memory_order_relaxed);
        limit_violations.store(0, std::memory_order_relaxed);
    }

    // ========== Event Callback ==========

    /// Sets a callback for security event notifications
    ///
    /// @param callback Function to call on security events (nullptr to disable)
    /// @param user_data User data passed to callback
    ///
    /// @note NOT thread-safe. Set before starting concurrent operations.
    /// @note The callback is invoked synchronously from the recording thread.
    void set_event_callback(SecurityEventCallback callback,
                            void* user_data = nullptr) noexcept {
        event_callback_ = callback;
        callback_user_data_ = user_data;
    }

    /// Check if an event callback is registered
    [[nodiscard]] bool has_event_callback() const noexcept {
        return event_callback_ != nullptr;
    }

private:
    SecurityEventCallback event_callback_{nullptr};
    void* callback_user_data_{nullptr};

    /// Internal: notify callback if set
    void notify_event(SecurityEvent event, const char* context = nullptr) noexcept {
        if (event_callback_ != nullptr) {
            event_callback_(event, context, callback_user_data_);
        }
    }
};

// Ensure atomic operations are lock-free for performance
static_assert(std::atomic<std::size_t>::is_always_lock_free,
              "SecurityStats requires lock-free atomics");

} // namespace dotvm::core
