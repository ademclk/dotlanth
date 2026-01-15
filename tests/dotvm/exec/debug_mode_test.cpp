/// @file debug_mode_test.cpp
/// @brief Unit tests for EXEC-010: Debug Mode & Stepping

#include <gtest/gtest.h>

#include <dotvm/exec/execution_engine.hpp>
#include <dotvm/exec/execution_context.hpp>
#include <dotvm/exec/debug_context.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/value.hpp>

#include <vector>
#include <array>
#include <functional>

using namespace dotvm;
using namespace dotvm::exec;
using namespace dotvm::core;

// Use exec::opcode namespace for instruction encoding
namespace op = dotvm::exec::opcode;

// ============================================================================
// Test Fixtures
// ============================================================================

class DebugModeTest : public ::testing::Test {
protected:
    // Helper to create NOP instruction
    static std::uint32_t make_nop() {
        return encode_type_c(op::NOP, 0);
    }

    // Helper to create HALT instruction
    static std::uint32_t make_halt() {
        return encode_type_c(op::HALT, 0);
    }

    // Helper to create DEBUG instruction (explicit breakpoint)
    static std::uint32_t make_debug() {
        return encode_type_c(op::DEBUG, 0);
    }

    // Helper to create ADDI instruction (add immediate)
    static std::uint32_t make_addi(std::uint8_t rd, std::uint16_t imm) {
        return encode_type_b(op::ADDI, rd, imm);
    }

    // Helper to create MOVI instruction (move immediate)
    static std::uint32_t make_movi(std::uint8_t rd, std::uint16_t imm) {
        return encode_type_b(op::MOVI, rd, imm);
    }

    // Helper to create JMP instruction
    static std::uint32_t make_jmp(std::int32_t offset) {
        return encode_type_c(op::JMP, offset);
    }
};

// ============================================================================
// DebugContext Unit Tests
// ============================================================================

TEST_F(DebugModeTest, DebugContextDefaultsToDisabled) {
    DebugContext ctx;
    EXPECT_FALSE(ctx.enabled);
    EXPECT_FALSE(ctx.stepping);
    EXPECT_TRUE(ctx.breakpoints.empty());
    EXPECT_EQ(ctx.callback, nullptr);
}

TEST_F(DebugModeTest, DebugContextSetBreakpoint) {
    DebugContext ctx;

    ctx.set_breakpoint(10);
    EXPECT_TRUE(ctx.has_breakpoint(10));
    EXPECT_FALSE(ctx.has_breakpoint(11));
    EXPECT_EQ(ctx.breakpoint_count(), 1);
}

TEST_F(DebugModeTest, DebugContextRemoveBreakpoint) {
    DebugContext ctx;

    ctx.set_breakpoint(10);
    ctx.set_breakpoint(20);
    EXPECT_EQ(ctx.breakpoint_count(), 2);

    ctx.remove_breakpoint(10);
    EXPECT_FALSE(ctx.has_breakpoint(10));
    EXPECT_TRUE(ctx.has_breakpoint(20));
    EXPECT_EQ(ctx.breakpoint_count(), 1);
}

TEST_F(DebugModeTest, DebugContextClearBreakpoints) {
    DebugContext ctx;

    ctx.set_breakpoint(10);
    ctx.set_breakpoint(20);
    ctx.set_breakpoint(30);
    EXPECT_EQ(ctx.breakpoint_count(), 3);

    ctx.clear_breakpoints();
    EXPECT_EQ(ctx.breakpoint_count(), 0);
    EXPECT_FALSE(ctx.has_breakpoint(10));
}

TEST_F(DebugModeTest, DebugContextClearAll) {
    DebugContext ctx;
    ctx.enabled = true;
    ctx.stepping = true;
    ctx.set_breakpoint(10);
    ctx.callback = [](DebugEvent, ExecutionContext&) {};

    ctx.clear();

    EXPECT_FALSE(ctx.enabled);
    EXPECT_FALSE(ctx.stepping);
    EXPECT_TRUE(ctx.breakpoints.empty());
    EXPECT_EQ(ctx.callback, nullptr);
}

