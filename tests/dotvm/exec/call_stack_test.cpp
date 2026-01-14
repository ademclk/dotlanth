/// @file call_stack_test.cpp
/// @brief Unit tests for EXEC-007 Call Stack & Frame Management

#include <gtest/gtest.h>

#include <dotvm/exec/execution_engine.hpp>
#include <dotvm/exec/execution_context.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/core/call_stack.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/opcode.hpp>
#include <dotvm/core/value.hpp>
#include <dotvm/core/register_conventions.hpp>

#include <vector>
#include <array>

using namespace dotvm;
using namespace dotvm::exec;
using namespace dotvm::core;

// Alias for opcode namespace
namespace op = dotvm::core::opcode;

// ============================================================================
// CallStack Unit Tests
// ============================================================================

class CallStackTest : public ::testing::Test {
protected:
    CallStack stack_{1024};

    // Helper to create test saved registers
    std::array<Value, reg_range::CALLEE_SAVED_COUNT> make_saved_regs(std::int64_t base_value) {
        std::array<Value, reg_range::CALLEE_SAVED_COUNT> regs;
        for (std::size_t i = 0; i < regs.size(); ++i) {
            regs[i] = Value::from_int(base_value + static_cast<std::int64_t>(i));
        }
        return regs;
    }
};

TEST_F(CallStackTest, PushPop_SingleFrame) {
    auto saved = make_saved_regs(100);

    EXPECT_TRUE(stack_.push(42, saved, 10, 5));
    EXPECT_EQ(stack_.depth(), 1);

    auto frame_opt = stack_.pop();
    ASSERT_TRUE(frame_opt.has_value());

    const auto& frame = *frame_opt;
    EXPECT_EQ(frame.return_pc, 42);
    EXPECT_EQ(frame.base_reg, 10);
    EXPECT_EQ(frame.local_count, 5);
    EXPECT_EQ(stack_.depth(), 0);

    // Verify saved registers were preserved
    for (std::size_t i = 0; i < reg_range::CALLEE_SAVED_COUNT; ++i) {
        EXPECT_EQ(frame.saved_regs[i].as_integer(), static_cast<std::int64_t>(100 + i));
    }
}

TEST_F(CallStackTest, PushPop_MultipleFrames) {
    auto saved1 = make_saved_regs(100);
    auto saved2 = make_saved_regs(200);
    auto saved3 = make_saved_regs(300);

    EXPECT_TRUE(stack_.push(10, saved1));
    EXPECT_TRUE(stack_.push(20, saved2));
    EXPECT_TRUE(stack_.push(30, saved3));
    EXPECT_EQ(stack_.depth(), 3);

    // Pop in LIFO order
    auto frame3 = stack_.pop();
    ASSERT_TRUE(frame3.has_value());
    EXPECT_EQ(frame3->return_pc, 30);
    EXPECT_EQ(frame3->saved_regs[0].as_integer(), 300);

    auto frame2 = stack_.pop();
    ASSERT_TRUE(frame2.has_value());
    EXPECT_EQ(frame2->return_pc, 20);
    EXPECT_EQ(frame2->saved_regs[0].as_integer(), 200);

    auto frame1 = stack_.pop();
    ASSERT_TRUE(frame1.has_value());
    EXPECT_EQ(frame1->return_pc, 10);
    EXPECT_EQ(frame1->saved_regs[0].as_integer(), 100);

    EXPECT_EQ(stack_.depth(), 0);
}

TEST_F(CallStackTest, Overflow_ReturnsError) {
    CallStack small_stack{3};
    auto saved = make_saved_regs(0);

    EXPECT_TRUE(small_stack.push(1, saved));
    EXPECT_TRUE(small_stack.push(2, saved));
    EXPECT_TRUE(small_stack.push(3, saved));
    EXPECT_EQ(small_stack.depth(), 3);

    // Fourth push should fail
    EXPECT_FALSE(small_stack.push(4, saved));
    EXPECT_EQ(small_stack.depth(), 3);  // No change
}

TEST_F(CallStackTest, PopEmpty_ReturnsNullopt) {
    EXPECT_TRUE(stack_.empty());
    auto frame = stack_.pop();
    EXPECT_FALSE(frame.has_value());
}

TEST_F(CallStackTest, Top_ReturnsTopFrame) {
    EXPECT_EQ(stack_.top(), nullptr);

    auto saved = make_saved_regs(42);
    EXPECT_TRUE(stack_.push(100, saved));

    const auto* top = stack_.top();
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->return_pc, 100);
    EXPECT_EQ(stack_.depth(), 1);  // top() doesn't pop
}

