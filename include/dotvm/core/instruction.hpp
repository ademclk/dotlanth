/// @file instruction.hpp
/// @brief Instruction encoding, decoding, and classification for DotVM bytecode.
///
/// This header defines the instruction format for the DotVM virtual machine:
/// - Type A: Register-Register [opcode][Rd][Rs1][Rs2]
/// - Type B: Register-Immediate [opcode][Rd][imm16]
/// - Type C: Offset/Jump [opcode][offset24]
/// - Type S: Shift-Immediate [opcode][Rd][Rs1][shamt6]
/// - Type D: Jump with test [opcode][Rs][offset16]
/// - Type M: Memory Load/Store [opcode][Rd/Rs2][Rs1][offset8]
///
/// All instructions are 32 bits (4 bytes) and aligned to 4-byte boundaries.
///
/// @see decode_type_a, decode_type_b, etc. for decoding functions
/// @see encode_type_a, encode_type_b, etc. for encoding functions
/// @see classify_opcode for opcode category classification

#pragma once

#include <cstdint>

namespace dotvm::core {

/// @brief Instruction bit layout constants.
namespace instr_bits {
/// @brief Bit position of opcode field (bits 31:24).
inline constexpr int OPCODE_SHIFT = 24;
/// @brief Bit position of Rd field (bits 23:16).
inline constexpr int RD_SHIFT = 16;
/// @brief Bit position of Rs1 field (bits 15:8).
inline constexpr int RS1_SHIFT = 8;
/// @brief Bit position of Rs2 field (bits 7:0).
inline constexpr int RS2_SHIFT = 0;
/// @brief Bit position of imm16 field (bits 15:0).
inline constexpr int IMM16_SHIFT = 0;
/// @brief Bit position of offset24 field (bits 23:0).
inline constexpr int OFFSET24_SHIFT = 0;

/// @brief Mask for 8-bit opcode field.
inline constexpr std::uint32_t OPCODE_MASK = 0xFFU;
/// @brief Mask for 8-bit register field.
inline constexpr std::uint32_t REG_MASK = 0xFFU;
/// @brief Mask for 16-bit immediate field.
inline constexpr std::uint32_t IMM16_MASK = 0xFFFFU;
/// @brief Mask for 24-bit offset field.
inline constexpr std::uint32_t OFFSET24_MASK = 0xFFFFFFU;
/// @brief Mask for 6-bit shift amount (0-63).
inline constexpr std::uint32_t SHAMT6_MASK = 0x3FU;

/// @brief Sign bit position for 24-bit offset.
inline constexpr std::uint32_t OFFSET24_SIGN_BIT = 1U << 23;
/// @brief Sign extension mask for 24-bit to 32-bit conversion.
inline constexpr std::uint32_t OFFSET24_SIGN_EXTEND = 0xFF000000U;
}  // namespace instr_bits

/// @brief Opcode range constants defining instruction categories.
namespace opcode_range {
/// @brief First arithmetic opcode (0x00).
inline constexpr std::uint8_t ARITHMETIC_START = 0x00;
/// @brief Last arithmetic opcode (0x1F).
inline constexpr std::uint8_t ARITHMETIC_END = 0x1F;

/// @brief First bitwise opcode (0x20).
inline constexpr std::uint8_t BITWISE_START = 0x20;
/// @brief Last bitwise opcode (0x2F).
inline constexpr std::uint8_t BITWISE_END = 0x2F;

/// @brief First comparison opcode (0x30).
inline constexpr std::uint8_t COMPARISON_START = 0x30;
/// @brief Last comparison opcode (0x3F).
inline constexpr std::uint8_t COMPARISON_END = 0x3F;

/// @brief First control flow opcode (0x40).
inline constexpr std::uint8_t CONTROL_FLOW_START = 0x40;
/// @brief Last control flow opcode (0x5F).
inline constexpr std::uint8_t CONTROL_FLOW_END = 0x5F;

/// @brief First memory opcode (0x60).
inline constexpr std::uint8_t MEMORY_START = 0x60;
/// @brief Last memory opcode (0x7F).
inline constexpr std::uint8_t MEMORY_END = 0x7F;

/// @brief First data move opcode (0x80).
inline constexpr std::uint8_t DATA_MOVE_START = 0x80;
/// @brief Last data move opcode (0x8F).
inline constexpr std::uint8_t DATA_MOVE_END = 0x8F;

/// @brief First reserved opcode in 0x90 range.
inline constexpr std::uint8_t RESERVED_90_START = 0x90;
/// @brief Last reserved opcode in 0x90 range (0x9F).
inline constexpr std::uint8_t RESERVED_90_END = 0x9F;

/// @brief First state opcode (0xA0).
inline constexpr std::uint8_t STATE_START = 0xA0;
/// @brief Last state opcode (0xAF).
inline constexpr std::uint8_t STATE_END = 0xAF;

/// @brief First crypto opcode (0xB0).
inline constexpr std::uint8_t CRYPTO_START = 0xB0;
/// @brief Last crypto opcode (0xBF).
inline constexpr std::uint8_t CRYPTO_END = 0xBF;

/// @brief First ParaDot opcode (0xC0).
inline constexpr std::uint8_t PARA_DOT_START = 0xC0;
/// @brief Last ParaDot opcode (0xCF).
inline constexpr std::uint8_t PARA_DOT_END = 0xCF;

/// @brief First reserved opcode in 0xD0 range.
inline constexpr std::uint8_t RESERVED_D0_START = 0xD0;
/// @brief Last reserved opcode in 0xD0-0xEF range.
inline constexpr std::uint8_t RESERVED_EF_END = 0xEF;

/// @brief First system opcode (0xF0).
inline constexpr std::uint8_t SYSTEM_START = 0xF0;
/// @brief Last system opcode (0xFF).
inline constexpr std::uint8_t SYSTEM_END = 0xFF;
}  // namespace opcode_range

/// @brief Opcode categories for classification.
enum class OpcodeCategory : std::uint8_t {
    Arithmetic = 0,   ///< 0x00-0x1F: ADD, SUB, MUL, DIV, etc.
    Bitwise = 1,      ///< 0x20-0x2F: AND, OR, XOR, NOT, etc.
    Comparison = 2,   ///< 0x30-0x3F: CMP, TEST, etc.
    ControlFlow = 3,  ///< 0x40-0x5F: JMP, JZ, CALL, RET, etc.
    Memory = 4,       ///< 0x60-0x7F: LOAD, STORE, ALLOC, etc.
    DataMove = 5,     ///< 0x80-0x8F: MOV, LDI, etc.
    Reserved90 = 6,   ///< 0x90-0x9F: Reserved for future use.
    State = 7,        ///< 0xA0-0xAF: State management operations.
    Crypto = 8,       ///< 0xB0-0xBF: Cryptographic operations.
    ParaDot = 9,      ///< 0xC0-0xCF: Parallel Dot operations.
    ReservedD0 = 10,  ///< 0xD0-0xEF: Reserved for future use.
    System = 11       ///< 0xF0-0xFF: System calls and control.
};

/// @brief Instruction format type enumeration.
enum class InstructionType : std::uint8_t {
    TypeA = 0,  ///< Register-Register: [opcode][Rd][Rs1][Rs2]
    TypeB = 1,  ///< Register-Immediate: [opcode][Rd][imm16]
    TypeC = 2,  ///< Offset/Jump: [opcode][offset24]
    TypeS = 3,  ///< Shift-Immediate: [opcode][Rd][Rs1][shamt6]
    TypeD = 4,  ///< Jump with test: [opcode][Rs][offset16]
    TypeM = 5   ///< Memory Load/Store: [opcode][Rd/Rs2][Rs1][offset8]
};

/// @brief Decoded Type A instruction: Register-Register operations.
///
/// Layout: [31:24]=opcode [23:16]=Rd [15:8]=Rs1 [7:0]=Rs2
struct DecodedTypeA {
    std::uint8_t opcode;  ///< Operation code (8 bits).
    std::uint8_t rd;      ///< Destination register (0-255).
    std::uint8_t rs1;     ///< Source register 1 (0-255).
    std::uint8_t rs2;     ///< Source register 2 (0-255).