TEST_F(DebugModeTest, DebugEventToString) {
    EXPECT_STREQ(to_string(DebugEvent::Break), "Break");
    EXPECT_STREQ(to_string(DebugEvent::Step), "Step");
    EXPECT_STREQ(to_string(DebugEvent::WatchHit), "WatchHit");
    EXPECT_STREQ(to_string(DebugEvent::Exception), "Exception");
}

// ============================================================================
// ExecutionEngine Debug API Tests
// ============================================================================

TEST_F(DebugModeTest, EngineDebugDefaultsToDisabled) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    EXPECT_FALSE(engine.debug_enabled());
    EXPECT_TRUE(engine.breakpoints().empty());
}

TEST_F(DebugModeTest, EngineEnableDisableDebug) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    engine.enable_debug(true);
    EXPECT_TRUE(engine.debug_enabled());

    engine.enable_debug(false);
    EXPECT_FALSE(engine.debug_enabled());
}

TEST_F(DebugModeTest, EngineSetBreakpoint) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    engine.set_breakpoint(5);
    engine.set_breakpoint(10);

    EXPECT_TRUE(engine.has_breakpoint(5));
    EXPECT_TRUE(engine.has_breakpoint(10));
    EXPECT_FALSE(engine.has_breakpoint(7));
    EXPECT_EQ(engine.breakpoints().size(), 2);
}

TEST_F(DebugModeTest, EngineRemoveBreakpoint) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    engine.set_breakpoint(5);
    engine.set_breakpoint(10);
    engine.remove_breakpoint(5);

    EXPECT_FALSE(engine.has_breakpoint(5));
    EXPECT_TRUE(engine.has_breakpoint(10));
}

TEST_F(DebugModeTest, EngineClearBreakpoints) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    engine.set_breakpoint(5);
    engine.set_breakpoint(10);
    engine.clear_breakpoints();

    EXPECT_TRUE(engine.breakpoints().empty());
}

// ============================================================================
// Breakpoint Hit Tests
// ============================================================================

TEST_F(DebugModeTest, BreakpointHitsAndPausesExecution) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    // Code: NOP, NOP, ADDI R1,1, NOP, HALT
    std::vector<std::uint32_t> code = {
        make_nop(),         // PC=0
        make_nop(),         // PC=1
        make_addi(1, 1),    // PC=2 (breakpoint)
        make_nop(),         // PC=3
        make_halt()         // PC=4
    };

    engine.enable_debug(true);
    engine.set_breakpoint(2);  // Break at ADDI

    // Record callback events
    std::vector<std::pair<DebugEvent, std::size_t>> events;
    engine.set_debug_callback([&events](DebugEvent event, ExecutionContext& ctx) {
        events.push_back({event, ctx.pc});
    });

    // Execute - will hit breakpoint at PC=2
    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::Interrupted);
    EXPECT_EQ(engine.pc(), 2);  // Stopped at breakpoint
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].first, DebugEvent::Break);
    EXPECT_EQ(events[0].second, 2);
}

TEST_F(DebugModeTest, ContinueAfterBreakpoint) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_nop(),         // PC=0
        make_addi(1, 1),    // PC=1 (breakpoint)
        make_addi(1, 2),    // PC=2
        make_halt()         // PC=3
    };

    engine.enable_debug(true);
    engine.set_breakpoint(1);

    // Hit breakpoint
    auto result1 = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result1, ExecResult::Interrupted);
    EXPECT_EQ(engine.pc(), 1);

    // Remove breakpoint and continue
    engine.remove_breakpoint(1);
    auto result2 = engine.continue_execution();
    EXPECT_EQ(result2, ExecResult::Success);
    EXPECT_EQ(ctx.registers().read(1).as_integer(), 3);  // 1 + 2
}

TEST_F(DebugModeTest, MultipleBreakpoints) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_movi(1, 10),   // PC=0
        make_nop(),         // PC=1 (breakpoint)
        make_addi(1, 5),    // PC=2
        make_nop(),         // PC=3 (breakpoint)
        make_halt()         // PC=4
    };

    engine.enable_debug(true);
    engine.set_breakpoint(1);
    engine.set_breakpoint(3);

    // First breakpoint
    auto result1 = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result1, ExecResult::Interrupted);
    EXPECT_EQ(engine.pc(), 1);

    // Remove first breakpoint and continue to second
    engine.remove_breakpoint(1);
    auto result2 = engine.continue_execution();
    EXPECT_EQ(result2, ExecResult::Interrupted);
    EXPECT_EQ(engine.pc(), 3);

    // Continue to halt
    engine.remove_breakpoint(3);
    auto result3 = engine.continue_execution();
    EXPECT_EQ(result3, ExecResult::Success);
}

