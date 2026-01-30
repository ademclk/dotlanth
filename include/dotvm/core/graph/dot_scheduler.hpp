#pragma once

/// @file dot_scheduler.hpp
/// @brief Thread-safe scheduler for Dot execution with dependency tracking
///
/// Provides a DotScheduler class that manages Dot execution ordering based on
/// dependencies, with priority-based scheduling and handle-based access with
/// ABA protection.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dotvm/core/graph/dependency_graph.hpp"
#include "dotvm/core/result.hpp"

namespace dotvm::core::graph {

// ============================================================================
// DotHandle Struct
// ============================================================================

/// @brief Handle to a scheduled Dot with ABA protection
///
/// Uses index + generation counter to prevent ABA problem when slots are
/// reused. Total size is 8 bytes for efficient copying.
struct DotHandle {
    /// @brief Slot index in the scheduler's internal storage
    std::uint32_t index{0};

    /// @brief Generation counter for ABA protection
    std::uint32_t generation{0};

    /// @brief Check if this handle is valid (non-zero generation)
    [[nodiscard]] constexpr bool is_valid() const noexcept { return generation != 0; }

    /// @brief Create an invalid handle
    [[nodiscard]] static constexpr DotHandle invalid() noexcept { return DotHandle{0, 0}; }

    /// @brief Equality comparison
    [[nodiscard]] constexpr bool operator==(const DotHandle& other) const noexcept {
        return index == other.index && generation == other.generation;
    }

    /// @brief Inequality comparison
    [[nodiscard]] constexpr bool operator!=(const DotHandle& other) const noexcept {
        return !(*this == other);
    }
};

// ============================================================================
// DotState Enum
// ============================================================================

/// @brief Execution state of a scheduled Dot
enum class DotState : std::uint8_t {
    /// Waiting for dependencies to complete
    Pending = 0,

    /// All dependencies met, waiting in ready queue
    Ready = 1,

    /// Currently being executed by a worker
    Running = 2,

    /// Execution completed successfully
    Done = 3,

    /// Execution failed with an error
    Failed = 4,

    /// Cancelled before completion
    Cancelled = 5,
};

/// @brief Convert DotState to human-readable string
[[nodiscard]] constexpr std::string_view to_string(DotState state) noexcept {
    switch (state) {
        case DotState::Pending:
            return "Pending";
        case DotState::Ready:
            return "Ready";
        case DotState::Running:
            return "Running";
        case DotState::Done:
            return "Done";
        case DotState::Failed:
            return "Failed";
        case DotState::Cancelled:
            return "Cancelled";
    }
    return "Unknown";
}

// ============================================================================
// SchedulerError Enum
// ============================================================================

/// @brief Error codes for scheduler operations (160-179 range)
enum class SchedulerError : std::uint8_t {
    /// Handle does not refer to a valid scheduled Dot
    HandleNotFound = 160,

    /// Maximum number of pending Dots exceeded
    MaxPendingExceeded = 161,

    /// Dot has already been cancelled
    DotAlreadyCancelled = 162,

    /// Dot has already completed (Done or Failed)
    DotAlreadyCompleted = 163,

    /// A dependency handle is invalid
    DependencyNotFound = 164,

    /// Priority value is out of valid range
    InvalidPriority = 165,

    /// Bytecode span is empty or invalid
    InvalidBytecode = 166,

    /// Scheduler is shutting down, rejecting new submissions
    ShuttingDown = 167,

    /// Wait operation timed out
    WaitTimeout = 168,

    /// Internal dependency graph error
    InternalGraphError = 169,

    /// Dot execution failed
    ExecutionFailed = 170,

    /// Handle generation mismatch (stale handle)
    StaleHandle = 171,

    /// Dot is not in Running state
    DotNotRunning = 172,

