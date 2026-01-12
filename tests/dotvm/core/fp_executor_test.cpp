/// @file fp_executor_test.cpp
/// @brief Unit tests for floating-point instruction executor (EXEC-003)

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "dotvm/core/arch_config.hpp"
#include "dotvm/core/executor.hpp"
#include "dotvm/core/fpu.hpp"
#include "dotvm/core/instruction.hpp"
#include "dotvm/core/opcode.hpp"

namespace dotvm::core {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class FloatingPointExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_ = std::make_unique<VmContext>(VmConfig::arch64());

        auto strict_config = VmConfig::arch64();
        strict_config.strict_overflow = true;
        ctx_strict_ = std::make_unique<VmContext>(strict_config);
    }

    // Helper to create a simple program with instructions
    static std::vector<std::uint8_t> make_program(
        std::initializer_list<std::uint32_t> instructions) {
        std::vector<std::uint8_t> code;
        code.reserve(instructions.size() * 4);
        for (auto instr : instructions) {
            // Little-endian encoding
            code.push_back(static_cast<std::uint8_t>(instr & 0xFF));
            code.push_back(static_cast<std::uint8_t>((instr >> 8) & 0xFF));
            code.push_back(static_cast<std::uint8_t>((instr >> 16) & 0xFF));
            code.push_back(static_cast<std::uint8_t>((instr >> 24) & 0xFF));
        }
        return code;
    }

    // Helper to run a single FP instruction and get result
    StepResult run_fp_instr(VmContext& ctx, std::uint8_t op, std::uint8_t rd,
                            std::uint8_t rs1, std::uint8_t rs2 = 0) {
        auto code = make_program({encode_type_a(op, rd, rs1, rs2),
                                   encode_type_a(opcode::HALT, 0, 0, 0)});
        Executor exec{ctx, code};
        return exec.step();
    }

    std::unique_ptr<VmContext> ctx_;
    std::unique_ptr<VmContext> ctx_strict_;

    // Test constants
    static constexpr double QNAN = std::numeric_limits<double>::quiet_NaN();
    static constexpr double POS_INF = std::numeric_limits<double>::infinity();
    static constexpr double NEG_INF = -std::numeric_limits<double>::infinity();
    static constexpr double POS_ZERO = 0.0;
    static constexpr double NEG_ZERO = -0.0;
};

// ============================================================================
// FADD Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, FADD_BasicPositive) {
    ctx_->registers().write(1, Value::from_float(1.5));
    ctx_->registers().write(2, Value::from_float(2.5));

    auto result = run_fp_instr(*ctx_, opcode::FADD, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 4.0);
}

TEST_F(FloatingPointExecutorTest, FADD_NegativeNumbers) {
    ctx_->registers().write(1, Value::from_float(-1.5));
    ctx_->registers().write(2, Value::from_float(-2.5));

    auto result = run_fp_instr(*ctx_, opcode::FADD, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), -4.0);
}