TEST_F(CallStackTest, WouldOverflow_ReturnsCorrectState) {
    CallStack small_stack{2};
    auto saved = make_saved_regs(0);

    EXPECT_FALSE(small_stack.would_overflow());
    EXPECT_TRUE(small_stack.push(1, saved));
    EXPECT_FALSE(small_stack.would_overflow());
    EXPECT_TRUE(small_stack.push(2, saved));
    EXPECT_TRUE(small_stack.would_overflow());
}

TEST_F(CallStackTest, Clear_EmptiesStack) {
    auto saved = make_saved_regs(0);
    EXPECT_TRUE(stack_.push(1, saved));
    EXPECT_TRUE(stack_.push(2, saved));
    EXPECT_EQ(stack_.depth(), 2);

    stack_.clear();
    EXPECT_TRUE(stack_.empty());
    EXPECT_EQ(stack_.depth(), 0);
}

// ============================================================================
// ExecutionEngine Integration Tests
// ============================================================================

class CallStackExecutionTest : public ::testing::Test {
protected:
    VmContext ctx_;
    ExecutionEngine engine_{ctx_};

    // Helper to create Type C instruction (CALL, JMP)
    static std::uint32_t make_call(std::int32_t offset) {
        return encode_type_c(op::CALL, offset);
    }

    // Helper to create RET instruction
    static std::uint32_t make_ret() {
        return encode_type_c(op::RET, 0);
    }

    // Helper to create HALT instruction
    static std::uint32_t make_halt() {
        return encode_type_c(op::HALT, 0);
    }

    // Helper to create Type B instruction (MOVI)
    // Note: MOVI (0x81) is in exec::opcode, not core::opcode
    static std::uint32_t make_movi(std::uint8_t rd, std::int16_t imm) {
        constexpr std::uint8_t MOVI = 0x81;
        return encode_type_b(MOVI, rd, static_cast<std::uint16_t>(imm));
    }

    // Helper to create NOP instruction
    static std::uint32_t make_nop() {
        return encode_type_c(op::NOP, 0);
    }

    // Helper to run code
    ExecResult run(const std::vector<std::uint32_t>& code) {
        return engine_.execute(code.data(), code.size(), 0, {});
    }
};

