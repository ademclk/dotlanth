#pragma once

/// @file execution_context.hpp
/// @brief Lightweight execution state for computed-goto dispatch loop
///
/// This header provides the ExecutionContext structure which holds the
/// minimal state needed during bytecode execution. The structure is
/// cache-line aligned (64 bytes) for optimal performance.

#include <cstddef>
#include <cstdint>

namespace dotvm::exec {

/// Cache line size for alignment (common x86-64 value)
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

/// Execution result codes
enum class ExecResult : std::uint8_t {
    Success = 0,            ///< Execution completed normally (HALT)
    Error = 1,              ///< Execution error occurred
    InvalidOpcode = 2,      ///< Unknown or reserved opcode
    CfiViolation = 3,       ///< Control flow integrity violation
    OutOfBounds = 4,        ///< PC out of code section bounds
    Interrupted = 5,        ///< Execution was interrupted
    DivisionByZero = 6,     ///< Division by zero (if strict mode enabled)
    MemoryError = 7,        ///< Memory access error (bounds violation, invalid handle)
    UnalignedAccess = 8,    ///< Misaligned memory access (EXEC-006)
    StackOverflow = 9,      ///< Call stack overflow (EXEC-007)
    ExecutionLimit = 10,    ///< Instruction limit exceeded (EXEC-008)
    UnhandledException = 11, ///< Unhandled exception (EXEC-011)
    JitFallback = 12        ///< JIT compilation not available, use interpreter (EXEC-012)
};

/// Convert ExecResult to string representation
[[nodiscard]] constexpr const char* to_string(ExecResult result) noexcept {
    switch (result) {
        case ExecResult::Success:            return "Success";
        case ExecResult::Error:              return "Error";
        case ExecResult::InvalidOpcode:      return "InvalidOpcode";
        case ExecResult::CfiViolation:       return "CfiViolation";
        case ExecResult::OutOfBounds:        return "OutOfBounds";
        case ExecResult::Interrupted:        return "Interrupted";
        case ExecResult::DivisionByZero:     return "DivisionByZero";
        case ExecResult::MemoryError:        return "MemoryError";
        case ExecResult::UnalignedAccess:    return "UnalignedAccess";
        case ExecResult::StackOverflow:      return "StackOverflow";
        case ExecResult::ExecutionLimit:     return "ExecutionLimit";
        case ExecResult::UnhandledException: return "UnhandledException";
        case ExecResult::JitFallback:        return "JitFallback";
    }
    return "Unknown";
}

/// Lightweight execution state for computed-goto dispatch
///
/// This structure is kept minimal and cache-line aligned for maximum
/// performance in the dispatch loop hot path. All fields are accessed
/// frequently during execution.
///
/// @note The structure size is exactly 64 bytes (one cache line) to
///       prevent false sharing and ensure optimal cache utilization.
struct alignas(CACHE_LINE_SIZE) ExecutionContext {
    /// Pointer to code section (readonly during execution)
    /// Instructions are 32-bit aligned
    const std::uint32_t* code{nullptr};

    /// Program counter (instruction index, NOT byte offset)
    /// Points to the next instruction to execute
    std::size_t pc{0};

    /// Code section size in instructions (NOT bytes)
    /// Used for bounds checking in the dispatch loop
    std::size_t code_size{0};

    /// Execution halted flag
    /// Set by HALT instruction or on error
    bool halted{false};

    /// Error result if execution stopped due to error
    ExecResult error{ExecResult::Success};

    /// Instructions executed counter (for profiling)
    std::uint64_t instructions_executed{0};

    /// Maximum instruction limit (0 = unlimited) - EXEC-008
    std::uint64_t max_instructions{0};

    /// Padding to ensure cache line alignment
    /// Layout on 64-bit:
    ///   code:                 8 bytes (offset 0)
    ///   pc:                   8 bytes (offset 8)
    ///   code_size:            8 bytes (offset 16)
    ///   halted:               1 byte  (offset 24)
    ///   error:                1 byte  (offset 25)
    ///   [padding]:            6 bytes (offset 26-31, align for uint64)
    ///   instructions_executed:8 bytes (offset 32)
    ///   max_instructions:     8 bytes (offset 40)
    /// Subtotal: 48 bytes, need 16 bytes padding for 64
    std::uint8_t _reserved[16]{};

    /// Check if execution should continue
    [[nodiscard]] constexpr bool should_continue() const noexcept {
        return !halted && pc < code_size;
    }

    /// Advance PC by one instruction
    constexpr void advance() noexcept {
        ++pc;
    }

    /// Jump to absolute instruction index
    /// @param target Target instruction index
    constexpr void jump_to(std::size_t target) noexcept {
        pc = target;
    }

    /// Jump relative to current PC
    /// @param offset Signed offset in instructions
    constexpr void jump_relative(std::int32_t offset) noexcept {
        // Safe casting: pc is always positive, and offset is bounded
        pc = static_cast<std::size_t>(
            static_cast<std::int64_t>(pc) + static_cast<std::int64_t>(offset));
    }

    /// Mark execution as halted with success
    constexpr void halt() noexcept {
        halted = true;
        error = ExecResult::Success;
    }

    /// Mark execution as halted with error
    /// @param err Error code
    constexpr void halt_with_error(ExecResult err) noexcept {
        halted = true;
        error = err;
    }

    /// Reset execution state for re-execution
    /// @param new_code Pointer to code section
    /// @param new_size Size in instructions
    /// @param entry_point Starting instruction index
    /// @param max_instr Maximum instruction limit (0 = unlimited)
    constexpr void reset(const std::uint32_t* new_code,
                         std::size_t new_size,
                         std::size_t entry_point = 0,
                         std::uint64_t max_instr = 0) noexcept {
        code = new_code;
        code_size = new_size;
        pc = entry_point;
        halted = false;
        error = ExecResult::Success;
        instructions_executed = 0;
        max_instructions = max_instr;
    }
};

// Verify cache line alignment
static_assert(sizeof(ExecutionContext) == CACHE_LINE_SIZE,
              "ExecutionContext must be exactly one cache line (64 bytes)");
static_assert(alignof(ExecutionContext) == CACHE_LINE_SIZE,
              "ExecutionContext must be cache-line aligned");

}  // namespace dotvm::exec
