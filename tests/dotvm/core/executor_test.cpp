/// @file executor_test.cpp
/// @brief Unit tests for the instruction executor

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "dotvm/core/executor.hpp"
#include "dotvm/core/instruction.hpp"
#include "dotvm/core/opcode.hpp"

namespace dotvm::core {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class ArithmeticExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx_arch32_ = std::make_unique<VmContext>(VmConfig::arch32());
        ctx_arch64_ = std::make_unique<VmContext>(VmConfig::arch64());

        auto strict_config = VmConfig::arch64();
        strict_config.strict_overflow = true;
        ctx_strict_ = std::make_unique<VmContext>(strict_config);
    }

    std::unique_ptr<VmContext> ctx_arch32_;
    std::unique_ptr<VmContext> ctx_arch64_;
    std::unique_ptr<VmContext> ctx_strict_;
};

class ExecutorTest : public ::testing::Test {
protected:
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
};

// ============================================================================
// ADD Tests
// ============================================================================

TEST_F(ArithmeticExecutorTest, ADD_BasicPositive) {
    ctx_arch64_->registers().write(1, Value::from_int(10));
    ctx_arch64_->registers().write(2, Value::from_int(20));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::ADD, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 30);
}

TEST_F(ArithmeticExecutorTest, ADD_NegativeNumbers) {
    ctx_arch64_->registers().write(1, Value::from_int(-10));
    ctx_arch64_->registers().write(2, Value::from_int(-20));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::ADD, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), -30);
}

TEST_F(ArithmeticExecutorTest, ADD_DestinationR0_Ignored) {
    ctx_arch64_->registers().write(1, Value::from_int(10));
    ctx_arch64_->registers().write(2, Value::from_int(20));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::ADD, 0, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // R0 should remain 0 (hardwired)
    EXPECT_EQ(ctx_arch64_->registers().read(0).as_integer(), 0);
}

TEST_F(ArithmeticExecutorTest, ADD_Overflow_NonStrict_Wraps) {
    // Note: Value system uses 48-bit NaN-boxing, so use 32-bit max for Arch32 overflow test
    ctx_arch32_->registers().write(1, Value::from_int(std::numeric_limits<std::int32_t>::max()));
    ctx_arch32_->registers().write(2, Value::from_int(1));

    ArithmeticExecutor exec{*ctx_arch32_};
    auto decoded = decode_type_a(encode_type_a(opcode::ADD, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    // Non-strict mode: no error, result wraps to INT32_MIN
    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch32_->registers().read(3).as_integer(),
              std::numeric_limits<std::int32_t>::min());
}

TEST_F(ArithmeticExecutorTest, ADD_Overflow_Strict_ReturnsError) {
    // Create a strict 32-bit config to test overflow detection
    auto strict32_config = VmConfig::arch32();
    strict32_config.strict_overflow = true;
    VmContext ctx_strict32{strict32_config};

    ctx_strict32.registers().write(1, Value::from_int(std::numeric_limits<std::int32_t>::max()));
    ctx_strict32.registers().write(2, Value::from_int(1));

    ArithmeticExecutor exec{ctx_strict32};
    auto decoded = decode_type_a(encode_type_a(opcode::ADD, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    // INT32_MAX + 1 overflows 32-bit signed integer
    EXPECT_EQ(result.err, ExecutionError::IntegerOverflow);
    EXPECT_FALSE(result.should_halt);  // Overflow is non-fatal
}

// ============================================================================
// SUB Tests
// ============================================================================

TEST_F(ArithmeticExecutorTest, SUB_BasicPositive) {
    ctx_arch64_->registers().write(1, Value::from_int(30));
    ctx_arch64_->registers().write(2, Value::from_int(10));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::SUB, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 20);
}

TEST_F(ArithmeticExecutorTest, SUB_NegativeResult) {
    ctx_arch64_->registers().write(1, Value::from_int(10));
    ctx_arch64_->registers().write(2, Value::from_int(30));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::SUB, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), -20);
}

TEST_F(ArithmeticExecutorTest, SUB_Overflow_Strict_ReturnsError) {
    // Create a strict 32-bit config to test overflow detection
    auto strict32_config = VmConfig::arch32();
    strict32_config.strict_overflow = true;
    VmContext ctx_strict32{strict32_config};

    ctx_strict32.registers().write(1, Value::from_int(std::numeric_limits<std::int32_t>::min()));
    ctx_strict32.registers().write(2, Value::from_int(1));

    ArithmeticExecutor exec{ctx_strict32};
    auto decoded = decode_type_a(encode_type_a(opcode::SUB, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    // INT32_MIN - 1 overflows 32-bit signed integer
    EXPECT_EQ(result.err, ExecutionError::IntegerOverflow);
}

// ============================================================================
// MUL Tests
// ============================================================================

TEST_F(ArithmeticExecutorTest, MUL_BasicPositive) {
    ctx_arch64_->registers().write(1, Value::from_int(6));
    ctx_arch64_->registers().write(2, Value::from_int(7));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::MUL, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 42);
}

TEST_F(ArithmeticExecutorTest, MUL_NegativeNumbers) {
    ctx_arch64_->registers().write(1, Value::from_int(-6));
    ctx_arch64_->registers().write(2, Value::from_int(7));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::MUL, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), -42);
}

