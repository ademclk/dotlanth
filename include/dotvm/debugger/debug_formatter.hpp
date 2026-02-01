#pragma once

/// @file debug_formatter.hpp
/// @brief TOOL-011 Debug Client - Output formatting utilities
///
/// Provides formatted output for debugger commands.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "dotvm/core/disassembler.hpp"
#include "dotvm/core/value.hpp"
#include "dotvm/debugger/breakpoint_manager.hpp"
#include "dotvm/debugger/debug_session.hpp"
#include "dotvm/debugger/watch_manager.hpp"

namespace dotvm::debugger {

/// @brief Formatting options
struct FormatOptions {
    bool show_hex{true};        ///< Show hex values
    bool show_decimal{true};    ///< Show decimal values
    bool use_colors{true};      ///< Use ANSI colors
    std::size_t hex_width{16};  ///< Width for hex addresses
};

/// @brief Formats debugger output
class DebugFormatter {
public:
    /// @brief Construct formatter with options
    explicit DebugFormatter(FormatOptions opts = {}) : opts_(opts) {}

    /// @brief Format a single register value
    [[nodiscard]] std::string format_register(std::uint8_t index, core::Value value) const;

    /// @brief Format all registers
    [[nodiscard]] std::string
    format_registers(std::span<const std::pair<std::uint8_t, core::Value>> registers) const;

    /// @brief Format a memory dump
    [[nodiscard]] std::string format_memory(std::size_t start_offset,
                                            std::span<const std::uint8_t> data) const;

    /// @brief Format a disassembled instruction
    [[nodiscard]] std::string format_instruction(const core::DisasmInstruction& instr,
                                                 bool is_current = false) const;

    /// @brief Format multiple disassembled instructions
    [[nodiscard]] std::string
    format_disassembly(std::span<const core::DisasmInstruction> instructions,
                       std::size_t current_pc) const;

    /// @brief Format a breakpoint
    [[nodiscard]] std::string format_breakpoint(const Breakpoint& bp) const;

    /// @brief Format all breakpoints
    [[nodiscard]] std::string
    format_breakpoints(std::span<const Breakpoint* const> breakpoints) const;

    /// @brief Format a watchpoint
    [[nodiscard]] std::string format_watchpoint(const Watchpoint& wp) const;

    /// @brief Format all watchpoints
    [[nodiscard]] std::string
    format_watchpoints(std::span<const Watchpoint* const> watchpoints) const;

    /// @brief Format a stack frame
    [[nodiscard]] std::string format_frame(const StackFrame& frame, bool is_selected = false) const;

    /// @brief Format the call stack (backtrace)
    [[nodiscard]] std::string format_backtrace(std::span<const StackFrame> frames,
                                               std::size_t selected_frame) const;

    /// @brief Format an address
    [[nodiscard]] std::string format_address(std::size_t address) const;

    /// @brief Format a hex value
    [[nodiscard]] std::string format_hex(std::uint64_t value, int width = 0) const;

    /// @brief Format session state for the prompt
    [[nodiscard]] std::string format_prompt(SessionState state, std::size_t pc) const;

    /// @brief Format help text
    [[nodiscard]] std::string format_help() const;

    /// @brief Get/set options
    [[nodiscard]] const FormatOptions& options() const noexcept { return opts_; }
    void set_options(FormatOptions opts) { opts_ = opts; }

private:
    FormatOptions opts_;

    [[nodiscard]] std::string color_address(const std::string& s) const;
    [[nodiscard]] std::string color_value(const std::string& s) const;
    [[nodiscard]] std::string color_mnemonic(const std::string& s) const;
    [[nodiscard]] std::string color_register(const std::string& s) const;
    [[nodiscard]] std::string color_highlight(const std::string& s) const;
    [[nodiscard]] std::string color_dim(const std::string& s) const;
    [[nodiscard]] std::string color_error(const std::string& s) const;
    [[nodiscard]] std::string color_success(const std::string& s) const;
};

}  // namespace dotvm::debugger
