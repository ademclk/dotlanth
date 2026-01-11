#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dotvm::core {

/// Security event statistics for monitoring and auditing.
///
/// Tracks security-relevant events that occur during VM execution:
/// - Memory safety violations (bounds, use-after-free attempts)
/// - Control flow integrity violations
/// - Resource exhaustion events
///
/// Thread Safety: All counters are atomic for safe concurrent access.
/// Performance: Uses relaxed memory ordering for minimal overhead.
struct SecurityStats {
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

    // ========== Query Methods ==========

    /// Returns true if any security violation has been recorded.
    [[nodiscard]] bool has_violations() const noexcept {
        return bounds_violations.load(std::memory_order_relaxed) > 0 ||
               invalid_handle_accesses.load(std::memory_order_relaxed) > 0 ||
               cfi_violations.load(std::memory_order_relaxed) > 0;
    }

    /// Returns true if any resource exhaustion event has occurred.
    [[nodiscard]] bool has_exhaustion_events() const noexcept {
        return allocation_limit_hits.load(std::memory_order_relaxed) > 0 ||
               handle_table_exhaustions.load(std::memory_order_relaxed) > 0;
    }

    /// Returns total security violation count.
    [[nodiscard]] std::size_t total_violations() const noexcept {
        return bounds_violations.load(std::memory_order_relaxed) +
               invalid_handle_accesses.load(std::memory_order_relaxed) +
               cfi_violations.load(std::memory_order_relaxed);
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
    };

    /// Takes a consistent snapshot of all statistics.
    [[nodiscard]] Snapshot snapshot() const noexcept {
        return Snapshot{
            .generation_wraparounds = generation_wraparounds.load(std::memory_order_acquire),
            .bounds_violations = bounds_violations.load(std::memory_order_acquire),
            .invalid_handle_accesses = invalid_handle_accesses.load(std::memory_order_acquire),
            .cfi_violations = cfi_violations.load(std::memory_order_acquire),
            .allocation_limit_hits = allocation_limit_hits.load(std::memory_order_acquire),
            .handle_table_exhaustions = handle_table_exhaustions.load(std::memory_order_acquire),
            .total_allocations = total_allocations.load(std::memory_order_acquire),
            .total_deallocations = total_deallocations.load(std::memory_order_acquire)
        };
    }

    /// Resets all counters to zero.
    void reset() noexcept {
        generation_wraparounds.store(0, std::memory_order_relaxed);
        bounds_violations.store(0, std::memory_order_relaxed);
        invalid_handle_accesses.store(0, std::memory_order_relaxed);
        cfi_violations.store(0, std::memory_order_relaxed);
        allocation_limit_hits.store(0, std::memory_order_relaxed);
        handle_table_exhaustions.store(0, std::memory_order_relaxed);
        total_allocations.store(0, std::memory_order_relaxed);
        total_deallocations.store(0, std::memory_order_relaxed);
    }
};

// Ensure atomic operations are lock-free for performance
static_assert(std::atomic<std::size_t>::is_always_lock_free,
              "SecurityStats requires lock-free atomics");

} // namespace dotvm::core
