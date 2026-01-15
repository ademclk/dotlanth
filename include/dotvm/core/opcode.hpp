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
// Comparison Operations (0x30-0x3F) - EXEC-009
// Type A (register-register): [opcode][Rd][Rs1][Rs2]
// Type B (immediate): [opcode][Rd][imm16] - accumulator style
// Result: 1 if condition true, 0 if false
// ============================================================================

/// EQ: Rd = (Rs1 == Rs2) ? 1 : 0 (Type A)
inline constexpr std::uint8_t EQ = 0x30;

/// NE: Rd = (Rs1 != Rs2) ? 1 : 0 (Type A)
inline constexpr std::uint8_t NE = 0x31;

/// LT: Rd = (Rs1 < Rs2) ? 1 : 0 (signed, Type A)
inline constexpr std::uint8_t LT = 0x32;

/// LE: Rd = (Rs1 <= Rs2) ? 1 : 0 (signed, Type A)
inline constexpr std::uint8_t LE = 0x33;

/// GT: Rd = (Rs1 > Rs2) ? 1 : 0 (signed, Type A)
inline constexpr std::uint8_t GT = 0x34;

/// GE: Rd = (Rs1 >= Rs2) ? 1 : 0 (signed, Type A)
inline constexpr std::uint8_t GE = 0x35;

/// LTU: Rd = (Rs1 < Rs2) ? 1 : 0 (unsigned, Type A)
inline constexpr std::uint8_t LTU = 0x36;

/// LEU: Rd = (Rs1 <= Rs2) ? 1 : 0 (unsigned, Type A)
inline constexpr std::uint8_t LEU = 0x37;

/// GTU: Rd = (Rs1 > Rs2) ? 1 : 0 (unsigned, Type A)
inline constexpr std::uint8_t GTU = 0x38;

/// GEU: Rd = (Rs1 >= Rs2) ? 1 : 0 (unsigned, Type A)
inline constexpr std::uint8_t GEU = 0x39;

/// TEST: Rd = ((Rs1 & Rs2) != 0) ? 1 : 0 (Type A)
inline constexpr std::uint8_t TEST = 0x3A;

/// CMPI_EQ: Rd = (Rd == sign_extend(imm16)) ? 1 : 0 (Type B)
inline constexpr std::uint8_t CMPI_EQ = 0x3B;

/// CMPI_NE: Rd = (Rd != sign_extend(imm16)) ? 1 : 0 (Type B)
inline constexpr std::uint8_t CMPI_NE = 0x3C;

/// CMPI_LT: Rd = (Rd < sign_extend(imm16)) ? 1 : 0 (signed, Type B)
inline constexpr std::uint8_t CMPI_LT = 0x3D;

/// CMPI_GE: Rd = (Rd >= sign_extend(imm16)) ? 1 : 0 (signed, Type B)
inline constexpr std::uint8_t CMPI_GE = 0x3E;

/// Reserved comparison opcode
inline constexpr std::uint8_t COMPARISON_RESERVED = 0x3F;

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
// Memory Load/Store Operations (0x60-0x68) - EXEC-006
// Type M format: [opcode(8)][Rd/Rs2(8)][Rs1(8)][offset8(8)]
// - LOAD: Rd = mem[Rs1 + offset8]; Rs1 holds memory handle
// - STORE: mem[Rs1 + offset8] = Rs2; Rs1 holds memory handle
// - Alignment: 2-byte for 16-bit, 4-byte for 32-bit, 8-byte for 64-bit
// - Misaligned access results in UnalignedAccess error
// ============================================================================

/// LOAD8: Rd = zero_extend(mem[handle + offset8]); loads 1 byte
inline constexpr std::uint8_t LOAD8 = 0x60;

/// LOAD16: Rd = zero_extend(mem[handle + offset8]); loads 2 bytes (2-byte aligned)
inline constexpr std::uint8_t LOAD16 = 0x61;

/// LOAD32: Rd = zero_extend(mem[handle + offset8]); loads 4 bytes (4-byte aligned)
inline constexpr std::uint8_t LOAD32 = 0x62;

/// LOAD64: Rd = mem[handle + offset8]; loads 8 bytes (8-byte aligned)
inline constexpr std::uint8_t LOAD64 = 0x63;

/// STORE8: mem[handle + offset8] = Rs2[7:0]; stores 1 byte
inline constexpr std::uint8_t STORE8 = 0x64;

/// STORE16: mem[handle + offset8] = Rs2[15:0]; stores 2 bytes (2-byte aligned)
inline constexpr std::uint8_t STORE16 = 0x65;

/// STORE32: mem[handle + offset8] = Rs2[31:0]; stores 4 bytes (4-byte aligned)
inline constexpr std::uint8_t STORE32 = 0x66;

/// STORE64: mem[handle + offset8] = Rs2; stores 8 bytes (8-byte aligned)
inline constexpr std::uint8_t STORE64 = 0x67;

/// LEA: Rd = effective_address(handle, offset8); Load Effective Address
inline constexpr std::uint8_t LEA = 0x68;

/// First reserved memory opcode (after EXEC-006 opcodes)
inline constexpr std::uint8_t MEMORY_RESERVED_START = 0x69;

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

/// Check if opcode is a EXEC-006 typed memory load/store operation (Type M)
/// Includes: LOAD8, LOAD16, LOAD32, LOAD64, STORE8, STORE16, STORE32, STORE64, LEA
[[nodiscard]] constexpr bool is_typed_memory_op(std::uint8_t op) noexcept {
    return op >= opcode::LOAD8 && op <= opcode::LEA;
}

/// Check if opcode is a LOAD operation (Type M)
/// Includes: LOAD8, LOAD16, LOAD32, LOAD64
[[nodiscard]] constexpr bool is_load_op(std::uint8_t op) noexcept {
    return op >= opcode::LOAD8 && op <= opcode::LOAD64;
}

/// Check if opcode is a STORE operation (Type M)
/// Includes: STORE8, STORE16, STORE32, STORE64
[[nodiscard]] constexpr bool is_store_op(std::uint8_t op) noexcept {
    return op >= opcode::STORE8 && op <= opcode::STORE64;
}

/// Check if opcode is a comparison instruction (0x30-0x3E) - EXEC-009
/// Includes: EQ, NE, LT, LE, GT, GE, LTU, LEU, GTU, GEU, TEST, CMPI_*
[[nodiscard]] constexpr bool is_comparison_op(std::uint8_t op) noexcept {
    return op >= opcode::EQ && op <= opcode::CMPI_GE;
}

/// Check if opcode is a Type A comparison instruction (register-register)
/// Includes: EQ, NE, LT, LE, GT, GE, LTU, LEU, GTU, GEU, TEST
[[nodiscard]] constexpr bool is_type_a_comparison(std::uint8_t op) noexcept {
    return op >= opcode::EQ && op <= opcode::TEST;
}

/// Check if opcode is a Type B comparison instruction (immediate)
/// Includes: CMPI_EQ, CMPI_NE, CMPI_LT, CMPI_GE
[[nodiscard]] constexpr bool is_comparison_imm_op(std::uint8_t op) noexcept {
    return op >= opcode::CMPI_EQ && op <= opcode::CMPI_GE;
}

}  // namespace dotvm::core
