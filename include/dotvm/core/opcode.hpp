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

/// BGE: if (Rs1 >= Rs2) PC = PC + sign_extend(offset8) - Branch if greater or equal, signed (Type
/// A)
inline constexpr std::uint8_t BGE = 0x48;

/// CALL: push return address to CFI stack, PC = PC + sign_extend(offset24) (Type C)
inline constexpr std::uint8_t CALL = 0x50;

/// RET: pop return address from CFI stack, PC = return_addr
inline constexpr std::uint8_t RET = 0x51;

// ============================================================================
// Exception Handling (0x52-0x55) - EXEC-011
// TRY/CATCH/THROW/ENDTRY for structured exception handling
// ============================================================================

/// TRY: Push exception handler frame (Type B)
/// Format: [TRY][handler_offset16][catch_types8]
/// - handler_offset16: Signed offset to CATCH handler from current PC
/// - catch_types8: Bitmask of exception types to catch (see catch_mask)
/// Pushes an ExceptionFrame with handler_pc = pc + offset, current call stack depth
inline constexpr std::uint8_t TRY = 0x52;

/// CATCH: Exception handler entry marker (Type C)
/// Format: [CATCH][24-bit reserved]
/// Marks the start of an exception handler block. During normal execution,
/// this is a NOP. The handler code follows immediately after this instruction.
/// When an exception is caught, execution jumps here and the exception
/// data is available via the exception context.
inline constexpr std::uint8_t CATCH = 0x53;

/// THROW: Raise an exception (Type A)
/// Format: [THROW][Rtype][Rpayload][unused]
/// - Rtype: Register containing ErrorCode (uint32_t)
/// - Rpayload: Register containing payload value (uint64_t)
/// Sets the current exception and searches for a matching handler.
/// If found, unwinds stack and jumps to handler. If not found, halts
/// with UnhandledException error.
inline constexpr std::uint8_t THROW = 0x54;

/// ENDTRY: Normal exit from try block (Type C)
/// Format: [ENDTRY][24-bit reserved]
/// Pops the current exception frame without triggering the handler.
/// Used at the end of successful try block execution.
inline constexpr std::uint8_t ENDTRY = 0x55;

/// Reserved exception opcodes (0x56-0x5E)
inline constexpr std::uint8_t EXCEPTION_RESERVED_START = 0x56;
inline constexpr std::uint8_t EXCEPTION_RESERVED_END = 0x5E;

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
// Cryptographic Operations (0xB0-0xBF) - SEC-008
// Type A format: [opcode(8)][Rd(8)][Rs1(8)][Rs2(8)]
// - Rd: Destination handle (output buffer)
// - Rs1: Input handle (data to process)
// - Rs2: Key/signature handle (for keyed operations)
// All crypto opcodes require Permission::Crypto
// ============================================================================

/// HASH_SHA256: Rd = SHA-256(data at Rs1)
/// Output: 32-byte digest stored in memory referenced by Rd
/// Rs2 is ignored for hash operations
inline constexpr std::uint8_t HASH_SHA256 = 0xB0;

/// HASH_BLAKE3: Rd = BLAKE3(data at Rs1)
/// Output: 32-byte digest stored in memory referenced by Rd
/// Rs2 is ignored for hash operations
inline constexpr std::uint8_t HASH_BLAKE3 = 0xB1;

/// HASH_KECCAK: Rd = Keccak-256(data at Rs1)
/// Output: 32-byte digest stored in memory referenced by Rd
/// Ethereum-compatible (0x01 padding), not NIST SHA-3
/// Rs2 is ignored for hash operations
inline constexpr std::uint8_t HASH_KECCAK = 0xB2;

/// Reserved hash opcode
inline constexpr std::uint8_t HASH_RESERVED = 0xB3;

/// SIGN_ED25519: Rd = Ed25519_Sign(message at Rs1, private_key at Rs2)
/// Output: 64-byte signature stored in memory referenced by Rd
/// Rs2 must point to 64-byte private key (32-byte seed + 32-byte public)
inline constexpr std::uint8_t SIGN_ED25519 = 0xB4;

/// VERIFY_ED25519: Rd = Ed25519_Verify(message at Rs1, sig_pubkey at Rs2)
/// Output: 1 (valid) or 0 (invalid) stored in register Rd
/// Rs2 must point to 96-byte buffer (64-byte signature + 32-byte public key)
inline constexpr std::uint8_t VERIFY_ED25519 = 0xB5;

/// Reserved signature opcodes
inline constexpr std::uint8_t SIG_RESERVED_START = 0xB6;
inline constexpr std::uint8_t SIG_RESERVED_END = 0xB7;

