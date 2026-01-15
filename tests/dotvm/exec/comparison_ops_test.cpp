/// @file comparison_ops_test.cpp
/// @brief Unit tests for EXEC-009 Comparison Operations
///
/// Tests for TEST and CMPI_* opcodes added in EXEC-009.
/// Existing comparison opcodes (EQ, NE, LT, LE, GT, GE, LTU, LEU, GTU, GEU)
/// are tested in execution_engine_test.cpp.

#include <gtest/gtest.h>

#include <dotvm/exec/execution_engine.hpp>
#include <dotvm/exec/execution_context.hpp>
#include <dotvm/exec/dispatch_macros.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/value.hpp>
#include <dotvm/core/alu.hpp>
#include <dotvm/core/opcode.hpp>

#include <vector>
#include <cstdint>
#include <limits>

using namespace dotvm;

// Explicitly use exec namespace items but NOT its opcode sub-namespace
using dotvm::exec::ExecutionEngine;
using dotvm::exec::ExecutionContext;
using dotvm::exec::ExecResult;

// Explicitly use core namespace items but NOT its opcode sub-namespace
using dotvm::core::VmContext;
using dotvm::core::Value;
using dotvm::core::ALU;
using dotvm::core::Architecture;
using dotvm::core::encode_type_a;
using dotvm::core::encode_type_b;
using dotvm::core::encode_type_c;
using dotvm::core::encode_type_d;

// Use exec::opcode for execution engine tests
namespace opcode = dotvm::exec::opcode;

// ============================================================================
// Test Fixture
// ============================================================================

class ComparisonOpsTest : public ::testing::Test {
protected:
    VmContext ctx_;
    ExecutionEngine engine_{ctx_};

    // Helper to create Type A instruction
    static std::uint32_t make_type_a(std::uint8_t op, std::uint8_t rd,
                                      std::uint8_t rs1, std::uint8_t rs2) {
        return encode_type_a(op, rd, rs1, rs2);
    }

    // Helper to create Type B instruction
    static std::uint32_t make_type_b(std::uint8_t op, std::uint8_t rd,
                                      std::uint16_t imm) {
        return encode_type_b(op, rd, imm);
    }

    // Helper to create Type C instruction (HALT)
    static std::uint32_t make_halt() {
        return encode_type_c(opcode::HALT, 0);
    }

    // Helper to run code
    ExecResult run(const std::vector<std::uint32_t>& code) {
        return engine_.execute(code.data(), code.size(), 0, {});
    }
};

// ============================================================================
// ALU TEST Operation Tests (Unit Tests)
// ============================================================================

class ALUTestOpTest : public ::testing::Test {
protected:
    ALU alu32_{Architecture::Arch32};
    ALU alu64_{Architecture::Arch64};
};

TEST_F(ALUTestOpTest, CommonBits_ReturnsOne) {
    auto a = Value::from_int(0b1100);
    auto b = Value::from_int(0b1010);

    // 0b1100 & 0b1010 = 0b1000 != 0 -> 1
    EXPECT_EQ(alu64_.test(a, b).as_integer(), 1);
    EXPECT_EQ(alu32_.test(a, b).as_integer(), 1);
}

TEST_F(ALUTestOpTest, NoCommonBits_ReturnsZero) {
    auto a = Value::from_int(0b1100);
    auto b = Value::from_int(0b0011);

    // 0b1100 & 0b0011 = 0b0000 == 0 -> 0
    EXPECT_EQ(alu64_.test(a, b).as_integer(), 0);
    EXPECT_EQ(alu32_.test(a, b).as_integer(), 0);
}

TEST_F(ALUTestOpTest, ZeroOperand_ReturnsZero) {
    auto a = Value::from_int(0);
    auto b = Value::from_int(0xFFFFFFFF);

    // 0 & anything = 0 -> 0
    EXPECT_EQ(alu64_.test(a, b).as_integer(), 0);
    EXPECT_EQ(alu32_.test(a, b).as_integer(), 0);
}

TEST_F(ALUTestOpTest, AllOnes_ReturnsOne) {
    auto a = Value::from_int(-1);  // All bits set
    auto b = Value::from_int(1);

    // -1 & 1 = 1 != 0 -> 1
    EXPECT_EQ(alu64_.test(a, b).as_integer(), 1);
    EXPECT_EQ(alu32_.test(a, b).as_integer(), 1);
}