TEST_F(ArithmeticExecutorTest, MUL_ByZero) {
    ctx_arch64_->registers().write(1, Value::from_int(12345));
    ctx_arch64_->registers().write(2, Value::from_int(0));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::MUL, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 0);
}

// ============================================================================
// DIV Tests
// ============================================================================

TEST_F(ArithmeticExecutorTest, DIV_BasicPositive) {
    ctx_arch64_->registers().write(1, Value::from_int(42));
    ctx_arch64_->registers().write(2, Value::from_int(6));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::DIV, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 7);
}

TEST_F(ArithmeticExecutorTest, DIV_Truncation) {
    ctx_arch64_->registers().write(1, Value::from_int(10));
    ctx_arch64_->registers().write(2, Value::from_int(3));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::DIV, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // 10 / 3 = 3 (truncated)
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 3);
}

TEST_F(ArithmeticExecutorTest, DIV_ByZero_NonStrict_ReturnsZero) {
    ctx_arch64_->registers().write(1, Value::from_int(42));
    ctx_arch64_->registers().write(2, Value::from_int(0));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::DIV, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 0);
}

TEST_F(ArithmeticExecutorTest, DIV_ByZero_Strict_ReturnsError) {
    ctx_strict_->registers().write(1, Value::from_int(42));
    ctx_strict_->registers().write(2, Value::from_int(0));

    ArithmeticExecutor exec{*ctx_strict_};
    auto decoded = decode_type_a(encode_type_a(opcode::DIV, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::DivisionByZero);
    EXPECT_EQ(ctx_strict_->registers().read(3).as_integer(), 0);
}

// ============================================================================
// MOD Tests
// ============================================================================

TEST_F(ArithmeticExecutorTest, MOD_BasicPositive) {
    ctx_arch64_->registers().write(1, Value::from_int(10));
    ctx_arch64_->registers().write(2, Value::from_int(3));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::MOD, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 1);
}

TEST_F(ArithmeticExecutorTest, MOD_ByZero_Strict_ReturnsError) {
    ctx_strict_->registers().write(1, Value::from_int(42));
    ctx_strict_->registers().write(2, Value::from_int(0));

    ArithmeticExecutor exec{*ctx_strict_};
    auto decoded = decode_type_a(encode_type_a(opcode::MOD, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::DivisionByZero);
}

// ============================================================================
// NEG Tests
// ============================================================================

TEST_F(ArithmeticExecutorTest, NEG_BasicPositive) {
    ctx_arch64_->registers().write(1, Value::from_int(42));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::NEG, 3, 1, 0));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), -42);
}

TEST_F(ArithmeticExecutorTest, NEG_BasicNegative) {
    ctx_arch64_->registers().write(1, Value::from_int(-42));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::NEG, 3, 1, 0));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 42);
}

TEST_F(ArithmeticExecutorTest, NEG_Zero) {
    ctx_arch64_->registers().write(1, Value::from_int(0));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::NEG, 3, 1, 0));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 0);
}

// ============================================================================
// ADDI Tests (Type B - Accumulator Style)
// ============================================================================

