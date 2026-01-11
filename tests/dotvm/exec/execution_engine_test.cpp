/// @file execution_engine_test.cpp
/// @brief Unit tests for the computed-goto dispatch execution engine

#include <gtest/gtest.h>

#include <dotvm/exec/execution_engine.hpp>
#include <dotvm/exec/execution_context.hpp>
#include <dotvm/exec/dispatch_macros.hpp>
#include <dotvm/exec/profiling.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/value.hpp>

#include <vector>
#include <array>

using namespace dotvm;
using namespace dotvm::exec;
using namespace dotvm::core;

// ============================================================================
// Test Fixtures
// ============================================================================

class ExecutionEngineTest : public ::testing::Test {
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

    // Helper to create Type C instruction
    static std::uint32_t make_type_c(std::uint8_t op, std::int32_t offset) {
        return encode_type_c(op, offset);
    }

    // Helper to run code
    ExecResult run(const std::vector<std::uint32_t>& code,
                   const std::vector<Value>& const_pool = {}) {
        return engine_.execute(code.data(), code.size(), 0, const_pool);
    }
};

// ============================================================================
// ExecutionContext Tests
// ============================================================================

TEST(ExecutionContextTest, SizeIsCacheLine) {
    EXPECT_EQ(sizeof(ExecutionContext), 64);
    EXPECT_EQ(alignof(ExecutionContext), 64);
}

TEST(ExecutionContextTest, DefaultConstruction) {
    ExecutionContext ctx;
    EXPECT_EQ(ctx.code, nullptr);
    EXPECT_EQ(ctx.pc, 0);
    EXPECT_EQ(ctx.code_size, 0);
    EXPECT_FALSE(ctx.halted);
    EXPECT_EQ(ctx.error, ExecResult::Success);
}

TEST(ExecutionContextTest, Reset) {
    ExecutionContext ctx;
    std::array<std::uint32_t, 4> code{};

    ctx.reset(code.data(), code.size(), 2);

    EXPECT_EQ(ctx.code, code.data());
    EXPECT_EQ(ctx.code_size, 4);
    EXPECT_EQ(ctx.pc, 2);
    EXPECT_FALSE(ctx.halted);
}

TEST(ExecutionContextTest, JumpRelative) {
    ExecutionContext ctx;
    ctx.pc = 10;

    ctx.jump_relative(5);
    EXPECT_EQ(ctx.pc, 15);

    ctx.jump_relative(-3);
    EXPECT_EQ(ctx.pc, 12);
}

TEST(ExecutionContextTest, JumpTo) {
    ExecutionContext ctx;
    ctx.pc = 10;

    ctx.jump_to(42);
    EXPECT_EQ(ctx.pc, 42);
}

TEST(ExecutionContextTest, HaltWithError) {
    ExecutionContext ctx;

    ctx.halt_with_error(ExecResult::InvalidOpcode);

    EXPECT_TRUE(ctx.halted);
    EXPECT_EQ(ctx.error, ExecResult::InvalidOpcode);
}

// ============================================================================
// Basic Execution Tests
// ============================================================================

TEST_F(ExecutionEngineTest, HaltImmediately) {
    std::vector<std::uint32_t> code = {
        make_type_c(opcode::HALT, 0)  // HALT
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_TRUE(engine_.halted());
    EXPECT_EQ(engine_.instructions_executed(), 1);
}

TEST_F(ExecutionEngineTest, NopThenHalt) {
    std::vector<std::uint32_t> code = {
        make_type_c(opcode::NOP, 0),   // NOP
        make_type_c(opcode::NOP, 0),   // NOP
        make_type_c(opcode::HALT, 0)   // HALT
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(engine_.instructions_executed(), 3);
}

TEST_F(ExecutionEngineTest, EmptyCodeOutOfBounds) {
    std::vector<std::uint32_t> code;  // Empty

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::OutOfBounds);
}

// ============================================================================
// Arithmetic Tests
// ============================================================================

TEST_F(ExecutionEngineTest, AddTwoRegisters) {
    // R1 = 10, R2 = 20, R3 = R1 + R2
    ctx_.registers().write(1, Value::from_int(10));
    ctx_.registers().write(2, Value::from_int(20));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::ADD, 3, 1, 2),  // ADD R3, R1, R2
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 30);
}