TEST_F(ALUTestOpTest, HighBitSet_Arch32_ReturnsOne) {
    auto a = Value::from_int(static_cast<std::int64_t>(0x80000000));  // Bit 31 set
    auto b = Value::from_int(static_cast<std::int64_t>(0x80000000));

    // High bit set on both -> result is non-zero
    EXPECT_EQ(alu32_.test(a, b).as_integer(), 1);
}

// ============================================================================
// Execution Engine TEST Instruction Tests
// ============================================================================

TEST_F(ComparisonOpsTest, Test_BothBitsSet_ReturnsOne) {
    ctx_.registers().write(1, Value::from_int(0b1100));
    ctx_.registers().write(2, Value::from_int(0b1010));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::TEST, 3, 1, 2),  // TEST R3, R1, R2
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 1);
}

TEST_F(ComparisonOpsTest, Test_NoBitsCommon_ReturnsZero) {
    ctx_.registers().write(1, Value::from_int(0b1100));
    ctx_.registers().write(2, Value::from_int(0b0011));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::TEST, 3, 1, 2),  // TEST R3, R1, R2
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 0);
}

TEST_F(ComparisonOpsTest, Test_ZeroRegister_Destination) {
    // Writing to R0 should be no-op (R0 always reads 0)
    ctx_.registers().write(1, Value::from_int(0xF));
    ctx_.registers().write(2, Value::from_int(0xF));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::TEST, 0, 1, 2),  // TEST R0, R1, R2
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(0).as_integer(), 0);  // R0 always 0
}

TEST_F(ComparisonOpsTest, Test_SingleBitCheck) {
    // Test if bit 2 is set in R1
    ctx_.registers().write(1, Value::from_int(0b0100));  // Bit 2 set
    ctx_.registers().write(2, Value::from_int(0b0100));  // Mask for bit 2

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::TEST, 3, 1, 2),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 1);
}

// ============================================================================
// CMPI_EQ Tests
// ============================================================================

TEST_F(ComparisonOpsTest, CmpiEq_Equal_ReturnsOne) {
    ctx_.registers().write(1, Value::from_int(42));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_EQ, 1, 42),  // CMPI_EQ R1, 42
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

TEST_F(ComparisonOpsTest, CmpiEq_NotEqual_ReturnsZero) {
    ctx_.registers().write(1, Value::from_int(42));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_EQ, 1, 43),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 0);
}

TEST_F(ComparisonOpsTest, CmpiEq_Zero_Equal) {
    ctx_.registers().write(1, Value::from_int(0));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_EQ, 1, 0),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

TEST_F(ComparisonOpsTest, CmpiEq_NegativeNumber) {
    ctx_.registers().write(1, Value::from_int(-1));

    std::vector<std::uint32_t> code = {
        // -1 as 16-bit immediate is 0xFFFF which sign-extends to -1
        make_type_b(opcode::CMPI_EQ, 1, 0xFFFF),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

// ============================================================================
// CMPI_NE Tests
// ============================================================================

TEST_F(ComparisonOpsTest, CmpiNe_NotEqual_ReturnsOne) {
    ctx_.registers().write(1, Value::from_int(42));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_NE, 1, 43),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

TEST_F(ComparisonOpsTest, CmpiNe_Equal_ReturnsZero) {
    ctx_.registers().write(1, Value::from_int(42));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_NE, 1, 42),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 0);
}

// ============================================================================
// CMPI_LT Tests
// ============================================================================

TEST_F(ComparisonOpsTest, CmpiLt_LessThan_ReturnsOne) {
    ctx_.registers().write(1, Value::from_int(10));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_LT, 1, 20),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

TEST_F(ComparisonOpsTest, CmpiLt_GreaterThan_ReturnsZero) {
    ctx_.registers().write(1, Value::from_int(30));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_LT, 1, 20),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 0);
}

TEST_F(ComparisonOpsTest, CmpiLt_Equal_ReturnsZero) {
    ctx_.registers().write(1, Value::from_int(20));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_LT, 1, 20),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 0);
}

