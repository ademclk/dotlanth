#pragma once

#include <cstdint>

namespace dotvm::core {

// Instruction bit layout constants
namespace instr_bits {
    // Field positions (bit indices)
    inline constexpr int OPCODE_SHIFT   = 24;
    inline constexpr int RD_SHIFT       = 16;
    inline constexpr int RS1_SHIFT      = 8;
    inline constexpr int RS2_SHIFT      = 0;
    inline constexpr int IMM16_SHIFT    = 0;
    inline constexpr int OFFSET24_SHIFT = 0;

    // Field masks (after shifting)
    inline constexpr std::uint32_t OPCODE_MASK   = 0xFFU;       // 8 bits
    inline constexpr std::uint32_t REG_MASK      = 0xFFU;       // 8 bits
    inline constexpr std::uint32_t IMM16_MASK    = 0xFFFFU;     // 16 bits
    inline constexpr std::uint32_t OFFSET24_MASK = 0xFFFFFFU;   // 24 bits
    inline constexpr std::uint32_t SHAMT6_MASK   = 0x3FU;       // 6 bits (0-63)

    // Sign extension constants for 24-bit offset
    inline constexpr std::uint32_t OFFSET24_SIGN_BIT    = 1U << 23;
    inline constexpr std::uint32_t OFFSET24_SIGN_EXTEND = 0xFF000000U;
} // namespace instr_bits

// Opcode range constants
namespace opcode_range {
    // Arithmetic: 0x00-0x1F (32 opcodes)
    inline constexpr std::uint8_t ARITHMETIC_START = 0x00;
    inline constexpr std::uint8_t ARITHMETIC_END   = 0x1F;

    // Bitwise: 0x20-0x2F (16 opcodes)
    inline constexpr std::uint8_t BITWISE_START = 0x20;
    inline constexpr std::uint8_t BITWISE_END   = 0x2F;

    // Comparison: 0x30-0x3F (16 opcodes)
    inline constexpr std::uint8_t COMPARISON_START = 0x30;
    inline constexpr std::uint8_t COMPARISON_END   = 0x3F;

    // ControlFlow: 0x40-0x5F (32 opcodes)
    inline constexpr std::uint8_t CONTROL_FLOW_START = 0x40;
    inline constexpr std::uint8_t CONTROL_FLOW_END   = 0x5F;

    // Memory: 0x60-0x7F (32 opcodes)
    inline constexpr std::uint8_t MEMORY_START = 0x60;
    inline constexpr std::uint8_t MEMORY_END   = 0x7F;

    // DataMove: 0x80-0x8F (16 opcodes)
    inline constexpr std::uint8_t DATA_MOVE_START = 0x80;
    inline constexpr std::uint8_t DATA_MOVE_END   = 0x8F;

    // Reserved: 0x90-0x9F (16 opcodes)
    inline constexpr std::uint8_t RESERVED_90_START = 0x90;
    inline constexpr std::uint8_t RESERVED_90_END   = 0x9F;

    // State: 0xA0-0xAF (16 opcodes)
    inline constexpr std::uint8_t STATE_START = 0xA0;
    inline constexpr std::uint8_t STATE_END   = 0xAF;

    // Crypto: 0xB0-0xBF (16 opcodes)
    inline constexpr std::uint8_t CRYPTO_START = 0xB0;
    inline constexpr std::uint8_t CRYPTO_END   = 0xBF;

    // ParaDot: 0xC0-0xCF (16 opcodes)
    inline constexpr std::uint8_t PARA_DOT_START = 0xC0;
    inline constexpr std::uint8_t PARA_DOT_END   = 0xCF;

    // Reserved: 0xD0-0xEF (32 opcodes)
    inline constexpr std::uint8_t RESERVED_D0_START = 0xD0;
    inline constexpr std::uint8_t RESERVED_EF_END   = 0xEF;