TEST_F(CallStackExecutionTest, CallAndRet_PreservesCalleeSavedRegisters) {
    // Setup: Set distinct values in callee-saved registers R16-R31
    for (std::uint8_t i = 0; i < 16; ++i) {
        ctx_.registers().write(reg_range::CALLEE_SAVED_START + i,
                               Value::from_int(1000 + i));
    }

    // Code:
    // 0: CALL +2       -> jumps to instruction 2 (function)
    // 1: HALT
    // 2: MOVI R16, 999 -> modifies R16 (callee-saved)
    // 3: RET           -> should restore R16-R31
    std::vector<std::uint32_t> code = {
        make_call(2),       // 0: CALL to instruction 2
        make_halt(),        // 1: HALT (return point)
        make_movi(16, 999), // 2: function: modify R16
        make_ret()          // 3: RET
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    // Verify callee-saved registers are restored
    for (std::uint8_t i = 0; i < 16; ++i) {
        EXPECT_EQ(ctx_.registers().read(reg_range::CALLEE_SAVED_START + i).as_integer(),
                  1000 + i)
            << "Register R" << static_cast<int>(reg_range::CALLEE_SAVED_START + i)
            << " not properly restored";
    }
}

TEST_F(CallStackExecutionTest, NestedCalls_CorrectRegisterRestoration) {
    // Setup: Set initial values in R16
    ctx_.registers().write(16, Value::from_int(100));

    // Code structure:
    // 0: MOVI R16, 1    ; main: set R16 = 1
    // 1: CALL +4        ; call func1 at instruction 5
    // 2: HALT           ; (should have R16 = 1 after return)
    // 3: NOP            ; padding
    // 4: NOP            ; padding
    // 5: MOVI R16, 2    ; func1: set R16 = 2
    // 6: CALL +3        ; call func2 at instruction 9
    // 7: RET            ; return to main (R16 should be 2)
    // 8: NOP            ; padding
    // 9: MOVI R16, 3    ; func2: set R16 = 3
    // 10: RET           ; return to func1 (R16 should be 2)

    std::vector<std::uint32_t> code = {
        make_movi(16, 1),   // 0: main
        make_call(4),       // 1: call func1 (at 5)
        make_halt(),        // 2: end
        make_nop(),  // 3: padding
        make_nop(),  // 4: padding
        make_movi(16, 2),   // 5: func1
        make_call(3),       // 6: call func2 (at 9)
        make_ret(),         // 7: return from func1
        make_nop(),  // 8: padding
        make_movi(16, 3),   // 9: func2
        make_ret()          // 10: return from func2
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    // After all returns, R16 should be restored to 1 (main's value before CALL)
    EXPECT_EQ(ctx_.registers().read(16).as_integer(), 1);
}

TEST_F(CallStackExecutionTest, StackOverflow_ReturnsError) {
    // Create a context with small max call depth
    VmConfig config;
    config.resource_limits.max_call_depth = 3;
    VmContext small_ctx{config};
    ExecutionEngine small_engine{small_ctx};

    // Code: recursive function that overflows
    // 0: CALL 0    ; call self (infinite recursion)
    // 1: HALT      ; never reached
    std::vector<std::uint32_t> code = {
        make_call(0),   // 0: recursive call
        make_halt()     // 1: unreached
    };

    auto result = small_engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::StackOverflow);
}

TEST_F(CallStackExecutionTest, Ret_FallbackToR1WhenStackEmpty) {
    // Setup: R1 contains return address, no CALL was made
    ctx_.registers().write(1, Value::from_int(2));  // Return to instruction 2

    // Code:
    // 0: RET       ; pop from empty stack, fallback to R1 (2)
    // 1: MOVI R2, 999  ; skipped
    // 2: MOVI R2, 42   ; executed
    // 3: HALT
    std::vector<std::uint32_t> code = {
        make_ret(),         // 0: return using R1
        make_movi(2, 999),  // 1: skipped
        make_movi(2, 42),   // 2: executed
        make_halt()         // 3: halt
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 42);
}

TEST_F(CallStackExecutionTest, Call_StillWritesToR1ForCompatibility) {
    // Code:
    // 0: CALL +2   ; call function at 2, should write return addr to R1
    // 1: HALT      ; return point (instruction 1)
    // 2: HALT      ; function just halts immediately
    std::vector<std::uint32_t> code = {
        make_call(2),   // 0: CALL
        make_halt(),    // 1: return point
        make_halt()     // 2: function
    };

    // Run but stop immediately (function halts)
    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    // R1 should contain return address (instruction 1)
    EXPECT_EQ(ctx_.registers().read(1).as_integer(), 1);
}

TEST_F(CallStackExecutionTest, CallStack_DepthTracking) {
    EXPECT_EQ(ctx_.call_stack().depth(), 0);

    // Code: two nested calls
    // 0: CALL +3   ; call func1 at 3
    // 1: HALT
    // 2: NOP
    // 3: CALL +2   ; func1 calls func2 at 5
    // 4: RET       ; return from func1
    // 5: RET       ; func2 returns immediately
    std::vector<std::uint32_t> code = {
        make_call(3),   // 0
        make_halt(),    // 1
        make_nop(),  // 2
        make_call(2),   // 3
        make_ret(),     // 4
        make_ret()      // 5
    };

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    // After all returns, stack should be empty
    EXPECT_EQ(ctx_.call_stack().depth(), 0);
}

// ============================================================================
// CallFrame Tests
// ============================================================================

TEST(CallFrameTest, DefaultConstruction) {
    CallFrame frame;
    EXPECT_EQ(frame.return_pc, 0);
    EXPECT_EQ(frame.base_reg, 0);
    EXPECT_EQ(frame.local_count, 0);
    for (const auto& reg : frame.saved_regs) {
        EXPECT_EQ(reg.as_integer(), 0);
    }
}

TEST(CallFrameTest, Equality) {
    CallFrame frame1;
    frame1.return_pc = 42;
    frame1.base_reg = 10;
    frame1.local_count = 5;

    CallFrame frame2;
    frame2.return_pc = 42;
    frame2.base_reg = 10;
    frame2.local_count = 5;

    EXPECT_EQ(frame1, frame2);

    frame2.return_pc = 43;
    EXPECT_NE(frame1, frame2);
}

TEST(CallFrameTest, SizeVerification) {
    // CallFrame should be 144 bytes: 8 + 1 + 1 + 6 + 128
    EXPECT_EQ(sizeof(CallFrame), 144);
}