TEST_F(ArithmeticExecutorTest, ADDI_BasicPositive) {
    ctx_arch64_->registers().write(5, Value::from_int(100));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_b(encode_type_b(opcode::ADDI, 5, 50));
    auto result = exec.execute_type_b(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // Rd = Rd + imm -> 100 + 50 = 150
    EXPECT_EQ(ctx_arch64_->registers().read(5).as_integer(), 150);
}

TEST_F(ArithmeticExecutorTest, ADDI_NegativeImmediate) {
    ctx_arch64_->registers().write(5, Value::from_int(100));

    ArithmeticExecutor exec{*ctx_arch64_};
    // 0xFFFE = -2 when sign-extended
    auto decoded = decode_type_b(encode_type_b(opcode::ADDI, 5, 0xFFFE));
    auto result = exec.execute_type_b(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(5).as_integer(), 98);
}

TEST_F(ArithmeticExecutorTest, ADDI_MaxImmediate) {
    ctx_arch64_->registers().write(5, Value::from_int(0));

    ArithmeticExecutor exec{*ctx_arch64_};
    // 0x7FFF = 32767 (max positive 16-bit signed)
    auto decoded = decode_type_b(encode_type_b(opcode::ADDI, 5, 0x7FFF));
    auto result = exec.execute_type_b(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(5).as_integer(), 32767);
}

TEST_F(ArithmeticExecutorTest, ADDI_MinImmediate) {
    ctx_arch64_->registers().write(5, Value::from_int(0));

    ArithmeticExecutor exec{*ctx_arch64_};
    // 0x8000 = -32768 (min negative 16-bit signed)
    auto decoded = decode_type_b(encode_type_b(opcode::ADDI, 5, 0x8000));
    auto result = exec.execute_type_b(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(5).as_integer(), -32768);
}

// ============================================================================
// SUBI Tests
// ============================================================================

TEST_F(ArithmeticExecutorTest, SUBI_BasicPositive) {
    ctx_arch64_->registers().write(5, Value::from_int(100));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_b(encode_type_b(opcode::SUBI, 5, 30));
    auto result = exec.execute_type_b(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(5).as_integer(), 70);
}

// ============================================================================
// MULI Tests
// ============================================================================

TEST_F(ArithmeticExecutorTest, MULI_BasicPositive) {
    ctx_arch64_->registers().write(5, Value::from_int(10));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_b(encode_type_b(opcode::MULI, 5, 5));
    auto result = exec.execute_type_b(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(ctx_arch64_->registers().read(5).as_integer(), 50);
}

// ============================================================================
// Executor Tests
// ============================================================================

TEST_F(ExecutorTest, Step_HALT_StopsExecution) {
    VmContext ctx{VmConfig::arch64()};
    auto code = make_program({
        encode_type_c(opcode::HALT, 0)  // HALT instruction
    });

    Executor exec{ctx, code};
    auto result = exec.step();

    EXPECT_EQ(result.err, ExecutionError::Halted);
    EXPECT_TRUE(result.should_halt);
    EXPECT_TRUE(exec.state().halted);
}

TEST_F(ExecutorTest, Step_NOP_AdvancesPC) {
    VmContext ctx{VmConfig::arch64()};
    auto code = make_program({
        encode_type_c(opcode::NOP, 0),  // NOP
        encode_type_c(opcode::HALT, 0)  // HALT
    });

    Executor exec{ctx, code};

    // First step: NOP
    auto result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_EQ(exec.state().pc, 4u);

    // Second step: HALT
    result = exec.step();
    EXPECT_EQ(result.err, ExecutionError::Halted);
}

TEST_F(ExecutorTest, Run_SimpleArithmetic) {
    VmContext ctx{VmConfig::arch64()};

    // Set up registers
    ctx.registers().write(1, Value::from_int(10));
    ctx.registers().write(2, Value::from_int(20));

    auto code = make_program({
        encode_type_a(opcode::ADD, 3, 1, 2),  // R3 = R1 + R2
        encode_type_c(opcode::HALT, 0)        // HALT
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 30);
    EXPECT_EQ(exec.state().instructions_executed, 2u);
}

TEST_F(ExecutorTest, Run_MultipleOperations) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(100));

    auto code = make_program({
        encode_type_b(opcode::ADDI, 1, 50),    // R1 = R1 + 50 = 150
        encode_type_b(opcode::MULI, 1, 2),     // R1 = R1 * 2 = 300
        encode_type_b(opcode::SUBI, 1, 100),   // R1 = R1 - 100 = 200
        encode_type_c(opcode::HALT, 0)         // HALT
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(1).as_integer(), 200);
}

TEST_F(ExecutorTest, Run_InstructionLimit) {
    VmContext ctx{VmConfig::arch64()};

    auto code = make_program({
        encode_type_c(opcode::NOP, 0),  // NOP
        encode_type_c(opcode::NOP, 0),  // NOP
        encode_type_c(opcode::NOP, 0),  // NOP
        encode_type_c(opcode::HALT, 0)  // HALT
    });

    Executor exec{ctx, code};
    auto result = exec.run(2);  // Limit to 2 instructions

    EXPECT_EQ(result, ExecutionError::InstructionLimitExceeded);
    EXPECT_EQ(exec.state().instructions_executed, 2u);
}

TEST_F(ExecutorTest, PCOutOfBounds_ReturnsError) {
    VmContext ctx{VmConfig::arch64()};
    auto code = make_program({});  // Empty code

    Executor exec{ctx, code};
    auto result = exec.step();

    EXPECT_EQ(result.err, ExecutionError::PCOutOfBounds);
}

TEST_F(ExecutorTest, PCNotAligned_ReturnsError) {
    VmContext ctx{VmConfig::arch64()};
    auto code = make_program({
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    exec.state().pc = 1;  // Misaligned
    auto result = exec.step();

    EXPECT_EQ(result.err, ExecutionError::PCNotAligned);
}

// ============================================================================
// Architecture Compatibility Tests
// ============================================================================

TEST_F(ArithmeticExecutorTest, ADD_Arch32_Wraps) {
    ctx_arch32_->registers().write(1, Value::from_int(0x7FFFFFFF));  // INT32_MAX
    ctx_arch32_->registers().write(2, Value::from_int(1));

    ArithmeticExecutor exec{*ctx_arch32_};
    auto decoded = decode_type_a(encode_type_a(opcode::ADD, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // Should wrap to INT32_MIN in 32-bit mode
    EXPECT_EQ(ctx_arch32_->registers().read(3).as_integer(),
              std::numeric_limits<std::int32_t>::min());
}

TEST_F(ArithmeticExecutorTest, ADD_Arch64_NoWrap) {
    ctx_arch64_->registers().write(1, Value::from_int(0x7FFFFFFF));  // INT32_MAX
    ctx_arch64_->registers().write(2, Value::from_int(1));

    ArithmeticExecutor exec{*ctx_arch64_};
    auto decoded = decode_type_a(encode_type_a(opcode::ADD, 3, 1, 2));
    auto result = exec.execute_type_a(decoded);

    EXPECT_EQ(result.err, ExecutionError::Success);
    // Should NOT wrap in 64-bit mode (just be INT32_MAX + 1)
    EXPECT_EQ(ctx_arch64_->registers().read(3).as_integer(), 0x80000000LL);
}

// ============================================================================
// Bitwise Operation Tests (ROL, ROR, SHLI, SHRI, SARI)
// ============================================================================

TEST_F(ExecutorTest, Run_ROL_BasicOperation) {
    VmContext ctx{VmConfig::arch64()};

    // R1 = 0x1 (bit 0 set)
    ctx.registers().write(1, Value::from_int(1));
    ctx.registers().write(2, Value::from_int(4));  // Rotate by 4

    auto code = make_program({
        encode_type_a(opcode::ROL, 3, 1, 2),  // R3 = ROL(R1, R2)
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // After ROL by 4: 0x1 -> 0x10
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x10);
}

TEST_F(ExecutorTest, Run_ROL_ZeroRotation) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(0));  // Rotate by 0

    auto code = make_program({
        encode_type_a(opcode::ROL, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Rotating by 0 should give same value
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x12345678);
}

TEST_F(ExecutorTest, Run_ROL_Arch32) {
    VmContext ctx{VmConfig::arch32()};

    // R1 = 0x80000001 (high bit and low bit set for 32-bit)
    ctx.registers().write(1, Value::from_int(0x80000001));
    ctx.registers().write(2, Value::from_int(1));  // Rotate by 1

    auto code = make_program({
        encode_type_a(opcode::ROL, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // After ROL by 1: high bit wraps to bit 0
    // 0x80000001 -> 0x00000003
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x00000003);
}

TEST_F(ExecutorTest, Run_ROR_BasicOperation) {
    VmContext ctx{VmConfig::arch64()};

    // R1 = 0x10 (bit 4 set)
    ctx.registers().write(1, Value::from_int(0x10));
    ctx.registers().write(2, Value::from_int(4));  // Rotate by 4

    auto code = make_program({
        encode_type_a(opcode::ROR, 3, 1, 2),  // R3 = ROR(R1, R2)
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // After ROR by 4: 0x10 -> 0x1
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x1);
}

TEST_F(ExecutorTest, Run_ROR_ZeroRotation) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0xABCDEF));
    ctx.registers().write(2, Value::from_int(0));

    auto code = make_program({
        encode_type_a(opcode::ROR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0xABCDEF);
}

TEST_F(ExecutorTest, Run_ROR_Arch32) {
    VmContext ctx{VmConfig::arch32()};

    ctx.registers().write(1, Value::from_int(0x00000003));  // Low 2 bits set
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_program({
        encode_type_a(opcode::ROR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Bit 0 wraps to high bit: 0x00000003 -> 0x80000001
    // In 32-bit signed representation, this is -2147483647
    EXPECT_EQ(ctx.registers().read(3).as_integer(),
              static_cast<std::int32_t>(0x80000001U));
}

TEST_F(ExecutorTest, Run_ROL_ROR_Inverse) {
    VmContext ctx{VmConfig::arch32()};

    // Use 32-bit mode for cleaner inverse property testing
    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(7));

    auto code = make_program({
        encode_type_a(opcode::ROL, 3, 1, 2),  // R3 = ROL(R1, 7)
        encode_type_a(opcode::ROR, 4, 3, 2),  // R4 = ROR(R3, 7)
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // ROL then ROR by same amount should give original value
    EXPECT_EQ(ctx.registers().read(4).as_integer(), 0x12345678);
}

// ============================================================================
// Shift-Immediate Tests (SHLI, SHRI, SARI)
// ============================================================================

TEST_F(ExecutorTest, Run_SHLI_BasicOperation) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(1));  // R1 = 1

    auto code = make_program({
        encode_type_s(opcode::SHLI, 3, 1, 4),  // R3 = R1 << 4
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 16);  // 1 << 4 = 16
}

TEST_F(ExecutorTest, Run_SHLI_ZeroShift) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));

    auto code = make_program({
        encode_type_s(opcode::SHLI, 3, 1, 0),  // R3 = R1 << 0
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x12345678);
}

TEST_F(ExecutorTest, Run_SHLI_MaxShift) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(1));

    auto code = make_program({
        encode_type_s(opcode::SHLI, 3, 1, 40),  // R3 = R1 << 40 (large but safe shift)
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // 1 << 40 = 0x10000000000 (well within 48-bit range)
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 1LL << 40);
}

TEST_F(ExecutorTest, Run_SHRI_BasicOperation) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x100));  // R1 = 256

    auto code = make_program({
        encode_type_s(opcode::SHRI, 3, 1, 4),  // R3 = R1 >> 4 (logical)
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 16);  // 256 >> 4 = 16
}

TEST_F(ExecutorTest, Run_SHRI_ZeroFill) {
    VmContext ctx{VmConfig::arch64()};

    // Use a positive value with upper bits set (within 48-bit range)
    // 0x0000F00000000000 (bits 44-47 set, but below sign bit 47)
    ctx.registers().write(1, Value::from_int(0x700000000000LL));

    auto code = make_program({
        encode_type_s(opcode::SHRI, 3, 1, 4),  // R3 = R1 >> 4 (logical)
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // 0x700000000000 >> 4 = 0x070000000000
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x070000000000LL);
}

TEST_F(ExecutorTest, Run_SARI_BasicOperation) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(-64));  // Negative number

    auto code = make_program({
        encode_type_s(opcode::SARI, 3, 1, 2),  // R3 = R1 >> 2 (arithmetic)
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Arithmetic shift should sign-extend: -64 >> 2 = -16
    EXPECT_EQ(ctx.registers().read(3).as_integer(), -16);
}

TEST_F(ExecutorTest, Run_SARI_SignPreservation) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(-1));  // All bits set

    auto code = make_program({
        encode_type_s(opcode::SARI, 3, 1, 10),  // R3 = R1 >> 10 (arithmetic)
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Arithmetic shift of -1 should stay -1 (all ones)
    EXPECT_EQ(ctx.registers().read(3).as_integer(), -1);
}

TEST_F(ExecutorTest, Run_SARI_PositiveNumber) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(128));

    auto code = make_program({
        encode_type_s(opcode::SARI, 3, 1, 3),  // R3 = 128 >> 3
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // For positive numbers, SAR == SHR: 128 >> 3 = 16
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 16);
}

TEST_F(ExecutorTest, Run_SHLI_Arch32) {
    VmContext ctx{VmConfig::arch32()};

    ctx.registers().write(1, Value::from_int(1));

    auto code = make_program({
        encode_type_s(opcode::SHLI, 3, 1, 31),  // R3 = 1 << 31 (max for 32-bit)
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // 1 << 31 = 0x80000000 = INT32_MIN in signed representation
    EXPECT_EQ(ctx.registers().read(3).as_integer(),
              std::numeric_limits<std::int32_t>::min());
}

// ============================================================================
// ANDI/ORI/XORI Tests (verify reassigned opcodes still work)
// ============================================================================

TEST_F(ExecutorTest, Run_ANDI_WithNewOpcode) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0xFF00FF00));

    auto code = make_program({
        encode_type_b(opcode::ANDI, 5, 0x00FF),  // R5 = R5 & 0x00FF
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // 0xFF00FF00 & 0x00FF = 0x00000000
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0);
}

TEST_F(ExecutorTest, Run_ORI_WithNewOpcode) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0xFF00));

    auto code = make_program({
        encode_type_b(opcode::ORI, 5, 0x00FF),  // R5 = R5 | 0x00FF
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0xFFFF);
}

TEST_F(ExecutorTest, Run_XORI_WithNewOpcode) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0xFFFF));

    auto code = make_program({
        encode_type_b(opcode::XORI, 5, 0x0F0F),  // R5 = R5 ^ 0x0F0F
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0xF0F0);
}

// ============================================================================
// Opcode Classification Tests
// ============================================================================

TEST(OpcodeTest, IsTypeA_Arithmetic) {
    EXPECT_TRUE(is_type_a_arithmetic(opcode::ADD));
    EXPECT_TRUE(is_type_a_arithmetic(opcode::SUB));
    EXPECT_TRUE(is_type_a_arithmetic(opcode::MUL));
    EXPECT_TRUE(is_type_a_arithmetic(opcode::DIV));
    EXPECT_TRUE(is_type_a_arithmetic(opcode::MOD));
    EXPECT_TRUE(is_type_a_arithmetic(opcode::NEG));
    EXPECT_FALSE(is_type_a_arithmetic(opcode::ADDI));
}

TEST(OpcodeTest, IsTypeB_Arithmetic) {
    EXPECT_TRUE(is_type_b_arithmetic(opcode::ADDI));
    EXPECT_TRUE(is_type_b_arithmetic(opcode::SUBI));
    EXPECT_TRUE(is_type_b_arithmetic(opcode::MULI));
    EXPECT_FALSE(is_type_b_arithmetic(opcode::ADD));
}

// ============================================================================
// Bitwise Opcode Classification Tests
// ============================================================================

TEST(OpcodeTest, IsTypeA_Bitwise) {
    // Type A bitwise: AND, OR, XOR, NOT, SHL, SHR, SAR, ROL, ROR
    EXPECT_TRUE(is_type_a_bitwise(opcode::AND));
    EXPECT_TRUE(is_type_a_bitwise(opcode::OR));
    EXPECT_TRUE(is_type_a_bitwise(opcode::XOR));
    EXPECT_TRUE(is_type_a_bitwise(opcode::NOT));
    EXPECT_TRUE(is_type_a_bitwise(opcode::SHL));
    EXPECT_TRUE(is_type_a_bitwise(opcode::SHR));
    EXPECT_TRUE(is_type_a_bitwise(opcode::SAR));
    EXPECT_TRUE(is_type_a_bitwise(opcode::ROL));
    EXPECT_TRUE(is_type_a_bitwise(opcode::ROR));
    // Should not match Type S or Type B
    EXPECT_FALSE(is_type_a_bitwise(opcode::SHLI));
    EXPECT_FALSE(is_type_a_bitwise(opcode::ANDI));
}

TEST(OpcodeTest, IsTypeS_Bitwise) {
    // Type S bitwise: SHLI, SHRI, SARI
    EXPECT_TRUE(is_type_s_bitwise(opcode::SHLI));
    EXPECT_TRUE(is_type_s_bitwise(opcode::SHRI));
    EXPECT_TRUE(is_type_s_bitwise(opcode::SARI));
    // Should not match Type A or Type B
    EXPECT_FALSE(is_type_s_bitwise(opcode::SHL));
    EXPECT_FALSE(is_type_s_bitwise(opcode::ANDI));
}

TEST(OpcodeTest, IsTypeB_Bitwise) {
    // Type B bitwise: ANDI, ORI, XORI
    EXPECT_TRUE(is_type_b_bitwise(opcode::ANDI));
    EXPECT_TRUE(is_type_b_bitwise(opcode::ORI));
    EXPECT_TRUE(is_type_b_bitwise(opcode::XORI));
    // Should not match Type A or Type S
    EXPECT_FALSE(is_type_b_bitwise(opcode::AND));
    EXPECT_FALSE(is_type_b_bitwise(opcode::SHLI));
}

TEST(OpcodeTest, IsBitwise_All) {
    // All bitwise opcodes should return true
    EXPECT_TRUE(is_bitwise(opcode::AND));
    EXPECT_TRUE(is_bitwise(opcode::OR));
    EXPECT_TRUE(is_bitwise(opcode::XOR));
    EXPECT_TRUE(is_bitwise(opcode::NOT));
    EXPECT_TRUE(is_bitwise(opcode::SHL));
    EXPECT_TRUE(is_bitwise(opcode::SHR));
    EXPECT_TRUE(is_bitwise(opcode::SAR));
    EXPECT_TRUE(is_bitwise(opcode::ROL));
    EXPECT_TRUE(is_bitwise(opcode::ROR));
    EXPECT_TRUE(is_bitwise(opcode::SHLI));
    EXPECT_TRUE(is_bitwise(opcode::SHRI));
    EXPECT_TRUE(is_bitwise(opcode::SARI));
    EXPECT_TRUE(is_bitwise(opcode::ANDI));
    EXPECT_TRUE(is_bitwise(opcode::ORI));
    EXPECT_TRUE(is_bitwise(opcode::XORI));
    // Arithmetic opcodes should return false
    EXPECT_FALSE(is_bitwise(opcode::ADD));
    EXPECT_FALSE(is_bitwise(opcode::SUB));
}

TEST(OpcodeTest, SignExtendImm16) {
    // Positive values
    EXPECT_EQ(sign_extend_imm16(0), 0);
    EXPECT_EQ(sign_extend_imm16(1), 1);
    EXPECT_EQ(sign_extend_imm16(0x7FFF), 32767);

    // Negative values (sign extension)
    EXPECT_EQ(sign_extend_imm16(0xFFFF), -1);
    EXPECT_EQ(sign_extend_imm16(0xFFFE), -2);
    EXPECT_EQ(sign_extend_imm16(0x8000), -32768);
}

// ============================================================================
// ExecutionError Tests
// ============================================================================

TEST(ExecutionErrorTest, ToString) {
    EXPECT_EQ(to_string(ExecutionError::Success), "Success");
    EXPECT_EQ(to_string(ExecutionError::IntegerOverflow), "Integer overflow");
    EXPECT_EQ(to_string(ExecutionError::DivisionByZero), "Division by zero");
    EXPECT_EQ(to_string(ExecutionError::InvalidOpcode), "Invalid opcode");
}

TEST(ExecutionErrorTest, IsFatalError) {
    // Non-fatal errors
    EXPECT_FALSE(is_fatal_error(ExecutionError::Success));
    EXPECT_FALSE(is_fatal_error(ExecutionError::IntegerOverflow));
    EXPECT_FALSE(is_fatal_error(ExecutionError::DivisionByZero));

    // Fatal errors
    EXPECT_TRUE(is_fatal_error(ExecutionError::InvalidOpcode));
    EXPECT_TRUE(is_fatal_error(ExecutionError::PCOutOfBounds));
}

// ============================================================================
// StepResult Tests
// ============================================================================

TEST(StepResultTest, Success) {
    auto result = StepResult::success();
    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_FALSE(result.should_halt);
    EXPECT_EQ(result.next_pc, 0u);
}

TEST(StepResultTest, Halt) {
    auto result = StepResult::halt();
    EXPECT_EQ(result.err, ExecutionError::Halted);
    EXPECT_TRUE(result.should_halt);
}

TEST(StepResultTest, Jump) {
    auto result = StepResult::jump(0x1000);
    EXPECT_EQ(result.err, ExecutionError::Success);
    EXPECT_FALSE(result.should_halt);
    EXPECT_EQ(result.next_pc, 0x1000u);
}

TEST(StepResultTest, Error) {
    auto result = StepResult::make_error(ExecutionError::InvalidOpcode);
    EXPECT_EQ(result.err, ExecutionError::InvalidOpcode);
    EXPECT_TRUE(result.should_halt);  // Fatal error should halt
}

// ============================================================================
// Bitwise Edge Case Tests (EXEC-004-ITER-003)
// ============================================================================

// --- AND Operation Edge Cases ---

TEST_F(ExecutorTest, Run_AND_AllZeros) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0xFFFFFFFF));
    ctx.registers().write(2, Value::from_int(0));

    auto code = make_program({
        encode_type_a(opcode::AND, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0);  // x & 0 = 0
}

TEST_F(ExecutorTest, Run_AND_AllOnes) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(-1));  // All ones

    auto code = make_program({
        encode_type_a(opcode::AND, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x12345678);  // x & -1 = x
}

TEST_F(ExecutorTest, Run_AND_MixedPatterns) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0xAAAAAAAA));  // 1010...
    ctx.registers().write(2, Value::from_int(0x55555555));  // 0101...

    auto code = make_program({
        encode_type_a(opcode::AND, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0);  // No overlapping bits
}

// --- OR Operation Edge Cases ---

TEST_F(ExecutorTest, Run_OR_AllZeros) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(0));

    auto code = make_program({
        encode_type_a(opcode::OR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x12345678);  // x | 0 = x
}

TEST_F(ExecutorTest, Run_OR_AllOnes) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(-1));

    auto code = make_program({
        encode_type_a(opcode::OR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), -1);  // x | -1 = -1
}

