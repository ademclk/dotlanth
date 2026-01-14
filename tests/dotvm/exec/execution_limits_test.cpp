/// @file execution_limits_test.cpp
/// @brief Unit tests for EXEC-008: Execution Limit Enforcement

#include <gtest/gtest.h>

#include <dotvm/exec/execution_engine.hpp>
#include <dotvm/exec/execution_context.hpp>
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

class ExecutionLimitsTest : public ::testing::Test {
protected:
    // Helper to create JMP instruction (unconditional jump)
    static std::uint32_t make_jmp(std::int32_t offset) {
        return encode_type_c(0x40, offset);  // JMP opcode = 0x40
    }

    // Helper to create HALT instruction
    static std::uint32_t make_halt() {
        return encode_type_c(0x5F, 0);  // HALT opcode = 0x5F
    }

    // Helper to create ADDI instruction (add immediate)
    static std::uint32_t make_addi(std::uint8_t rd, std::uint16_t imm) {
        return encode_type_b(0x08, rd, imm);  // ADDI opcode = 0x08
    }

    // Helper to create NOP instruction
    static std::uint32_t make_nop() {
        return encode_type_c(0xF0, 0);  // NOP opcode = 0xF0
    }
};

// ============================================================================
// ExecutionContext Tests
// ============================================================================

TEST_F(ExecutionLimitsTest, ExecutionContextSizeStillCacheLine) {
    // Verify that adding max_instructions didn't break cache alignment
    EXPECT_EQ(sizeof(ExecutionContext), 64);
    EXPECT_EQ(alignof(ExecutionContext), 64);
}

TEST_F(ExecutionLimitsTest, ExecutionContextDefaultsToUnlimited) {
    ExecutionContext ctx;
    EXPECT_EQ(ctx.max_instructions, 0);  // 0 = unlimited
}

TEST_F(ExecutionLimitsTest, ExecutionContextResetSetsLimit) {
    ExecutionContext ctx;
    std::array<std::uint32_t, 4> code{};

    ctx.reset(code.data(), code.size(), 0, 1000);
    EXPECT_EQ(ctx.max_instructions, 1000);
    EXPECT_EQ(ctx.instructions_executed, 0);
}

TEST_F(ExecutionLimitsTest, ExecResultHasExecutionLimit) {
    // Verify ExecutionLimit error code exists
    EXPECT_EQ(static_cast<std::uint8_t>(ExecResult::ExecutionLimit), 10);
}

TEST_F(ExecutionLimitsTest, ExecResultToStringReturnsExecutionLimit) {
    EXPECT_STREQ(to_string(ExecResult::ExecutionLimit), "ExecutionLimit");
}

// ============================================================================
// Unlimited Execution Tests
// ============================================================================

TEST_F(ExecutionLimitsTest, UnlimitedExecutionCompletes) {
    // Create VM with unlimited execution (default)
    VmContext ctx;
    ExecutionEngine engine{ctx};

    // Create code with 1000 NOPs + HALT
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 1000; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(engine.context().instructions_executed, 1001);  // 1000 NOPs + 1 HALT
}

// ============================================================================
// Exact Limit Tests
// ============================================================================

TEST_F(ExecutionLimitsTest, ExactLimitHitsExecutionLimit) {
    // Create VM with sandboxed config (1M instruction limit)
    VmConfig config;
    config.resource_limits.max_instructions = 100;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create code with 200 NOPs (more than limit)
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 200; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::ExecutionLimit);
    EXPECT_EQ(engine.context().instructions_executed, 100);
    EXPECT_TRUE(engine.context().halted);
}

TEST_F(ExecutionLimitsTest, UnderLimitCompletes) {
    // Create VM with limit of 1000
    VmConfig config;
    config.resource_limits.max_instructions = 1000;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create code with 500 NOPs (under limit)
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 500; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(engine.context().instructions_executed, 501);  // 500 NOPs + 1 HALT
}

TEST_F(ExecutionLimitsTest, ExactlyAtLimitCompletes) {
    // Create VM with limit of 100
    VmConfig config;
    config.resource_limits.max_instructions = 101;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create code with exactly 100 NOPs + HALT (101 total)
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 100; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(engine.context().instructions_executed, 101);
}

// ============================================================================
// Sandboxed Config Tests
// ============================================================================

