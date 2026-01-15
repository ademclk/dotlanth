#pragma once

/// @file debug_context.hpp
/// @brief Debug mode support for DotVM execution engine (EXEC-010)
///
/// This header provides types and structures for debug mode support including
/// breakpoints, stepping, and callback-based debugging. The design targets
/// <5x overhead when debug mode is enabled.

#include <dotvm/exec/execution_context.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_set>

namespace dotvm::exec {

// ============================================================================
// Debug Event Types
// ============================================================================

/// Events that can trigger the debug callback
///
/// @note These events are raised during execution when debug mode is enabled
///       and the corresponding condition occurs.
enum class DebugEvent : std::uint8_t {
    /// Breakpoint hit (explicit DEBUG opcode or set breakpoint)
    Break = 0,

    /// Single-step completed (after step() call)
    Step = 1,

    /// Watch condition triggered (memory or register watch)
    WatchHit = 2,

    /// Exception occurred during execution
    Exception = 3
};

/// Convert DebugEvent to string representation
[[nodiscard]] constexpr const char* to_string(DebugEvent event) noexcept {
    switch (event) {
        case DebugEvent::Break:     return "Break";
        case DebugEvent::Step:      return "Step";
        case DebugEvent::WatchHit:  return "WatchHit";
        case DebugEvent::Exception: return "Exception";
    }
    return "Unknown";
}

// ============================================================================
// Debug Callback Type
// ============================================================================

/// Callback function type for debug events
///
/// The callback receives the event type and a reference to the execution
/// context at the point of the event. The callback can inspect state but
/// should not modify the execution context directly.
///
/// @param event The type of debug event that occurred
/// @param ctx Reference to the current execution context
///
/// @note Callbacks are invoked synchronously during execution. Long-running
///       callbacks will pause execution. The callback should be lightweight.
///
/// @example
/// ```cpp
/// auto callback = [](DebugEvent event, ExecutionContext& ctx) {
///     std::cout << "Debug: " << to_string(event)
///               << " at PC=" << ctx.pc << std::endl;
/// };
/// engine.set_debug_callback(callback);
/// ```
using DebugCallback = std::function<void(DebugEvent, ExecutionContext&)>;

// ============================================================================
// Debug Context
// ============================================================================

/// Debug context holding all debug-related state
///
/// This structure is kept separate from ExecutionContext to maintain
/// cache-line optimization for the hot execution path. Debug state is
/// accessed via an [[unlikely]] branch when debug mode is enabled.
///
/// @note Thread Safety: NOT thread-safe. Use one DebugContext per engine.
struct DebugContext {
    /// Whether debug mode is enabled
    ///
    /// When false, all debug checks are skipped in the dispatch loop.
    /// The check uses [[unlikely]] to help branch prediction.
    bool enabled{false};

    /// Whether single-step mode is active
    ///
    /// When true, execution will pause after each instruction and
    /// invoke the callback with DebugEvent::Step.
    bool stepping{false};

    /// Set of breakpoint addresses (program counter values)
    ///
    /// Breakpoints are checked after each instruction fetch when debug
    /// mode is enabled. Uses std::unordered_set for O(1) average lookup.
    ///
    /// @note PC values are instruction indices, not byte offsets.
    std::unordered_set<std::size_t> breakpoints;

    /// Callback function for debug events
    ///
    /// Called when a debug event occurs. If null, events are silently
    /// recorded but no callback is invoked.
    DebugCallback callback;

    // ========================================================================
    // Breakpoint Management
    // ========================================================================

    /// Set a breakpoint at the given PC
    ///
    /// @param pc Program counter value where execution should pause
    void set_breakpoint(std::size_t pc) {
        breakpoints.insert(pc);
    }

    /// Remove a breakpoint at the given PC
    ///
    /// @param pc Program counter value to remove breakpoint from
    void remove_breakpoint(std::size_t pc) {
        breakpoints.erase(pc);
    }

    /// Check if a breakpoint exists at the given PC
    ///
    /// @param pc Program counter value to check
    /// @return true if a breakpoint exists at this address
    [[nodiscard]] bool has_breakpoint(std::size_t pc) const noexcept {
        return breakpoints.contains(pc);
    }

    /// Get the number of breakpoints
    [[nodiscard]] std::size_t breakpoint_count() const noexcept {
        return breakpoints.size();
    }

    /// Clear all breakpoints
    void clear_breakpoints() noexcept {
        breakpoints.clear();
    }

    // ========================================================================
    // State Management
    // ========================================================================

    /// Clear all debug state
    ///
    /// Disables debug mode, clears all breakpoints, and removes the callback.
    void clear() noexcept {
        enabled = false;
        stepping = false;
        breakpoints.clear();
        callback = nullptr;
    }

    /// Invoke the callback if set
    ///
    /// @param event The debug event that occurred
    /// @param ctx The execution context at the point of the event
    void invoke_callback(DebugEvent event, ExecutionContext& ctx) {
        if (callback) {
            callback(event, ctx);
        }
    }
};

}  // namespace dotvm::exec
