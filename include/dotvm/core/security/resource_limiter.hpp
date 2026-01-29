#pragma once

/// @file resource_limiter.hpp
/// @brief SEC-004 Resource Limits - Runtime enforcement of resource usage limits
///
/// This header provides active tracking and enforcement of resource usage
/// against configurable limits. It is designed for high performance with
/// minimal overhead (<5%) using atomic operations with relaxed ordering.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <string_view>

#include "dotvm/core/result.hpp"

// Forward declaration
namespace dotvm::core {
struct SecurityStats;
}

namespace dotvm::core::security {

// ============================================================================
// EnforcementAction Enum
// ============================================================================

/// @brief Actions that can be taken when a resource limit is hit
///
/// These represent the possible responses to a resource limit violation.
enum class EnforcementAction : std::uint8_t {
    Allow = 0,  ///< Operation permitted
    Deny,       ///< Operation blocked
    Throttle,   ///< Operation delayed (rate limiting)
    Terminate   ///< Stop execution immediately
};

/// @brief Returns a human-readable name for an enforcement action
[[nodiscard]] constexpr std::string_view to_string(EnforcementAction action) noexcept {
    switch (action) {
        case EnforcementAction::Allow:
            return "Allow";
        case EnforcementAction::Deny:
            return "Deny";
        case EnforcementAction::Throttle:
            return "Throttle";
        case EnforcementAction::Terminate:
            return "Terminate";
    }
    return "Unknown";
}

// ============================================================================
// ResourceLimitError Enum
// ============================================================================

/// @brief Error types for resource limit violations
///
/// These represent specific resource constraints that have been exceeded.
enum class ResourceLimitError : std::uint8_t {
    Success = 0,               ///< No error (should not be used as an error)
    MemoryLimitExceeded,       ///< Total memory allocation limit exceeded
    InstructionLimitExceeded,  ///< Maximum instruction count exceeded
    StackDepthExceeded,        ///< Call stack depth limit exceeded
    AllocationSizeExceeded,    ///< Single allocation size limit exceeded
    TimeExpired,               ///< Maximum execution time exceeded
    Throttled                  ///< Operation throttled due to rate limiting
};

/// @brief Returns a human-readable name for a resource limit error
[[nodiscard]] constexpr std::string_view to_string(ResourceLimitError error) noexcept {
    switch (error) {
        case ResourceLimitError::Success:
            return "Success";
        case ResourceLimitError::MemoryLimitExceeded:
            return "MemoryLimitExceeded";
        case ResourceLimitError::InstructionLimitExceeded:
            return "InstructionLimitExceeded";
        case ResourceLimitError::StackDepthExceeded:
            return "StackDepthExceeded";
        case ResourceLimitError::AllocationSizeExceeded:
            return "AllocationSizeExceeded";
        case ResourceLimitError::TimeExpired:
            return "TimeExpired";
        case ResourceLimitError::Throttled:
            return "Throttled";
    }
    return "Unknown";
}

// ============================================================================
// RuntimeLimits Struct
// ============================================================================

/// @brief Configuration for resource limits during execution
///
/// Defines the maximum resource usage allowed for a VM execution context.
/// A limit value of 0 means unlimited (no enforcement).
///
/// Thread Safety: This struct is immutable after construction. Copy it
/// to share across threads.
///
/// @code
/// // Use standard limits
/// auto limits = RuntimeLimits::standard();
///
/// // Use custom limits
/// RuntimeLimits custom{
///     .max_memory = 32 * 1024 * 1024,  // 32 MB
///     .max_instructions = 500'000,
///     .max_stack_depth = 512
/// };
/// @endcode
struct RuntimeLimits {
    /// Maximum total memory allocation in bytes (0 = unlimited)
    /// Default: 64 MB
    std::uint64_t max_memory{67'108'864};

    /// Maximum number of instructions to execute (0 = unlimited)
    /// Default: 1 million
    std::uint64_t max_instructions{1'000'000};

    /// Maximum call stack depth (0 = unlimited)
    /// Default: 1024
    std::uint32_t max_stack_depth{1024};