TEST_F(ComparisonOpsTest, CmpiLt_SignedComparison_NegativeValue) {
    ctx_.registers().write(1, Value::from_int(-10));

    std::vector<std::uint32_t> code = {
        // -10 < 5 should be true (signed comparison)
        make_type_b(opcode::CMPI_LT, 1, 5),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

TEST_F(ComparisonOpsTest, CmpiLt_SignedComparison_NegativeImmediate) {
    ctx_.registers().write(1, Value::from_int(-20));

    std::vector<std::uint32_t> code = {
        // -20 < -10 should be true
        // -10 as imm16 = 0xFFF6
        make_type_b(opcode::CMPI_LT, 1, static_cast<std::uint16_t>(-10)),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

// ============================================================================
// CMPI_GE Tests
// ============================================================================

TEST_F(ComparisonOpsTest, CmpiGe_GreaterThan_ReturnsOne) {
    ctx_.registers().write(1, Value::from_int(30));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_GE, 1, 20),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

TEST_F(ComparisonOpsTest, CmpiGe_Equal_ReturnsOne) {
    ctx_.registers().write(1, Value::from_int(20));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_GE, 1, 20),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

TEST_F(ComparisonOpsTest, CmpiGe_LessThan_ReturnsZero) {
    ctx_.registers().write(1, Value::from_int(10));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_GE, 1, 20),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 0);
}

TEST_F(ComparisonOpsTest, CmpiGe_SignedComparison_NegativeValue) {
    ctx_.registers().write(1, Value::from_int(-5));

    std::vector<std::uint32_t> code = {
        // -5 >= -10 should be true
        make_type_b(opcode::CMPI_GE, 1, static_cast<std::uint16_t>(-10)),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

// ============================================================================
// Chained Comparison Tests
// ============================================================================

TEST_F(ComparisonOpsTest, ChainedComparison_RangeCheck_InRange) {
    // Check if 15 is in range [10, 20)
    ctx_.registers().write(1, Value::from_int(15));  // value
    ctx_.registers().write(2, Value::from_int(10));  // lower
    ctx_.registers().write(3, Value::from_int(20));  // upper

    std::vector<std::uint32_t> code = {
        // R4 = (R1 >= R2), i.e., value >= lower
        make_type_a(opcode::GE, 4, 1, 2),
        // R5 = (R1 < R3), i.e., value < upper
        make_type_a(opcode::LT, 5, 1, 3),
        // R6 = R4 & R5 (both conditions must be true)
        make_type_a(opcode::AND, 6, 4, 5),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(4).as_integer(), 1);  // 15 >= 10
    EXPECT_EQ(ctx_.registers().read(5).as_integer(), 1);  // 15 < 20
    EXPECT_EQ(ctx_.registers().read(6).as_integer(), 1);  // In range
}

TEST_F(ComparisonOpsTest, ChainedComparison_RangeCheck_OutOfRange) {
    // Check if 25 is in range [10, 20)
    ctx_.registers().write(1, Value::from_int(25));  // value
    ctx_.registers().write(2, Value::from_int(10));  // lower
    ctx_.registers().write(3, Value::from_int(20));  // upper

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::GE, 4, 1, 2),  // R4 = 25 >= 10 = 1
        make_type_a(opcode::LT, 5, 1, 3),  // R5 = 25 < 20 = 0
        make_type_a(opcode::AND, 6, 4, 5), // R6 = 1 & 0 = 0
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(4).as_integer(), 1);  // 25 >= 10
    EXPECT_EQ(ctx_.registers().read(5).as_integer(), 0);  // 25 >= 20
    EXPECT_EQ(ctx_.registers().read(6).as_integer(), 0);  // Out of range
}

// ============================================================================
// Branch with Comparison Result Tests
// ============================================================================

TEST_F(ComparisonOpsTest, BranchOnComparisonResult_Taken) {
    ctx_.registers().write(1, Value::from_int(5));
    ctx_.registers().write(2, Value::from_int(10));

    std::vector<std::uint32_t> code = {
        // R3 = (R1 < R2) = 1
        make_type_a(opcode::LT, 3, 1, 2),
        // JNZ R3, +2 (jump over next instruction if R3 != 0)
        encode_type_d(opcode::JNZ, 3, 2),
        // This should be skipped
        make_type_b(opcode::MOVI, 4, 999),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 1);
    EXPECT_EQ(ctx_.registers().read(4).as_integer(), 0);  // Should be skipped
}

TEST_F(ComparisonOpsTest, BranchOnComparisonResult_NotTaken) {
    ctx_.registers().write(1, Value::from_int(15));
    ctx_.registers().write(2, Value::from_int(10));

    std::vector<std::uint32_t> code = {
        // R3 = (R1 < R2) = 0 (15 is not less than 10)
        make_type_a(opcode::LT, 3, 1, 2),
        // JNZ R3, +2 (jump is NOT taken because R3 == 0)
        encode_type_d(opcode::JNZ, 3, 2),
        // This should execute
        make_type_b(opcode::MOVI, 4, 999),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 0);
    EXPECT_EQ(ctx_.registers().read(4).as_integer(), 999);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(ComparisonOpsTest, CmpiLt_MaxPositiveImmediate) {
    // Max positive 16-bit signed value is 32767
    ctx_.registers().write(1, Value::from_int(32766));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::CMPI_LT, 1, 32767),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

TEST_F(ComparisonOpsTest, CmpiLt_MinNegativeImmediate) {
    // Min negative 16-bit signed value is -32768
    ctx_.registers().write(1, Value::from_int(-32769));

    std::vector<std::uint32_t> code = {
        // -32768 as uint16 = 0x8000
        make_type_b(opcode::CMPI_LT, 1, 0x8000),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);  // -32769 < -32768
}

TEST_F(ComparisonOpsTest, Test_LargeBitPattern) {
    ctx_.registers().write(1, Value::from_int(0x0F0F0F0F));
    ctx_.registers().write(2, Value::from_int(0xF0F0F0F0));

    std::vector<std::uint32_t> code = {
        // 0x0F0F0F0F & 0xF0F0F0F0 = 0 -> result is 0
        make_type_a(opcode::TEST, 3, 1, 2),
        make_halt()
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 0);
}

// ============================================================================
// Opcode Classification Tests
// ============================================================================

TEST(OpcodeClassificationTest, IsComparisonOp) {
    // Use core::opcode namespace for comparison helper functions
    namespace cop = core::opcode;

    // Type A comparison ops
    EXPECT_TRUE(core::is_comparison_op(cop::EQ));
    EXPECT_TRUE(core::is_comparison_op(cop::NE));
    EXPECT_TRUE(core::is_comparison_op(cop::LT));
    EXPECT_TRUE(core::is_comparison_op(cop::LE));
    EXPECT_TRUE(core::is_comparison_op(cop::GT));
    EXPECT_TRUE(core::is_comparison_op(cop::GE));
    EXPECT_TRUE(core::is_comparison_op(cop::LTU));
    EXPECT_TRUE(core::is_comparison_op(cop::LEU));
    EXPECT_TRUE(core::is_comparison_op(cop::GTU));
    EXPECT_TRUE(core::is_comparison_op(cop::GEU));
    EXPECT_TRUE(core::is_comparison_op(cop::TEST));

    // Type B comparison ops
    EXPECT_TRUE(core::is_comparison_op(cop::CMPI_EQ));
    EXPECT_TRUE(core::is_comparison_op(cop::CMPI_NE));
    EXPECT_TRUE(core::is_comparison_op(cop::CMPI_LT));
    EXPECT_TRUE(core::is_comparison_op(cop::CMPI_GE));

    // Non-comparison ops
    EXPECT_FALSE(core::is_comparison_op(cop::ADD));
    EXPECT_FALSE(core::is_comparison_op(cop::AND));
    EXPECT_FALSE(core::is_comparison_op(cop::JMP));
    EXPECT_FALSE(core::is_comparison_op(cop::COMPARISON_RESERVED));
}

TEST(OpcodeClassificationTest, IsTypeAComparison) {
    namespace cop = core::opcode;
    EXPECT_TRUE(core::is_type_a_comparison(cop::EQ));
    EXPECT_TRUE(core::is_type_a_comparison(cop::TEST));
    EXPECT_FALSE(core::is_type_a_comparison(cop::CMPI_EQ));
    EXPECT_FALSE(core::is_type_a_comparison(cop::ADD));
}

TEST(OpcodeClassificationTest, IsComparisonImmOp) {
    namespace cop = core::opcode;
    EXPECT_TRUE(core::is_comparison_imm_op(cop::CMPI_EQ));
    EXPECT_TRUE(core::is_comparison_imm_op(cop::CMPI_NE));
    EXPECT_TRUE(core::is_comparison_imm_op(cop::CMPI_LT));
    EXPECT_TRUE(core::is_comparison_imm_op(cop::CMPI_GE));
    EXPECT_FALSE(core::is_comparison_imm_op(cop::EQ));
    EXPECT_FALSE(core::is_comparison_imm_op(cop::TEST));
    EXPECT_FALSE(core::is_comparison_imm_op(cop::ADDI));
}