    constexpr bool operator==(const DecodedTypeA&) const noexcept = default;
};

/// @brief Decoded Type B instruction: Register-Immediate operations.
///
/// Layout: [31:24]=opcode [23:16]=Rd [15:0]=imm16
struct DecodedTypeB {
    std::uint8_t opcode;  ///< Operation code (8 bits).
    std::uint8_t rd;      ///< Destination register (0-255).
    std::uint16_t imm16;  ///< 16-bit immediate value.

    constexpr bool operator==(const DecodedTypeB&) const noexcept = default;
};

/// @brief Decoded Type C instruction: Offset/Jump operations.
///
/// Layout: [31:24]=opcode [23:0]=offset24 (signed)
struct DecodedTypeC {
    std::uint8_t opcode;    ///< Operation code (8 bits).
    std::int32_t offset24;  ///< Sign-extended 24-bit offset.

    constexpr bool operator==(const DecodedTypeC&) const noexcept = default;
};

/// @brief Decoded Type S instruction: Shift-Immediate operations.
///
/// Layout: [31:24]=opcode [23:16]=Rd [15:8]=Rs1 [5:0]=shamt6
/// Used for SHLI, SHRI, SARI with 6-bit shift amount.
struct DecodedTypeS {
    std::uint8_t opcode;  ///< Operation code (8 bits).
    std::uint8_t rd;      ///< Destination register (0-255).
    std::uint8_t rs1;     ///< Source register (0-255).
    std::uint8_t shamt6;  ///< 6-bit shift amount (0-63).

