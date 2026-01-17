#pragma once

/// @file dispatch_macros.hpp
/// @brief Computed-goto dispatch macros for high-performance instruction dispatch
///
/// This header provides macros for computed-goto based instruction dispatch,
/// which is significantly faster than switch-based dispatch on GCC/Clang.
/// Falls back to switch-based dispatch on other compilers.
///
/// Performance target: <10 cycles per dispatch

#include <cstddef>
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
    #define DOTVM_DISPATCH(opcode) goto* dispatch_table[(opcode)]

    /// NEXT: Fetch next instruction and dispatch
    /// This is the critical hot path - must be minimal
    /// @note Requires: exec_ctx_, instr, dispatch_table in scope
    #define DOTVM_NEXT()                                                                    \
        do {                                                                                \
            if (exec_ctx_.pc >= exec_ctx_.code_size) [[unlikely]]                           \
                goto op_OUT_OF_BOUNDS;                                                      \
            /* Check instruction limit (0 = unlimited) - EXEC-008 */                        \
            if (exec_ctx_.max_instructions > 0 &&                                           \
                exec_ctx_.instructions_executed >= exec_ctx_.max_instructions) [[unlikely]] \
                goto op_EXECUTION_LIMIT;                                                    \
            instr = exec_ctx_.code[exec_ctx_.pc++];                                         \
            ++exec_ctx_.instructions_executed;                                              \
            DOTVM_DISPATCH(instr >> 24);                                                    \
        } while (0)

    /// NEXT_NO_CHECK: Dispatch without bounds check
    /// Use only when target is known to be valid (after validated jump)
    #define DOTVM_NEXT_NO_CHECK()                   \
        do {                                        \
            instr = exec_ctx_.code[exec_ctx_.pc++]; \
            ++exec_ctx_.instructions_executed;      \
            DOTVM_DISPATCH(instr >> 24);            \
        } while (0)

    /// HANDLER_BEGIN: Start of an opcode handler
    /// @param name Handler label name (e.g., ADD, SUB)
    #define DOTVM_HANDLER_BEGIN(name) op_##name : {
    /// HANDLER_END: End of handler, dispatch next instruction
    #define DOTVM_HANDLER_END() \
        DOTVM_NEXT();           \
        }

    /// HANDLER_END_NO_CHECK: End handler without bounds check
    #define DOTVM_HANDLER_END_NO_CHECK() \
        DOTVM_NEXT_NO_CHECK();           \
        }

    /// RETURN_ERROR: Return from dispatch loop with error
    /// @param result ExecResult error code
    #define DOTVM_RETURN_ERROR(result)         \
        do {                                   \
            exec_ctx_.halt_with_error(result); \
            return result;                     \
        } while (0)

    /// RETURN_SUCCESS: Return from dispatch loop successfully
    #define DOTVM_RETURN_SUCCESS()      \
        do {                            \
            exec_ctx_.halt();           \
            return ExecResult::Success; \
        } while (0)

#else                              // !DOTVM_HAS_COMPUTED_GOTO

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
    #define DOTVM_HANDLER_END() \
        break;                  \
        }

    /// Fallback: Break from case
    #define DOTVM_HANDLER_END_NO_CHECK() \
        break;                           \
        }

    /// Fallback: Return error
    #define DOTVM_RETURN_ERROR(result)         \
        do {                                   \
            exec_ctx_.halt_with_error(result); \
            return result;                     \
        } while (0)

    /// Fallback: Return success
    #define DOTVM_RETURN_SUCCESS()      \
        do {                            \
            exec_ctx_.halt();           \
            return ExecResult::Success; \
        } while (0)

#endif  // DOTVM_HAS_COMPUTED_GOTO

// ============================================================================
// Branch Prediction Hints
// ============================================================================

/// Hint that condition is likely true
#define DOTVM_LIKELY(x) (__builtin_expect(!!(x), 1))

/// Hint that condition is likely false
#define DOTVM_UNLIKELY(x) (__builtin_expect(!!(x), 0))

// ============================================================================
// Opcode Enumeration
// ============================================================================

