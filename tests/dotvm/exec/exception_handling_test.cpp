/// @file exception_handling_test.cpp
/// @brief Unit tests for EXEC-011: Exception Handling

#include <array>
#include <vector>

#include <dotvm/core/exception_context.hpp>
#include <dotvm/core/exception_types.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/opcode.hpp>
#include <dotvm/core/value.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/exec/execution_context.hpp>
#include <dotvm/exec/execution_engine.hpp>

#include <gtest/gtest.h>

using namespace dotvm;
using namespace dotvm::exec;
using namespace dotvm::core;

// Use exec::opcode namespace for instruction encoding
namespace op = dotvm::exec::opcode;

// ============================================================================
// Test Fixtures
// ============================================================================

class ExceptionHandlingTest : public ::testing::Test {
protected:
    // Helper to create NOP instruction
    static std::uint32_t make_nop() { return encode_type_c(op::NOP, 0); }

    // Helper to create HALT instruction
    static std::uint32_t make_halt() { return encode_type_c(op::HALT, 0); }

    // Helper to create TRY instruction
    // Format: [TRY][handler_offset16][catch_types8]
    static std::uint32_t make_try(std::int16_t handler_offset, std::uint8_t catch_types) {
        return encode_type_b(op::TRY, catch_types, static_cast<std::uint16_t>(handler_offset));
    }

    // Helper to create CATCH instruction
    static std::uint32_t make_catch() { return encode_type_c(op::CATCH, 0); }

    // Helper to create THROW instruction
    // Format: [THROW][Rtype][Rpayload][unused]
    static std::uint32_t make_throw(std::uint8_t rtype, std::uint8_t rpayload) {
        return encode_type_a(op::THROW, rtype, rpayload, 0);
    }

    // Helper to create ENDTRY instruction
    static std::uint32_t make_endtry() { return encode_type_c(op::ENDTRY, 0); }

    // Helper to create MOVI instruction (move immediate)
    static std::uint32_t make_movi(std::uint8_t rd, std::uint16_t imm) {
        return encode_type_b(op::MOVI, rd, imm);
    }

    // Helper to create ADDI instruction
    static std::uint32_t make_addi(std::uint8_t rd, std::uint16_t imm) {
        return encode_type_b(op::ADDI, rd, imm);
    }

    // Helper to create JMP instruction
    static std::uint32_t make_jmp(std::int32_t offset) { return encode_type_c(op::JMP, offset); }

    // Helper to create CALL instruction
    static std::uint32_t make_call(std::int32_t offset) { return encode_type_c(op::CALL, offset); }

    // Helper to create RET instruction
    static std::uint32_t make_ret() { return encode_type_c(op::RET, 0); }
};

// ============================================================================
// ExceptionContext Unit Tests
// ============================================================================

TEST_F(ExceptionHandlingTest, ExceptionContextDefaultsEmpty) {
    ExceptionContext ctx;
    EXPECT_TRUE(ctx.empty());
    EXPECT_EQ(ctx.depth(), 0);
    EXPECT_FALSE(ctx.has_pending_exception());
    EXPECT_EQ(ctx.max_depth(), DEFAULT_MAX_EXCEPTION_DEPTH);
}

TEST_F(ExceptionHandlingTest, ExceptionContextPushPopFrame) {
    ExceptionContext ctx;

    auto frame = ExceptionFrame::make(100, 0, 0, catch_mask::ALL);
    EXPECT_TRUE(ctx.push_frame(frame));
    EXPECT_EQ(ctx.depth(), 1);
    EXPECT_FALSE(ctx.empty());

    auto popped = ctx.pop_frame();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(popped->handler_pc, 100);
    EXPECT_TRUE(ctx.empty());
}

TEST_F(ExceptionHandlingTest, ExceptionContextFindHandler) {
    ExceptionContext ctx;

    // Push handler for DivByZero only
    auto frame1 = ExceptionFrame::make(100, 0, 0, catch_mask::DIVZERO);
    ASSERT_TRUE(ctx.push_frame(frame1));

    // Push handler for all exceptions
    auto frame2 = ExceptionFrame::make(200, 5, 1, catch_mask::ALL);
    ASSERT_TRUE(ctx.push_frame(frame2));

    // Search for DivByZero - should find frame2 (top, catches ALL)
    auto found = ctx.find_handler(ErrorCode::DivByZero);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->handler_pc, 200);

    // Search for OutOfBounds - should find frame2 (catches ALL)
    found = ctx.find_handler(ErrorCode::OutOfBounds);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->handler_pc, 200);
}

