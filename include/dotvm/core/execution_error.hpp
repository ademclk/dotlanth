#pragma once

/// @file execution_error.hpp
/// @brief Execution error types for DotVM executor
///
/// This header defines error types that can occur during instruction execution.

#include <cstdint>
#include <string_view>

namespace dotvm::core {

/// Execution error codes
///
/// These errors are set when instruction execution encounters an error condition.
/// Some errors (like IntegerOverflow) may only be set when strict_overflow is enabled.
enum class ExecutionError : std::uint8_t {
    /// No error - execution succeeded
    Success = 0,

    // =========================================================================
    // Arithmetic errors (1-15)
    // =========================================================================

    /// Integer overflow detected (only in strict_overflow mode)
    IntegerOverflow = 1,

    /// Division or modulo by zero
    DivisionByZero = 2,

    // =========================================================================
    // Opcode errors (16-31)
    // =========================================================================

    /// Invalid or unimplemented opcode
    InvalidOpcode = 16,

    /// Reserved opcode (reserved for future use)
    ReservedOpcode = 17,

    /// Opcode not yet implemented
    NotImplemented = 18,

    // =========================================================================
    // Execution flow errors (32-47)
    // =========================================================================

    /// Program counter out of bounds
    PCOutOfBounds = 32,

    /// Program counter not aligned to instruction boundary
    PCNotAligned = 33,

    /// Execution halted (HALT instruction executed)
    Halted = 34,

    /// Maximum instruction count exceeded
    InstructionLimitExceeded = 35,

    // =========================================================================
    // Register errors (48-63)
    // =========================================================================

    /// Invalid register index
    InvalidRegister = 48,

    // =========================================================================
    // Memory errors (64-79)
    // =========================================================================

    /// Memory access out of bounds
    MemoryOutOfBounds = 64,

    /// Unaligned memory access
    UnalignedAccess = 65,
};

/// Convert ExecutionError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(ExecutionError error) noexcept {
    switch (error) {
        case ExecutionError::Success:
            return "Success";
        case ExecutionError::IntegerOverflow:
            return "Integer overflow";
        case ExecutionError::DivisionByZero:
            return "Division by zero";
        case ExecutionError::InvalidOpcode:
            return "Invalid opcode";
        case ExecutionError::ReservedOpcode:
            return "Reserved opcode";
        case ExecutionError::NotImplemented:
            return "Not implemented";
        case ExecutionError::PCOutOfBounds:
            return "Program counter out of bounds";
        case ExecutionError::PCNotAligned:
            return "Program counter not aligned";
        case ExecutionError::Halted:
            return "Execution halted";
        case ExecutionError::InstructionLimitExceeded:
            return "Instruction limit exceeded";
        case ExecutionError::InvalidRegister:
            return "Invalid register";
        case ExecutionError::MemoryOutOfBounds:
            return "Memory out of bounds";
        case ExecutionError::UnalignedAccess:
            return "Unaligned memory access";
        default:
            return "Unknown error";
    }
}

/// Check if error is a success (no error)
[[nodiscard]] constexpr bool is_success(ExecutionError error) noexcept {
    return error == ExecutionError::Success;
}

/// Check if error is an arithmetic error
[[nodiscard]] constexpr bool is_arithmetic_error(ExecutionError error) noexcept {
    auto code = static_cast<std::uint8_t>(error);
    return code >= 1 && code <= 15;
}

/// Check if error should halt execution
[[nodiscard]] constexpr bool is_fatal_error(ExecutionError error) noexcept {
    switch (error) {
        case ExecutionError::Success:
        case ExecutionError::IntegerOverflow:  // Non-fatal: continues execution
        case ExecutionError::DivisionByZero:   // Non-fatal: returns 0, continues
            return false;
        default:
            return true;
    }
}

/// Execution state tracking
struct ExecutionState {
    /// Current program counter (byte offset into code section)
    std::uint64_t pc{0};

    /// True if execution has halted
    bool halted{false};

    /// Last execution error encountered
    ExecutionError last_error{ExecutionError::Success};

    /// Number of instructions executed
    std::uint64_t instructions_executed{0};

    /// Number of arithmetic overflow events (for diagnostics)
    std::uint64_t overflow_count{0};

    /// Number of division-by-zero events (for diagnostics)
    std::uint64_t div_zero_count{0};

    /// Reset execution state
    constexpr void reset() noexcept {
        pc = 0;
        halted = false;
        last_error = ExecutionError::Success;
        instructions_executed = 0;
        overflow_count = 0;
        div_zero_count = 0;
    }
};

}  // namespace dotvm::core
