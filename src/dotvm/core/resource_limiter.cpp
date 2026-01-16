/// @file resource_limiter.cpp
/// @brief SEC-004 Resource Limits - Implementation

#include "dotvm/core/security/resource_limiter.hpp"
#include "dotvm/core/security_stats.hpp"

namespace dotvm::core::security {

// ============================================================================
// Construction
// ============================================================================

ResourceLimiter::ResourceLimiter(RuntimeLimits limits) noexcept
    : limits_{limits},
      stats_{nullptr},
      current_memory_{0},
      current_instructions_{0},
      current_stack_depth_{0},
      start_time_{std::chrono::steady_clock::now()} {}

ResourceLimiter::ResourceLimiter(RuntimeLimits limits,
                                 SecurityStats* stats) noexcept
    : limits_{limits},
      stats_{stats},
      current_memory_{0},
      current_instructions_{0},
      current_stack_depth_{0},
      start_time_{std::chrono::steady_clock::now()} {}

// ============================================================================
// Memory Tracking
// ============================================================================

Result<void, ResourceLimitError>
ResourceLimiter::try_allocate(std::size_t bytes) noexcept {
    // Fast path: check single allocation size limit first
    if (limits_.max_allocation_size != 0) [[likely]] {
        if (bytes > limits_.max_allocation_size) {
            if (stats_ != nullptr) {
                stats_->record_allocation_limit_hit();
            }
            return ResourceLimitError::AllocationSizeExceeded;
        }
    }

    // Fast path: unlimited total memory
    if (limits_.max_memory == 0) [[unlikely]] {
        current_memory_.fetch_add(bytes, std::memory_order_relaxed);
        return Ok;
    }

    // Atomic compare-exchange loop for total memory limit
    std::uint64_t current = current_memory_.load(std::memory_order_relaxed);
    std::uint64_t desired;
    do {
        // Check if allocation would exceed limit
        if (current > limits_.max_memory - bytes) {
            if (stats_ != nullptr) {
                stats_->record_limit_violation();
            }
            return ResourceLimitError::MemoryLimitExceeded;
        }
        desired = current + bytes;
    } while (!current_memory_.compare_exchange_weak(
        current, desired, std::memory_order_relaxed, std::memory_order_relaxed));

    return Ok;
}

void ResourceLimiter::on_deallocate(std::size_t bytes) noexcept {
    // Saturating subtraction to prevent underflow
    std::uint64_t current = current_memory_.load(std::memory_order_relaxed);
    std::uint64_t desired;
    do {
        desired = (bytes > current) ? 0 : (current - bytes);
    } while (!current_memory_.compare_exchange_weak(
        current, desired, std::memory_order_relaxed, std::memory_order_relaxed));
}

// ============================================================================
// Instruction Tracking
// ============================================================================

Result<void, ResourceLimitError> ResourceLimiter::try_execute() noexcept {
    // Fast path: unlimited instructions
    if (limits_.max_instructions == 0) [[unlikely]] {
        current_instructions_.fetch_add(1, std::memory_order_relaxed);
        return Ok;
    }

    // Increment and check
    std::uint64_t current =
        current_instructions_.fetch_add(1, std::memory_order_relaxed);

    if (current >= limits_.max_instructions) [[unlikely]] {
        if (stats_ != nullptr) {
            stats_->record_limit_violation();
        }
        return ResourceLimitError::InstructionLimitExceeded;
    }

    return Ok;
}

Result<void, ResourceLimitError>
ResourceLimiter::try_execute_batch(std::uint64_t count) noexcept {
    // Fast path: unlimited instructions
    if (limits_.max_instructions == 0) [[unlikely]] {
        current_instructions_.fetch_add(count, std::memory_order_relaxed);
        return Ok;
    }

    // Atomic add and check
    std::uint64_t prev =
        current_instructions_.fetch_add(count, std::memory_order_relaxed);

    if (prev + count > limits_.max_instructions) [[unlikely]] {
        if (stats_ != nullptr) {
            stats_->record_limit_violation();
        }
        return ResourceLimitError::InstructionLimitExceeded;
    }

    return Ok;
}

// ============================================================================
// Stack Tracking
// ============================================================================

Result<void, ResourceLimitError> ResourceLimiter::try_push_frame() noexcept {
    // Fast path: unlimited stack depth
    if (limits_.max_stack_depth == 0) [[unlikely]] {
        current_stack_depth_.fetch_add(1, std::memory_order_relaxed);
        return Ok;
    }

    // Atomic compare-exchange loop for stack depth limit
    std::uint32_t current = current_stack_depth_.load(std::memory_order_relaxed);
    std::uint32_t desired;
    do {
        if (current >= limits_.max_stack_depth) [[unlikely]] {
            if (stats_ != nullptr) {
                stats_->record_limit_violation();
            }
            return ResourceLimitError::StackDepthExceeded;
        }
        desired = current + 1;
    } while (!current_stack_depth_.compare_exchange_weak(
        current, desired, std::memory_order_relaxed, std::memory_order_relaxed));

    return Ok;
}

void ResourceLimiter::on_pop_frame() noexcept {
    // Saturating subtraction to prevent underflow
    std::uint32_t current = current_stack_depth_.load(std::memory_order_relaxed);
    if (current > 0) {
        current_stack_depth_.fetch_sub(1, std::memory_order_relaxed);
    }
}

// ============================================================================
// Time Tracking
// ============================================================================

bool ResourceLimiter::is_time_expired() const noexcept {
    // Fast path: unlimited time
    if (limits_.max_execution_time_ms == 0) [[unlikely]] {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);

    return elapsed.count() >= static_cast<std::int64_t>(limits_.max_execution_time_ms);
}

std::uint64_t ResourceLimiter::elapsed_time_ms() const noexcept {
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_);
    return static_cast<std::uint64_t>(elapsed.count());
}

// ============================================================================
// Control
// ============================================================================

void ResourceLimiter::reset() noexcept {
    current_memory_.store(0, std::memory_order_relaxed);
    current_instructions_.store(0, std::memory_order_relaxed);
    current_stack_depth_.store(0, std::memory_order_relaxed);
    start_time_ = std::chrono::steady_clock::now();
}

}  // namespace dotvm::core::security