TEST_F(FloatingPointExecutorTest, FADD_NaN_Propagation_FirstOperand) {
    ctx_->registers().write(1, Value::from_float(QNAN));
    ctx_->registers().write(2, Value::from_float(1.0));

    auto result = run_fp_instr(*ctx_, opcode::FADD, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FADD_NaN_Propagation_SecondOperand) {
    ctx_->registers().write(1, Value::from_float(1.0));
    ctx_->registers().write(2, Value::from_float(QNAN));

    auto result = run_fp_instr(*ctx_, opcode::FADD, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FADD_Infinity_Plus_Finite) {
    ctx_->registers().write(1, Value::from_float(POS_INF));
    ctx_->registers().write(2, Value::from_float(1.0));

    auto result = run_fp_instr(*ctx_, opcode::FADD, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isinf(ctx_->registers().read(3).as_float()));
    EXPECT_GT(ctx_->registers().read(3).as_float(), 0);
}

TEST_F(FloatingPointExecutorTest, FADD_PosInf_Plus_NegInf_IsNaN) {
    ctx_->registers().write(1, Value::from_float(POS_INF));
    ctx_->registers().write(2, Value::from_float(NEG_INF));

    auto result = run_fp_instr(*ctx_, opcode::FADD, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FADD_IntegerCoercion) {
    ctx_->registers().write(1, Value::from_int(10));
    ctx_->registers().write(2, Value::from_float(0.5));

    auto result = run_fp_instr(*ctx_, opcode::FADD, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 10.5);
}

// ============================================================================
// FSUB Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, FSUB_Basic) {
    ctx_->registers().write(1, Value::from_float(5.5));
    ctx_->registers().write(2, Value::from_float(2.5));

    auto result = run_fp_instr(*ctx_, opcode::FSUB, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 3.0);
}

TEST_F(FloatingPointExecutorTest, FSUB_NaN_Propagation) {
    ctx_->registers().write(1, Value::from_float(QNAN));
    ctx_->registers().write(2, Value::from_float(1.0));

    auto result = run_fp_instr(*ctx_, opcode::FSUB, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FSUB_Infinity_Minus_Same_IsNaN) {
    ctx_->registers().write(1, Value::from_float(POS_INF));
    ctx_->registers().write(2, Value::from_float(POS_INF));

    auto result = run_fp_instr(*ctx_, opcode::FSUB, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

// ============================================================================
// FMUL Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, FMUL_Basic) {
    ctx_->registers().write(1, Value::from_float(2.5));
    ctx_->registers().write(2, Value::from_float(4.0));

    auto result = run_fp_instr(*ctx_, opcode::FMUL, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 10.0);
}

TEST_F(FloatingPointExecutorTest, FMUL_NaN_Propagation) {
    ctx_->registers().write(1, Value::from_float(QNAN));
    ctx_->registers().write(2, Value::from_float(2.0));

    auto result = run_fp_instr(*ctx_, opcode::FMUL, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FMUL_Infinity_Times_Zero_IsNaN) {
    ctx_->registers().write(1, Value::from_float(POS_INF));
    ctx_->registers().write(2, Value::from_float(0.0));

    auto result = run_fp_instr(*ctx_, opcode::FMUL, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FMUL_Infinity_Times_Negative) {
    ctx_->registers().write(1, Value::from_float(POS_INF));
    ctx_->registers().write(2, Value::from_float(-1.0));

    auto result = run_fp_instr(*ctx_, opcode::FMUL, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isinf(ctx_->registers().read(3).as_float()));
    EXPECT_LT(ctx_->registers().read(3).as_float(), 0);
}

// ============================================================================
// FDIV Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, FDIV_Basic) {
    ctx_->registers().write(1, Value::from_float(10.0));
    ctx_->registers().write(2, Value::from_float(4.0));

    auto result = run_fp_instr(*ctx_, opcode::FDIV, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 2.5);
}

TEST_F(FloatingPointExecutorTest, FDIV_ByZero_PositiveInfinity) {
    ctx_->registers().write(1, Value::from_float(1.0));
    ctx_->registers().write(2, Value::from_float(0.0));

    auto result = run_fp_instr(*ctx_, opcode::FDIV, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isinf(ctx_->registers().read(3).as_float()));
    EXPECT_GT(ctx_->registers().read(3).as_float(), 0);
}

TEST_F(FloatingPointExecutorTest, FDIV_ByZero_NegativeInfinity) {
    ctx_->registers().write(1, Value::from_float(-1.0));
    ctx_->registers().write(2, Value::from_float(0.0));

    auto result = run_fp_instr(*ctx_, opcode::FDIV, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isinf(ctx_->registers().read(3).as_float()));
    EXPECT_LT(ctx_->registers().read(3).as_float(), 0);
}

TEST_F(FloatingPointExecutorTest, FDIV_ZeroByZero_IsNaN) {
    ctx_->registers().write(1, Value::from_float(0.0));
    ctx_->registers().write(2, Value::from_float(0.0));

    auto result = run_fp_instr(*ctx_, opcode::FDIV, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FDIV_NaN_Propagation) {
    ctx_->registers().write(1, Value::from_float(QNAN));
    ctx_->registers().write(2, Value::from_float(2.0));

    auto result = run_fp_instr(*ctx_, opcode::FDIV, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FDIV_InfinityByInfinity_IsNaN) {
    ctx_->registers().write(1, Value::from_float(POS_INF));
    ctx_->registers().write(2, Value::from_float(POS_INF));

    auto result = run_fp_instr(*ctx_, opcode::FDIV, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

// ============================================================================
// FNEG Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, FNEG_Positive) {
    ctx_->registers().write(1, Value::from_float(3.14));

    auto result = run_fp_instr(*ctx_, opcode::FNEG, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), -3.14);
}

TEST_F(FloatingPointExecutorTest, FNEG_Negative) {
    ctx_->registers().write(1, Value::from_float(-2.71));

    auto result = run_fp_instr(*ctx_, opcode::FNEG, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 2.71);
}

TEST_F(FloatingPointExecutorTest, FNEG_PositiveZero) {
    ctx_->registers().write(1, Value::from_float(POS_ZERO));

    auto result = run_fp_instr(*ctx_, opcode::FNEG, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // Result should be -0.0
    EXPECT_TRUE(std::signbit(ctx_->registers().read(3).as_float()));
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 0.0);
}

TEST_F(FloatingPointExecutorTest, FNEG_NegativeZero) {
    ctx_->registers().write(1, Value::from_float(NEG_ZERO));

    auto result = run_fp_instr(*ctx_, opcode::FNEG, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // Result should be +0.0
    EXPECT_FALSE(std::signbit(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FNEG_PositiveInfinity) {
    ctx_->registers().write(1, Value::from_float(POS_INF));

    auto result = run_fp_instr(*ctx_, opcode::FNEG, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isinf(ctx_->registers().read(3).as_float()));
    EXPECT_LT(ctx_->registers().read(3).as_float(), 0);
}

TEST_F(FloatingPointExecutorTest, FNEG_NaN_PreservesNaN) {
    ctx_->registers().write(1, Value::from_float(QNAN));

    auto result = run_fp_instr(*ctx_, opcode::FNEG, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

// ============================================================================
// FSQRT Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, FSQRT_Basic) {
    ctx_->registers().write(1, Value::from_float(4.0));

    auto result = run_fp_instr(*ctx_, opcode::FSQRT, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 2.0);
}

TEST_F(FloatingPointExecutorTest, FSQRT_One) {
    ctx_->registers().write(1, Value::from_float(1.0));

    auto result = run_fp_instr(*ctx_, opcode::FSQRT, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 1.0);
}

TEST_F(FloatingPointExecutorTest, FSQRT_Zero) {
    ctx_->registers().write(1, Value::from_float(0.0));

    auto result = run_fp_instr(*ctx_, opcode::FSQRT, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 0.0);
}

TEST_F(FloatingPointExecutorTest, FSQRT_NegativeZero) {
    ctx_->registers().write(1, Value::from_float(NEG_ZERO));

    auto result = run_fp_instr(*ctx_, opcode::FSQRT, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // sqrt(-0.0) = -0.0 per IEEE 754
    EXPECT_TRUE(std::signbit(ctx_->registers().read(3).as_float()));
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 0.0);
}

TEST_F(FloatingPointExecutorTest, FSQRT_Negative_IsNaN) {
    ctx_->registers().write(1, Value::from_float(-1.0));

    auto result = run_fp_instr(*ctx_, opcode::FSQRT, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FSQRT_Negative_Strict_ReturnsError) {
    ctx_strict_->registers().write(1, Value::from_float(-1.0));

    auto result = run_fp_instr(*ctx_strict_, opcode::FSQRT, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::FloatingPointInvalid);
    EXPECT_TRUE(std::isnan(ctx_strict_->registers().read(3).as_float()));
}

TEST_F(FloatingPointExecutorTest, FSQRT_PositiveInfinity) {
    ctx_->registers().write(1, Value::from_float(POS_INF));

    auto result = run_fp_instr(*ctx_, opcode::FSQRT, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isinf(ctx_->registers().read(3).as_float()));
    EXPECT_GT(ctx_->registers().read(3).as_float(), 0);
}

TEST_F(FloatingPointExecutorTest, FSQRT_NaN_PreservesNaN) {
    ctx_->registers().write(1, Value::from_float(QNAN));

    auto result = run_fp_instr(*ctx_, opcode::FSQRT, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_TRUE(std::isnan(ctx_->registers().read(3).as_float()));
}

// ============================================================================
// FCMP Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, FCMP_LessThan) {
    ctx_->registers().write(1, Value::from_float(1.0));
    ctx_->registers().write(2, Value::from_float(2.0));

    auto code = make_program({encode_type_a(opcode::FCMP, 0, 1, 2),
                               encode_type_a(opcode::HALT, 0, 0, 0)});
    Executor exec{*ctx_, code};
    auto result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Success);

    EXPECT_TRUE(exec.state().fp_flags.less_than);
    EXPECT_FALSE(exec.state().fp_flags.equal);
    EXPECT_FALSE(exec.state().fp_flags.greater_than);
    EXPECT_FALSE(exec.state().fp_flags.unordered);
}

TEST_F(FloatingPointExecutorTest, FCMP_GreaterThan) {
    ctx_->registers().write(1, Value::from_float(5.0));
    ctx_->registers().write(2, Value::from_float(2.0));

    auto code = make_program({encode_type_a(opcode::FCMP, 0, 1, 2),
                               encode_type_a(opcode::HALT, 0, 0, 0)});
    Executor exec{*ctx_, code};
    auto result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Success);

    EXPECT_FALSE(exec.state().fp_flags.less_than);
    EXPECT_FALSE(exec.state().fp_flags.equal);
    EXPECT_TRUE(exec.state().fp_flags.greater_than);
    EXPECT_FALSE(exec.state().fp_flags.unordered);
}

TEST_F(FloatingPointExecutorTest, FCMP_Equal) {
    ctx_->registers().write(1, Value::from_float(3.14));
    ctx_->registers().write(2, Value::from_float(3.14));

    auto code = make_program({encode_type_a(opcode::FCMP, 0, 1, 2),
                               encode_type_a(opcode::HALT, 0, 0, 0)});
    Executor exec{*ctx_, code};
    auto result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Success);

    EXPECT_FALSE(exec.state().fp_flags.less_than);
    EXPECT_TRUE(exec.state().fp_flags.equal);
    EXPECT_FALSE(exec.state().fp_flags.greater_than);
    EXPECT_FALSE(exec.state().fp_flags.unordered);
}

TEST_F(FloatingPointExecutorTest, FCMP_NegativeZero_Equals_PositiveZero) {
    ctx_->registers().write(1, Value::from_float(NEG_ZERO));
    ctx_->registers().write(2, Value::from_float(POS_ZERO));

    auto code = make_program({encode_type_a(opcode::FCMP, 0, 1, 2),
                               encode_type_a(opcode::HALT, 0, 0, 0)});
    Executor exec{*ctx_, code};
    auto result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Success);

    EXPECT_TRUE(exec.state().fp_flags.equal);
    EXPECT_FALSE(exec.state().fp_flags.unordered);
}

TEST_F(FloatingPointExecutorTest, FCMP_NaN_FirstOperand_Unordered) {
    ctx_->registers().write(1, Value::from_float(QNAN));
    ctx_->registers().write(2, Value::from_float(1.0));

    auto code = make_program({encode_type_a(opcode::FCMP, 0, 1, 2),
                               encode_type_a(opcode::HALT, 0, 0, 0)});
    Executor exec{*ctx_, code};
    auto result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Success);

    EXPECT_FALSE(exec.state().fp_flags.less_than);
    EXPECT_FALSE(exec.state().fp_flags.equal);
    EXPECT_FALSE(exec.state().fp_flags.greater_than);
    EXPECT_TRUE(exec.state().fp_flags.unordered);
}

TEST_F(FloatingPointExecutorTest, FCMP_NaN_SecondOperand_Unordered) {
    ctx_->registers().write(1, Value::from_float(1.0));
    ctx_->registers().write(2, Value::from_float(QNAN));

    auto code = make_program({encode_type_a(opcode::FCMP, 0, 1, 2),
                               encode_type_a(opcode::HALT, 0, 0, 0)});
    Executor exec{*ctx_, code};
    auto result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Success);

    EXPECT_TRUE(exec.state().fp_flags.unordered);
}

TEST_F(FloatingPointExecutorTest, FCMP_BothNaN_Unordered) {
    ctx_->registers().write(1, Value::from_float(QNAN));
    ctx_->registers().write(2, Value::from_float(QNAN));

    auto code = make_program({encode_type_a(opcode::FCMP, 0, 1, 2),
                               encode_type_a(opcode::HALT, 0, 0, 0)});
    Executor exec{*ctx_, code};
    auto result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Success);

    EXPECT_TRUE(exec.state().fp_flags.unordered);
}

TEST_F(FloatingPointExecutorTest, FCMP_Infinity_Ordered) {
    ctx_->registers().write(1, Value::from_float(POS_INF));
    ctx_->registers().write(2, Value::from_float(1.0e100));

    auto code = make_program({encode_type_a(opcode::FCMP, 0, 1, 2),
                               encode_type_a(opcode::HALT, 0, 0, 0)});
    Executor exec{*ctx_, code};
    auto result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Success);

    EXPECT_TRUE(exec.state().fp_flags.greater_than);
    EXPECT_FALSE(exec.state().fp_flags.unordered);
}

// ============================================================================
// F2I Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, F2I_Basic) {
    ctx_->registers().write(1, Value::from_float(42.7));

    auto result = run_fp_instr(*ctx_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_->registers().read(3).as_integer(), 42);
}

TEST_F(FloatingPointExecutorTest, F2I_Negative) {
    ctx_->registers().write(1, Value::from_float(-42.7));

    auto result = run_fp_instr(*ctx_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_->registers().read(3).as_integer(), -42);
}

TEST_F(FloatingPointExecutorTest, F2I_Zero) {
    ctx_->registers().write(1, Value::from_float(0.0));

    auto result = run_fp_instr(*ctx_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_->registers().read(3).as_integer(), 0);
}

TEST_F(FloatingPointExecutorTest, F2I_NaN_ReturnsZero) {
    ctx_->registers().write(1, Value::from_float(QNAN));

    auto result = run_fp_instr(*ctx_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_->registers().read(3).as_integer(), 0);
}

TEST_F(FloatingPointExecutorTest, F2I_PositiveInfinity_Saturates) {
    ctx_->registers().write(1, Value::from_float(POS_INF));

    auto result = run_fp_instr(*ctx_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // Arch64 uses 48-bit signed integers (NaN-boxing)
    EXPECT_EQ(ctx_->registers().read(3).as_integer(), arch_config::INT48_MAX);
}

TEST_F(FloatingPointExecutorTest, F2I_NegativeInfinity_Saturates) {
    ctx_->registers().write(1, Value::from_float(NEG_INF));

    auto result = run_fp_instr(*ctx_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // Arch64 uses 48-bit signed integers (NaN-boxing)
    EXPECT_EQ(ctx_->registers().read(3).as_integer(), arch_config::INT48_MIN);
}

TEST_F(FloatingPointExecutorTest, F2I_LargeValue_Saturates) {
    // Value larger than 48-bit signed max (> 2^47)
    ctx_->registers().write(1, Value::from_float(1e15));

    auto result = run_fp_instr(*ctx_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // Arch64 uses 48-bit signed integers (NaN-boxing)
    EXPECT_EQ(ctx_->registers().read(3).as_integer(), arch_config::INT48_MAX);
}

TEST_F(FloatingPointExecutorTest, F2I_LargeValue_Strict_ReturnsError) {
    // Value larger than 48-bit signed max (> 2^47)
    ctx_strict_->registers().write(1, Value::from_float(1e15));

    auto result = run_fp_instr(*ctx_strict_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::ConversionOverflow);
    // Arch64 uses 48-bit signed integers (NaN-boxing)
    EXPECT_EQ(ctx_strict_->registers().read(3).as_integer(), arch_config::INT48_MAX);
}

TEST_F(FloatingPointExecutorTest, F2I_TruncateTowardZero_Positive) {
    ctx_->registers().write(1, Value::from_float(2.9));

    auto result = run_fp_instr(*ctx_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_->registers().read(3).as_integer(), 2);
}

TEST_F(FloatingPointExecutorTest, F2I_TruncateTowardZero_Negative) {
    ctx_->registers().write(1, Value::from_float(-2.9));

    auto result = run_fp_instr(*ctx_, opcode::F2I, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_->registers().read(3).as_integer(), -2);
}

// ============================================================================
// I2F Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, I2F_Basic) {
    ctx_->registers().write(1, Value::from_int(42));

    auto result = run_fp_instr(*ctx_, opcode::I2F, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 42.0);
}

TEST_F(FloatingPointExecutorTest, I2F_Negative) {
    ctx_->registers().write(1, Value::from_int(-42));

    auto result = run_fp_instr(*ctx_, opcode::I2F, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), -42.0);
}

TEST_F(FloatingPointExecutorTest, I2F_Zero) {
    ctx_->registers().write(1, Value::from_int(0));

    auto result = run_fp_instr(*ctx_, opcode::I2F, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 0.0);
}

TEST_F(FloatingPointExecutorTest, I2F_LargePrecisionLoss) {
    // Use a value within 48-bit range but large enough to potentially lose precision
    // 2^46 is within 48-bit range and representable exactly in double
    const std::int64_t large_val = (1LL << 46);
    ctx_->registers().write(1, Value::from_int(large_val));

    auto result = run_fp_instr(*ctx_, opcode::I2F, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // This value should convert exactly
    double converted = ctx_->registers().read(3).as_float();
    EXPECT_DOUBLE_EQ(converted, static_cast<double>(large_val));
}

// ============================================================================
// Denormal (Subnormal) Tests
// ============================================================================

TEST_F(FloatingPointExecutorTest, FADD_Denormals) {
    double denorm = std::numeric_limits<double>::denorm_min();
    ctx_->registers().write(1, Value::from_float(denorm));
    ctx_->registers().write(2, Value::from_float(denorm));

    auto result = run_fp_instr(*ctx_, opcode::FADD, 3, 1, 2);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 2 * denorm);
}

TEST_F(FloatingPointExecutorTest, FSQRT_Denormal) {
    double denorm = std::numeric_limits<double>::denorm_min();
    ctx_->registers().write(1, Value::from_float(denorm));

    auto result = run_fp_instr(*ctx_, opcode::FSQRT, 3, 1, 0);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // sqrt of denormal is a small positive value
    EXPECT_GT(ctx_->registers().read(3).as_float(), 0);
}

// ============================================================================
// Execution via Executor (integration tests)
// ============================================================================

TEST_F(FloatingPointExecutorTest, ExecuteProgram_FADD_And_FMUL) {
    // R1 = 2.0, R2 = 3.0
    // R3 = R1 + R2 = 5.0
    // R4 = R3 * R1 = 10.0
    // HALT
    ctx_->registers().write(1, Value::from_float(2.0));
    ctx_->registers().write(2, Value::from_float(3.0));

    auto code = make_program({
        encode_type_a(opcode::FADD, 3, 1, 2),  // R3 = 2.0 + 3.0 = 5.0
        encode_type_a(opcode::FMUL, 4, 3, 1),  // R4 = 5.0 * 2.0 = 10.0
        encode_type_a(opcode::HALT, 0, 0, 0)
    });

    Executor exec{*ctx_, code};
    auto error = exec.run();

    EXPECT_EQ(error, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 5.0);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(4).as_float(), 10.0);
}

TEST_F(FloatingPointExecutorTest, ExecuteProgram_MixedIntAndFloat) {
    // R1 = 10 (int), R2 = 0.5 (float)
    // R3 = I2F(R1) = 10.0
    // R4 = R3 + R2 = 10.5
    // R5 = F2I(R4) = 10
    ctx_->registers().write(1, Value::from_int(10));
    ctx_->registers().write(2, Value::from_float(0.5));

    auto code = make_program({
        encode_type_a(opcode::I2F, 3, 1, 0),   // R3 = 10.0
        encode_type_a(opcode::FADD, 4, 3, 2),  // R4 = 10.5
        encode_type_a(opcode::F2I, 5, 4, 0),   // R5 = 10
        encode_type_a(opcode::HALT, 0, 0, 0)
    });

    Executor exec{*ctx_, code};
    auto error = exec.run();

    EXPECT_EQ(error, ExecutionError::Success);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(3).as_float(), 10.0);
    EXPECT_DOUBLE_EQ(ctx_->registers().read(4).as_float(), 10.5);
    EXPECT_EQ(ctx_->registers().read(5).as_integer(), 10);
}

// ============================================================================
// FPU Helper Tests
// ============================================================================

TEST(FpuTest, IsNegativeZero) {
    EXPECT_TRUE(fpu::is_negative_zero(-0.0));
    EXPECT_FALSE(fpu::is_negative_zero(0.0));
    EXPECT_FALSE(fpu::is_negative_zero(1.0));
}

TEST(FpuTest, IsPositiveZero) {
    EXPECT_TRUE(fpu::is_positive_zero(0.0));
    EXPECT_FALSE(fpu::is_positive_zero(-0.0));
    EXPECT_FALSE(fpu::is_positive_zero(1.0));
}

TEST(FpuTest, IsZero) {
    EXPECT_TRUE(fpu::is_zero(0.0));
    EXPECT_TRUE(fpu::is_zero(-0.0));
    EXPECT_FALSE(fpu::is_zero(1.0));
}

TEST(FpuTest, IsQuietNaN) {
    EXPECT_TRUE(fpu::is_quiet_nan(std::numeric_limits<double>::quiet_NaN()));
    EXPECT_FALSE(fpu::is_quiet_nan(0.0));
    EXPECT_FALSE(fpu::is_quiet_nan(std::numeric_limits<double>::infinity()));
}

TEST(FpuTest, MakeInf) {
    EXPECT_TRUE(std::isinf(fpu::make_inf(false)));
    EXPECT_GT(fpu::make_inf(false), 0);
    EXPECT_TRUE(std::isinf(fpu::make_inf(true)));
    EXPECT_LT(fpu::make_inf(true), 0);
}

TEST(FpuTest, Signbit) {
    EXPECT_FALSE(fpu::signbit(1.0));
    EXPECT_TRUE(fpu::signbit(-1.0));
    EXPECT_FALSE(fpu::signbit(0.0));
    EXPECT_TRUE(fpu::signbit(-0.0));
}

}  // namespace
}  // namespace dotvm::core