    /// Dependency would create a cycle
    CycleDetected = 173,
};

/// @brief Convert SchedulerError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(SchedulerError error) noexcept {
    switch (error) {
        case SchedulerError::HandleNotFound:
            return "HandleNotFound";
        case SchedulerError::MaxPendingExceeded:
            return "MaxPendingExceeded";
        case SchedulerError::DotAlreadyCancelled:
            return "DotAlreadyCancelled";
        case SchedulerError::DotAlreadyCompleted:
            return "DotAlreadyCompleted";
        case SchedulerError::DependencyNotFound:
            return "DependencyNotFound";
        case SchedulerError::InvalidPriority:
            return "InvalidPriority";
        case SchedulerError::InvalidBytecode:
            return "InvalidBytecode";
        case SchedulerError::ShuttingDown:
            return "ShuttingDown";
        case SchedulerError::WaitTimeout:
            return "WaitTimeout";
        case SchedulerError::InternalGraphError:
            return "InternalGraphError";
        case SchedulerError::ExecutionFailed:
            return "ExecutionFailed";
        case SchedulerError::StaleHandle:
            return "StaleHandle";
        case SchedulerError::DotNotRunning:
            return "DotNotRunning";
        case SchedulerError::CycleDetected:
            return "CycleDetected";
    }
    return "Unknown";
}

// ============================================================================
// SchedulerConfig Struct
// ============================================================================

/// @brief Configuration for DotScheduler
struct SchedulerConfig {
    /// Maximum number of pending Dots allowed
    std::size_t max_pending{10000};

    /// Minimum valid priority value
    std::int32_t min_priority{-1000};

    /// Maximum valid priority value
    std::int32_t max_priority{1000};

    /// @brief Default configuration
    [[nodiscard]] static constexpr SchedulerConfig defaults() noexcept { return SchedulerConfig{}; }
};

// ============================================================================
// SchedulerStats Struct
// ============================================================================

/// @brief Statistics about scheduler state
struct SchedulerStats {
    /// Number of Dots in Pending state
    std::size_t pending_count{0};

    /// Number of Dots in Ready state
    std::size_t ready_count{0};

    /// Number of Dots in Running state
    std::size_t running_count{0};

    /// Number of Dots in Done state
    std::size_t done_count{0};

    /// Number of Dots in Failed state
    std::size_t failed_count{0};

    /// Number of Dots in Cancelled state
    std::size_t cancelled_count{0};

    /// Total number of Dots ever submitted
    std::size_t total_submitted{0};
};

// ============================================================================
// DotScheduler Class
// ============================================================================

/// @brief Thread-safe scheduler for Dot execution with dependency tracking
///
/// DotScheduler manages the execution order of Dots based on their
/// dependencies. It provides:
/// - Handle-based access with ABA protection via generation counters
/// - Priority-based scheduling within ready Dots
/// - Integration with DependencyGraph for dependency tracking
/// - Thread-safe operations with shared_mutex for read/write separation
///
/// Workers should:
/// 1. Call try_pop_ready() to get a Dot to execute
/// 2. Call get_bytecode() to retrieve the bytecode
/// 3. Execute the bytecode
/// 4. Call notify_complete() or notify_failed()
class DotScheduler {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, SchedulerError>;

    /// @brief Construct a DotScheduler with optional configuration
    explicit DotScheduler(SchedulerConfig config = SchedulerConfig::defaults()) noexcept;

    /// @brief Destructor
    ~DotScheduler() = default;

    // Non-copyable, non-movable (contains mutex and condition_variable)
    DotScheduler(const DotScheduler&) = delete;
    DotScheduler& operator=(const DotScheduler&) = delete;
    DotScheduler(DotScheduler&&) = delete;
    DotScheduler& operator=(DotScheduler&&) = delete;

    // ========================================================================
    // Submission Operations
    // ========================================================================