    constexpr bool operator==(const DecodedTypeS&) const noexcept = default;
};

/// @brief Decoded Type D instruction: JZ/JNZ operations.
///
/// Layout: [31:24]=opcode [23:16]=Rs [15:0]=offset16 (signed)
/// Used for JZ and JNZ instructions that test a single register against zero.
struct DecodedTypeD {
    std::uint8_t opcode;    ///< Operation code (8 bits).
    std::uint8_t rs;        ///< Register to test against zero (0-255).
    std::int16_t offset16;  ///< Sign-extended 16-bit offset.

    constexpr bool operator==(const DecodedTypeD&) const noexcept = default;
};

/// @brief Decoded Type M instruction: Memory Load/Store operations.
///
/// Layout: [31:24]=opcode [23:16]=Rd/Rs2 [15:8]=Rs1 [7:0]=offset8 (signed)
/// - For LOAD/LEA: Rd = destination register, Rs1 = base handle, offset8 = signed offset
/// - For STORE: Rs2 = source value register, Rs1 = base handle, offset8 = signed offset
struct DecodedTypeM {
    std::uint8_t opcode;  ///< Operation code (8 bits).
    std::uint8_t rd_rs2;  ///< Destination (LOAD/LEA) or Source value (STORE).
    std::uint8_t rs1;     ///< Base handle register (0-255).
    std::int8_t offset8;  ///< Signed 8-bit offset (-128 to +127).