// ============================================================================
// Step Execution Tests
// ============================================================================

TEST_F(DebugModeTest, StepIntoExecutesOneInstruction) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_movi(1, 10),   // PC=0
        make_addi(1, 5),    // PC=1
        make_halt()         // PC=2
    };

    engine.enable_debug(true);

    // Initialize execution with breakpoint at PC=0 to pause immediately
    engine.set_breakpoint(0);
    auto init_result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(init_result, ExecResult::Interrupted);
    engine.remove_breakpoint(0);

    // Step once
    auto result1 = engine.step_into();
    EXPECT_EQ(result1, ExecResult::Interrupted);
    EXPECT_EQ(engine.pc(), 1);
    EXPECT_EQ(ctx.registers().read(1).as_integer(), 10);

    // Step again
    auto result2 = engine.step_into();
    EXPECT_EQ(result2, ExecResult::Interrupted);
    EXPECT_EQ(engine.pc(), 2);
    EXPECT_EQ(ctx.registers().read(1).as_integer(), 15);
}

TEST_F(DebugModeTest, StepCallsCallback) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_nop(),
        make_halt()
    };

    engine.enable_debug(true);

    int step_count = 0;
    engine.set_debug_callback([&step_count](DebugEvent event, ExecutionContext&) {
        if (event == DebugEvent::Step) {
            ++step_count;
        }
    });

    // Initialize at PC=0
    engine.set_breakpoint(0);
    (void)engine.execute(code.data(), code.size(), 0, {});
    engine.remove_breakpoint(0);

    (void)engine.step_into();
    EXPECT_EQ(step_count, 1);
}

// ============================================================================
// DEBUG Opcode Tests
// ============================================================================

TEST_F(DebugModeTest, DebugOpcodeTriggersBreakWhenEnabled) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_nop(),         // PC=0
        make_debug(),       // PC=1 (explicit DEBUG opcode)
        make_nop(),         // PC=2
        make_halt()         // PC=3
    };

    engine.enable_debug(true);

    bool callback_called = false;
    engine.set_debug_callback([&callback_called](DebugEvent event, ExecutionContext&) {
        if (event == DebugEvent::Break) {
            callback_called = true;
        }
    });

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::Interrupted);
    EXPECT_TRUE(callback_called);
    EXPECT_EQ(engine.pc(), 2);  // After DEBUG instruction
}

TEST_F(DebugModeTest, DebugOpcodeBehavesLikeNopWhenDisabled) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_movi(1, 42),   // PC=0
        make_debug(),       // PC=1 (treated as NOP)
        make_addi(1, 8),    // PC=2
        make_halt()         // PC=3
    };

    // Debug mode disabled (default)
    EXPECT_FALSE(engine.debug_enabled());

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx.registers().read(1).as_integer(), 50);  // 42 + 8
}

// ============================================================================
// Register Inspection Tests
// ============================================================================

TEST_F(DebugModeTest, InspectRegisterReturnsValue) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    // Set register values directly
    ctx.registers().write(5, Value::from_int(100));
    ctx.registers().write(10, Value::from_int(200));

    EXPECT_EQ(engine.inspect_register(5).as_integer(), 100);
    EXPECT_EQ(engine.inspect_register(10).as_integer(), 200);
    EXPECT_EQ(engine.inspect_register(0).as_integer(), 0);  // R0 always zero
}

TEST_F(DebugModeTest, InspectRegisterDuringExecution) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_movi(1, 42),   // PC=0
        make_nop(),         // PC=1 (breakpoint)
        make_halt()         // PC=2
    };

    engine.enable_debug(true);
    engine.set_breakpoint(1);

    // Execute until breakpoint
    (void)engine.execute(code.data(), code.size(), 0, {});

    // Inspect R1 after MOVI executed
    EXPECT_EQ(engine.inspect_register(1).as_integer(), 42);
}

// ============================================================================
// Memory Inspection Tests
// ============================================================================