    /// @brief Submit a new Dot for scheduling
    ///
    /// @param bytecode The bytecode to execute (copied internally)
    /// @param dependencies Handles of Dots that must complete first
    /// @param priority Scheduling priority (higher = more urgent)
    /// @return Handle to the submitted Dot, or error
    [[nodiscard]] Result<DotHandle> submit(std::span<const std::uint8_t> bytecode,
                                           std::span<const DotHandle> dependencies = {},
                                           std::int32_t priority = 0) noexcept;

    // ========================================================================
    // State Query Operations
    // ========================================================================

    /// @brief Get the current state of a scheduled Dot
    [[nodiscard]] Result<DotState> get_state(DotHandle handle) const noexcept;

    /// @brief Get the bytecode for a Dot (only valid for Running Dots)
    [[nodiscard]] Result<std::span<const std::uint8_t>>
    get_bytecode(DotHandle handle) const noexcept;

    // ========================================================================
    // Worker Interface
    // ========================================================================

    /// @brief Try to pop a ready Dot for execution
    ///
    /// Returns the highest-priority ready Dot and transitions it to Running.
    /// Returns nullopt if no Dots are ready.
    [[nodiscard]] std::optional<DotHandle> try_pop_ready() noexcept;

    /// @brief Notify that a Dot has completed successfully
    [[nodiscard]] Result<void> notify_complete(DotHandle handle) noexcept;

    /// @brief Notify that a Dot has failed
    [[nodiscard]] Result<void> notify_failed(DotHandle handle) noexcept;

    // ========================================================================
    // Wait Operations
    // ========================================================================

    /// @brief Wait for a Dot to reach a terminal state (Done, Failed, Cancelled)
    [[nodiscard]] Result<void> wait(DotHandle handle) noexcept;

    /// @brief Wait for a Dot with a timeout
    template <typename Rep, typename Period>
    [[nodiscard]] Result<void> wait_for(DotHandle handle,
                                        const std::chrono::duration<Rep, Period>& timeout) noexcept;

    // ========================================================================
    // Cancellation
    // ========================================================================

    /// @brief Cancel a Dot
    ///
    /// - If Pending: Transitions to Cancelled
    /// - If Ready: Removes from queue, transitions to Cancelled
    /// - If Running: Transitions to Cancelled (cooperative, worker should check)
    /// - If Done/Failed/Cancelled: Returns DotAlreadyCompleted
    [[nodiscard]] Result<void> cancel(DotHandle handle) noexcept;

    // ========================================================================
    // Statistics and Lifecycle
    // ========================================================================

    /// @brief Get current scheduler statistics
    [[nodiscard]] SchedulerStats stats() const noexcept;

    /// @brief Initiate graceful shutdown
    ///
    /// Rejects new submissions and cancels all pending Dots.
    void shutdown() noexcept;

    /// @brief Check if scheduler is shutting down
    [[nodiscard]] bool is_shutting_down() const noexcept;

private:
    /// @brief Internal representation of a scheduled Dot
    struct ScheduledDot {
        std::vector<std::uint8_t> bytecode;
        DotState state{DotState::Pending};
        std::int32_t priority{0};
        std::uint32_t generation{0};
        DotId graph_id{0};  // ID in the dependency graph
    };

    /// @brief Entry in the ready queue (priority queue)
    struct ReadyEntry {
        DotHandle handle;
        std::int32_t priority;
        std::uint64_t submit_order;  // For FIFO ordering at same priority

        /// @brief Comparison for max-heap (higher priority first, earlier submit first)
        [[nodiscard]] bool operator<(const ReadyEntry& other) const noexcept {
            if (priority != other.priority) {
                return priority < other.priority;  // Higher priority first
            }
            return submit_order > other.submit_order;  // Earlier submit first (FIFO)
        }
    };

    SchedulerConfig config_;
    DependencyGraph graph_;

    std::vector<ScheduledDot> dots_;
    std::vector<std::uint32_t> free_indices_;
    std::priority_queue<ReadyEntry> ready_queue_;
    std::unordered_map<DotId, std::uint32_t> graph_id_to_index_;  // O(1) lookup
    std::uint64_t next_submit_order_{0};
    std::uint32_t next_graph_id_{1};  // Start at 1 (0 is reserved)