TEST_F(ExceptionHandlingTest, ExceptionContextFindHandlerWithTypeFiltering) {
    ExceptionContext ctx;

    // Push handler for DivByZero only
    auto frame1 = ExceptionFrame::make(100, 0, 0, catch_mask::DIVZERO);
    ASSERT_TRUE(ctx.push_frame(frame1));

    // Push handler for OutOfBounds only
    auto frame2 = ExceptionFrame::make(200, 5, 1, catch_mask::BOUNDS);
    ASSERT_TRUE(ctx.push_frame(frame2));

    // Search for DivByZero - should find frame1 (skips frame2)
    auto found = ctx.find_handler(ErrorCode::DivByZero);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->handler_pc, 100);

    // Search for OutOfBounds - should find frame2
    found = ctx.find_handler(ErrorCode::OutOfBounds);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->handler_pc, 200);

    // Search for StackOverflow - no handler
    found = ctx.find_handler(ErrorCode::StackOverflow);
    EXPECT_EQ(found, nullptr);
}

TEST_F(ExceptionHandlingTest, ExceptionContextSetClearException) {
    ExceptionContext ctx;

    EXPECT_FALSE(ctx.has_pending_exception());

    auto exc = Exception::make(ErrorCode::DivByZero, 42, 10);
    ctx.set_exception(exc);

    EXPECT_TRUE(ctx.has_pending_exception());
    EXPECT_EQ(ctx.current_exception().type_id, ErrorCode::DivByZero);
    EXPECT_EQ(ctx.current_exception().payload, 42);
    EXPECT_EQ(ctx.current_exception().throw_pc, 10);

    ctx.clear_exception();
    EXPECT_FALSE(ctx.has_pending_exception());
}

TEST_F(ExceptionHandlingTest, ExceptionContextOverflowProtection) {
    ExceptionContext ctx{3};  // Max depth of 3

    auto frame = ExceptionFrame::make(100, 0, 0, catch_mask::ALL);

    EXPECT_TRUE(ctx.push_frame(frame));
    EXPECT_TRUE(ctx.push_frame(frame));
    EXPECT_TRUE(ctx.push_frame(frame));
    EXPECT_FALSE(ctx.push_frame(frame));  // Should fail - overflow

    EXPECT_EQ(ctx.depth(), 3);
    EXPECT_TRUE(ctx.would_overflow());
}

TEST_F(ExceptionHandlingTest, ExceptionContextUnwindTo) {
    ExceptionContext ctx;

    ASSERT_TRUE(ctx.push_frame(ExceptionFrame::make(100, 0, 0, catch_mask::ALL)));
    ASSERT_TRUE(ctx.push_frame(ExceptionFrame::make(200, 5, 1, catch_mask::ALL)));
    ASSERT_TRUE(ctx.push_frame(ExceptionFrame::make(300, 10, 2, catch_mask::ALL)));

    EXPECT_EQ(ctx.depth(), 3);

    ctx.unwind_to(1);  // Unwind to depth 1 (keep only first frame)
    EXPECT_EQ(ctx.depth(), 1);

    auto frame = ctx.frame_at(0);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(frame->handler_pc, 100);
}

// ============================================================================
// ErrorCode and Exception Tests
// ============================================================================

TEST_F(ExceptionHandlingTest, ErrorCodeToString) {
    EXPECT_EQ(to_string(ErrorCode::None), "None");
    EXPECT_EQ(to_string(ErrorCode::DivByZero), "DivByZero");
    EXPECT_EQ(to_string(ErrorCode::OutOfBounds), "OutOfBounds");
    EXPECT_EQ(to_string(ErrorCode::StackOverflow), "StackOverflow");
    EXPECT_EQ(to_string(ErrorCode::InvalidHandle), "InvalidHandle");
    EXPECT_EQ(to_string(ErrorCode::TypeMismatch), "TypeMismatch");
    EXPECT_EQ(to_string(ErrorCode::Custom), "Custom");
}

