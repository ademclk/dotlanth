#pragma once

/// @file opcode.hpp
/// @brief Opcode definitions for DotVM instruction set
///
/// This header defines all opcode constants organized by category.
/// Each opcode is a unique 8-bit value in the range 0x00-0xFF.

#include <cstdint>

namespace dotvm::core {

/// Arithmetic opcodes (0x00-0x1F)
///
/// Type A (register-register): [opcode][Rd][Rs1][Rs2]
/// Type B (immediate): [opcode][Rd][imm16] - accumulator style (Rd = Rd OP imm)
namespace opcode {

// ============================================================================
// Arithmetic - Register-Register (Type A)
// ============================================================================

/// ADD: Rd = Rs1 + Rs2
inline constexpr std::uint8_t ADD = 0x00;

/// SUB: Rd = Rs1 - Rs2
inline constexpr std::uint8_t SUB = 0x01;

/// MUL: Rd = Rs1 * Rs2
inline constexpr std::uint8_t MUL = 0x02;

/// DIV: Rd = Rs1 / Rs2 (truncated toward zero)
/// Division by zero returns 0 and may set error flag
inline constexpr std::uint8_t DIV = 0x03;

/// MOD: Rd = Rs1 % Rs2
/// Division by zero returns 0 and may set error flag
inline constexpr std::uint8_t MOD = 0x04;

/// NEG: Rd = -Rs1 (Rs2 ignored)
inline constexpr std::uint8_t NEG = 0x05;

// ============================================================================
// Arithmetic - Register-Immediate (Type B, Accumulator Style)
// ============================================================================

/// ADDI: Rd = Rd + sign_extend(imm16)
inline constexpr std::uint8_t ADDI = 0x06;

/// SUBI: Rd = Rd - sign_extend(imm16)
inline constexpr std::uint8_t SUBI = 0x07;

/// MULI: Rd = Rd * sign_extend(imm16)
inline constexpr std::uint8_t MULI = 0x08;

// ============================================================================
// Reserved arithmetic opcodes (0x09-0x0F)
// ============================================================================

/// First reserved arithmetic opcode
inline constexpr std::uint8_t ARITHMETIC_RESERVED_START = 0x09;

/// Last reserved integer arithmetic opcode
inline constexpr std::uint8_t ARITHMETIC_RESERVED_END = 0x0F;

// ============================================================================
// Floating Point Arithmetic (0x10-0x18) - IEEE 754 double-precision
// ============================================================================

/// FADD: Rd = Rs1 + Rs2 (IEEE 754 double)
inline constexpr std::uint8_t FADD = 0x10;

/// FSUB: Rd = Rs1 - Rs2 (IEEE 754 double)
inline constexpr std::uint8_t FSUB = 0x11;

/// FMUL: Rd = Rs1 * Rs2 (IEEE 754 double)
inline constexpr std::uint8_t FMUL = 0x12;

/// FDIV: Rd = Rs1 / Rs2 (div by zero -> +/-Inf per IEEE 754)
inline constexpr std::uint8_t FDIV = 0x13;

/// FNEG: Rd = -Rs1 (IEEE 754 negation, Rs2 ignored)
inline constexpr std::uint8_t FNEG = 0x14;

/// FSQRT: Rd = sqrt(Rs1) (IEEE 754 square root, Rs2 ignored)
inline constexpr std::uint8_t FSQRT = 0x15;

/// FCMP: Compare Rs1 and Rs2, set FP flags (LT, EQ, GT, UNORD)
/// Rd is ignored, only sets flags in ExecutionState
inline constexpr std::uint8_t FCMP = 0x16;

/// F2I: Rd = int64(Rs1) (float to integer, saturation on overflow, Rs2 ignored)
/// NaN -> 0, +Inf -> INT64_MAX, -Inf -> INT64_MIN
inline constexpr std::uint8_t F2I = 0x17;

/// I2F: Rd = double(Rs1) (integer to float, Rs2 ignored)
inline constexpr std::uint8_t I2F = 0x18;

// ============================================================================
// Reserved floating point opcodes (0x19-0x1F)
// ============================================================================

/// First reserved floating point opcode
inline constexpr std::uint8_t FP_RESERVED_START = 0x19;

/// Last reserved floating point opcode
inline constexpr std::uint8_t FP_RESERVED_END = 0x1F;

// ============================================================================
// Bitwise - Register-Register (Type A)
// ============================================================================

/// AND: Rd = Rs1 & Rs2
inline constexpr std::uint8_t AND = 0x20;

/// OR: Rd = Rs1 | Rs2
inline constexpr std::uint8_t OR = 0x21;

/// XOR: Rd = Rs1 ^ Rs2
inline constexpr std::uint8_t XOR = 0x22;

/// NOT: Rd = ~Rs1 (unary, Rs2 ignored)
inline constexpr std::uint8_t NOT = 0x23;

/// SHL: Rd = Rs1 << (Rs2 & mask) - logical shift left
inline constexpr std::uint8_t SHL = 0x24;

/// SHR: Rd = Rs1 >> (Rs2 & mask) - logical shift right (zero-fill)
inline constexpr std::uint8_t SHR = 0x25;

/// SAR: Rd = Rs1 >> (Rs2 & mask) - arithmetic shift right (sign-fill)
inline constexpr std::uint8_t SAR = 0x26;

/// ROL: Rd = rotate_left(Rs1, Rs2 % width)
inline constexpr std::uint8_t ROL = 0x27;

/// ROR: Rd = rotate_right(Rs1, Rs2 % width)
inline constexpr std::uint8_t ROR = 0x28;

// ============================================================================
// Bitwise - Shift-Immediate (Type S)
// ============================================================================

/// SHLI: Rd = Rs1 << shamt6 - shift left immediate
inline constexpr std::uint8_t SHLI = 0x29;

/// SHRI: Rd = Rs1 >> shamt6 - logical shift right immediate
inline constexpr std::uint8_t SHRI = 0x2A;

/// SARI: Rd = Rs1 >> shamt6 - arithmetic shift right immediate
inline constexpr std::uint8_t SARI = 0x2B;

// ============================================================================
// Bitwise - Register-Immediate (Type B, Accumulator Style)
// ============================================================================

/// ANDI: Rd = Rd & zero_extend(imm16)
inline constexpr std::uint8_t ANDI = 0x2C;

/// ORI: Rd = Rd | zero_extend(imm16)
inline constexpr std::uint8_t ORI = 0x2D;

/// XORI: Rd = Rd ^ zero_extend(imm16)
inline constexpr std::uint8_t XORI = 0x2E;

/// Reserved bitwise opcode
inline constexpr std::uint8_t BITWISE_RESERVED = 0x2F;

// ============================================================================
// Control Flow (0x40-0x5F) - EXEC-005
// ============================================================================

/// JMP: PC = PC + sign_extend(offset24) - Unconditional jump (Type C)
inline constexpr std::uint8_t JMP = 0x40;

/// JZ: if (Rs == 0) PC = PC + sign_extend(offset16) - Jump if zero (Type B)
inline constexpr std::uint8_t JZ = 0x41;

/// JNZ: if (Rs != 0) PC = PC + sign_extend(offset16) - Jump if not zero (Type B)
inline constexpr std::uint8_t JNZ = 0x42;

/// BEQ: if (Rs1 == Rs2) PC = PC + sign_extend(offset8) - Branch if equal (Type A)
inline constexpr std::uint8_t BEQ = 0x43;

/// BNE: if (Rs1 != Rs2) PC = PC + sign_extend(offset8) - Branch if not equal (Type A)
inline constexpr std::uint8_t BNE = 0x44;

/// BLT: if (Rs1 < Rs2) PC = PC + sign_extend(offset8) - Branch if less than, signed (Type A)
inline constexpr std::uint8_t BLT = 0x45;

/// BLE: if (Rs1 <= Rs2) PC = PC + sign_extend(offset8) - Branch if less or equal, signed (Type A)
inline constexpr std::uint8_t BLE = 0x46;

/// BGT: if (Rs1 > Rs2) PC = PC + sign_extend(offset8) - Branch if greater than, signed (Type A)
inline constexpr std::uint8_t BGT = 0x47;

/// BGE: if (Rs1 >= Rs2) PC = PC + sign_extend(offset8) - Branch if greater or equal, signed (Type A)
inline constexpr std::uint8_t BGE = 0x48;

/// CALL: push return address to CFI stack, PC = PC + sign_extend(offset24) (Type C)
inline constexpr std::uint8_t CALL = 0x50;

/// RET: pop return address from CFI stack, PC = return_addr
inline constexpr std::uint8_t RET = 0x51;

/// HALT: Stop execution (moved to control flow range per EXEC-005)
inline constexpr std::uint8_t HALT = 0x5F;

// ============================================================================
// System opcodes (0xF0-0xFF) - Essential for execution
// ============================================================================

/// NOP: No operation
inline constexpr std::uint8_t NOP = 0xF0;

}  // namespace opcode

/// Check if opcode is a Type A arithmetic instruction (register-register)
[[nodiscard]] constexpr bool is_type_a_arithmetic(std::uint8_t op) noexcept {
    return op >= opcode::ADD && op <= opcode::NEG;
}

/// Check if opcode is a Type B arithmetic instruction (immediate)
[[nodiscard]] constexpr bool is_type_b_arithmetic(std::uint8_t op) noexcept {
    return op >= opcode::ADDI && op <= opcode::MULI;
}

/// Check if opcode is a floating-point instruction (Type A format)
[[nodiscard]] constexpr bool is_floating_point(std::uint8_t op) noexcept {
    return op >= opcode::FADD && op <= opcode::I2F;
}

/// Check if opcode is any arithmetic instruction (integer or floating-point)
[[nodiscard]] constexpr bool is_arithmetic(std::uint8_t op) noexcept {
    return is_type_a_arithmetic(op) || is_type_b_arithmetic(op) || is_floating_point(op);
}

/// Check if opcode is a reserved arithmetic opcode (within 0x00-0x1F range)
[[nodiscard]] constexpr bool is_arithmetic_reserved(std::uint8_t op) noexcept {
    return (op >= opcode::ARITHMETIC_RESERVED_START && op <= opcode::ARITHMETIC_RESERVED_END) ||
           (op >= opcode::FP_RESERVED_START && op <= opcode::FP_RESERVED_END);
}

/// Check if opcode is a Type A bitwise instruction (register-register)
/// Includes: AND, OR, XOR, NOT, SHL, SHR, SAR, ROL, ROR
[[nodiscard]] constexpr bool is_type_a_bitwise(std::uint8_t op) noexcept {
    return op >= opcode::AND && op <= opcode::ROR;
}

/// Check if opcode is a Type S bitwise instruction (shift-immediate)
/// Includes: SHLI, SHRI, SARI
[[nodiscard]] constexpr bool is_type_s_bitwise(std::uint8_t op) noexcept {
    return op >= opcode::SHLI && op <= opcode::SARI;
}

/// Check if opcode is a Type B bitwise instruction (immediate)
/// Includes: ANDI, ORI, XORI
[[nodiscard]] constexpr bool is_type_b_bitwise(std::uint8_t op) noexcept {
    return op >= opcode::ANDI && op <= opcode::XORI;
}

/// Check if opcode is any bitwise instruction
[[nodiscard]] constexpr bool is_bitwise(std::uint8_t op) noexcept {
    return is_type_a_bitwise(op) || is_type_s_bitwise(op) || is_type_b_bitwise(op);
}

/// Sign-extend a 16-bit immediate to 64-bit
[[nodiscard]] constexpr std::int64_t sign_extend_imm16(std::uint16_t imm) noexcept {
    return static_cast<std::int64_t>(static_cast<std::int16_t>(imm));
}

}  // namespace dotvm::core