// --- XOR Operation Edge Cases ---

TEST_F(ExecutorTest, Run_XOR_SelfXor) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));

    auto code = make_program({
        encode_type_a(opcode::XOR, 3, 1, 1),  // R3 = R1 ^ R1
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0);  // x ^ x = 0
}

TEST_F(ExecutorTest, Run_XOR_Identity) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(0));

    auto code = make_program({
        encode_type_a(opcode::XOR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x12345678);  // x ^ 0 = x
}

TEST_F(ExecutorTest, Run_XOR_AllOnes) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(-1));

    auto code = make_program({
        encode_type_a(opcode::XOR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), ~0x12345678LL);  // x ^ -1 = ~x
}

// --- NOT Operation Edge Cases ---

TEST_F(ExecutorTest, Run_NOT_Zero) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0));

    auto code = make_program({
        encode_type_a(opcode::NOT, 3, 1, 0),  // Rs2 ignored
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), -1);  // ~0 = -1
}

TEST_F(ExecutorTest, Run_NOT_AllOnes) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(-1));

    auto code = make_program({
        encode_type_a(opcode::NOT, 3, 1, 0),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0);  // ~(-1) = 0
}

TEST_F(ExecutorTest, Run_NOT_Arch32_SignBit) {
    VmContext ctx{VmConfig::arch32()};

    ctx.registers().write(1, Value::from_int(0));

    auto code = make_program({
        encode_type_a(opcode::NOT, 3, 1, 0),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // In 32-bit mode, ~0 should be -1 (sign-extended)
    EXPECT_EQ(ctx.registers().read(3).as_integer(), -1);
}

// --- SHL (Shift Left) Edge Cases ---

TEST_F(ExecutorTest, Run_SHL_ByZero) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(0));

    auto code = make_program({
        encode_type_a(opcode::SHL, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x12345678);  // x << 0 = x
}

TEST_F(ExecutorTest, Run_SHL_LargeShift_Masked) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(1));
    ctx.registers().write(2, Value::from_int(100));  // > 47, gets masked

    auto code = make_program({
        encode_type_a(opcode::SHL, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Shift amount masked to valid range
}

// --- SHR (Shift Right Logical) Edge Cases ---

TEST_F(ExecutorTest, Run_SHR_NegativeValue_Arch32) {
    VmContext ctx{VmConfig::arch32()};

    // Negative value should be treated as unsigned for logical shift
    ctx.registers().write(1, Value::from_int(-1));  // 0xFFFFFFFF in 32-bit
    ctx.registers().write(2, Value::from_int(4));

    auto code = make_program({
        encode_type_a(opcode::SHR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Logical shift should zero-fill from left
    // 0xFFFFFFFF >> 4 (logical) = 0x0FFFFFFF = 268435455
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x0FFFFFFF);
}

TEST_F(ExecutorTest, Run_SHR_ByZero) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(0));

    auto code = make_program({
        encode_type_a(opcode::SHR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x12345678);  // x >> 0 = x
}

// --- SAR (Shift Arithmetic Right) Edge Cases ---

TEST_F(ExecutorTest, Run_SAR_PositiveByLarge) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(0x7FFFFFFF));  // Max positive 32-bit
    ctx.registers().write(2, Value::from_int(31));

    auto code = make_program({
        encode_type_a(opcode::SAR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0);  // All shifted out
}

TEST_F(ExecutorTest, Run_SAR_NegativeByLarge) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(-1024));
    ctx.registers().write(2, Value::from_int(30));

    auto code = make_program({
        encode_type_a(opcode::SAR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), -1);  // Sign extended
}

// --- ROL/ROR Boundary Cases ---

TEST_F(ExecutorTest, Run_ROL_FullRotation) {
    VmContext ctx{VmConfig::arch32()};

    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(32));  // Full rotation = no change

    auto code = make_program({
        encode_type_a(opcode::ROL, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 0x12345678);  // 32 mod 32 = 0
}

TEST_F(ExecutorTest, Run_ROR_NegativeRotation) {
    VmContext ctx{VmConfig::arch64()};

    // Negative rotation amount should be handled via modulo
    ctx.registers().write(1, Value::from_int(0x12345678));
    ctx.registers().write(2, Value::from_int(-4));

    auto code = make_program({
        encode_type_a(opcode::ROR, 3, 1, 2),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Should handle negative modulo correctly
}

// --- ANDI Immediate Boundary Cases ---

TEST_F(ExecutorTest, Run_ANDI_ZeroImmediate) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0xFFFFFFFF));

    auto code = make_program({
        encode_type_b(opcode::ANDI, 5, 0x0000),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0);  // x & 0 = 0
}

TEST_F(ExecutorTest, Run_ANDI_MaxImmediate) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0x12345678));

    auto code = make_program({
        encode_type_b(opcode::ANDI, 5, 0xFFFF),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0x5678);  // Keep lower 16 bits
}

TEST_F(ExecutorTest, Run_ANDI_SignBitImmediate) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0xFFFFFFFF));

    auto code = make_program({
        encode_type_b(opcode::ANDI, 5, 0x8000),  // Just the sign bit of 16-bit
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Note: ANDI uses zero-extension, so 0x8000 is just 32768
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0x8000);
}

// --- ORI Immediate Boundary Cases ---

TEST_F(ExecutorTest, Run_ORI_ZeroImmediate) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0x12345678));

    auto code = make_program({
        encode_type_b(opcode::ORI, 5, 0x0000),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0x12345678);  // x | 0 = x
}