    // Statistics counters
    std::size_t pending_count_{0};
    std::size_t ready_count_{0};
    std::size_t running_count_{0};
    std::size_t done_count_{0};
    std::size_t failed_count_{0};
    std::size_t cancelled_count_{0};
    std::size_t total_submitted_{0};

    bool shutting_down_{false};

    mutable std::shared_mutex mutex_;
    std::condition_variable_any completion_cv_;

    // Internal helpers
    [[nodiscard]] bool validate_handle_unlocked(DotHandle handle) const noexcept;
    [[nodiscard]] bool validate_handle_with_generation_unlocked(DotHandle handle) const noexcept;
    [[nodiscard]] DotHandle allocate_slot_unlocked(std::vector<std::uint8_t> bytecode,
                                                   std::int32_t priority) noexcept;
    void transition_to_ready_unlocked(std::uint32_t index) noexcept;
    void update_stats_for_transition_unlocked(DotState from, DotState to) noexcept;
    [[nodiscard]] static bool is_terminal_state(DotState state) noexcept;
    void propagate_failure_to_dependents_unlocked(DotId graph_id) noexcept;
    void cleanup_terminal_node_unlocked(std::uint32_t index) noexcept;
};

// ============================================================================
// Template Implementation
// ============================================================================

template <typename Rep, typename Period>
DotScheduler::Result<void>
DotScheduler::wait_for(DotHandle handle,
                       const std::chrono::duration<Rep, Period>& timeout) noexcept {
    std::shared_lock lock(mutex_);

    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    const ScheduledDot& dot = dots_[handle.index];
    if (dot.generation != handle.generation) {
        return SchedulerError::StaleHandle;
    }

    if (is_terminal_state(dot.state)) {
        if (dot.state == DotState::Failed) {
            return SchedulerError::ExecutionFailed;
        }
        return Result<void>{};
    }

    // Upgrade to unique lock for condition variable wait
    lock.unlock();
    std::unique_lock ulock(mutex_);

    // Re-validate after reacquiring lock
    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    const bool completed = completion_cv_.wait_for(ulock, timeout, [this, handle]() {
        if (!validate_handle_with_generation_unlocked(handle)) {
            return true;  // Handle invalid or stale, stop waiting
        }
        return is_terminal_state(dots_[handle.index].state);
    });

    if (!completed) {
        return SchedulerError::WaitTimeout;
    }

    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    const ScheduledDot& final_dot = dots_[handle.index];
    if (final_dot.generation != handle.generation) {
        return SchedulerError::StaleHandle;
    }

    if (final_dot.state == DotState::Failed) {
        return SchedulerError::ExecutionFailed;
    }

    return Result<void>{};
}

}  // namespace dotvm::core::graph

// ============================================================================
// std::formatter specializations
// ============================================================================

template <>
struct std::formatter<dotvm::core::graph::DotHandle> : std::formatter<std::string> {
    auto format(const dotvm::core::graph::DotHandle& h, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "DotHandle{{index={}, gen={}}}", h.index, h.generation);
    }
};

template <>
struct std::formatter<dotvm::core::graph::DotState> : std::formatter<std::string_view> {
    auto format(dotvm::core::graph::DotState state, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(state), ctx);
    }
};

template <>
struct std::formatter<dotvm::core::graph::SchedulerError> : std::formatter<std::string_view> {
    auto format(dotvm::core::graph::SchedulerError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};

// Hash specialization for DotHandle (needed for unordered containers)
template <>
struct std::hash<dotvm::core::graph::DotHandle> {
    std::size_t operator()(const dotvm::core::graph::DotHandle& h) const noexcept {
        // Combine index and generation into a single hash
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(h.generation) << 32) |
                                          h.index);
    }
};