/// Individual opcode values for the dispatch table
/// These values must match the opcode ranges in instruction.hpp
namespace opcode {
// Arithmetic (0x00-0x1F)
inline constexpr std::uint8_t ADD = 0x00;
inline constexpr std::uint8_t SUB = 0x01;
inline constexpr std::uint8_t MUL = 0x02;
inline constexpr std::uint8_t DIV = 0x03;
inline constexpr std::uint8_t MOD = 0x04;
inline constexpr std::uint8_t NEG = 0x05;
inline constexpr std::uint8_t ABS = 0x06;
inline constexpr std::uint8_t ADDI = 0x08;
inline constexpr std::uint8_t SUBI = 0x09;
inline constexpr std::uint8_t MULI = 0x0A;
inline constexpr std::uint8_t DIVI = 0x0B;
inline constexpr std::uint8_t MODI = 0x0C;

// Bitwise (0x20-0x2F)
inline constexpr std::uint8_t AND = 0x20;
inline constexpr std::uint8_t OR = 0x21;
inline constexpr std::uint8_t XOR = 0x22;
inline constexpr std::uint8_t NOT = 0x23;
inline constexpr std::uint8_t SHL = 0x24;
inline constexpr std::uint8_t SHR = 0x25;
inline constexpr std::uint8_t SAR = 0x26;
inline constexpr std::uint8_t ROL = 0x27;   // NEW: Rotate left
inline constexpr std::uint8_t ROR = 0x28;   // NEW: Rotate right
inline constexpr std::uint8_t SHLI = 0x29;  // NEW: Shift left immediate
inline constexpr std::uint8_t SHRI = 0x2A;  // NEW: Shift right immediate (logical)
inline constexpr std::uint8_t SARI = 0x2B;  // NEW: Shift right immediate (arithmetic)
inline constexpr std::uint8_t ANDI = 0x2C;  // MOVED from 0x28
inline constexpr std::uint8_t ORI = 0x2D;   // MOVED from 0x29
inline constexpr std::uint8_t XORI = 0x2E;  // MOVED from 0x2A

// Comparison (0x30-0x3F) - EXEC-009
inline constexpr std::uint8_t EQ = 0x30;
inline constexpr std::uint8_t NE = 0x31;
inline constexpr std::uint8_t LT = 0x32;
inline constexpr std::uint8_t LE = 0x33;
inline constexpr std::uint8_t GT = 0x34;
inline constexpr std::uint8_t GE = 0x35;
inline constexpr std::uint8_t LTU = 0x36;
inline constexpr std::uint8_t LEU = 0x37;
inline constexpr std::uint8_t GTU = 0x38;
inline constexpr std::uint8_t GEU = 0x39;
inline constexpr std::uint8_t TEST = 0x3A;     // Bitwise test: (Rs1 & Rs2) != 0
inline constexpr std::uint8_t CMPI_EQ = 0x3B;  // Compare immediate equal
inline constexpr std::uint8_t CMPI_NE = 0x3C;  // Compare immediate not equal
inline constexpr std::uint8_t CMPI_LT = 0x3D;  // Compare immediate less than (signed)
inline constexpr std::uint8_t CMPI_GE = 0x3E;  // Compare immediate greater or equal (signed)

// Control Flow (0x40-0x5F) - EXEC-005
inline constexpr std::uint8_t JMP = 0x40;  // Unconditional jump (Type C, 24-bit offset)
inline constexpr std::uint8_t JZ = 0x41;   // Jump if zero (Type B, rs + 16-bit offset)
inline constexpr std::uint8_t JNZ = 0x42;  // Jump if not zero (Type B, rs + 16-bit offset)
inline constexpr std::uint8_t BEQ = 0x43;  // Branch if equal (Type A, rs1 == rs2)
inline constexpr std::uint8_t BNE = 0x44;  // Branch if not equal (Type A, rs1 != rs2)
inline constexpr std::uint8_t BLT = 0x45;  // Branch if less than (Type A, rs1 < rs2, signed)
inline constexpr std::uint8_t BLE = 0x46;  // Branch if less or equal (Type A, rs1 <= rs2, signed)
inline constexpr std::uint8_t BGT = 0x47;  // Branch if greater than (Type A, rs1 > rs2, signed)
inline constexpr std::uint8_t BGE =
    0x48;  // Branch if greater or equal (Type A, rs1 >= rs2, signed)
inline constexpr std::uint8_t CALL = 0x50;    // Call subroutine (Type C, CFI push + 24-bit offset)
inline constexpr std::uint8_t RET = 0x51;     // Return from subroutine (CFI pop)
inline constexpr std::uint8_t TRY = 0x52;     // Push exception handler frame (EXEC-011)
inline constexpr std::uint8_t CATCH = 0x53;   // Exception handler entry marker (EXEC-011)
inline constexpr std::uint8_t THROW = 0x54;   // Raise exception (EXEC-011)
inline constexpr std::uint8_t ENDTRY = 0x55;  // Pop exception frame, normal exit (EXEC-011)
inline constexpr std::uint8_t HALT = 0x5F;    // Halt execution

// Memory Load/Store - EXEC-006 (Type M format)
// Format: [opcode(8)][Rd/Rs2(8)][Rs1(8)][offset8(8)]
inline constexpr std::uint8_t LOAD8 = 0x60;    // Load byte (zero-extend)
inline constexpr std::uint8_t LOAD16 = 0x61;   // Load halfword (2-byte aligned)
inline constexpr std::uint8_t LOAD32 = 0x62;   // Load word (4-byte aligned)
inline constexpr std::uint8_t LOAD64 = 0x63;   // Load doubleword (8-byte aligned)
inline constexpr std::uint8_t STORE8 = 0x64;   // Store byte
inline constexpr std::uint8_t STORE16 = 0x65;  // Store halfword (2-byte aligned)
inline constexpr std::uint8_t STORE32 = 0x66;  // Store word (4-byte aligned)
inline constexpr std::uint8_t STORE64 = 0x67;  // Store doubleword (8-byte aligned)
inline constexpr std::uint8_t LEA = 0x68;      // Load Effective Address

// Data Move (0x80-0x8F)
inline constexpr std::uint8_t MOV = 0x80;
inline constexpr std::uint8_t MOVI = 0x81;
inline constexpr std::uint8_t LOADK = 0x82;
inline constexpr std::uint8_t MOVHI = 0x83;
inline constexpr std::uint8_t MOVLO = 0x84;
inline constexpr std::uint8_t XCHG = 0x85;

// System (0xF0-0xFF)
inline constexpr std::uint8_t NOP = 0xF0;
inline constexpr std::uint8_t BREAK = 0xF1;
inline constexpr std::uint8_t DEBUG = 0xFD;  // Debug mode breakpoint (EXEC-010)
inline constexpr std::uint8_t SYSCALL = 0xFE;
// Note: HALT moved to Control Flow section (0x5F) per EXEC-005
}  // namespace opcode

}  // namespace dotvm::exec
