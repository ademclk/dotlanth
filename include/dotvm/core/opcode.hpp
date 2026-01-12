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
// Reserved arithmetic opcodes (0x09-0x1F)
// ============================================================================

/// First reserved arithmetic opcode
inline constexpr std::uint8_t ARITHMETIC_RESERVED_START = 0x09;

/// Last arithmetic opcode (reserved)
inline constexpr std::uint8_t ARITHMETIC_RESERVED_END = 0x1F;

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

/// Check if opcode is any arithmetic instruction
[[nodiscard]] constexpr bool is_arithmetic(std::uint8_t op) noexcept {
    return is_type_a_arithmetic(op) || is_type_b_arithmetic(op);
}

/// Sign-extend a 16-bit immediate to 64-bit
[[nodiscard]] constexpr std::int64_t sign_extend_imm16(std::uint16_t imm) noexcept {
    return static_cast<std::int64_t>(static_cast<std::int16_t>(imm));
}

}  // namespace dotvm::core