    // System: 0xF0-0xFF (16 opcodes)
    inline constexpr std::uint8_t SYSTEM_START = 0xF0;
    inline constexpr std::uint8_t SYSTEM_END   = 0xFF;
} // namespace opcode_range

// Opcode categories
enum class OpcodeCategory : std::uint8_t {
    Arithmetic  = 0,   // 0x00-0x1F
    Bitwise     = 1,   // 0x20-0x2F
    Comparison  = 2,   // 0x30-0x3F
    ControlFlow = 3,   // 0x40-0x5F
    Memory      = 4,   // 0x60-0x7F
    DataMove    = 5,   // 0x80-0x8F
    Reserved90  = 6,   // 0x90-0x9F
    State       = 7,   // 0xA0-0xAF
    Crypto      = 8,   // 0xB0-0xBF
    ParaDot     = 9,   // 0xC0-0xCF
    ReservedD0  = 10,  // 0xD0-0xEF
    System      = 11   // 0xF0-0xFF
};

// Instruction type enumeration
enum class InstructionType : std::uint8_t {
    TypeA = 0,  // Register-Register: [opcode][Rd][Rs1][Rs2]
    TypeB = 1,  // Register-Immediate: [opcode][Rd][imm16]
    TypeC = 2,  // Offset/Jump: [opcode][offset24]
    TypeS = 3,  // Shift-Immediate: [opcode][Rd][Rs1][shamt6]
    TypeD = 4,  // Jump with test: [opcode][Rs][offset16] (EXEC-005)
    TypeM = 5   // Memory Load/Store: [opcode][Rd/Rs2][Rs1][offset8] (EXEC-006)
};

// Decoded Type A instruction: Register-Register operations
// Layout: [31:24]=opcode [23:16]=Rd [15:8]=Rs1 [7:0]=Rs2
struct DecodedTypeA {
    std::uint8_t opcode;
    std::uint8_t rd;   // Destination register
    std::uint8_t rs1;  // Source register 1
    std::uint8_t rs2;  // Source register 2

    constexpr bool operator==(const DecodedTypeA&) const noexcept = default;
};

// Decoded Type B instruction: Register-Immediate operations
// Layout: [31:24]=opcode [23:16]=Rd [15:0]=imm16
struct DecodedTypeB {
    std::uint8_t opcode;
    std::uint8_t rd;       // Destination register
    std::uint16_t imm16;   // 16-bit immediate value

    constexpr bool operator==(const DecodedTypeB&) const noexcept = default;
};

// Decoded Type C instruction: Offset/Jump operations
// Layout: [31:24]=opcode [23:0]=offset24 (signed)
struct DecodedTypeC {
    std::uint8_t opcode;
    std::int32_t offset24;  // Sign-extended 24-bit offset

    constexpr bool operator==(const DecodedTypeC&) const noexcept = default;
};

// Decoded Type S instruction: Shift-Immediate operations
// Layout: [31:24]=opcode [23:16]=Rd [15:8]=Rs1 [5:0]=shamt6
// Used for SHLI, SHRI, SARI with 6-bit shift amount
struct DecodedTypeS {
    std::uint8_t opcode;
    std::uint8_t rd;      // Destination register
    std::uint8_t rs1;     // Source register
    std::uint8_t shamt6;  // 6-bit shift amount (0-63)

    constexpr bool operator==(const DecodedTypeS&) const noexcept = default;
};

// Decoded Type D instruction: JZ/JNZ operations (EXEC-005)
// Layout: [31:24]=opcode [23:16]=Rs [15:0]=offset16 (signed)
// Used for JZ and JNZ instructions that test a single register against zero
struct DecodedTypeD {
    std::uint8_t opcode;
    std::uint8_t rs;        // Register to test against zero
    std::int16_t offset16;  // Sign-extended 16-bit offset

