/// @file debug_formatter.cpp
/// @brief TOOL-011 Debug Client - Output formatter implementation

#include "dotvm/debugger/debug_formatter.hpp"

#include <iomanip>
#include <sstream>

namespace dotvm::debugger {

namespace {

// ANSI color codes
constexpr const char* kReset = "\033[0m";
constexpr const char* kBold = "\033[1m";
constexpr const char* kDim = "\033[2m";
constexpr const char* kCyan = "\033[36m";
constexpr const char* kYellow = "\033[33m";
constexpr const char* kGreen = "\033[32m";
constexpr const char* kRed = "\033[31m";
constexpr const char* kMagenta = "\033[35m";

}  // namespace

std::string DebugFormatter::color_address(const std::string& s) const {
    if (!opts_.use_colors)
        return s;
    return std::string(kCyan) + s + kReset;
}

std::string DebugFormatter::color_value(const std::string& s) const {
    if (!opts_.use_colors)
        return s;
    return std::string(kYellow) + s + kReset;
}

std::string DebugFormatter::color_mnemonic(const std::string& s) const {
    if (!opts_.use_colors)
        return s;
    return std::string(kBold) + kGreen + s + kReset;
}

std::string DebugFormatter::color_register(const std::string& s) const {
    if (!opts_.use_colors)
        return s;
    return std::string(kMagenta) + s + kReset;
}

std::string DebugFormatter::color_highlight(const std::string& s) const {
    if (!opts_.use_colors)
        return s;
    return std::string(kBold) + s + kReset;
}

std::string DebugFormatter::color_dim(const std::string& s) const {
    if (!opts_.use_colors)
        return s;
    return std::string(kDim) + s + kReset;
}

std::string DebugFormatter::color_error(const std::string& s) const {
    if (!opts_.use_colors)
        return s;
    return std::string(kRed) + s + kReset;
}

std::string DebugFormatter::color_success(const std::string& s) const {
    if (!opts_.use_colors)
        return s;
    return std::string(kGreen) + s + kReset;
}

std::string DebugFormatter::format_hex(std::uint64_t value, int width) const {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0');
    if (width > 0) {
        oss << std::setw(width);
    }
    oss << value;
    return oss.str();
}

std::string DebugFormatter::format_address(std::size_t address) const {
    return color_address(format_hex(address, 4));
}

std::string DebugFormatter::format_register(std::uint8_t index, core::Value value) const {
    std::ostringstream oss;
    oss << color_register("R" + std::to_string(index));
    int pad_width = 3 - static_cast<int>(std::to_string(index).length());
    if (pad_width > 0) {
        oss << std::setw(pad_width) << "";
    }
    oss << " = ";

    if (opts_.show_hex) {
        oss << color_value(format_hex(static_cast<std::uint64_t>(value.as_integer()), 16));
    }
    if (opts_.show_decimal && opts_.show_hex) {
        oss << " (" << value.as_integer() << ")";
    } else if (opts_.show_decimal) {
        oss << value.as_integer();
    }

    return oss.str();
}

std::string DebugFormatter::format_registers(
    std::span<const std::pair<std::uint8_t, core::Value>> registers) const {
    std::ostringstream oss;
    for (const auto& [index, value] : registers) {
        oss << format_register(index, value) << "\n";
    }
    return oss.str();
}

std::string DebugFormatter::format_memory(std::size_t start_offset,
                                          std::span<const std::uint8_t> data) const {
    std::ostringstream oss;
    constexpr std::size_t bytes_per_line = 16;

    for (std::size_t i = 0; i < data.size(); i += bytes_per_line) {
        // Address
        oss << format_address(start_offset + i) << ":  ";

        // Hex bytes
        for (std::size_t j = 0; j < bytes_per_line; ++j) {
            if (i + j < data.size()) {
                oss << std::hex << std::setfill('0') << std::setw(2)
                    << static_cast<int>(data[i + j]) << " ";
            } else {
                oss << "   ";
            }
            if (j == 7) {
                oss << " ";  // Extra space in middle
            }
        }

        // ASCII representation
        oss << " |";
        for (std::size_t j = 0; j < bytes_per_line && i + j < data.size(); ++j) {
            char c = static_cast<char>(data[i + j]);
            if (c >= 32 && c < 127) {
                oss << c;
            } else {
                oss << '.';
            }
        }
        oss << "|\n";
    }

    return oss.str();
}

std::string DebugFormatter::format_instruction(const core::DisasmInstruction& instr,
                                               bool is_current) const {
    std::ostringstream oss;

    // Current instruction marker
    if (is_current) {
        oss << color_highlight("-> ");
    } else {
        oss << "   ";
    }

    // Address
    oss << format_address(instr.address) << ":  ";

    // Mnemonic
    oss << color_mnemonic(std::string(instr.mnemonic));

    // Pad mnemonic to fixed width
    int pad = 8 - static_cast<int>(instr.mnemonic.length());
    if (pad > 0) {
        oss << std::string(static_cast<std::size_t>(pad), ' ');
    }

    // Operands based on instruction type
    switch (instr.type) {
        case core::InstructionType::TypeA:
            oss << color_register("R" + std::to_string(instr.rd)) << ", "
                << color_register("R" + std::to_string(instr.rs1)) << ", "
                << color_register("R" + std::to_string(instr.rs2));
            break;
        case core::InstructionType::TypeB:
            oss << color_register("R" + std::to_string(instr.rd)) << ", "
                << color_value("#" + std::to_string(instr.immediate));
            break;
        case core::InstructionType::TypeC:
            if (instr.immediate >= 0) {
                oss << "+" << instr.immediate;
            } else {
                oss << instr.immediate;
            }
            if (instr.target.has_value()) {
                oss << color_dim(" ; -> " + format_hex(instr.target.value(), 4));
            }
            break;
        case core::InstructionType::TypeD:
            oss << color_register("R" + std::to_string(instr.rs1));
            if (instr.immediate >= 0) {
                oss << ", +" << instr.immediate;
            } else {
                oss << ", " << instr.immediate;
            }
            break;
        case core::InstructionType::TypeM:
            oss << color_register("R" + std::to_string(instr.rd)) << ", ["
                << color_register("R" + std::to_string(instr.rs1));
            if (instr.immediate != 0) {
                if (instr.immediate > 0) {
                    oss << "+" << instr.immediate;
                } else {
                    oss << instr.immediate;
                }
            }
            oss << "]";
            break;
        case core::InstructionType::TypeS:
            oss << color_register("R" + std::to_string(instr.rd)) << ", "
                << color_register("R" + std::to_string(instr.rs1)) << ", "
                << color_value("#" + std::to_string(instr.immediate));
            break;
    }

    return oss.str();
}

std::string
DebugFormatter::format_disassembly(std::span<const core::DisasmInstruction> instructions,
                                   std::size_t current_pc) const {
    std::ostringstream oss;
    for (const auto& instr : instructions) {
        bool is_current = (instr.address == current_pc * 4);  // Convert PC to byte offset
        oss << format_instruction(instr, is_current) << "\n";
    }
    return oss.str();
}

std::string DebugFormatter::format_breakpoint(const Breakpoint& bp) const {
    std::ostringstream oss;

    oss << std::setw(3) << bp.id << ": ";
    oss << format_address(bp.address);
    oss << "  ";

    if (bp.enabled) {
        oss << color_success("[enabled] ");
    } else {
        oss << color_dim("[disabled]");
    }

    if (!bp.condition.empty()) {
        oss << " if " << color_value(bp.condition);
    }

    if (bp.hit_count > 0) {
        oss << color_dim(" (hit " + std::to_string(bp.hit_count) + "x)");
    }

    if (bp.ignore_count > 0) {
        oss << color_dim(" (ignore " + std::to_string(bp.ignore_count) + ")");
    }

    return oss.str();
}

std::string
DebugFormatter::format_breakpoints(std::span<const Breakpoint* const> breakpoints) const {
    if (breakpoints.empty()) {
        return "No breakpoints set.\n";
    }

    std::ostringstream oss;
    oss << "Breakpoints:\n";
    for (const auto* bp : breakpoints) {
        oss << format_breakpoint(*bp) << "\n";
    }
    return oss.str();
}

std::string DebugFormatter::format_watchpoint(const Watchpoint& wp) const {
    std::ostringstream oss;

    oss << std::setw(3) << wp.id << ": ";
    oss << "handle=" << wp.handle.index;
    oss << " offset=" << format_hex(wp.offset, 0);
    oss << " size=" << wp.size;
    oss << " type=" << to_string(wp.type);

    if (wp.enabled) {
        oss << color_success(" [enabled] ");
    } else {
        oss << color_dim(" [disabled]");
    }

    if (wp.hit_count > 0) {
        oss << color_dim(" (triggered " + std::to_string(wp.hit_count) + "x)");
    }

    return oss.str();
}

std::string
DebugFormatter::format_watchpoints(std::span<const Watchpoint* const> watchpoints) const {
    if (watchpoints.empty()) {
        return "No watchpoints set.\n";
    }

    std::ostringstream oss;
    oss << "Watchpoints:\n";
    for (const auto* wp : watchpoints) {
        oss << format_watchpoint(*wp) << "\n";
    }
    return oss.str();
}

std::string DebugFormatter::format_frame(const StackFrame& frame, bool is_selected) const {
    std::ostringstream oss;

    if (is_selected) {
        oss << color_highlight("* ");
    } else {
        oss << "  ";
    }

    oss << "#" << frame.frame_index << "  ";
    oss << format_address(frame.pc);

    if (!frame.function_name.empty()) {
        oss << " in " << color_highlight(frame.function_name);
    } else {
        oss << " " << color_dim("(unknown)");
    }

    if (!frame.source_file.empty()) {
        oss << " at " << frame.source_file;
        if (frame.line_number > 0) {
            oss << ":" << frame.line_number;
        }
    }

    return oss.str();
}

std::string DebugFormatter::format_backtrace(std::span<const StackFrame> frames,
                                             std::size_t selected_frame) const {
    if (frames.empty()) {
        return "No call stack available.\n";
    }

    std::ostringstream oss;
    for (const auto& frame : frames) {
        bool is_selected = (frame.frame_index == selected_frame);
        oss << format_frame(frame, is_selected) << "\n";
    }
    return oss.str();
}

std::string DebugFormatter::format_prompt(SessionState state, std::size_t pc) const {
    std::ostringstream oss;
    oss << "(dotdb)";

    if (state == SessionState::Paused) {
        oss << " [" << format_hex(pc, 4) << "]";
    } else if (state == SessionState::Halted) {
        oss << " [halted]";
    }

    oss << " ";
    return oss.str();
}

std::string DebugFormatter::format_help() const {
    return R"(DotVM Debugger Commands
=======================

Execution:
  run (r)              Start execution
  continue (c)         Continue to next breakpoint
  step (s)             Step one instruction (into)
  next (n)             Step over (skip calls)
  finish (fin)         Run until function returns
  quit (q)             Exit debugger

Breakpoints:
  b <addr>             Set breakpoint at address
  breakpoint set --address <pc>
  bl                   List all breakpoints
  bd <id>              Delete breakpoint
  be <id>              Enable breakpoint
  bdi <id>             Disable breakpoint
  bc <id> <expr>       Set breakpoint condition

Inspection:
  reg [r1...]          Show register values
  x <h> <off> <sz>     Examine memory (handle, offset, size)
  dis [--count N]      Disassemble at PC
  bt                   Show call stack
  f <n>                Select stack frame

Watchpoints:
  w <h> <off> <sz>     Set memory watchpoint
  wl                   List watchpoints
  wd <id>              Delete watchpoint

Source:
  l                    Show source at PC

Other:
  help (h)             Show this help

Addresses can be decimal or hexadecimal (0x prefix).
)";
}

}  // namespace dotvm::debugger