TEST_F(DebugModeTest, InspectMemoryReturnsBytes) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    // Allocate memory and write data
    auto alloc_result = ctx.memory().allocate(16);
    ASSERT_TRUE(alloc_result.has_value());
    auto handle = *alloc_result;

    ASSERT_EQ(ctx.memory().write<std::uint32_t>(handle, 0, 0xDEADBEEF), MemoryError::Success);
    ASSERT_EQ(ctx.memory().write<std::uint32_t>(handle, 4, 0xCAFEBABE), MemoryError::Success);

    auto bytes = engine.inspect_memory(handle, 0, 8);

    EXPECT_EQ(bytes.size(), 8);
    // Little-endian: 0xDEADBEEF = EF BE AD DE
    EXPECT_EQ(bytes[0], 0xEF);
    EXPECT_EQ(bytes[1], 0xBE);
    EXPECT_EQ(bytes[2], 0xAD);
    EXPECT_EQ(bytes[3], 0xDE);
}

TEST_F(DebugModeTest, InspectMemoryInvalidHandle) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    Handle invalid_handle{};  // Invalid handle (index=0, generation=0)
    auto bytes = engine.inspect_memory(invalid_handle, 0, 8);

    EXPECT_TRUE(bytes.empty());
}

// ============================================================================
// Callback Tests
// ============================================================================

TEST_F(DebugModeTest, CallbackReceivesCorrectEvent) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_nop(),
        make_halt()
    };

    engine.enable_debug(true);
    engine.set_breakpoint(0);

    DebugEvent received_event = DebugEvent::Step;  // Initialize to different value
    engine.set_debug_callback([&received_event](DebugEvent event, ExecutionContext&) {
        received_event = event;
    });

    (void)engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(received_event, DebugEvent::Break);
}

TEST_F(DebugModeTest, CallbackReceivesCorrectPC) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_nop(),         // PC=0
        make_nop(),         // PC=1
        make_nop(),         // PC=2 (breakpoint)
        make_halt()
    };

    engine.enable_debug(true);
    engine.set_breakpoint(2);

    std::size_t received_pc = 999;
    engine.set_debug_callback([&received_pc](DebugEvent, ExecutionContext& ctx) {
        received_pc = ctx.pc;
    });

    (void)engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(received_pc, 2);
}

// ============================================================================
// Exception Callback Tests
// ============================================================================

TEST_F(DebugModeTest, ExceptionCallbackOnInvalidOpcode) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code = {
        make_nop(),
        0xFF000000,  // Invalid opcode
        make_halt()
    };

    engine.enable_debug(true);

    DebugEvent received_event = DebugEvent::Step;
    engine.set_debug_callback([&received_event](DebugEvent event, ExecutionContext&) {
        received_event = event;
    });

    auto result = engine.execute(code.data(), code.size(), 0, {});

    EXPECT_EQ(result, ExecResult::InvalidOpcode);
    EXPECT_EQ(received_event, DebugEvent::Exception);
}

// ============================================================================
// Performance Tests (Basic Sanity Checks)
// ============================================================================

TEST_F(DebugModeTest, DebugDisabledNoOverhead) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    // Large loop: many NOPs
    std::vector<std::uint32_t> code;
    for (int i = 0; i < 10000; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    // Debug disabled - should execute normally
    EXPECT_FALSE(engine.debug_enabled());

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(engine.context().instructions_executed, 10001);
}

TEST_F(DebugModeTest, DebugEnabledNoBreakpointsExecutes) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    std::vector<std::uint32_t> code;
    for (int i = 0; i < 100; ++i) {
        code.push_back(make_nop());
    }
    code.push_back(make_halt());

    engine.enable_debug(true);
    // No breakpoints set

    auto result = engine.execute(code.data(), code.size(), 0, {});
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(engine.context().instructions_executed, 101);
}

// ============================================================================
// Reset Tests
// ============================================================================

TEST_F(DebugModeTest, ResetClearsDebugState) {
    VmContext ctx;
    ExecutionEngine engine{ctx};

    engine.enable_debug(true);
    engine.set_breakpoint(5);
    engine.set_debug_callback([](DebugEvent, ExecutionContext&) {});

    engine.reset();

    // Debug context should be cleared
    EXPECT_FALSE(engine.debug_enabled());
    EXPECT_TRUE(engine.breakpoints().empty());
}