TEST_F(ExceptionHandlingTest, ErrorToCatchMask) {
    EXPECT_EQ(error_to_catch_mask(ErrorCode::DivByZero), catch_mask::DIVZERO);
    EXPECT_EQ(error_to_catch_mask(ErrorCode::OutOfBounds), catch_mask::BOUNDS);
    EXPECT_EQ(error_to_catch_mask(ErrorCode::StackOverflow), catch_mask::STACK);
    EXPECT_EQ(error_to_catch_mask(ErrorCode::Custom), catch_mask::CUSTOM);
}

TEST_F(ExceptionHandlingTest, CatchMaskMatches) {
    // ALL catches everything
    EXPECT_TRUE(catch_mask_matches(catch_mask::ALL, ErrorCode::DivByZero));
    EXPECT_TRUE(catch_mask_matches(catch_mask::ALL, ErrorCode::OutOfBounds));
    EXPECT_TRUE(catch_mask_matches(catch_mask::ALL, ErrorCode::Custom));

    // Specific masks
    EXPECT_TRUE(catch_mask_matches(catch_mask::DIVZERO, ErrorCode::DivByZero));
    EXPECT_FALSE(catch_mask_matches(catch_mask::DIVZERO, ErrorCode::OutOfBounds));

    // Combined masks
    std::uint8_t combined = catch_mask::DIVZERO | catch_mask::BOUNDS;
    EXPECT_TRUE(catch_mask_matches(combined, ErrorCode::DivByZero));
    EXPECT_TRUE(catch_mask_matches(combined, ErrorCode::OutOfBounds));
    EXPECT_FALSE(catch_mask_matches(combined, ErrorCode::StackOverflow));
}

TEST_F(ExceptionHandlingTest, ExceptionFactoryMethods) {
    // Use 3-arg overload: type, payload, pc
    auto exc1 = Exception::make(ErrorCode::DivByZero, 0ULL, 5);
    EXPECT_EQ(exc1.type_id, ErrorCode::DivByZero);
    EXPECT_EQ(exc1.throw_pc, 5);
    EXPECT_EQ(exc1.payload, 0);
    EXPECT_TRUE(exc1.message.empty());

    auto exc2 = Exception::make(ErrorCode::OutOfBounds, 42, 10);
    EXPECT_EQ(exc2.type_id, ErrorCode::OutOfBounds);
    EXPECT_EQ(exc2.payload, 42);
    EXPECT_EQ(exc2.throw_pc, 10);
}

// ============================================================================
// ExecutionEngine Exception Handling Integration Tests
// ============================================================================

TEST_F(ExceptionHandlingTest, BasicTryEndtryNoException) {
    // Test: TRY block with normal exit (no exception thrown)
    // Code: TRY -> NOP -> ENDTRY -> HALT
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_try(5, catch_mask::ALL),  // 0: TRY, handler at offset 5 (PC 5)
        make_nop(),                    // 1: NOP
        make_endtry(),                 // 2: ENDTRY
        make_halt()                    // 3: HALT
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::Success);

    // Exception context should be empty
    EXPECT_TRUE(ctx.exception_context().empty());
}

TEST_F(ExceptionHandlingTest, BasicThrowCatch) {
    // Test: Exception is thrown and caught
    // Code: TRY -> MOVI (type) -> THROW -> HALT (skipped) | CATCH -> HALT
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_try(5, catch_mask::ALL),  // 0: TRY, handler at offset 5 (PC 5)
        make_movi(2, 1),               // 1: R2 = 1 (DivByZero)
        make_movi(3, 42),              // 2: R3 = 42 (payload)
        make_throw(2, 3),              // 3: THROW R2, R3
        make_halt(),                   // 4: HALT (should be skipped)
        make_catch(),                  // 5: CATCH (handler entry)
        make_halt()                    // 6: HALT
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::Success);

    // Verify exception was set
    EXPECT_TRUE(ctx.exception_context().has_pending_exception());
    EXPECT_EQ(ctx.exception_context().current_exception().type_id, ErrorCode::DivByZero);
    EXPECT_EQ(ctx.exception_context().current_exception().payload, 42);
}

