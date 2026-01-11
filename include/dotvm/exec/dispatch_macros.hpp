#pragma once

/// @file dispatch_macros.hpp
/// @brief Computed-goto dispatch macros for high-performance instruction dispatch
///
/// This header provides macros for computed-goto based instruction dispatch,
/// which is significantly faster than switch-based dispatch on GCC/Clang.
/// Falls back to switch-based dispatch on other compilers.
///
/// Performance target: <10 cycles per dispatch

#include <cstdint>

namespace dotvm::exec {

// ============================================================================
// Compiler Detection
// ============================================================================

/// Check for computed-goto support (GCC/Clang extension)
#if defined(__GNUC__) || defined(__clang__)
    #define DOTVM_HAS_COMPUTED_GOTO 1
#else
    #define DOTVM_HAS_COMPUTED_GOTO 0
#endif

// ============================================================================
// Dispatch Table Configuration
// ============================================================================

/// Total number of opcodes (all possible 8-bit values)
inline constexpr std::size_t OPCODE_COUNT = 256;

/// Opcode mask for extracting from instruction
inline constexpr std::uint32_t OPCODE_MASK = 0xFFU;

/// Opcode shift position (high byte of 32-bit instruction)
inline constexpr int OPCODE_SHIFT = 24;

// ============================================================================
// Computed-Goto Macros (GCC/Clang)
// ============================================================================

#if DOTVM_HAS_COMPUTED_GOTO

/// DISPATCH: Jump to handler for given opcode
/// @param opcode 8-bit opcode value
/// @note dispatch_table must be in scope
#define DOTVM_DISPATCH(opcode) goto *dispatch_table[(opcode)]

/// NEXT: Fetch next instruction and dispatch
/// This is the critical hot path - must be minimal
/// @note Requires: exec_ctx_, instr, dispatch_table in scope
#define DOTVM_NEXT() do {                                          \
    if (exec_ctx_.pc >= exec_ctx_.code_size) [[unlikely]]          \
        goto op_OUT_OF_BOUNDS;                                     \
    instr = exec_ctx_.code[exec_ctx_.pc++];                        \
    ++exec_ctx_.instructions_executed;                             \
    DOTVM_DISPATCH(instr >> 24);                                   \
} while(0)

/// NEXT_NO_CHECK: Dispatch without bounds check
/// Use only when target is known to be valid (after validated jump)
#define DOTVM_NEXT_NO_CHECK() do {                                 \
    instr = exec_ctx_.code[exec_ctx_.pc++];                        \
    ++exec_ctx_.instructions_executed;                             \
    DOTVM_DISPATCH(instr >> 24);                                   \
} while(0)

/// HANDLER_BEGIN: Start of an opcode handler
/// @param name Handler label name (e.g., ADD, SUB)
#define DOTVM_HANDLER_BEGIN(name) op_##name: {

/// HANDLER_END: End of handler, dispatch next instruction
#define DOTVM_HANDLER_END() DOTVM_NEXT(); }

/// HANDLER_END_NO_CHECK: End handler without bounds check
#define DOTVM_HANDLER_END_NO_CHECK() DOTVM_NEXT_NO_CHECK(); }

/// RETURN_ERROR: Return from dispatch loop with error
/// @param result ExecResult error code
#define DOTVM_RETURN_ERROR(result) do {                            \
    exec_ctx_.halt_with_error(result);                             \
    return result;                                                 \
} while(0)

/// RETURN_SUCCESS: Return from dispatch loop successfully
#define DOTVM_RETURN_SUCCESS() do {                                \
    exec_ctx_.halt();                                              \
    return ExecResult::Success;                                    \
} while(0)

#else // !DOTVM_HAS_COMPUTED_GOTO

// ============================================================================
// Switch-Based Fallback Macros
// ============================================================================

/// Fallback: Dispatch is handled by switch statement
#define DOTVM_DISPATCH(opcode) /* handled by switch */

/// Fallback: Continue to next iteration
#define DOTVM_NEXT() continue

/// Fallback: Same as NEXT
#define DOTVM_NEXT_NO_CHECK() continue

/// Fallback: Case label
#define DOTVM_HANDLER_BEGIN(name) case static_cast<std::uint8_t>(Opcode::name): {

/// Fallback: Break from case
#define DOTVM_HANDLER_END() break; }

/// Fallback: Break from case
#define DOTVM_HANDLER_END_NO_CHECK() break; }

/// Fallback: Return error
#define DOTVM_RETURN_ERROR(result) do {                            \
    exec_ctx_.halt_with_error(result);                             \
    return result;                                                 \
} while(0)

/// Fallback: Return success
#define DOTVM_RETURN_SUCCESS() do {                                \
    exec_ctx_.halt();                                              \
    return ExecResult::Success;                                    \
} while(0)

#endif // DOTVM_HAS_COMPUTED_GOTO

// ============================================================================
// Branch Prediction Hints
// ============================================================================