TEST_F(ExecutionEngineTest, SubTwoRegisters) {
    ctx_.registers().write(1, Value::from_int(50));
    ctx_.registers().write(2, Value::from_int(20));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::SUB, 3, 1, 2),  // SUB R3, R1, R2
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 30);
}

TEST_F(ExecutionEngineTest, MulTwoRegisters) {
    ctx_.registers().write(1, Value::from_int(6));
    ctx_.registers().write(2, Value::from_int(7));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::MUL, 3, 1, 2),  // MUL R3, R1, R2
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 42);
}

TEST_F(ExecutionEngineTest, DivTwoRegisters) {
    ctx_.registers().write(1, Value::from_int(100));
    ctx_.registers().write(2, Value::from_int(5));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::DIV, 3, 1, 2),  // DIV R3, R1, R2
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 20);
}

TEST_F(ExecutionEngineTest, ModTwoRegisters) {
    ctx_.registers().write(1, Value::from_int(17));
    ctx_.registers().write(2, Value::from_int(5));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::MOD, 3, 1, 2),  // MOD R3, R1, R2
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 2);
}

TEST_F(ExecutionEngineTest, AddImmediate) {
    ctx_.registers().write(1, Value::from_int(100));

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::ADDI, 1, 42),  // ADDI R1, 42
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 142);
}

TEST_F(ExecutionEngineTest, NegRegister) {
    ctx_.registers().write(1, Value::from_int(42));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::NEG, 2, 1, 0),  // NEG R2, R1
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), -42);
}

// ============================================================================
// Bitwise Tests
// ============================================================================

TEST_F(ExecutionEngineTest, BitwiseAnd) {
    ctx_.registers().write(1, Value::from_int(0xFF00));
    ctx_.registers().write(2, Value::from_int(0x0FF0));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::AND, 3, 1, 2),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 0x0F00);
}

TEST_F(ExecutionEngineTest, BitwiseOr) {
    ctx_.registers().write(1, Value::from_int(0xF000));
    ctx_.registers().write(2, Value::from_int(0x000F));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::OR, 3, 1, 2),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 0xF00F);
}

TEST_F(ExecutionEngineTest, BitwiseXor) {
    ctx_.registers().write(1, Value::from_int(0xFF00));
    ctx_.registers().write(2, Value::from_int(0xF0F0));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::XOR, 3, 1, 2),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 0x0FF0);
}

TEST_F(ExecutionEngineTest, ShiftLeft) {
    ctx_.registers().write(1, Value::from_int(1));
    ctx_.registers().write(2, Value::from_int(4));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::SHL, 3, 1, 2),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 16);
}

TEST_F(ExecutionEngineTest, ShiftRightLogical) {
    ctx_.registers().write(1, Value::from_int(32));
    ctx_.registers().write(2, Value::from_int(2));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::SHR, 3, 1, 2),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 8);
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST_F(ExecutionEngineTest, CompareEqual_True) {
    ctx_.registers().write(1, Value::from_int(42));
    ctx_.registers().write(2, Value::from_int(42));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::EQ, 3, 1, 2),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 1);
}

TEST_F(ExecutionEngineTest, CompareEqual_False) {
    ctx_.registers().write(1, Value::from_int(42));
    ctx_.registers().write(2, Value::from_int(43));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::EQ, 3, 1, 2),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 0);
}

TEST_F(ExecutionEngineTest, CompareLessThan_True) {
    ctx_.registers().write(1, Value::from_int(10));
    ctx_.registers().write(2, Value::from_int(20));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::LT, 3, 1, 2),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 1);
}