    constexpr bool operator==(const DecodedTypeM&) const noexcept = default;
};

static_assert(sizeof(DecodedTypeA) == 4, "DecodedTypeA must be 4 bytes");
static_assert(sizeof(DecodedTypeB) == 4, "DecodedTypeB must be 4 bytes");
static_assert(sizeof(DecodedTypeC) == 8, "DecodedTypeC is 8 bytes due to alignment");
static_assert(sizeof(DecodedTypeS) == 4, "DecodedTypeS must be 4 bytes");
static_assert(sizeof(DecodedTypeD) == 4, "DecodedTypeD must be 4 bytes");
static_assert(sizeof(DecodedTypeM) == 4, "DecodedTypeM must be 4 bytes");

// --- Decoding Functions ---

/// @brief Extracts the opcode from any instruction.
/// @param instr The 32-bit instruction word.
/// @return The 8-bit opcode.
[[nodiscard]] constexpr std::uint8_t extract_opcode(std::uint32_t instr) noexcept {
    return static_cast<std::uint8_t>((instr >> instr_bits::OPCODE_SHIFT) & instr_bits::OPCODE_MASK);
}

/// @brief Decodes a Type A instruction (register-register).
/// @param instr The 32-bit instruction word.
/// @return Decoded fields: opcode, rd, rs1, rs2.
[[nodiscard]] constexpr DecodedTypeA decode_type_a(std::uint32_t instr) noexcept {
    return DecodedTypeA{
        .opcode = static_cast<std::uint8_t>((instr >> instr_bits::OPCODE_SHIFT) &
                                            instr_bits::OPCODE_MASK),
        .rd = static_cast<std::uint8_t>((instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .rs1 = static_cast<std::uint8_t>((instr >> instr_bits::RS1_SHIFT) & instr_bits::REG_MASK),
        .rs2 = static_cast<std::uint8_t>((instr >> instr_bits::RS2_SHIFT) & instr_bits::REG_MASK)};
}

/// @brief Decodes a Type B instruction (register-immediate).
/// @param instr The 32-bit instruction word.
/// @return Decoded fields: opcode, rd, imm16.
[[nodiscard]] constexpr DecodedTypeB decode_type_b(std::uint32_t instr) noexcept {
    return DecodedTypeB{
        .opcode = static_cast<std::uint8_t>((instr >> instr_bits::OPCODE_SHIFT) &
                                            instr_bits::OPCODE_MASK),
        .rd = static_cast<std::uint8_t>((instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .imm16 = static_cast<std::uint16_t>((instr >> instr_bits::IMM16_SHIFT) &
                                            instr_bits::IMM16_MASK)};
}

/// @brief Decodes a Type C instruction (offset/jump).
/// @param instr The 32-bit instruction word.
/// @return Decoded fields: opcode, offset24 (sign-extended).
[[nodiscard]] constexpr DecodedTypeC decode_type_c(std::uint32_t instr) noexcept {
    // Extract 24-bit value
    std::uint32_t raw_offset = (instr >> instr_bits::OFFSET24_SHIFT) & instr_bits::OFFSET24_MASK;

    // Sign-extend from 24 bits to 32 bits
    std::int32_t signed_offset;
    if ((raw_offset & instr_bits::OFFSET24_SIGN_BIT) != 0) {
        signed_offset = static_cast<std::int32_t>(raw_offset | instr_bits::OFFSET24_SIGN_EXTEND);
    } else {
        signed_offset = static_cast<std::int32_t>(raw_offset);
    }

    return DecodedTypeC{.opcode = static_cast<std::uint8_t>((instr >> instr_bits::OPCODE_SHIFT) &
                                                            instr_bits::OPCODE_MASK),
                        .offset24 = signed_offset};
}

/// @brief Decodes a Type S instruction (shift-immediate).
/// @param instr The 32-bit instruction word.
/// @return Decoded fields: opcode, rd, rs1, shamt6.
[[nodiscard]] constexpr DecodedTypeS decode_type_s(std::uint32_t instr) noexcept {
    return DecodedTypeS{
        .opcode = static_cast<std::uint8_t>((instr >> instr_bits::OPCODE_SHIFT) &
                                            instr_bits::OPCODE_MASK),
        .rd = static_cast<std::uint8_t>((instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .rs1 = static_cast<std::uint8_t>((instr >> instr_bits::RS1_SHIFT) & instr_bits::REG_MASK),
        .shamt6 = static_cast<std::uint8_t>(instr & instr_bits::SHAMT6_MASK)};
}

/// @brief Decodes a Type D instruction (jump with test).
/// @param instr The 32-bit instruction word.
/// @return Decoded fields: opcode, rs, offset16 (sign-extended).
[[nodiscard]] constexpr DecodedTypeD decode_type_d(std::uint32_t instr) noexcept {
    return DecodedTypeD{
        .opcode = static_cast<std::uint8_t>((instr >> instr_bits::OPCODE_SHIFT) &
                                            instr_bits::OPCODE_MASK),
        .rs = static_cast<std::uint8_t>((instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .offset16 = static_cast<std::int16_t>(instr & instr_bits::IMM16_MASK)};
}

/// @brief Decodes a Type M instruction (memory load/store).
/// @param instr The 32-bit instruction word.
/// @return Decoded fields: opcode, rd_rs2, rs1, offset8 (signed).
[[nodiscard]] constexpr DecodedTypeM decode_type_m(std::uint32_t instr) noexcept {
    return DecodedTypeM{
        .opcode = static_cast<std::uint8_t>((instr >> instr_bits::OPCODE_SHIFT) &
                                            instr_bits::OPCODE_MASK),
        .rd_rs2 = static_cast<std::uint8_t>((instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .rs1 = static_cast<std::uint8_t>((instr >> instr_bits::RS1_SHIFT) & instr_bits::REG_MASK),
        .offset8 = static_cast<std::int8_t>(instr & 0xFFU)};
}

// --- Encoding Functions ---

/// @brief Encodes a Type A instruction (register-register).
/// @param opcode Operation code.
/// @param rd Destination register.
/// @param rs1 Source register 1.
/// @param rs2 Source register 2.
/// @return The encoded 32-bit instruction word.
[[nodiscard]] constexpr std::uint32_t encode_type_a(std::uint8_t opcode, std::uint8_t rd,
                                                    std::uint8_t rs1, std::uint8_t rs2) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rd) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(rs1) << instr_bits::RS1_SHIFT) |
           (static_cast<std::uint32_t>(rs2) << instr_bits::RS2_SHIFT);
}

/// @brief Encodes a Type B instruction (register-immediate).
/// @param opcode Operation code.
/// @param rd Destination register.
/// @param imm16 16-bit immediate value.
/// @return The encoded 32-bit instruction word.
[[nodiscard]] constexpr std::uint32_t encode_type_b(std::uint8_t opcode, std::uint8_t rd,
                                                    std::uint16_t imm16) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rd) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(imm16) << instr_bits::IMM16_SHIFT);
}

/// @brief Encodes a Type C instruction (offset/jump).
/// @param opcode Operation code.
/// @param offset24 Signed 24-bit offset (truncated if out of range).
/// @return The encoded 32-bit instruction word.
/// @note Values outside [-8388608, 8388607] are truncated to 24 bits.
[[nodiscard]] constexpr std::uint32_t encode_type_c(std::uint8_t opcode,
                                                    std::int32_t offset24) noexcept {
    // Mask to 24 bits (handles both positive and negative values correctly)
    std::uint32_t masked_offset = static_cast<std::uint32_t>(offset24) & instr_bits::OFFSET24_MASK;

    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (masked_offset << instr_bits::OFFSET24_SHIFT);
}

/// @brief Encodes a Type S instruction (shift-immediate).
/// @param opcode Operation code.
/// @param rd Destination register.
/// @param rs1 Source register.
/// @param shamt6 Shift amount (masked to 6 bits, 0-63).
/// @return The encoded 32-bit instruction word.
[[nodiscard]] constexpr std::uint32_t encode_type_s(std::uint8_t opcode, std::uint8_t rd,
                                                    std::uint8_t rs1,
                                                    std::uint8_t shamt6) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rd) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(rs1) << instr_bits::RS1_SHIFT) |
           (static_cast<std::uint32_t>(shamt6) & instr_bits::SHAMT6_MASK);
}

/// @brief Encodes a Type D instruction (jump with test).
/// @param opcode Operation code.
/// @param rs Register to test.
/// @param offset16 Signed 16-bit offset.
/// @return The encoded 32-bit instruction word.
[[nodiscard]] constexpr std::uint32_t encode_type_d(std::uint8_t opcode, std::uint8_t rs,
                                                    std::int16_t offset16) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rs) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(offset16)) &
            instr_bits::IMM16_MASK);
}

