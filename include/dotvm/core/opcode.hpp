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
// System opcodes (0xF0-0xFF) - Essential for execution
// ============================================================================

/// HALT: Stop execution
inline constexpr std::uint8_t HALT = 0xFF;

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

/// Sign-extend a 16-bit immediate to 64-bit
[[nodiscard]] constexpr std::int64_t sign_extend_imm16(std::uint16_t imm) noexcept {
    return static_cast<std::int64_t>(static_cast<std::int16_t>(imm));
}

}  // namespace dotvm::core