TEST_F(ExceptionHandlingTest, UnhandledException) {
    // Test: Exception is thrown without handler
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_movi(2, 1),   // 0: R2 = 1 (DivByZero)
        make_movi(3, 0),   // 1: R3 = 0 (payload)
        make_throw(2, 3),  // 2: THROW R2, R3
        make_halt()        // 3: HALT (should not reach)
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::UnhandledException);
}

TEST_F(ExceptionHandlingTest, TypeFilteredCatch) {
    // Test: Exception type doesn't match handler, propagates
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_try(5, catch_mask::BOUNDS),  // 0: TRY, only catches OutOfBounds
        make_movi(2, 1),                  // 1: R2 = 1 (DivByZero)
        make_movi(3, 0),                  // 2: R3 = 0
        make_throw(2, 3),                 // 3: THROW DivByZero (not caught)
        make_halt(),                      // 4: HALT (skipped)
        make_catch(),                     // 5: CATCH (not reached for DivByZero)
        make_halt()                       // 6: HALT
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::UnhandledException);
}

TEST_F(ExceptionHandlingTest, NestedTryCatch) {
    // Test: Nested TRY blocks
    // Inner: catches BOUNDS only
    // Outer: catches ALL
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_try(9, catch_mask::ALL),     // 0: Outer TRY, handler at 9
        make_try(6, catch_mask::BOUNDS),  // 1: Inner TRY, handler at 7 (offset 6)
        make_movi(2, 1),                  // 2: R2 = 1 (DivByZero)
        make_movi(3, 0),                  // 3: R3 = 0
        make_throw(2, 3),                 // 4: THROW DivByZero
        make_halt(),                      // 5: (skipped)
        make_halt(),                      // 6: (skipped)
        make_catch(),                     // 7: Inner CATCH (not reached - type mismatch)
        make_halt(),                      // 8: (skipped)
        make_catch(),                     // 9: Outer CATCH (reached)
        make_movi(10, 99),                // 10: R10 = 99 (marker)
        make_halt()                       // 11: HALT
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx.registers().read(10).as_integer(), 99);
}

TEST_F(ExceptionHandlingTest, EndtryWithoutTry) {
    // Test: ENDTRY without TRY should error
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_endtry(),  // 0: ENDTRY without TRY
        make_halt()     // 1: HALT
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::Error);
}

TEST_F(ExceptionHandlingTest, VmContextExceptionContextAccess) {
    VmContext ctx;

    // Initial state
    EXPECT_TRUE(ctx.exception_context().empty());
    EXPECT_FALSE(ctx.has_pending_exception());

    // Modify exception context
    ASSERT_TRUE(
        ctx.exception_context().push_frame(ExceptionFrame::make(100, 0, 0, catch_mask::ALL)));

    EXPECT_FALSE(ctx.exception_context().empty());
    EXPECT_EQ(ctx.exception_context().depth(), 1);

    // Reset clears exception context
    ctx.reset();
    EXPECT_TRUE(ctx.exception_context().empty());
}

TEST_F(ExceptionHandlingTest, ExceptionFrameCatches) {
    ExceptionFrame frame = ExceptionFrame::make(100, 0, 0, catch_mask::DIVZERO);

    EXPECT_TRUE(frame.catches(ErrorCode::DivByZero));
    EXPECT_FALSE(frame.catches(ErrorCode::OutOfBounds));
    EXPECT_FALSE(frame.catches(ErrorCode::StackOverflow));

    ExceptionFrame all_frame = ExceptionFrame::make(100, 0, 0, catch_mask::ALL);
    EXPECT_TRUE(all_frame.catches(ErrorCode::DivByZero));
    EXPECT_TRUE(all_frame.catches(ErrorCode::OutOfBounds));
    EXPECT_TRUE(all_frame.catches(ErrorCode::Custom));
}