TEST_F(ExecutionEngineTest, CompareGreaterThan_True) {
    ctx_.registers().write(1, Value::from_int(20));
    ctx_.registers().write(2, Value::from_int(10));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::GT, 3, 1, 2),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 1);
}

// ============================================================================
// Data Move Tests
// ============================================================================

TEST_F(ExecutionEngineTest, MovRegister) {
    ctx_.registers().write(1, Value::from_int(999));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::MOV, 2, 1, 0),  // MOV R2, R1
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 999);
}

TEST_F(ExecutionEngineTest, MovImmediate) {
    std::vector<std::uint32_t> code = {
        make_type_b(opcode::MOVI, 1, 12345),  // MOVI R1, 12345
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 12345);
}

TEST_F(ExecutionEngineTest, MovImmediateNegative) {
    // Test signed immediate (using two's complement)
    std::vector<std::uint32_t> code = {
        make_type_b(opcode::MOVI, 1, static_cast<std::uint16_t>(-100)),
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), -100);
}

TEST_F(ExecutionEngineTest, LoadConstant) {
    std::vector<Value> const_pool = {
        Value::from_int(111),
        Value::from_int(222),
        Value::from_int(333)
    };

    std::vector<std::uint32_t> code = {
        make_type_b(opcode::LOADK, 1, 0),  // LOADK R1, const[0]
        make_type_b(opcode::LOADK, 2, 1),  // LOADK R2, const[1]
        make_type_b(opcode::LOADK, 3, 2),  // LOADK R3, const[2]
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code, const_pool);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 111);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 222);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 333);
}

// ============================================================================
// Control Flow Tests
// ============================================================================

TEST_F(ExecutionEngineTest, UnconditionalJump) {
    std::vector<std::uint32_t> code = {
        make_type_c(opcode::JMP, 2),       // 0: JMP +2 (to HALT)
        make_type_b(opcode::MOVI, 1, 999), // 1: MOVI R1, 999 (skipped)
        make_type_c(opcode::HALT, 0)       // 2: HALT
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    // R1 should still be zero (MOVI was skipped)
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 0);
}

TEST_F(ExecutionEngineTest, BranchEqual_Taken) {
    ctx_.registers().write(1, Value::from_int(42));
    ctx_.registers().write(2, Value::from_int(42));

    std::vector<std::uint32_t> code = {
        // BEQ uses Type A format: rd=cond1, rs1=cond2, rs2=offset
        make_type_a(opcode::BEQ, 1, 2, 2),  // 0: BEQ R1, R2, +2 (to HALT)
        make_type_b(opcode::MOVI, 3, 999),  // 1: MOVI R3, 999 (skipped)
        make_type_c(opcode::HALT, 0)        // 2: HALT
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 0);  // Skipped
}

TEST_F(ExecutionEngineTest, BranchEqual_NotTaken) {
    ctx_.registers().write(1, Value::from_int(42));
    ctx_.registers().write(2, Value::from_int(43));  // Different

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::BEQ, 1, 2, 2),  // 0: BEQ R1, R2, +2 (not taken)
        make_type_b(opcode::MOVI, 3, 999),  // 1: MOVI R3, 999 (executed)
        make_type_c(opcode::HALT, 0)        // 2: HALT
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 999);  // Executed
}

TEST_F(ExecutionEngineTest, BranchNotEqual_Taken) {
    ctx_.registers().write(1, Value::from_int(42));
    ctx_.registers().write(2, Value::from_int(43));  // Different

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::BNE, 1, 2, 2),  // 0: BNE R1, R2, +2
        make_type_b(opcode::MOVI, 3, 999),  // 1: MOVI R3, 999 (skipped)
        make_type_c(opcode::HALT, 0)        // 2: HALT
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(3).as_integer(), 0);  // Skipped
}

// ============================================================================
// Loop Test (Counter)
// ============================================================================