    constexpr bool operator==(const DecodedTypeD&) const noexcept = default;
};

// Decoded Type M instruction: Memory Load/Store operations (EXEC-006)
// Layout: [31:24]=opcode [23:16]=Rd/Rs2 [15:8]=Rs1 [7:0]=offset8 (signed)
// - For LOAD/LEA: Rd = destination register, Rs1 = base handle, offset8 = signed offset
// - For STORE: Rs2 = source value register, Rs1 = base handle, offset8 = signed offset
struct DecodedTypeM {
    std::uint8_t opcode;
    std::uint8_t rd_rs2;   // Destination (LOAD/LEA) or Source value (STORE)
    std::uint8_t rs1;      // Base handle register
    std::int8_t offset8;   // Signed 8-bit offset (-128 to +127)

    constexpr bool operator==(const DecodedTypeM&) const noexcept = default;
};

static_assert(sizeof(DecodedTypeA) == 4, "DecodedTypeA must be 4 bytes");
static_assert(sizeof(DecodedTypeB) == 4, "DecodedTypeB must be 4 bytes");
static_assert(sizeof(DecodedTypeC) == 8, "DecodedTypeC is 8 bytes due to alignment");
static_assert(sizeof(DecodedTypeS) == 4, "DecodedTypeS must be 4 bytes");
static_assert(sizeof(DecodedTypeD) == 4, "DecodedTypeD must be 4 bytes");
static_assert(sizeof(DecodedTypeM) == 4, "DecodedTypeM must be 4 bytes");

// --- Decoding Functions ---

// Extract opcode from any instruction
[[nodiscard]] constexpr std::uint8_t extract_opcode(std::uint32_t instr) noexcept {
    return static_cast<std::uint8_t>(
        (instr >> instr_bits::OPCODE_SHIFT) & instr_bits::OPCODE_MASK);
}

// Type A Decoder: [31:24]=opcode, [23:16]=Rd, [15:8]=Rs1, [7:0]=Rs2
[[nodiscard]] constexpr DecodedTypeA decode_type_a(std::uint32_t instr) noexcept {
    return DecodedTypeA{
        .opcode = static_cast<std::uint8_t>(
            (instr >> instr_bits::OPCODE_SHIFT) & instr_bits::OPCODE_MASK),
        .rd = static_cast<std::uint8_t>(
            (instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .rs1 = static_cast<std::uint8_t>(
            (instr >> instr_bits::RS1_SHIFT) & instr_bits::REG_MASK),
        .rs2 = static_cast<std::uint8_t>(
            (instr >> instr_bits::RS2_SHIFT) & instr_bits::REG_MASK)
    };
}

// Type B Decoder: [31:24]=opcode, [23:16]=Rd, [15:0]=imm16
[[nodiscard]] constexpr DecodedTypeB decode_type_b(std::uint32_t instr) noexcept {
    return DecodedTypeB{
        .opcode = static_cast<std::uint8_t>(
            (instr >> instr_bits::OPCODE_SHIFT) & instr_bits::OPCODE_MASK),
        .rd = static_cast<std::uint8_t>(
            (instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .imm16 = static_cast<std::uint16_t>(
            (instr >> instr_bits::IMM16_SHIFT) & instr_bits::IMM16_MASK)
    };
}

// Type C Decoder: [31:24]=opcode, [23:0]=offset24 (signed)
[[nodiscard]] constexpr DecodedTypeC decode_type_c(std::uint32_t instr) noexcept {
    // Extract 24-bit value
    std::uint32_t raw_offset =
        (instr >> instr_bits::OFFSET24_SHIFT) & instr_bits::OFFSET24_MASK;

    // Sign-extend from 24 bits to 32 bits
    std::int32_t signed_offset;
    if ((raw_offset & instr_bits::OFFSET24_SIGN_BIT) != 0) {
        signed_offset = static_cast<std::int32_t>(
            raw_offset | instr_bits::OFFSET24_SIGN_EXTEND);
    } else {
        signed_offset = static_cast<std::int32_t>(raw_offset);
    }

    return DecodedTypeC{
        .opcode = static_cast<std::uint8_t>(
            (instr >> instr_bits::OPCODE_SHIFT) & instr_bits::OPCODE_MASK),
        .offset24 = signed_offset
    };
}

// Type S Decoder: [31:24]=opcode, [23:16]=Rd, [15:8]=Rs1, [5:0]=shamt6
// Used for shift-immediate operations (SHLI, SHRI, SARI)
[[nodiscard]] constexpr DecodedTypeS decode_type_s(std::uint32_t instr) noexcept {
    return DecodedTypeS{
        .opcode = static_cast<std::uint8_t>(
            (instr >> instr_bits::OPCODE_SHIFT) & instr_bits::OPCODE_MASK),
        .rd = static_cast<std::uint8_t>(
            (instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .rs1 = static_cast<std::uint8_t>(
            (instr >> instr_bits::RS1_SHIFT) & instr_bits::REG_MASK),
        .shamt6 = static_cast<std::uint8_t>(
            instr & instr_bits::SHAMT6_MASK)
    };
}

// Type D Decoder: [31:24]=opcode, [23:16]=Rs, [15:0]=offset16 (signed)
// Used for JZ/JNZ instructions (EXEC-005)
[[nodiscard]] constexpr DecodedTypeD decode_type_d(std::uint32_t instr) noexcept {
    return DecodedTypeD{
        .opcode = static_cast<std::uint8_t>(
            (instr >> instr_bits::OPCODE_SHIFT) & instr_bits::OPCODE_MASK),
        .rs = static_cast<std::uint8_t>(
            (instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .offset16 = static_cast<std::int16_t>(
            instr & instr_bits::IMM16_MASK)
    };
}

// Type M Decoder: [31:24]=opcode, [23:16]=Rd/Rs2, [15:8]=Rs1, [7:0]=offset8 (signed)
// Used for memory load/store operations (EXEC-006)
[[nodiscard]] constexpr DecodedTypeM decode_type_m(std::uint32_t instr) noexcept {
    return DecodedTypeM{
        .opcode = static_cast<std::uint8_t>(
            (instr >> instr_bits::OPCODE_SHIFT) & instr_bits::OPCODE_MASK),
        .rd_rs2 = static_cast<std::uint8_t>(
            (instr >> instr_bits::RD_SHIFT) & instr_bits::REG_MASK),
        .rs1 = static_cast<std::uint8_t>(
            (instr >> instr_bits::RS1_SHIFT) & instr_bits::REG_MASK),
        .offset8 = static_cast<std::int8_t>(instr & 0xFFU)
    };
}

// --- Encoding Functions ---

// Type A Encoder: register-register
[[nodiscard]] constexpr std::uint32_t encode_type_a(
    std::uint8_t opcode,
    std::uint8_t rd,
    std::uint8_t rs1,
    std::uint8_t rs2) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rd) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(rs1) << instr_bits::RS1_SHIFT) |
           (static_cast<std::uint32_t>(rs2) << instr_bits::RS2_SHIFT);
}

// Type B Encoder: register-immediate
[[nodiscard]] constexpr std::uint32_t encode_type_b(
    std::uint8_t opcode,
    std::uint8_t rd,
    std::uint16_t imm16) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rd) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(imm16) << instr_bits::IMM16_SHIFT);
}

// Type C Encoder: offset/jump (accepts signed 24-bit offset)
// Note: Values outside [-8388608, 8388607] are truncated to 24 bits.
// Callers should validate offset range if needed.
[[nodiscard]] constexpr std::uint32_t encode_type_c(
    std::uint8_t opcode,
    std::int32_t offset24) noexcept {
    // Mask to 24 bits (handles both positive and negative values correctly)
    std::uint32_t masked_offset =
        static_cast<std::uint32_t>(offset24) & instr_bits::OFFSET24_MASK;

    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (masked_offset << instr_bits::OFFSET24_SHIFT);
}

// Type S Encoder: shift-immediate (SHLI, SHRI, SARI)
// Layout: [31:24]=opcode [23:16]=Rd [15:8]=Rs1 [5:0]=shamt6
// Note: shamt6 is masked to 6 bits (0-63)
[[nodiscard]] constexpr std::uint32_t encode_type_s(
    std::uint8_t opcode,
    std::uint8_t rd,
    std::uint8_t rs1,
    std::uint8_t shamt6) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rd) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(rs1) << instr_bits::RS1_SHIFT) |
           (static_cast<std::uint32_t>(shamt6) & instr_bits::SHAMT6_MASK);
}

