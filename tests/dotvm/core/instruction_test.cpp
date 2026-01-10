#include <gtest/gtest.h>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/register_file.hpp>

#include <vector>

using namespace dotvm::core;

class InstructionEncodingTest : public ::testing::Test {};

// Size guarantees
TEST_F(InstructionEncodingTest, DecodedTypeASizeIs4Bytes) {
    EXPECT_EQ(sizeof(DecodedTypeA), 4);
}

TEST_F(InstructionEncodingTest, DecodedTypeBSizeIs4Bytes) {
    EXPECT_EQ(sizeof(DecodedTypeB), 4);
}

// Type A round-trip tests
TEST_F(InstructionEncodingTest, TypeARoundTrip) {
    std::vector<std::tuple<std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t>> test_cases = {
        {0x00, 0, 0, 0},             // All zeros
        {0x1F, 255, 255, 255},       // Max values
        {0x10, 1, 2, 3},             // Simple case
        {0x05, 0, 1, 0},             // R0 as dest
        {0x0A, 32, 64, 128},         // Various registers
        {0xFF, 100, 100, 100},       // System opcode
    };

    for (const auto& [opcode, rd, rs1, rs2] : test_cases) {
        std::uint32_t encoded = encode_type_a(opcode, rd, rs1, rs2);
        DecodedTypeA decoded = decode_type_a(encoded);

        EXPECT_EQ(decoded.opcode, opcode) << "Opcode mismatch";
        EXPECT_EQ(decoded.rd, rd) << "Rd mismatch";
        EXPECT_EQ(decoded.rs1, rs1) << "Rs1 mismatch";
        EXPECT_EQ(decoded.rs2, rs2) << "Rs2 mismatch";
    }
}

// Type B round-trip tests
TEST_F(InstructionEncodingTest, TypeBRoundTrip) {
    std::vector<std::tuple<std::uint8_t, std::uint8_t, std::uint16_t>> test_cases = {
        {0x80, 0, 0},                // All zeros
        {0x8F, 255, 0xFFFF},         // Max values
        {0x85, 10, 0x1234},          // Simple case
        {0x60, 1, 0x8000},           // Memory opcode with sign bit
        {0x00, 5, 1},                // Min immediate
    };

    for (const auto& [opcode, rd, imm16] : test_cases) {
        std::uint32_t encoded = encode_type_b(opcode, rd, imm16);
        DecodedTypeB decoded = decode_type_b(encoded);

        EXPECT_EQ(decoded.opcode, opcode) << "Opcode mismatch";
        EXPECT_EQ(decoded.rd, rd) << "Rd mismatch";
        EXPECT_EQ(decoded.imm16, imm16) << "Imm16 mismatch";
    }
}

// Type C round-trip tests (positive offsets)
TEST_F(InstructionEncodingTest, TypeCRoundTripPositive) {
    std::vector<std::pair<std::uint8_t, std::int32_t>> test_cases = {
        {0x40, 0},                   // Zero offset
        {0x45, 1},                   // Min positive
        {0x4F, 0x7FFFFF},            // Max positive 24-bit (8388607)
        {0x50, 100},                 // Simple case
        {0x48, 0x123456},            // Large positive
    };

    for (const auto& [opcode, offset] : test_cases) {
        std::uint32_t encoded = encode_type_c(opcode, offset);
        DecodedTypeC decoded = decode_type_c(encoded);

        EXPECT_EQ(decoded.opcode, opcode) << "Opcode mismatch for offset " << offset;
        EXPECT_EQ(decoded.offset24, offset) << "Offset mismatch";
    }
}

// Type C round-trip tests (negative offsets)
TEST_F(InstructionEncodingTest, TypeCRoundTripNegative) {
    std::vector<std::pair<std::uint8_t, std::int32_t>> test_cases = {
        {0x40, -1},                  // -1
        {0x45, -100},                // Simple negative
        {0x4F, -0x800000},           // Min negative 24-bit (-8388608)
        {0x50, -0x7FFFFF},           // Near min negative
        {0x48, -256},                // -256
    };

    for (const auto& [opcode, offset] : test_cases) {
        std::uint32_t encoded = encode_type_c(opcode, offset);
        DecodedTypeC decoded = decode_type_c(encoded);

        EXPECT_EQ(decoded.opcode, opcode) << "Opcode mismatch for offset " << offset;
        EXPECT_EQ(decoded.offset24, offset) << "Offset mismatch for input " << offset;
    }
}