TEST_F(ExecutionEngineTest, SimpleCounterLoop) {
    // Count from 0 to 5
    // R1 = counter, R2 = limit, R3 = increment
    std::vector<std::uint32_t> code = {
        make_type_b(opcode::MOVI, 1, 0),    // 0: MOVI R1, 0  (counter)
        make_type_b(opcode::MOVI, 2, 5),    // 1: MOVI R2, 5  (limit)
        make_type_b(opcode::MOVI, 3, 1),    // 2: MOVI R3, 1  (increment)
        // loop:
        make_type_a(opcode::ADD, 1, 1, 3),  // 3: ADD R1, R1, R3
        make_type_a(opcode::LT, 4, 1, 2),   // 4: LT R4, R1, R2
        // Branch back if R4 != 0 (offset -2 from instruction 5 to 3)
        make_type_a(opcode::BNE, 4, 0, static_cast<std::uint8_t>(-2)),  // 5: BNE R4, R0, -2
        make_type_c(opcode::HALT, 0)        // 6: HALT
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 5);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(ExecutionEngineTest, InvalidOpcode) {
    std::vector<std::uint32_t> code = {
        0x07000000,  // Invalid opcode 0x07 (undefined in arithmetic range)
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::InvalidOpcode);
}

TEST_F(ExecutionEngineTest, ReservedOpcode) {
    std::vector<std::uint32_t> code = {
        0x90000000,  // Reserved opcode 0x90
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::CfiViolation);
}

// ============================================================================
// Register Zero Tests
// ============================================================================

TEST_F(ExecutionEngineTest, WriteToR0Ignored) {
    std::vector<std::uint32_t> code = {
        make_type_b(opcode::MOVI, 0, 999),  // MOVI R0, 999 (should be ignored)
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(0).as_integer(), 0);  // Still zero
}

TEST_F(ExecutionEngineTest, ReadFromR0AlwaysZero) {
    ctx_.registers().write(1, Value::from_int(100));

    std::vector<std::uint32_t> code = {
        make_type_a(opcode::ADD, 2, 1, 0),  // ADD R2, R1, R0 (R0 is 0)
        make_type_c(opcode::HALT, 0)
    };

    auto result = run(code);

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 100);  // 100 + 0
}

// ============================================================================
// Profiling Tests
// ============================================================================

TEST(ProfilingTest, RdtscReturnsValue) {
    auto start = rdtsc();
    volatile int x = 0;
    for (int i = 0; i < 1000; ++i) {
        x += i;
    }
    auto end = rdtsc();

    // On x86-64, this should return non-zero
    // On unsupported platforms, it returns 0
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
    EXPECT_GT(end, start);
#else
    EXPECT_EQ(start, 0);
    EXPECT_EQ(end, 0);
#endif
}

TEST(ProfilingTest, ScopedCycleCounter) {
    std::uint64_t cycles = 0;
    {
        ScopedCycleCounter counter(cycles);
        volatile int x = 0;
        for (int i = 0; i < 100; ++i) {
            x += i;
        }
    }
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
    EXPECT_GT(cycles, 0);
#endif
}

TEST(ProfilingTest, DispatchStatsAvgCycles) {
    DispatchStats stats;
    stats.total_cycles = 1000;
    stats.instructions_executed = 100;

    EXPECT_DOUBLE_EQ(stats.avg_cycles_per_instruction(), 10.0);
}

TEST(ProfilingTest, DispatchStatsBranchRatio) {
    DispatchStats stats;
    stats.taken_branches = 75;
    stats.not_taken_branches = 25;

    EXPECT_DOUBLE_EQ(stats.branch_taken_ratio(), 0.75);
}

// ============================================================================
// ExecResult String Conversion Tests
// ============================================================================

TEST(ExecResultTest, ToStringConversion) {
    EXPECT_STREQ(to_string(ExecResult::Success), "Success");
    EXPECT_STREQ(to_string(ExecResult::InvalidOpcode), "InvalidOpcode");
    EXPECT_STREQ(to_string(ExecResult::CfiViolation), "CfiViolation");
    EXPECT_STREQ(to_string(ExecResult::OutOfBounds), "OutOfBounds");
}
