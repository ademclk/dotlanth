#pragma once

/// @file debug_session.hpp
/// @brief TOOL-011 Debug Client - Session state management
///
/// Manages the debugger session state machine and current execution context.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dotvm::debugger {

/// @brief Debug session state
enum class SessionState : std::uint8_t {
    NotStarted = 0,  ///< Program not yet started
    Running = 1,     ///< Program is executing
    Paused = 2,      ///< Program is paused (at breakpoint or step)
    Halted = 3       ///< Program has terminated
};

/// @brief Convert session state to string
[[nodiscard]] constexpr const char* to_string(SessionState state) noexcept {
    switch (state) {
        case SessionState::NotStarted:
            return "NotStarted";
        case SessionState::Running:
            return "Running";
        case SessionState::Paused:
            return "Paused";
        case SessionState::Halted:
            return "Halted";
    }
    return "Unknown";
}

/// @brief Reason for entering paused state
enum class PauseReason : std::uint8_t {
    None = 0,        ///< Not paused
    Breakpoint = 1,  ///< Hit a breakpoint
    Step = 2,        ///< Step completed
    Watch = 3,       ///< Watchpoint triggered
    Exception = 4,   ///< Exception occurred
    UserRequest = 5  ///< User requested pause (Ctrl+C)
};

/// @brief Convert pause reason to string
[[nodiscard]] constexpr const char* to_string(PauseReason reason) noexcept {
    switch (reason) {
        case PauseReason::None:
            return "None";
        case PauseReason::Breakpoint:
            return "Breakpoint";
        case PauseReason::Step:
            return "Step";
        case PauseReason::Watch:
            return "Watch";
        case PauseReason::Exception:
            return "Exception";
        case PauseReason::UserRequest:
            return "UserRequest";
    }
    return "Unknown";
}

/// @brief Stack frame information
struct StackFrame {
    std::size_t frame_index;    ///< Frame index (0 = current)
    std::size_t pc;             ///< Program counter
    std::size_t return_pc;      ///< Return address (0 for bottom frame)
    std::string function_name;  ///< Function name (if debug info available)
    std::string source_file;    ///< Source file (if debug info available)
    std::uint32_t line_number;  ///< Line number (if debug info available)
};

/// @brief Debug session state manager
///
/// Tracks the current state of the debug session, including:
/// - Session state (NotStarted/Running/Paused/Halted)
/// - Current PC and pause reason
/// - Call stack for backtrace
/// - Selected frame for inspection
class DebugSession {
public:
    /// @brief Construct a new debug session
    DebugSession() = default;

    /// @brief Get current session state
    [[nodiscard]] SessionState state() const noexcept { return state_; }

    /// @brief Get reason for current pause (if paused)
    [[nodiscard]] PauseReason pause_reason() const noexcept { return pause_reason_; }

    /// @brief Get current program counter
    [[nodiscard]] std::size_t pc() const noexcept { return pc_; }

    /// @brief Get the ID of the breakpoint that was hit (if any)
    [[nodiscard]] std::optional<std::uint32_t> hit_breakpoint_id() const noexcept {
        return hit_breakpoint_id_;
    }

    /// @brief Get the ID of the watchpoint that was triggered (if any)
    [[nodiscard]] std::optional<std::uint32_t> triggered_watchpoint_id() const noexcept {
        return triggered_watchpoint_id_;
    }

    /// @brief Get the current call stack
    [[nodiscard]] const std::vector<StackFrame>& call_stack() const noexcept { return call_stack_; }

    /// @brief Get the currently selected frame index
    [[nodiscard]] std::size_t selected_frame() const noexcept { return selected_frame_; }

    /// @brief Transition to Running state
    void start();

    /// @brief Transition to Paused state
    /// @param pc Current program counter
    /// @param reason Why execution paused
    void pause(std::size_t pc, PauseReason reason);

    /// @brief Transition to Halted state
    void halt();

    /// @brief Resume from paused state to running
    void resume();

    /// @brief Reset session to NotStarted state
    void reset();

    /// @brief Set the breakpoint that was hit
    void set_hit_breakpoint(std::uint32_t id);

    /// @brief Set the watchpoint that was triggered
    void set_triggered_watchpoint(std::uint32_t id);

    /// @brief Update the call stack
    void update_call_stack(std::vector<StackFrame> frames);

    /// @brief Select a stack frame for inspection
    /// @param frame_index The frame index to select
    /// @return true if frame exists and was selected
    bool select_frame(std::size_t frame_index);

    /// @brief Check if the session can run (not halted)
    [[nodiscard]] bool can_run() const noexcept {
        return state_ == SessionState::NotStarted || state_ == SessionState::Paused;
    }

    /// @brief Check if the session can be stepped (paused)
    [[nodiscard]] bool can_step() const noexcept { return state_ == SessionState::Paused; }

private:
    SessionState state_{SessionState::NotStarted};
    PauseReason pause_reason_{PauseReason::None};
    std::size_t pc_{0};

    std::optional<std::uint32_t> hit_breakpoint_id_;
    std::optional<std::uint32_t> triggered_watchpoint_id_;

    std::vector<StackFrame> call_stack_;
    std::size_t selected_frame_{0};
};

}  // namespace dotvm::debugger
