/// @file debug_client.cpp
/// @brief TOOL-011 Debug Client - Main debugger implementation

#include "dotvm/debugger/debug_client.hpp"

#include <charconv>
#include <sstream>

#include "dotvm/core/disassembler.hpp"

namespace dotvm::debugger {

DebugClient::DebugClient(exec::ExecutionEngine& engine, core::VmContext& vm_ctx,
                         DebugClientOptions opts)
    : engine_(engine),
      vm_ctx_(vm_ctx),
      opts_(opts),
      formatter_(FormatOptions{.show_hex = opts.show_hex,
                               .show_decimal = opts.show_decimal,
                               .use_colors = opts.colors,
                               .hex_width = 16}) {
    // Enable debug mode on the engine
    engine_.enable_debug(true);
    setup_debug_callback();
}

DebugClient::~DebugClient() = default;

void DebugClient::load_bytecode(const std::uint32_t* code, std::size_t code_size,
                                std::size_t entry_point, std::span<const core::Value> constants) {
    code_ = code;
    code_size_ = code_size;
    entry_point_ = entry_point;
    constants_ = constants;
    program_loaded_ = true;
    session_.reset();
}

void DebugClient::setup_debug_callback() {
    engine_.set_debug_callback([this](exec::DebugEvent event, exec::ExecutionContext& ctx) {
        switch (event) {
            case exec::DebugEvent::Break:
                session_.pause(ctx.pc, PauseReason::Breakpoint);
                break;
            case exec::DebugEvent::Step:
                session_.pause(ctx.pc, PauseReason::Step);
                break;
            case exec::DebugEvent::WatchHit:
                session_.pause(ctx.pc, PauseReason::Watch);
                break;
            case exec::DebugEvent::Exception:
                session_.pause(ctx.pc, PauseReason::Exception);
                break;
        }
    });
}

void DebugClient::sync_breakpoints_to_engine() {
    // Clear existing breakpoints in engine
    engine_.clear_breakpoints();

    // Add all active breakpoints
    for (std::size_t addr : breakpoints_.get_active_addresses()) {
        engine_.set_breakpoint(addr);
    }
}

std::string DebugClient::prompt() const {
    return formatter_.format_prompt(session_.state(), session_.pc());
}

CommandResult DebugClient::execute_command(std::string_view line) {
    auto cmd = parser_.parse(line);

    if (cmd.empty()) {
        return {true, "", false, true};
    }

    // Dispatch to command handlers
    if (cmd.command == "run")
        return cmd_run(cmd);
    if (cmd.command == "continue")
        return cmd_continue(cmd);
    if (cmd.command == "step")
        return cmd_step(cmd);
    if (cmd.command == "next")
        return cmd_next(cmd);
    if (cmd.command == "finish")
        return cmd_finish(cmd);
    if (cmd.command == "quit")
        return cmd_quit(cmd);
    if (cmd.command == "breakpoint")
        return cmd_breakpoint(cmd);
    if (cmd.command == "register")
        return cmd_register(cmd);
    if (cmd.command == "memory")
        return cmd_memory(cmd);
    if (cmd.command == "disassemble")
        return cmd_disassemble(cmd);
    if (cmd.command == "backtrace")
        return cmd_backtrace(cmd);
    if (cmd.command == "frame")
        return cmd_frame(cmd);
    if (cmd.command == "watchpoint")
        return cmd_watchpoint(cmd);
    if (cmd.command == "source")
        return cmd_source(cmd);
    if (cmd.command == "help")
        return cmd_help(cmd);

    return {false, "Unknown command: " + cmd.command + "\nType 'help' for commands.\n"};
}

void DebugClient::register_alias(std::string alias, std::string expansion) {
    parser_.register_alias(std::move(alias), std::move(expansion));
}

// ============================================================================
// Command Handlers
// ============================================================================

CommandResult DebugClient::cmd_run(const ParsedCommand& /*cmd*/) {
    if (!program_loaded_) {
        return {false, "No program loaded.\n"};
    }

    // Sync breakpoints to engine
    sync_breakpoints_to_engine();

    // Start execution
    session_.start();
    auto result = engine_.execute(code_, code_size_, entry_point_, constants_);

    // Check result
    if (result == exec::ExecResult::Success) {
        session_.halt();
        return {true, "Program completed successfully.\n"};
    }
    if (result == exec::ExecResult::Interrupted) {
        // Paused at breakpoint - session already updated by callback
        auto bp_result = breakpoints_.check_simple(session_.pc());
        if (bp_result.id.has_value()) {
            session_.set_hit_breakpoint(bp_result.id.value());
            return {true, "Breakpoint " + std::to_string(bp_result.id.value()) + " hit at " +
                              formatter_.format_address(session_.pc()) + "\n"};
        }
        return {true, "Stopped at " + formatter_.format_address(session_.pc()) + "\n"};
    }

    session_.halt();
    return {true, "Program terminated: " + std::string(exec::to_string(result)) + "\n"};
}

CommandResult DebugClient::cmd_continue(const ParsedCommand& /*cmd*/) {
    if (session_.state() == SessionState::NotStarted) {
        return {false, "Program not started. Use 'run' to begin.\n"};
    }
    if (session_.state() == SessionState::Halted) {
        return {false, "Program has halted. Use 'run' to restart.\n"};
    }
    if (session_.state() != SessionState::Paused) {
        return {false, "Program is not paused. Cannot continue.\n"};
    }

    session_.resume();
    auto result = engine_.continue_execution();

    if (result == exec::ExecResult::Success) {
        session_.halt();
        return {true, "Program completed successfully.\n"};
    }
    if (result == exec::ExecResult::Interrupted) {
        auto bp_result = breakpoints_.check_simple(engine_.pc());
        if (bp_result.id.has_value()) {
            session_.set_hit_breakpoint(bp_result.id.value());
            session_.pause(engine_.pc(), PauseReason::Breakpoint);
            return {true, "Breakpoint " + std::to_string(bp_result.id.value()) + " hit at " +
                              formatter_.format_address(engine_.pc()) + "\n"};
        }
        session_.pause(engine_.pc(), PauseReason::Breakpoint);
        return {true, "Stopped at " + formatter_.format_address(engine_.pc()) + "\n"};
    }

    session_.halt();
    return {true, "Program terminated: " + std::string(exec::to_string(result)) + "\n"};
}

CommandResult DebugClient::cmd_step(const ParsedCommand& /*cmd*/) {
    if (!session_.can_step() && session_.state() != SessionState::NotStarted) {
        if (session_.state() == SessionState::Halted) {
            return {false, "Program has halted. Use 'run' to restart.\n"};
        }
        return {false, "Program is running. Cannot step.\n"};
    }

    if (session_.state() == SessionState::NotStarted) {
        // First step - need to execute with stepping mode
        sync_breakpoints_to_engine();
        session_.start();
    }

    auto result = engine_.step_into();
    session_.pause(engine_.pc(), PauseReason::Step);

    std::ostringstream oss;
    oss << "Stepped to " << formatter_.format_address(engine_.pc()) << "\n";

    // Show current instruction
    if (engine_.pc() < code_size_) {
        std::span<const std::uint8_t> instr_bytes(
            reinterpret_cast<const std::uint8_t*>(&code_[engine_.pc()]), sizeof(std::uint32_t));
        auto instr =
            core::decode_instruction(instr_bytes, static_cast<std::uint32_t>(engine_.pc() * 4));
        oss << "   " << formatter_.format_instruction(instr, true) << "\n";
    }

    if (result != exec::ExecResult::Success && result != exec::ExecResult::Interrupted) {
        session_.halt();
        oss << "Program terminated: " << exec::to_string(result) << "\n";
    }

    return {true, oss.str()};
}

CommandResult DebugClient::cmd_next(const ParsedCommand& /*cmd*/) {
    // For now, step over is the same as step (would need call detection for proper impl)
    return cmd_step(ParsedCommand{});
}

CommandResult DebugClient::cmd_finish(const ParsedCommand& /*cmd*/) {
    if (!session_.can_step()) {
        return {false, "Cannot finish - program not paused.\n"};
    }

    // Simple implementation: continue until return or halt
    // A proper implementation would track call depth
    return cmd_continue(ParsedCommand{});
}

CommandResult DebugClient::cmd_quit(const ParsedCommand& /*cmd*/) {
    active_ = false;
    return {true, "Goodbye.\n", true};
}

CommandResult DebugClient::cmd_breakpoint(const ParsedCommand& cmd) {
    if (cmd.arg_count() == 0) {
        // List breakpoints
        auto bps = breakpoints_.list();
        return {true, formatter_.format_breakpoints(bps)};
    }

    std::string_view subcmd = cmd.arg(0);

    if (subcmd == "list") {
        auto bps = breakpoints_.list();
        return {true, formatter_.format_breakpoints(bps)};
    }

    if (subcmd == "set") {
        // Parse address from --address or positional arg
        std::optional<std::size_t> addr;
        for (std::size_t i = 1; i < cmd.arg_count(); ++i) {
            if (cmd.arg(i) == "--address" && i + 1 < cmd.arg_count()) {
                addr = parse_address(cmd.arg(i + 1));
                break;
            } else if (cmd.arg(i).starts_with("0x") ||
                       std::isdigit(static_cast<unsigned char>(cmd.arg(i)[0]))) {
                addr = parse_address(cmd.arg(i));
                break;
            }
        }

        if (!addr.has_value()) {
            return {false, "Usage: breakpoint set <address>\n"};
        }

        std::uint32_t id = breakpoints_.set(addr.value());
        sync_breakpoints_to_engine();
        return {true, "Breakpoint " + std::to_string(id) + " set at " +
                          formatter_.format_address(addr.value()) + "\n"};
    }

    if (subcmd == "delete") {
        if (cmd.arg_count() < 2) {
            return {false, "Usage: breakpoint delete <id>\n"};
        }
        auto id = parse_id(cmd.arg(1));
        if (!id.has_value()) {
            return {false, "Invalid breakpoint ID.\n"};
        }
        if (breakpoints_.remove(id.value())) {
            sync_breakpoints_to_engine();
            return {true, "Breakpoint " + std::to_string(id.value()) + " deleted.\n"};
        }
        return {false, "Breakpoint " + std::to_string(id.value()) + " not found.\n"};
    }

    if (subcmd == "enable") {
        if (cmd.arg_count() < 2) {
            return {false, "Usage: breakpoint enable <id>\n"};
        }
        auto id = parse_id(cmd.arg(1));
        if (!id.has_value()) {
            return {false, "Invalid breakpoint ID.\n"};
        }
        if (breakpoints_.enable(id.value())) {
            sync_breakpoints_to_engine();
            return {true, "Breakpoint " + std::to_string(id.value()) + " enabled.\n"};
        }
        return {false, "Breakpoint " + std::to_string(id.value()) + " not found.\n"};
    }

    if (subcmd == "disable") {
        if (cmd.arg_count() < 2) {
            return {false, "Usage: breakpoint disable <id>\n"};
        }
        auto id = parse_id(cmd.arg(1));
        if (!id.has_value()) {
            return {false, "Invalid breakpoint ID.\n"};
        }
        if (breakpoints_.disable(id.value())) {
            sync_breakpoints_to_engine();
            return {true, "Breakpoint " + std::to_string(id.value()) + " disabled.\n"};
        }
        return {false, "Breakpoint " + std::to_string(id.value()) + " not found.\n"};
    }

    if (subcmd == "condition") {
        if (cmd.arg_count() < 3) {
            return {false, "Usage: breakpoint condition <id> <expression>\n"};
        }
        auto id = parse_id(cmd.arg(1));
        if (!id.has_value()) {
            return {false, "Invalid breakpoint ID.\n"};
        }
        std::string condition{cmd.arg(2)};
        if (breakpoints_.set_condition(id.value(), condition)) {
            return {true, "Condition set on breakpoint " + std::to_string(id.value()) + ".\n"};
        }
        return {false, "Breakpoint " + std::to_string(id.value()) + " not found.\n"};
    }

    // Handle 'b <addr>' shorthand (from alias expansion)
    auto addr = parse_address(subcmd);
    if (addr.has_value()) {
        std::uint32_t id = breakpoints_.set(addr.value());
        sync_breakpoints_to_engine();
        return {true, "Breakpoint " + std::to_string(id) + " set at " +
                          formatter_.format_address(addr.value()) + "\n"};
    }

    return {false, "Unknown breakpoint subcommand: " + std::string(subcmd) + "\n"};
}

CommandResult DebugClient::cmd_register(const ParsedCommand& cmd) {
    std::vector<std::pair<std::uint8_t, core::Value>> regs;

    if (cmd.arg_count() == 0 || (cmd.arg_count() == 1 && cmd.arg(0) == "read")) {
        // Show all registers (R0-R15)
        for (std::uint8_t i = 0; i < 16; ++i) {
            regs.emplace_back(i, vm_ctx_.registers().read(i));
        }
    } else {
        // Show specific registers
        std::size_t start = (cmd.arg(0) == "read") ? 1 : 0;
        for (std::size_t i = start; i < cmd.arg_count(); ++i) {
            std::string_view arg = cmd.arg(i);
            // Parse register name like "r1" or "R1"
            if ((arg[0] == 'r' || arg[0] == 'R') && arg.length() > 1) {
                std::uint8_t idx = 0;
                auto result = std::from_chars(arg.data() + 1, arg.data() + arg.size(), idx);
                if (result.ec == std::errc()) {
                    regs.emplace_back(idx, vm_ctx_.registers().read(idx));
                }
            }
        }
    }

    return {true, formatter_.format_registers(regs)};
}

CommandResult DebugClient::cmd_memory(const ParsedCommand& cmd) {
    if (cmd.arg_count() < 3) {
        return {false, "Usage: memory read <handle> <offset> <size>\n"};
    }

    std::size_t start = (cmd.arg(0) == "read") ? 1 : 0;

    auto handle_idx = parse_id(cmd.arg(start));
    auto offset = parse_address(cmd.arg(start + 1));
    auto size = parse_address(cmd.arg(start + 2));

    if (!handle_idx.has_value() || !offset.has_value() || !size.has_value()) {
        return {false, "Invalid arguments. Usage: memory read <handle> <offset> <size>\n"};
    }

    core::Handle handle{handle_idx.value(), 0};
    auto data = engine_.inspect_memory(handle, offset.value(), size.value());

    return {true, formatter_.format_memory(offset.value(), data)};
}

CommandResult DebugClient::cmd_disassemble(const ParsedCommand& cmd) {
    if (code_ == nullptr || code_size_ == 0) {
        return {false, "No code loaded.\n"};
    }

    std::size_t count = opts_.disasm_count;

    // Parse --count option
    for (std::size_t i = 0; i < cmd.arg_count(); ++i) {
        if ((cmd.arg(i) == "--count" || cmd.arg(i) == "-c") && i + 1 < cmd.arg_count()) {
            auto parsed = parse_address(cmd.arg(i + 1));
            if (parsed.has_value()) {
                count = parsed.value();
            }
        }
    }

    std::size_t pc = engine_.pc();
    std::size_t start = pc;
    std::size_t end = std::min(start + count, code_size_);

    // Convert to byte array for disassembler
    std::span<const std::uint8_t> code_bytes(reinterpret_cast<const std::uint8_t*>(code_ + start),
                                             (end - start) * sizeof(std::uint32_t));

    auto instructions = core::disassemble(code_bytes);

    // Adjust addresses for the actual start position
    for (auto& instr : instructions) {
        instr.address += static_cast<std::uint32_t>(start * 4);
    }

    return {true, formatter_.format_disassembly(instructions, pc)};
}

CommandResult DebugClient::cmd_backtrace(const ParsedCommand& /*cmd*/) {
    // Build stack frames from VM context
    std::vector<StackFrame> frames;

    // Current frame
    frames.push_back(StackFrame{.frame_index = 0,
                                .pc = engine_.pc(),
                                .return_pc = 0,
                                .function_name = "",
                                .source_file = "",
                                .line_number = 0});

    // TODO: Get actual call stack from VM context when available
    // For now, just show current frame

    session_.update_call_stack(frames);
    return {true, formatter_.format_backtrace(frames, session_.selected_frame())};
}

CommandResult DebugClient::cmd_frame(const ParsedCommand& cmd) {
    if (cmd.arg_count() < 1) {
        return {false, "Usage: frame select <n>\n"};
    }

    std::size_t start = (cmd.arg(0) == "select") ? 1 : 0;
    if (start >= cmd.arg_count()) {
        return {false, "Usage: frame select <n>\n"};
    }

    auto idx = parse_address(cmd.arg(start));
    if (!idx.has_value()) {
        return {false, "Invalid frame index.\n"};
    }

    if (session_.select_frame(idx.value())) {
        return {true, "Selected frame #" + std::to_string(idx.value()) + "\n"};
    }
    return {false, "Frame " + std::to_string(idx.value()) + " does not exist.\n"};
}

CommandResult DebugClient::cmd_watchpoint(const ParsedCommand& cmd) {
    if (cmd.arg_count() == 0) {
        auto wps = watches_.list();
        return {true, formatter_.format_watchpoints(wps)};
    }

    std::string_view subcmd = cmd.arg(0);

    if (subcmd == "list") {
        auto wps = watches_.list();
        return {true, formatter_.format_watchpoints(wps)};
    }

    if (subcmd == "set") {
        if (cmd.arg_count() < 4) {
            return {false, "Usage: watchpoint set <handle> <offset> <size>\n"};
        }
        auto handle_idx = parse_id(cmd.arg(1));
        auto offset = parse_address(cmd.arg(2));
        auto size = parse_address(cmd.arg(3));

        if (!handle_idx.has_value() || !offset.has_value() || !size.has_value()) {
            return {false, "Invalid arguments.\n"};
        }

        core::Handle handle{handle_idx.value(), 0};
        std::uint32_t id = watches_.set(handle, offset.value(), size.value());
        return {true, "Watchpoint " + std::to_string(id) + " set.\n"};
    }

    if (subcmd == "delete") {
        if (cmd.arg_count() < 2) {
            return {false, "Usage: watchpoint delete <id>\n"};
        }
        auto id = parse_id(cmd.arg(1));
        if (!id.has_value()) {
            return {false, "Invalid watchpoint ID.\n"};
        }
        if (watches_.remove(id.value())) {
            return {true, "Watchpoint " + std::to_string(id.value()) + " deleted.\n"};
        }
        return {false, "Watchpoint " + std::to_string(id.value()) + " not found.\n"};
    }

    // Handle 'w <h> <off> <sz>' shorthand
    auto handle_idx = parse_id(subcmd);
    if (handle_idx.has_value() && cmd.arg_count() >= 3) {
        auto offset = parse_address(cmd.arg(1));
        auto size = parse_address(cmd.arg(2));
        if (offset.has_value() && size.has_value()) {
            core::Handle handle{handle_idx.value(), 0};
            std::uint32_t id = watches_.set(handle, offset.value(), size.value());
            return {true, "Watchpoint " + std::to_string(id) + " set.\n"};
        }
    }

    return {false, "Unknown watchpoint subcommand: " + std::string(subcmd) + "\n"};
}

CommandResult DebugClient::cmd_source(const ParsedCommand& /*cmd*/) {
    // Source mapping not yet implemented
    return {true, "Source mapping not available.\n"};
}

CommandResult DebugClient::cmd_help(const ParsedCommand& /*cmd*/) {
    return {true, formatter_.format_help()};
}

// ============================================================================
// Parsing Helpers
// ============================================================================

std::optional<std::size_t> DebugClient::parse_address(std::string_view s) const {
    if (s.empty()) {
        return std::nullopt;
    }

    std::size_t value = 0;
    int base = 10;
    const char* start = s.data();
    const char* end = s.data() + s.size();

    if (s.starts_with("0x") || s.starts_with("0X")) {
        base = 16;
        start += 2;
    }

    auto result = std::from_chars(start, end, value, base);
    if (result.ec != std::errc() || result.ptr != end) {
        return std::nullopt;
    }

    return value;
}

std::optional<std::uint32_t> DebugClient::parse_id(std::string_view s) const {
    auto addr = parse_address(s);
    if (!addr.has_value() || addr.value() > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(addr.value());
}

}  // namespace dotvm::debugger