/// ENCRYPT_AES256: Rd = AES-256-GCM_Encrypt(plaintext at Rs1, key at Rs2)
/// Output: nonce(12) + ciphertext + tag(16) stored in memory referenced by Rd
/// Rs2 must point to 32-byte AES-256 key
/// Nonce is randomly generated and prepended to output
inline constexpr std::uint8_t ENCRYPT_AES256 = 0xB8;

/// DECRYPT_AES256: Rd = AES-256-GCM_Decrypt(ciphertext at Rs1, key at Rs2)
/// Input format: nonce(12) + ciphertext + tag(16) at Rs1
/// Output: plaintext stored in memory referenced by Rd
/// Rs2 must point to 32-byte AES-256 key
/// Returns error if authentication tag verification fails
inline constexpr std::uint8_t DECRYPT_AES256 = 0xB9;

/// Reserved crypto opcodes (0xBA-0xBF)
inline constexpr std::uint8_t CRYPTO_RESERVED_START = 0xBA;
inline constexpr std::uint8_t CRYPTO_RESERVED_END = 0xBF;

// ============================================================================
// SIMD/ParaDot Operations (0xC0-0xCF)
// ============================================================================

/// VADD: V[vd] = V[vs1] + V[vs2]
inline constexpr std::uint8_t VADD = 0xC0;

/// VSUB: V[vd] = V[vs1] - V[vs2]
inline constexpr std::uint8_t VSUB = 0xC1;

/// VMUL: V[vd] = V[vs1] * V[vs2]
inline constexpr std::uint8_t VMUL = 0xC2;

// ============================================================================
// System opcodes (0xF0-0xFF) - Essential for execution
// ============================================================================

/// NOP: No operation
inline constexpr std::uint8_t NOP = 0xF0;

/// BREAK: Software breakpoint (halts with ExecResult::Interrupted)
inline constexpr std::uint8_t BREAK = 0xF1;

/// DEBUG: Debug mode breakpoint (EXEC-010)
/// When debug mode is enabled, triggers DebugEvent::Break callback.
/// When debug mode is disabled, behaves like NOP.
inline constexpr std::uint8_t DEBUG = 0xFD;

/// SYSCALL: System call (reserved for future use)
inline constexpr std::uint8_t SYSCALL = 0xFE;

}  // namespace opcode

/// Check if opcode is a debug/system instruction
[[nodiscard]] constexpr bool is_system_op(std::uint8_t op) noexcept {
    return op >= opcode::NOP;  // 0xF0-0xFF range
}

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

/// Check if opcode is an exception handling instruction (0x52-0x55) - EXEC-011
/// Includes: TRY, CATCH, THROW, ENDTRY
[[nodiscard]] constexpr bool is_exception_op(std::uint8_t op) noexcept {
    return op >= opcode::TRY && op <= opcode::ENDTRY;
}

/// Check if opcode is a control flow instruction (0x40-0x5F)
/// Includes: jumps, branches, CALL, RET, exception handling, HALT
[[nodiscard]] constexpr bool is_control_flow_op(std::uint8_t op) noexcept {
    return op >= opcode::JMP && op <= opcode::HALT;
}

/// Check if opcode is a cryptographic operation (0xB0-0xBF) - SEC-008
/// Includes: HASH_SHA256, HASH_BLAKE3, HASH_KECCAK, SIGN_ED25519,
///           VERIFY_ED25519, ENCRYPT_AES256, DECRYPT_AES256
[[nodiscard]] constexpr bool is_crypto_op(std::uint8_t op) noexcept {
    return op >= opcode::HASH_SHA256 && op <= opcode::CRYPTO_RESERVED_END;
}

/// Check if opcode is a SIMD operation (0xC0-0xCF)
[[nodiscard]] constexpr bool is_simd_op(std::uint8_t op) noexcept {
    return op >= opcode::VADD && op <= 0xCF;
}

/// Check if opcode is a hash operation (0xB0-0xB2)
/// Includes: HASH_SHA256, HASH_BLAKE3, HASH_KECCAK
[[nodiscard]] constexpr bool is_hash_op(std::uint8_t op) noexcept {
    return op >= opcode::HASH_SHA256 && op <= opcode::HASH_KECCAK;
}

/// Check if opcode is an Ed25519 signature operation (0xB4-0xB5)
/// Includes: SIGN_ED25519, VERIFY_ED25519
[[nodiscard]] constexpr bool is_signature_op(std::uint8_t op) noexcept {
    return op == opcode::SIGN_ED25519 || op == opcode::VERIFY_ED25519;
}