    /// Maximum single allocation size in bytes (0 = unlimited)
    /// Default: 1 MB
    std::uint64_t max_allocation_size{1'048'576};

    /// Maximum execution time in milliseconds (0 = unlimited)
    /// Default: 5000 ms (5 seconds)
    std::uint32_t max_execution_time_ms{5000};

    // ========== Factory Methods ==========

    /// @brief Create unlimited limits (no enforcement)
    ///
    /// Use for trusted code or when limits are enforced elsewhere.
    [[nodiscard]] static constexpr RuntimeLimits unlimited() noexcept {
        return RuntimeLimits{.max_memory = 0,
                             .max_instructions = 0,
                             .max_stack_depth = 0,
                             .max_allocation_size = 0,
                             .max_execution_time_ms = 0};
    }

    /// @brief Create standard limits (default values)
    ///
    /// Balanced limits suitable for most use cases.
    [[nodiscard]] static constexpr RuntimeLimits standard() noexcept { return RuntimeLimits{}; }

    /// @brief Create strict limits for untrusted code
    ///
    /// More restrictive limits for sandboxed execution.
    [[nodiscard]] static constexpr RuntimeLimits strict() noexcept {
        return RuntimeLimits{
            .max_memory = 16'777'216,  // 16 MB
            .max_instructions = 100'000,
            .max_stack_depth = 256,
            .max_allocation_size = 262'144,  // 256 KB
            .max_execution_time_ms = 1000    // 1 second
        };
    }

    // ========== Comparison ==========

    [[nodiscard]] constexpr bool operator==(const RuntimeLimits&) const noexcept = default;
};

// ============================================================================
// ResourceLimiter Class
// ============================================================================

/// @brief Runtime enforcement of resource limits
///
/// ResourceLimiter tracks resource usage (memory, instructions, stack depth,
/// time) and enforces limits. It is designed for high performance with
/// atomic operations using relaxed memory ordering.
///
/// Thread Safety: All tracking methods are thread-safe for concurrent calls.
/// However, the limits configuration should not be modified after construction.
///
/// Performance: Uses relaxed atomic operations with fast-path optimizations
/// for unlimited resources. Target overhead is <5%.
///
/// @code
/// ResourceLimiter limiter(RuntimeLimits::standard());
///
/// // Memory tracking
/// if (auto result = limiter.try_allocate(1024); !result) {
///     handle_error(result.error());
/// }
///
/// // Instruction tracking in hot loop
/// while (running) {
///     if (auto result = limiter.try_execute(); !result) {
///         break;
///     }
///     execute_instruction();
/// }
/// @endcode
class ResourceLimiter {
public:
    // ========== Construction ==========

    /// @brief Construct with specified limits and no stats tracking
    ///
    /// @param limits Resource limits to enforce (default: standard limits)
    explicit ResourceLimiter(RuntimeLimits limits = {}) noexcept;

    /// @brief Construct with limits and security stats integration
    ///
    /// @param limits Resource limits to enforce
    /// @param stats Optional security stats for violation recording
    ResourceLimiter(RuntimeLimits limits, SecurityStats* stats) noexcept;

    // Non-copyable, non-movable (contains atomic members)
    ResourceLimiter(const ResourceLimiter&) = delete;
    ResourceLimiter& operator=(const ResourceLimiter&) = delete;
    ResourceLimiter(ResourceLimiter&&) = delete;
    ResourceLimiter& operator=(ResourceLimiter&&) = delete;

    ~ResourceLimiter() = default;

    // ========== Memory Tracking ==========

    /// @brief Attempt to allocate memory
    ///
    /// Checks both single allocation size and total memory limits.
    ///
    /// @param bytes Number of bytes to allocate
    /// @return Ok if allocation is allowed, error otherwise
    ///
    /// Performance: O(1), uses relaxed atomic compare-exchange.
    [[nodiscard]] Result<void, ResourceLimitError> try_allocate(std::size_t bytes) noexcept;