// Type D Encoder: JZ/JNZ (EXEC-005)
// Layout: [31:24]=opcode [23:16]=Rs [15:0]=offset16 (signed)
[[nodiscard]] constexpr std::uint32_t encode_type_d(
    std::uint8_t opcode,
    std::uint8_t rs,
    std::int16_t offset16) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rs) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(offset16)) & instr_bits::IMM16_MASK);
}

// Type M Encoder: memory load/store (EXEC-006)
// Layout: [31:24]=opcode [23:16]=Rd/Rs2 [15:8]=Rs1 [7:0]=offset8
[[nodiscard]] constexpr std::uint32_t encode_type_m(
    std::uint8_t opcode,
    std::uint8_t rd_rs2,
    std::uint8_t rs1,
    std::int8_t offset8) noexcept {
    return (static_cast<std::uint32_t>(opcode) << instr_bits::OPCODE_SHIFT) |
           (static_cast<std::uint32_t>(rd_rs2) << instr_bits::RD_SHIFT) |
           (static_cast<std::uint32_t>(rs1) << instr_bits::RS1_SHIFT) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(offset8)));
}

// --- Opcode Classification Functions ---

// Classify opcode into its category
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

// Category predicates
[[nodiscard]] constexpr bool is_arithmetic_opcode(std::uint8_t opcode) noexcept {
    return opcode <= opcode_range::ARITHMETIC_END;
}