/// Check if opcode is an AES-256 encryption/decryption operation (0xB8-0xB9)
/// Includes: ENCRYPT_AES256, DECRYPT_AES256
[[nodiscard]] constexpr bool is_aes256_op(std::uint8_t op) noexcept {
    return op == opcode::ENCRYPT_AES256 || op == opcode::DECRYPT_AES256;
}

}  // namespace dotvm::core

// ============================================================================
// State Opcodes (0xA0-0xAF) - STATE-004
// ============================================================================
// Defined outside the opcode namespace to avoid conflicts with existing
// namespace structure, but uses the same pattern.

namespace dotvm::core::opcode {

// State Read Operations (0xA0-0xA7) - Require ReadState permission
// Type A format: [op][rd][rs1][rs2]
// - rd: destination register (result)
// - rs1: memory handle for key
// - rs2: transaction handle (0 = no transaction)

/// STATE_GET: rd = state[key@rs1] using tx@rs2 (0=no tx)
/// Reads value from state backend. If key not found, rd = 0 and error flag set.
inline constexpr std::uint8_t STATE_GET = 0xA0;

/// STATE_EXISTS: rd = exists(key@rs1) using tx@rs2
/// Checks if key exists. rd = 1 if exists, 0 otherwise.
inline constexpr std::uint8_t STATE_EXISTS = 0xA1;

/// Reserved state read opcodes (0xA2-0xA7)
inline constexpr std::uint8_t STATE_READ_RESERVED_START = 0xA2;
inline constexpr std::uint8_t STATE_READ_RESERVED_END = 0xA7;

// State Write Operations (0xA8-0xAF) - Require WriteState permission
// Type A format: [op][rd][rs1][rs2]
// Type B format: [op][rd][imm16] (for TX_BEGIN)

/// TX_BEGIN: rd = new_tx_handle() (Type B: imm16 = isolation level)
/// Begins a new transaction. Returns unique handle in rd.
/// imm16: 0 = Snapshot, 1 = ReadCommitted
inline constexpr std::uint8_t TX_BEGIN = 0xA8;

/// TX_COMMIT: rd = commit(tx@rs1) (Type A)
/// Commits transaction. rd = 1 on success, 0 on conflict.
inline constexpr std::uint8_t TX_COMMIT = 0xA9;

/// TX_ROLLBACK: rd = rollback(tx@rs1) (Type A)
/// Rolls back transaction. rd = 1 on success, 0 on error.
inline constexpr std::uint8_t TX_ROLLBACK = 0xAA;

/// STATE_PUT: state[key@rd] = value@rs1 using tx@rs2 (Type A)
/// Writes value to state backend.
inline constexpr std::uint8_t STATE_PUT = 0xAB;

/// STATE_DELETE: rd = delete(key@rs1) using tx@rs2 (Type A)
/// Deletes key from state. rd = 1 if deleted, 0 if not found.
inline constexpr std::uint8_t STATE_DELETE = 0xAC;

/// Reserved state write opcodes (0xAD-0xAF)
inline constexpr std::uint8_t STATE_WRITE_RESERVED_START = 0xAD;
inline constexpr std::uint8_t STATE_WRITE_RESERVED_END = 0xAF;

}  // namespace dotvm::core::opcode

namespace dotvm::core {

/// Check if opcode is a state operation (0xA0-0xAF) - STATE-004
/// Includes: STATE_GET, STATE_EXISTS, TX_BEGIN, TX_COMMIT, TX_ROLLBACK, STATE_PUT, STATE_DELETE
[[nodiscard]] constexpr bool is_state_op(std::uint8_t op) noexcept {
    return op >= opcode::STATE_GET && op <= opcode::STATE_WRITE_RESERVED_END;
}

/// Check if opcode is a state read operation (0xA0-0xA7)
/// Includes: STATE_GET, STATE_EXISTS
[[nodiscard]] constexpr bool is_state_read_op(std::uint8_t op) noexcept {
    return op >= opcode::STATE_GET && op <= opcode::STATE_READ_RESERVED_END;
}

/// Check if opcode is a state write operation (0xA8-0xAF)
/// Includes: TX_BEGIN, TX_COMMIT, TX_ROLLBACK, STATE_PUT, STATE_DELETE
[[nodiscard]] constexpr bool is_state_write_op(std::uint8_t op) noexcept {
    return op >= opcode::TX_BEGIN && op <= opcode::STATE_WRITE_RESERVED_END;
}

/// Check if opcode is a transaction operation (0xA8-0xAA)
/// Includes: TX_BEGIN, TX_COMMIT, TX_ROLLBACK
[[nodiscard]] constexpr bool is_transaction_op(std::uint8_t op) noexcept {
    return op >= opcode::TX_BEGIN && op <= opcode::TX_ROLLBACK;
}

}  // namespace dotvm::core