/// @brief Encodes a Type M instruction (memory load/store).
/// @param opcode Operation code.
/// @param rd_rs2 Destination (LOAD) or source value (STORE) register.
/// @param rs1 Base handle register.
/// @param offset8 Signed 8-bit offset.
/// @return The encoded 32-bit instruction word.
[[nodiscard]] constexpr std::uint32_t encode_type_m(std::uint8_t opcode, std::uint8_t rd_rs2,
                                                    std::uint8_t rs1,
                                                    std::int8_t offset8) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rd_rs2) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(rs1) << instr_bits::RS1_SHIFT) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(offset8)));
}

// --- Opcode Classification Functions ---

/// @brief Classifies an opcode into its functional category.
/// @param opcode The opcode to classify.
/// @return The opcode's category (Arithmetic, Memory, etc.).
[[nodiscard]] constexpr OpcodeCategory classify_opcode(std::uint8_t opcode) noexcept {
    if (opcode <= opcode_range::ARITHMETIC_END) {
        return OpcodeCategory::Arithmetic;
    }
    if (opcode <= opcode_range::BITWISE_END) {
        return OpcodeCategory::Bitwise;
    }
    if (opcode <= opcode_range::COMPARISON_END) {
        return OpcodeCategory::Comparison;
    }
    if (opcode <= opcode_range::CONTROL_FLOW_END) {
        return OpcodeCategory::ControlFlow;
    }
    if (opcode <= opcode_range::MEMORY_END) {
        return OpcodeCategory::Memory;
    }
    if (opcode <= opcode_range::DATA_MOVE_END) {
        return OpcodeCategory::DataMove;
    }
    if (opcode <= opcode_range::RESERVED_90_END) {
        return OpcodeCategory::Reserved90;
    }
    if (opcode <= opcode_range::STATE_END) {
        return OpcodeCategory::State;
    }
    if (opcode <= opcode_range::CRYPTO_END) {
        return OpcodeCategory::Crypto;
    }
    if (opcode <= opcode_range::PARA_DOT_END) {
        return OpcodeCategory::ParaDot;
    }
    if (opcode <= opcode_range::RESERVED_EF_END) {
        return OpcodeCategory::ReservedD0;
    }
    return OpcodeCategory::System;  // 0xF0-0xFF
}