/// Hint that condition is likely true
#define DOTVM_LIKELY(x)   (__builtin_expect(!!(x), 1))

/// Hint that condition is likely false
#define DOTVM_UNLIKELY(x) (__builtin_expect(!!(x), 0))

// ============================================================================
// Opcode Enumeration
// ============================================================================

/// Individual opcode values for the dispatch table
/// These values must match the opcode ranges in instruction.hpp
namespace opcode {
    // Arithmetic (0x00-0x1F)
    inline constexpr std::uint8_t ADD   = 0x00;
    inline constexpr std::uint8_t SUB   = 0x01;
    inline constexpr std::uint8_t MUL   = 0x02;
    inline constexpr std::uint8_t DIV   = 0x03;
    inline constexpr std::uint8_t MOD   = 0x04;
    inline constexpr std::uint8_t NEG   = 0x05;
    inline constexpr std::uint8_t ABS   = 0x06;
    inline constexpr std::uint8_t ADDI  = 0x08;
    inline constexpr std::uint8_t SUBI  = 0x09;
    inline constexpr std::uint8_t MULI  = 0x0A;
    inline constexpr std::uint8_t DIVI  = 0x0B;
    inline constexpr std::uint8_t MODI  = 0x0C;

    // Bitwise (0x20-0x2F)
    inline constexpr std::uint8_t AND   = 0x20;
    inline constexpr std::uint8_t OR    = 0x21;
    inline constexpr std::uint8_t XOR   = 0x22;
    inline constexpr std::uint8_t NOT   = 0x23;
    inline constexpr std::uint8_t SHL   = 0x24;
    inline constexpr std::uint8_t SHR   = 0x25;
    inline constexpr std::uint8_t SAR   = 0x26;
    inline constexpr std::uint8_t ANDI  = 0x28;
    inline constexpr std::uint8_t ORI   = 0x29;
    inline constexpr std::uint8_t XORI  = 0x2A;

    // Comparison (0x30-0x3F)
    inline constexpr std::uint8_t EQ    = 0x30;
    inline constexpr std::uint8_t NE    = 0x31;
    inline constexpr std::uint8_t LT    = 0x32;
    inline constexpr std::uint8_t LE    = 0x33;
    inline constexpr std::uint8_t GT    = 0x34;
    inline constexpr std::uint8_t GE    = 0x35;
    inline constexpr std::uint8_t LTU   = 0x36;
    inline constexpr std::uint8_t LEU   = 0x37;
    inline constexpr std::uint8_t GTU   = 0x38;
    inline constexpr std::uint8_t GEU   = 0x39;

    // Control Flow (0x40-0x5F)
    inline constexpr std::uint8_t JMP   = 0x40;
    inline constexpr std::uint8_t JMPR  = 0x41;
    inline constexpr std::uint8_t BEQ   = 0x42;
    inline constexpr std::uint8_t BNE   = 0x43;
    inline constexpr std::uint8_t BLT   = 0x44;
    inline constexpr std::uint8_t BGE   = 0x45;
    inline constexpr std::uint8_t BLTU  = 0x46;
    inline constexpr std::uint8_t BGEU  = 0x47;
    inline constexpr std::uint8_t CALL  = 0x48;
    inline constexpr std::uint8_t RET   = 0x49;
    inline constexpr std::uint8_t JMPI  = 0x4A;

    // Memory (0x60-0x7F)
    inline constexpr std::uint8_t LOAD  = 0x60;
    inline constexpr std::uint8_t STORE = 0x61;
    inline constexpr std::uint8_t LOADB = 0x62;
    inline constexpr std::uint8_t STOREB = 0x63;
    inline constexpr std::uint8_t LOADH = 0x64;
    inline constexpr std::uint8_t STOREH = 0x65;
    inline constexpr std::uint8_t LOADW = 0x66;
    inline constexpr std::uint8_t STOREW = 0x67;
    inline constexpr std::uint8_t ALLOC = 0x68;
    inline constexpr std::uint8_t FREE  = 0x69;
    inline constexpr std::uint8_t MEMCPY = 0x6A;
    inline constexpr std::uint8_t MEMSET = 0x6B;

    // Data Move (0x80-0x8F)
    inline constexpr std::uint8_t MOV   = 0x80;
    inline constexpr std::uint8_t MOVI  = 0x81;
    inline constexpr std::uint8_t LOADK = 0x82;
    inline constexpr std::uint8_t MOVHI = 0x83;
    inline constexpr std::uint8_t MOVLO = 0x84;
    inline constexpr std::uint8_t XCHG  = 0x85;

    // System (0xF0-0xFF)
    inline constexpr std::uint8_t NOP   = 0xF0;
    inline constexpr std::uint8_t BREAK = 0xF1;
    inline constexpr std::uint8_t SYSCALL = 0xFE;
    inline constexpr std::uint8_t HALT  = 0xFF;
}  // namespace opcode

}  // namespace dotvm::exec