TEST_F(ExecutionLimitsTest, SandboxedConfigEnforcesLimit) {
    // VmConfig::sandboxed() should set max_instructions = 1M
    VmContext ctx{VmConfig::sandboxed()};
    ExecutionEngine engine{ctx};

    EXPECT_EQ(ctx.config().resource_limits.max_instructions, 1'000'000);

    // Create infinite loop: JMP 0 (jump to self)
    // Note: JMP offset is relative to next instruction, so JMP 0 loops back to itself
    std::vector<std::uint32_t> code = {make_jmp(0)};

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::ExecutionLimit);
    EXPECT_EQ(engine.context().instructions_executed, 1'000'000);
}

TEST_F(ExecutionLimitsTest, RestrictedLimitsEnforcesOneMillionInstructions) {
    VmConfig config;
    config.resource_limits = ResourceLimits::restricted();
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    EXPECT_EQ(ctx.config().resource_limits.max_instructions, 1'000'000);
}

// ============================================================================
// Infinite Loop Detection Tests
// ============================================================================

TEST_F(ExecutionLimitsTest, InfiniteLoopDetectedWithLimit) {
    // Create VM with limit
    VmConfig config;
    config.resource_limits.max_instructions = 50;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create infinite loop: JMP 0 (jumps to itself)
    std::vector<std::uint32_t> code = {make_jmp(0)};

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::ExecutionLimit);
    EXPECT_EQ(engine.context().instructions_executed, 50);
}

TEST_F(ExecutionLimitsTest, TightLoopHitsLimit) {
    // Create VM with limit
    VmConfig config;
    config.resource_limits.max_instructions = 100;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create tight loop:
    // 0: ADDI R0, 1
    // 1: JMP -1  (jump back to ADDI, offset is -1 since PC is at 2)
    std::vector<std::uint32_t> code = {
        make_addi(0, 1),
        make_jmp(-1)
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::ExecutionLimit);
    EXPECT_EQ(engine.context().instructions_executed, 100);
}

// ============================================================================
// Step Function Tests
// ============================================================================

TEST_F(ExecutionLimitsTest, StepEnforcesLimit) {
    // Create VM with limit of 5
    VmConfig config;
    config.resource_limits.max_instructions = 5;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create code with 10 NOPs
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 10; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    // Run execute() - should hit limit after 5 instructions
    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::ExecutionLimit);
    EXPECT_EQ(engine.context().instructions_executed, 5);
    EXPECT_TRUE(engine.context().halted);
}

// ============================================================================
// Custom Limit Tests
// ============================================================================

TEST_F(ExecutionLimitsTest, CustomLimitEnforced) {
    // Create VM with custom limit
    VmConfig config;
    config.resource_limits.max_instructions = 7;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create code with 10 NOPs
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 10; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::ExecutionLimit);
    EXPECT_EQ(engine.context().instructions_executed, 7);
}

TEST_F(ExecutionLimitsTest, ZeroLimitMeansUnlimited) {
    // Create VM with explicit zero limit
    VmConfig config;
    config.resource_limits.max_instructions = 0;  // Unlimited
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create code with 10000 NOPs
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 10000; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(engine.context().instructions_executed, 10001);
}

// ============================================================================
// Re-execution Tests
// ============================================================================

TEST_F(ExecutionLimitsTest, LimitResetBetweenExecutions) {
    // Create VM with limit
    VmConfig config;
    config.resource_limits.max_instructions = 50;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create code with 30 NOPs
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 30; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    // First execution should succeed
    auto result1 = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result1, ExecResult::Success);
    EXPECT_EQ(engine.context().instructions_executed, 31);

    // Reset and execute again (counter should reset)
    engine.reset();
    auto result2 = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result2, ExecResult::Success);
    EXPECT_EQ(engine.context().instructions_executed, 31);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(ExecutionLimitsTest, LimitOfOneHaltsImmediately) {
    // Create VM with limit of 1
    VmConfig config;
    config.resource_limits.max_instructions = 1;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create code with NOPs
    std::vector<std::uint32_t> code = {
        make_nop(), make_nop(), make_halt()
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::ExecutionLimit);
    EXPECT_EQ(engine.context().instructions_executed, 1);
}

TEST_F(ExecutionLimitsTest, HaltDoesNotIncrementPastLimit) {
    // Create VM with exact limit
    VmConfig config;
    config.resource_limits.max_instructions = 10;
    VmContext ctx{config};
    ExecutionEngine engine{ctx};

    // Create code with exactly 10 instructions
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 9; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(engine.context().instructions_executed, 10);
}