TEST_F(ExceptionHandlingTest, OpcodeHelperFunctions) {
    // Test is_exception_op
    EXPECT_TRUE(is_exception_op(op::TRY));
    EXPECT_TRUE(is_exception_op(op::CATCH));
    EXPECT_TRUE(is_exception_op(op::THROW));
    EXPECT_TRUE(is_exception_op(op::ENDTRY));
    EXPECT_FALSE(is_exception_op(op::HALT));
    EXPECT_FALSE(is_exception_op(op::NOP));

    // Test is_control_flow_op (exceptions are in control flow range)
    EXPECT_TRUE(is_control_flow_op(op::TRY));
    EXPECT_TRUE(is_control_flow_op(op::CATCH));
    EXPECT_TRUE(is_control_flow_op(op::THROW));
    EXPECT_TRUE(is_control_flow_op(op::ENDTRY));
    EXPECT_TRUE(is_control_flow_op(op::JMP));
    EXPECT_TRUE(is_control_flow_op(op::CALL));
    EXPECT_TRUE(is_control_flow_op(op::RET));
    EXPECT_TRUE(is_control_flow_op(op::HALT));
}

TEST_F(ExceptionHandlingTest, ExecResultUnhandledException) {
    EXPECT_STREQ(to_string(ExecResult::UnhandledException), "UnhandledException");
}

// ============================================================================
// Additional Edge Case Tests
// ============================================================================

TEST_F(ExceptionHandlingTest, MultipleExceptions) {
    // Test: Throw in handler replaces previous exception
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_try(8, catch_mask::ALL),  // 0: Outer TRY
        make_try(5, catch_mask::ALL),  // 1: Inner TRY
        make_movi(2, 1),               // 2: R2 = 1 (DivByZero)
        make_throw(2, 2),              // 3: THROW
        make_halt(),                   // 4: (skipped)
        make_catch(),                  // 5: Inner CATCH
        make_movi(2, 2),               // 6: R2 = 2 (OutOfBounds)
        make_throw(2, 2),              // 7: THROW new exception
        make_catch(),                  // 8: Outer CATCH
        make_halt()                    // 9: HALT
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::Success);

    // Should have OutOfBounds exception (the second one)
    EXPECT_TRUE(ctx.exception_context().has_pending_exception());
    EXPECT_EQ(ctx.exception_context().current_exception().type_id, ErrorCode::OutOfBounds);
}

TEST_F(ExceptionHandlingTest, CatchSetMarkerRegister) {
    // Test: After catch, handler can set registers
    VmContext ctx;
    ExecutionEngine engine{ctx};

    // Set R5 to 0 initially
    ctx.registers().write(5, Value::from_int(0));

    std::vector<std::uint32_t> code = {
        make_try(5, catch_mask::ALL),  // 0: TRY
        make_movi(2, 1),               // 1: R2 = 1 (DivByZero)
        make_throw(2, 2),              // 2: THROW
        make_halt(),                   // 3: (skipped)
        make_halt(),                   // 4: (skipped)
        make_catch(),                  // 5: CATCH
        make_movi(5, 123),             // 6: R5 = 123 (marker)
        make_halt()                    // 7: HALT
    };

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx.registers().read(5).as_integer(), 123);
}

TEST_F(ExceptionHandlingTest, ExceptionContextClear) {
    ExceptionContext ctx;

    ASSERT_TRUE(ctx.push_frame(ExceptionFrame::make(100, 0, 0, catch_mask::ALL)));
    ASSERT_TRUE(ctx.push_frame(ExceptionFrame::make(200, 5, 1, catch_mask::ALL)));
    ctx.set_exception(Exception::make(ErrorCode::DivByZero));

    EXPECT_EQ(ctx.depth(), 2);
    EXPECT_TRUE(ctx.has_pending_exception());

    ctx.clear();

    EXPECT_EQ(ctx.depth(), 0);
    EXPECT_FALSE(ctx.has_pending_exception());
}

TEST_F(ExceptionHandlingTest, ExceptionIsActive) {
    Exception exc;
    EXPECT_FALSE(exc.is_active());

    exc.type_id = ErrorCode::DivByZero;
    EXPECT_TRUE(exc.is_active());

    exc.clear();
    EXPECT_FALSE(exc.is_active());
}

TEST_F(ExceptionHandlingTest, ExceptionIsCustom) {
    Exception exc = Exception::make(ErrorCode::DivByZero);
    EXPECT_FALSE(exc.is_custom());

    exc.type_id = ErrorCode::Custom;
    EXPECT_TRUE(exc.is_custom());

    exc.type_id = static_cast<ErrorCode>(0x1001);  // Custom + 1
    EXPECT_TRUE(exc.is_custom());
}