[[nodiscard]] constexpr bool is_bitwise_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::BITWISE_START &&
           opcode <= opcode_range::BITWISE_END;
}

[[nodiscard]] constexpr bool is_comparison_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::COMPARISON_START &&
           opcode <= opcode_range::COMPARISON_END;
}

[[nodiscard]] constexpr bool is_control_flow_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::CONTROL_FLOW_START &&
           opcode <= opcode_range::CONTROL_FLOW_END;
}

[[nodiscard]] constexpr bool is_memory_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::MEMORY_START &&
           opcode <= opcode_range::MEMORY_END;
}

[[nodiscard]] constexpr bool is_data_move_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::DATA_MOVE_START &&
           opcode <= opcode_range::DATA_MOVE_END;
}

[[nodiscard]] constexpr bool is_state_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::STATE_START &&
           opcode <= opcode_range::STATE_END;
}

[[nodiscard]] constexpr bool is_crypto_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::CRYPTO_START &&
           opcode <= opcode_range::CRYPTO_END;
}

[[nodiscard]] constexpr bool is_para_dot_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::PARA_DOT_START &&
           opcode <= opcode_range::PARA_DOT_END;
}

[[nodiscard]] constexpr bool is_system_opcode(std::uint8_t opcode) noexcept {
    return opcode >= opcode_range::SYSTEM_START;
}

[[nodiscard]] constexpr bool is_reserved_opcode(std::uint8_t opcode) noexcept {
    return (opcode >= opcode_range::RESERVED_90_START &&
            opcode <= opcode_range::RESERVED_90_END) ||
           (opcode >= opcode_range::RESERVED_D0_START &&
            opcode <= opcode_range::RESERVED_EF_END);
}

} // namespace dotvm::core
