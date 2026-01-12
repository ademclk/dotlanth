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

}  // namespace
}  // namespace dotvm::core