TEST_F(ExecutorTest, Run_ORI_MaxImmediate) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0x12340000));

    auto code = make_program({
        encode_type_b(opcode::ORI, 5, 0xFFFF),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0x1234FFFF);  // Set lower 16 bits
}

// --- XORI Immediate Boundary Cases ---

TEST_F(ExecutorTest, Run_XORI_ZeroImmediate) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0x12345678));

    auto code = make_program({
        encode_type_b(opcode::XORI, 5, 0x0000),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0x12345678);  // x ^ 0 = x
}

TEST_F(ExecutorTest, Run_XORI_MaxImmediate) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(5, Value::from_int(0x12345678));

    auto code = make_program({
        encode_type_b(opcode::XORI, 5, 0xFFFF),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // XOR with 0xFFFF flips lower 16 bits: 0x5678 ^ 0xFFFF = 0xA987
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 0x1234A987);
}

// --- SHLI/SHRI/SARI Boundary Shift Amounts ---

TEST_F(ExecutorTest, Run_SHLI_MaxShamt6) {
    VmContext ctx{VmConfig::arch64()};

    ctx.registers().write(1, Value::from_int(1));

    auto code = make_program({
        encode_type_s(opcode::SHLI, 3, 1, 63),  // Max 6-bit value
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // In 64-bit mode, shift of 63 is valid but masked
}

TEST_F(ExecutorTest, Run_SHRI_MaxShamt6_Arch32) {
    VmContext ctx{VmConfig::arch32()};

    ctx.registers().write(1, Value::from_int(-1));  // All bits set

    auto code = make_program({
        encode_type_s(opcode::SHRI, 3, 1, 31),  // Max valid for 32-bit
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Logical shift right by 31 on 32-bit -1 should give 1
    EXPECT_EQ(ctx.registers().read(3).as_integer(), 1);
}

TEST_F(ExecutorTest, Run_SARI_MaxShamt6_Arch32) {
    VmContext ctx{VmConfig::arch32()};

    ctx.registers().write(1, Value::from_int(std::numeric_limits<std::int32_t>::min()));

    auto code = make_program({
        encode_type_s(opcode::SARI, 3, 1, 31),
        encode_type_c(opcode::HALT, 0)
    });

    Executor exec{ctx, code};
    auto result = exec.run();

    EXPECT_EQ(result, ExecutionError::Success);
    // Arithmetic shift of INT32_MIN by 31 should give -1
    EXPECT_EQ(ctx.registers().read(3).as_integer(), -1);
}

}  // namespace
}  // namespace dotvm::core