    /// @brief Record memory deallocation
    ///
    /// Updates the current memory usage counter.
    ///
    /// @param bytes Number of bytes being deallocated
    ///
    /// Performance: O(1), uses relaxed atomic subtraction.
    void on_deallocate(std::size_t bytes) noexcept;

    /// @brief Get current memory usage
    [[nodiscard]] std::uint64_t current_memory() const noexcept {
        return current_memory_.load(std::memory_order_relaxed);
    }

    // ========== Instruction Tracking ==========

    /// @brief Attempt to execute a single instruction
    ///
    /// Increments the instruction counter and checks against the limit.
    ///
    /// @return Ok if execution is allowed, error otherwise
    ///
    /// Performance: O(1), optimized for hot path with unlimited check.
    [[nodiscard]] Result<void, ResourceLimitError> try_execute() noexcept;

    /// @brief Attempt to execute multiple instructions
    ///
    /// Increments the instruction counter by the specified count.
    /// Use this in JIT-compiled code or inner loops for better performance.
    ///
    /// @param count Number of instructions to execute
    /// @return Ok if execution is allowed, error otherwise
    ///
    /// Performance: O(1), batched update for reduced overhead.
    [[nodiscard]] Result<void, ResourceLimitError> try_execute_batch(std::uint64_t count) noexcept;

    /// @brief Get current instruction count
    [[nodiscard]] std::uint64_t current_instructions() const noexcept {
        return current_instructions_.load(std::memory_order_relaxed);
    }

    // ========== Stack Tracking ==========

    /// @brief Attempt to push a new stack frame
    ///
    /// Checks stack depth limit before allowing the push.
    ///
    /// @return Ok if push is allowed, error otherwise
    ///
    /// Performance: O(1), uses relaxed atomic operations.
    [[nodiscard]] Result<void, ResourceLimitError> try_push_frame() noexcept;

    /// @brief Record a stack frame pop
    ///
    /// Decrements the stack depth counter.
    ///
    /// Performance: O(1), uses relaxed atomic subtraction.
    void on_pop_frame() noexcept;

    /// @brief Get current stack depth
    [[nodiscard]] std::uint32_t current_stack_depth() const noexcept {
        return current_stack_depth_.load(std::memory_order_relaxed);
    }

    // ========== Time Tracking ==========

    /// @brief Check if execution time has expired
    ///
    /// Compares elapsed time since construction against the limit.
    ///
    /// @return true if time limit exceeded, false otherwise
    ///
    /// Performance: O(1), uses steady_clock for monotonic time.
    [[nodiscard]] bool is_time_expired() const noexcept;

    /// @brief Get elapsed time in milliseconds
    [[nodiscard]] std::uint64_t elapsed_time_ms() const noexcept;

    // ========== Control ==========

    /// @brief Reset all counters to zero
    ///
    /// Resets memory, instruction, and stack counters. Also resets the
    /// start time for time-based limits.
    ///
    /// @warning NOT thread-safe for concurrent operations. Call only when
    /// no other threads are using this limiter.
    void reset() noexcept;

    /// @brief Get the configured limits
    [[nodiscard]] const RuntimeLimits& limits() const noexcept { return limits_; }

private:
    RuntimeLimits limits_;
    SecurityStats* stats_{nullptr};

    // Counters using atomic operations for thread safety
    std::atomic<std::uint64_t> current_memory_{0};
    std::atomic<std::uint64_t> current_instructions_{0};
    std::atomic<std::uint32_t> current_stack_depth_{0};

    // Time tracking
    std::chrono::steady_clock::time_point start_time_;
};

// Ensure atomic operations are lock-free for performance
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "ResourceLimiter requires lock-free 64-bit atomics");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "ResourceLimiter requires lock-free 32-bit atomics");

}  // namespace dotvm::core::security

// ============================================================================
// std::formatter specializations
// ============================================================================

template <>
struct std::formatter<dotvm::core::security::EnforcementAction> : std::formatter<std::string_view> {
    auto format(dotvm::core::security::EnforcementAction e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};

template <>
struct std::formatter<dotvm::core::security::ResourceLimitError>
    : std::formatter<std::string_view> {
    auto format(dotvm::core::security::ResourceLimitError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