/// @brief Checks if opcode is arithmetic (0x00-0x1F).
[[nodiscard]] constexpr bool is_arithmetic_opcode(std::uint8_t opcode) noexcept {
    return opcode <= opcode_range::ARITHMETIC_END;
}

/// @brief Checks if opcode is bitwise (0x20-0x2F).
[[nodiscard]] constexpr bool is_bitwise_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::BITWISE_START && opcode <= opcode_range::BITWISE_END;
}

/// @brief Checks if opcode is comparison (0x30-0x3F).
[[nodiscard]] constexpr bool is_comparison_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::COMPARISON_START && opcode <= opcode_range::COMPARISON_END;
}

/// @brief Checks if opcode is control flow (0x40-0x5F).
[[nodiscard]] constexpr bool is_control_flow_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::CONTROL_FLOW_START && opcode <= opcode_range::CONTROL_FLOW_END;
}

/// @brief Checks if opcode is memory (0x60-0x7F).
[[nodiscard]] constexpr bool is_memory_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::MEMORY_START && opcode <= opcode_range::MEMORY_END;
}

/// @brief Checks if opcode is data move (0x80-0x8F).
[[nodiscard]] constexpr bool is_data_move_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::DATA_MOVE_START && opcode <= opcode_range::DATA_MOVE_END;
}

/// @brief Checks if opcode is state management (0xA0-0xAF).
[[nodiscard]] constexpr bool is_state_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::STATE_START && opcode <= opcode_range::STATE_END;
}

/// @brief Checks if opcode is cryptographic (0xB0-0xBF).
[[nodiscard]] constexpr bool is_crypto_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::CRYPTO_START && opcode <= opcode_range::CRYPTO_END;
}

/// @brief Checks if opcode is ParaDot (0xC0-0xCF).
[[nodiscard]] constexpr bool is_para_dot_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::PARA_DOT_START && opcode <= opcode_range::PARA_DOT_END;
}

/// @brief Checks if opcode is system (0xF0-0xFF).
[[nodiscard]] constexpr bool is_system_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::SYSTEM_START;
}

/// @brief Checks if opcode is in a reserved range (0x90-0x9F or 0xD0-0xEF).
[[nodiscard]] constexpr bool is_reserved_opcode(std::uint8_t opcode) noexcept {
    return (opcode >= opcode_range::RESERVED_90_START && opcode <= opcode_range::RESERVED_90_END) ||
           (opcode >= opcode_range::RESERVED_D0_START && opcode <= opcode_range::RESERVED_EF_END);
}

}  // namespace dotvm::core