// Opcode extraction
TEST_F(InstructionEncodingTest, ExtractOpcode) {
    EXPECT_EQ(extract_opcode(0xAB'CD'EF'12), 0xAB);
    EXPECT_EQ(extract_opcode(0x00'00'00'00), 0x00);
    EXPECT_EQ(extract_opcode(0xFF'FF'FF'FF), 0xFF);
    EXPECT_EQ(extract_opcode(0x40'12'34'56), 0x40);
}

// Opcode category classification
TEST_F(InstructionEncodingTest, ClassifyArithmetic) {
    for (std::uint8_t op = 0x00; op <= 0x1F; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::Arithmetic)
            << "Failed for opcode 0x" << std::hex << static_cast<int>(op);
        EXPECT_TRUE(is_arithmetic_opcode(op));
        EXPECT_FALSE(is_reserved_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyBitwise) {
    for (std::uint8_t op = 0x20; op <= 0x2F; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::Bitwise);
        EXPECT_TRUE(is_bitwise_opcode(op));
        EXPECT_FALSE(is_arithmetic_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyComparison) {
    for (std::uint8_t op = 0x30; op <= 0x3F; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::Comparison);
        EXPECT_TRUE(is_comparison_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyControlFlow) {
    for (std::uint8_t op = 0x40; op <= 0x5F; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::ControlFlow);
        EXPECT_TRUE(is_control_flow_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyMemory) {
    for (std::uint8_t op = 0x60; op <= 0x7F; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::Memory);
        EXPECT_TRUE(is_memory_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyDataMove) {
    for (std::uint8_t op = 0x80; op <= 0x8F; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::DataMove);
        EXPECT_TRUE(is_data_move_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyReserved90) {
    for (std::uint8_t op = 0x90; op <= 0x9F; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::Reserved90);
        EXPECT_TRUE(is_reserved_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyState) {
    for (std::uint8_t op = 0xA0; op <= 0xAF; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::State);
        EXPECT_TRUE(is_state_opcode(op));
        EXPECT_FALSE(is_reserved_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyCrypto) {
    for (std::uint8_t op = 0xB0; op <= 0xBF; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::Crypto);
        EXPECT_TRUE(is_crypto_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyParaDot) {
    for (std::uint8_t op = 0xC0; op <= 0xCF; ++op) {
        EXPECT_EQ(classify_opcode(op), OpcodeCategory::ParaDot);
        EXPECT_TRUE(is_para_dot_opcode(op));
    }
}

TEST_F(InstructionEncodingTest, ClassifyReservedD0) {
    for (int op = 0xD0; op <= 0xEF; ++op) {
        EXPECT_EQ(classify_opcode(static_cast<std::uint8_t>(op)), OpcodeCategory::ReservedD0);
        EXPECT_TRUE(is_reserved_opcode(static_cast<std::uint8_t>(op)));
    }
}

TEST_F(InstructionEncodingTest, ClassifySystem) {
    for (int op = 0xF0; op <= 0xFF; ++op) {
        EXPECT_EQ(classify_opcode(static_cast<std::uint8_t>(op)), OpcodeCategory::System);
        EXPECT_TRUE(is_system_opcode(static_cast<std::uint8_t>(op)));
        EXPECT_FALSE(is_reserved_opcode(static_cast<std::uint8_t>(op)));
    }
}

// Category boundaries
TEST_F(InstructionEncodingTest, CategoryBoundaries) {
    // Arithmetic -> Bitwise
    EXPECT_EQ(classify_opcode(0x1F), OpcodeCategory::Arithmetic);
    EXPECT_EQ(classify_opcode(0x20), OpcodeCategory::Bitwise);

    // Bitwise -> Comparison
    EXPECT_EQ(classify_opcode(0x2F), OpcodeCategory::Bitwise);
    EXPECT_EQ(classify_opcode(0x30), OpcodeCategory::Comparison);

    // Comparison -> ControlFlow
    EXPECT_EQ(classify_opcode(0x3F), OpcodeCategory::Comparison);
    EXPECT_EQ(classify_opcode(0x40), OpcodeCategory::ControlFlow);

    // ControlFlow -> Memory
    EXPECT_EQ(classify_opcode(0x5F), OpcodeCategory::ControlFlow);
    EXPECT_EQ(classify_opcode(0x60), OpcodeCategory::Memory);

    // Memory -> DataMove
    EXPECT_EQ(classify_opcode(0x7F), OpcodeCategory::Memory);
    EXPECT_EQ(classify_opcode(0x80), OpcodeCategory::DataMove);

    // DataMove -> Reserved90
    EXPECT_EQ(classify_opcode(0x8F), OpcodeCategory::DataMove);
    EXPECT_EQ(classify_opcode(0x90), OpcodeCategory::Reserved90);

    // Reserved90 -> State
    EXPECT_EQ(classify_opcode(0x9F), OpcodeCategory::Reserved90);
    EXPECT_EQ(classify_opcode(0xA0), OpcodeCategory::State);

    // State -> Crypto
    EXPECT_EQ(classify_opcode(0xAF), OpcodeCategory::State);
    EXPECT_EQ(classify_opcode(0xB0), OpcodeCategory::Crypto);

    // Crypto -> ParaDot
    EXPECT_EQ(classify_opcode(0xBF), OpcodeCategory::Crypto);
    EXPECT_EQ(classify_opcode(0xC0), OpcodeCategory::ParaDot);

    // ParaDot -> ReservedD0
    EXPECT_EQ(classify_opcode(0xCF), OpcodeCategory::ParaDot);
    EXPECT_EQ(classify_opcode(0xD0), OpcodeCategory::ReservedD0);

    // ReservedD0 -> System
    EXPECT_EQ(classify_opcode(0xEF), OpcodeCategory::ReservedD0);
    EXPECT_EQ(classify_opcode(0xF0), OpcodeCategory::System);
}

// Bit layout verification - Type A
TEST_F(InstructionEncodingTest, BitLayoutTypeA) {
    std::uint32_t instr = encode_type_a(0xAB, 0xCD, 0xEF, 0x12);

    EXPECT_EQ((instr >> 24) & 0xFF, 0xABu) << "Opcode at [31:24]";
    EXPECT_EQ((instr >> 16) & 0xFF, 0xCDu) << "Rd at [23:16]";
    EXPECT_EQ((instr >> 8) & 0xFF, 0xEFu)  << "Rs1 at [15:8]";
    EXPECT_EQ(instr & 0xFF, 0x12u)         << "Rs2 at [7:0]";
}

// Bit layout verification - Type B
TEST_F(InstructionEncodingTest, BitLayoutTypeB) {
    std::uint32_t instr = encode_type_b(0xAB, 0xCD, 0x1234);

    EXPECT_EQ((instr >> 24) & 0xFF, 0xABu)   << "Opcode at [31:24]";
    EXPECT_EQ((instr >> 16) & 0xFF, 0xCDu)   << "Rd at [23:16]";
    EXPECT_EQ(instr & 0xFFFF, 0x1234u)       << "imm16 at [15:0]";
}

// Bit layout verification - Type C
TEST_F(InstructionEncodingTest, BitLayoutTypeC) {
    std::uint32_t instr = encode_type_c(0xAB, 0x123456);

    EXPECT_EQ((instr >> 24) & 0xFF, 0xABu)    << "Opcode at [31:24]";
    EXPECT_EQ(instr & 0xFFFFFF, 0x123456u)    << "offset24 at [23:0]";
}

// Constexpr verification
TEST_F(InstructionEncodingTest, ConstexprOperations) {
    // Type A
    constexpr auto encoded_a = encode_type_a(0x10, 1, 2, 3);
    constexpr auto decoded_a = decode_type_a(encoded_a);

    static_assert(decoded_a.opcode == 0x10);
    static_assert(decoded_a.rd == 1);
    static_assert(decoded_a.rs1 == 2);
    static_assert(decoded_a.rs2 == 3);

    // Type B
    constexpr auto encoded_b = encode_type_b(0x80, 5, 0x1234);
    constexpr auto decoded_b = decode_type_b(encoded_b);

    static_assert(decoded_b.opcode == 0x80);
    static_assert(decoded_b.rd == 5);
    static_assert(decoded_b.imm16 == 0x1234);

    // Type C with negative offset
    constexpr auto encoded_c = encode_type_c(0x40, -100);
    constexpr auto decoded_c = decode_type_c(encoded_c);

    static_assert(decoded_c.opcode == 0x40);
    static_assert(decoded_c.offset24 == -100);

    // Opcode classification
    constexpr auto category = classify_opcode(0x15);
    static_assert(category == OpcodeCategory::Arithmetic);

    constexpr bool is_arith = is_arithmetic_opcode(0x10);
    static_assert(is_arith == true);

    // Runtime verification
    EXPECT_EQ(decoded_a.opcode, 0x10);
    EXPECT_EQ(decoded_b.imm16, 0x1234);
    EXPECT_EQ(decoded_c.offset24, -100);
}

// Struct equality operators
TEST_F(InstructionEncodingTest, DecodedTypeAEquality) {
    DecodedTypeA a1{.opcode = 0x10, .rd = 1, .rs1 = 2, .rs2 = 3};
    DecodedTypeA a2{.opcode = 0x10, .rd = 1, .rs1 = 2, .rs2 = 3};
    DecodedTypeA a3{.opcode = 0x10, .rd = 1, .rs1 = 2, .rs2 = 4};

    EXPECT_EQ(a1, a2);
    EXPECT_NE(a1, a3);
}

TEST_F(InstructionEncodingTest, DecodedTypeBEquality) {
    DecodedTypeB b1{.opcode = 0x80, .rd = 5, .imm16 = 0x1234};
    DecodedTypeB b2{.opcode = 0x80, .rd = 5, .imm16 = 0x1234};
    DecodedTypeB b3{.opcode = 0x80, .rd = 5, .imm16 = 0x1235};

    EXPECT_EQ(b1, b2);
    EXPECT_NE(b1, b3);
}

TEST_F(InstructionEncodingTest, DecodedTypeCEquality) {
    DecodedTypeC c1{.opcode = 0x40, .offset24 = 100};
    DecodedTypeC c2{.opcode = 0x40, .offset24 = 100};
    DecodedTypeC c3{.opcode = 0x40, .offset24 = -100};

    EXPECT_EQ(c1, c2);
    EXPECT_NE(c1, c3);
}

// Integration with RegisterFile
TEST_F(InstructionEncodingTest, RegisterFileIntegration) {
    RegisterFile rf;

    // Encode an instruction: ADD R5, R10, R15
    auto decoded = decode_type_a(encode_type_a(0x00, 5, 10, 15));

    // Setup source registers
    rf.write(decoded.rs1, Value::from_int(100));
    rf.write(decoded.rs2, Value::from_int(200));

    // Verify we can read from decoded register indices
    EXPECT_EQ(rf.read(decoded.rs1).as_integer(), 100);
    EXPECT_EQ(rf.read(decoded.rs2).as_integer(), 200);

    // Write to decoded destination
    rf.write(decoded.rd, Value::from_int(300));
    EXPECT_EQ(rf.read(decoded.rd).as_integer(), 300);

    // Verify R0 special handling still works with decoded indices
    auto r0_decoded = decode_type_a(encode_type_a(0x00, 0, 10, 15));
    rf.write(r0_decoded.rd, Value::from_int(999));
    EXPECT_EQ(rf.read(r0_decoded.rd).as_float(), 0.0);  // R0 always returns 0
}

// Edge case: All zeros instruction
TEST_F(InstructionEncodingTest, AllZerosInstruction) {
    std::uint32_t zero_instr = 0x00000000;

    DecodedTypeA a = decode_type_a(zero_instr);
    EXPECT_EQ(a.opcode, 0);
    EXPECT_EQ(a.rd, 0);
    EXPECT_EQ(a.rs1, 0);
    EXPECT_EQ(a.rs2, 0);

    DecodedTypeB b = decode_type_b(zero_instr);
    EXPECT_EQ(b.opcode, 0);
    EXPECT_EQ(b.rd, 0);
    EXPECT_EQ(b.imm16, 0);

    DecodedTypeC c = decode_type_c(zero_instr);
    EXPECT_EQ(c.opcode, 0);
    EXPECT_EQ(c.offset24, 0);
}

// Edge case: All ones instruction
TEST_F(InstructionEncodingTest, AllOnesInstruction) {
    std::uint32_t ones_instr = 0xFFFFFFFF;

    DecodedTypeA a = decode_type_a(ones_instr);
    EXPECT_EQ(a.opcode, 0xFF);
    EXPECT_EQ(a.rd, 0xFF);
    EXPECT_EQ(a.rs1, 0xFF);
    EXPECT_EQ(a.rs2, 0xFF);

    DecodedTypeB b = decode_type_b(ones_instr);
    EXPECT_EQ(b.opcode, 0xFF);
    EXPECT_EQ(b.rd, 0xFF);
    EXPECT_EQ(b.imm16, 0xFFFF);

    DecodedTypeC c = decode_type_c(ones_instr);
    EXPECT_EQ(c.opcode, 0xFF);
    EXPECT_EQ(c.offset24, -1);  // 0xFFFFFF sign-extended is -1
}

// Type C offset boundary values
TEST_F(InstructionEncodingTest, TypeCOffsetBoundaries) {
    // Max positive: 0x7FFFFF = 8388607
    {
        std::uint32_t encoded = encode_type_c(0x40, 0x7FFFFF);
        DecodedTypeC decoded = decode_type_c(encoded);
        EXPECT_EQ(decoded.offset24, 0x7FFFFF);
    }

    // Min negative: -0x800000 = -8388608
    {
        std::uint32_t encoded = encode_type_c(0x40, -0x800000);
        DecodedTypeC decoded = decode_type_c(encoded);
        EXPECT_EQ(decoded.offset24, -0x800000);
    }

    // Boundary: -1
    {
        std::uint32_t encoded = encode_type_c(0x40, -1);
        DecodedTypeC decoded = decode_type_c(encoded);
        EXPECT_EQ(decoded.offset24, -1);
    }
}
