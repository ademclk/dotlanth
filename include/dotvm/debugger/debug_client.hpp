#pragma once

/// @file debug_client.hpp
/// @brief TOOL-011 Debug Client - Main debugger facade
///
/// The main entry point for the interactive debugger, coordinating
/// all debugging subsystems.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/value.hpp"
#include "dotvm/core/vm_context.hpp"
#include "dotvm/debugger/breakpoint_manager.hpp"
#include "dotvm/debugger/command_parser.hpp"
#include "dotvm/debugger/debug_formatter.hpp"
#include "dotvm/debugger/debug_session.hpp"
#include "dotvm/debugger/watch_manager.hpp"
#include "dotvm/exec/execution_engine.hpp"

namespace dotvm::debugger {

/// @brief Result of executing a debugger command
struct CommandResult {
    bool success{true};       ///< Whether command succeeded
    std::string output;       ///< Output to display
    bool should_quit{false};  ///< Whether to exit the debugger
    bool needs_prompt{true};  ///< Whether to show prompt after
};

/// @brief Options for the debug client
struct DebugClientOptions {
    bool colors{true};            ///< Use ANSI colors
    bool show_hex{true};          ///< Show hex values
    bool show_decimal{true};      ///< Show decimal values
    std::size_t disasm_count{5};  ///< Default disassembly count
};

/// @brief Main debugger client facade
///
/// Coordinates the command parser, session state, breakpoint manager,
/// watch manager, and formatter to provide a complete debugging experience.
///
/// @example
/// ```cpp
/// core::VmContext vm_ctx(config);
/// exec::ExecutionEngine engine(vm_ctx);
///
/// DebugClient client(engine, vm_ctx);
/// client.load_bytecode(code, size, entry_point, constants);
///
/// while (client.active()) {
///     std::cout << client.prompt();
///     std::string line = read_line();
///     auto result = client.execute_command(line);
///     std::cout << result.output;
///     if (result.should_quit) break;
/// }
/// ```
class DebugClient {
public:
    /// @brief Construct debug client
    /// @param engine The execution engine to debug
    /// @param vm_ctx The VM context
    /// @param opts Debug client options
    DebugClient(exec::ExecutionEngine& engine, core::VmContext& vm_ctx,
                DebugClientOptions opts = {});

    ~DebugClient();

    // Non-copyable, non-movable (holds references)
    DebugClient(const DebugClient&) = delete;
    DebugClient& operator=(const DebugClient&) = delete;
    DebugClient(DebugClient&&) = delete;
    DebugClient& operator=(DebugClient&&) = delete;

    /// @brief Load bytecode for debugging
    /// @param code Pointer to instruction array
    /// @param code_size Number of instructions
    /// @param entry_point Entry point (instruction index)
    /// @param constants Constant pool
    void load_bytecode(const std::uint32_t* code, std::size_t code_size, std::size_t entry_point,
                       std::span<const core::Value> constants);

    /// @brief Execute a debugger command
    /// @param line The command line to execute
    /// @return Result containing output and status
    [[nodiscard]] CommandResult execute_command(std::string_view line);

    /// @brief Get the current prompt string
    [[nodiscard]] std::string prompt() const;

    /// @brief Check if the debugger is active (not exited)
    [[nodiscard]] bool active() const noexcept { return active_; }

    /// @brief Get the session state
    [[nodiscard]] const DebugSession& session() const noexcept { return session_; }

    /// @brief Get the breakpoint manager
    [[nodiscard]] const BreakpointManager& breakpoints() const noexcept { return breakpoints_; }

    /// @brief Get the watch manager
    [[nodiscard]] const WatchManager& watches() const noexcept { return watches_; }

    /// @brief Register a custom command alias
    void register_alias(std::string alias, std::string expansion);

private:
    // Command handlers
    CommandResult cmd_run(const ParsedCommand& cmd);
    CommandResult cmd_continue(const ParsedCommand& cmd);
    CommandResult cmd_step(const ParsedCommand& cmd);
    CommandResult cmd_next(const ParsedCommand& cmd);
    CommandResult cmd_finish(const ParsedCommand& cmd);
    CommandResult cmd_quit(const ParsedCommand& cmd);

    CommandResult cmd_breakpoint(const ParsedCommand& cmd);
    CommandResult cmd_register(const ParsedCommand& cmd);
    CommandResult cmd_memory(const ParsedCommand& cmd);
    CommandResult cmd_disassemble(const ParsedCommand& cmd);
    CommandResult cmd_backtrace(const ParsedCommand& cmd);
    CommandResult cmd_frame(const ParsedCommand& cmd);

    CommandResult cmd_watchpoint(const ParsedCommand& cmd);
    CommandResult cmd_source(const ParsedCommand& cmd);
    CommandResult cmd_help(const ParsedCommand& cmd);

    // Execution helpers
    void run_until_break();
    void step_single();
    void setup_debug_callback();
    void sync_breakpoints_to_engine();
    void update_session_state();

    // Parsing helpers
    [[nodiscard]] std::optional<std::size_t> parse_address(std::string_view s) const;
    [[nodiscard]] std::optional<std::uint32_t> parse_id(std::string_view s) const;

    // References to external state
    exec::ExecutionEngine& engine_;
    core::VmContext& vm_ctx_;

    // Internal state
    DebugClientOptions opts_;
    CommandParser parser_;
    DebugSession session_;
    BreakpointManager breakpoints_;
    WatchManager watches_;
    DebugFormatter formatter_;

    // Program state
    const std::uint32_t* code_{nullptr};
    std::size_t code_size_{0};
    std::size_t entry_point_{0};
    std::span<const core::Value> constants_;

    bool active_{true};
    bool program_loaded_{false};
};

}  // namespace dotvm::debugger
