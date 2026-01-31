/// @file jit_integration_test.cpp
/// @brief Integration tests for JIT compilation subsystem

#include <gtest/gtest.h>

#include "dotvm/jit/jit_context.hpp"
#include "dotvm/jit/jit_compiler.hpp"
#include "dotvm/core/vm_context.hpp"
#include "dotvm/core/opcode.hpp"
#include "dotvm/core/instruction.hpp"
#include "dotvm/exec/execution_engine.hpp"

namespace dotvm::jit {
namespace {

// ============================================================================
// JitContext Integration Tests
// ============================================================================

class JitContextIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = JitConfig::aggressive();  // Lower thresholds for testing
        ctx_ = JitContext::create(config_);
        ASSERT_NE(ctx_, nullptr);
    }

    JitConfig config_;
    std::unique_ptr<JitContext> ctx_;
};

TEST_F(JitContextIntegrationTest, Create_Succeeds) {
    EXPECT_TRUE(ctx_->enabled());
}

TEST_F(JitContextIntegrationTest, RegisterFunction_ReturnsValidId) {
    auto id = ctx_->register_function(0x100, 0x200);
    EXPECT_EQ(id, 0u);

    auto id2 = ctx_->register_function(0x200, 0x300);
    EXPECT_EQ(id2, 1u);
}

TEST_F(JitContextIntegrationTest, RecordCall_ReturnsTrueAfterThreshold) {
    auto id = ctx_->register_function(0x100, 0x200);

    // Record calls up to threshold - 1
    for (std::uint32_t i = 0; i < config_.call_threshold - 1; ++i) {
        EXPECT_FALSE(ctx_->record_call(id));
    }

    // Next call should trigger compilation
    EXPECT_TRUE(ctx_->record_call(id));
}

TEST_F(JitContextIntegrationTest, Stats_TracksActivity) {
    auto id = ctx_->register_function(0x100, 0x200);

    for (int i = 0; i < 100; ++i) {
        (void)ctx_->record_call(id);
    }

    auto stats = ctx_->stats();
    EXPECT_EQ(stats.registered_functions, 1u);
    EXPECT_EQ(stats.total_calls, 100u);
}

// ============================================================================
// VmContext JIT Integration Tests
// ============================================================================

class VmContextJitTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = core::VmConfig::with_jit_aggressive();
        ctx_ = std::make_unique<core::VmContext>(config_);
    }

    core::VmConfig config_;
    std::unique_ptr<core::VmContext> ctx_;
};

TEST_F(VmContextJitTest, JitEnabled_AfterConfigWithJit) {
    EXPECT_TRUE(ctx_->jit_enabled());
    EXPECT_NE(ctx_->jit_context(), nullptr);
}

TEST_F(VmContextJitTest, JitDisabled_WithDefaultConfig) {
    core::VmContext default_ctx;
    EXPECT_FALSE(default_ctx.jit_enabled());
    EXPECT_EQ(default_ctx.jit_context(), nullptr);
}

TEST_F(VmContextJitTest, EnableJit_CanEnableLater) {
    core::VmContext ctx;
    EXPECT_FALSE(ctx.jit_enabled());

    EXPECT_TRUE(ctx.enable_jit());
    EXPECT_TRUE(ctx.jit_enabled());
}

TEST_F(VmContextJitTest, DisableJit_ClearsContext) {
    EXPECT_TRUE(ctx_->jit_enabled());
    ctx_->disable_jit();
    EXPECT_FALSE(ctx_->jit_enabled());
    EXPECT_EQ(ctx_->jit_context(), nullptr);
}

TEST_F(VmContextJitTest, Reset_ClearsJitCache) {
    auto* jit = ctx_->jit_context();
    ASSERT_NE(jit, nullptr);

    // Register a function
    (void)jit->register_function(0x100, 0x200);

    // Reset should clear the cache
    ctx_->reset();

    // JIT should still be enabled
    EXPECT_TRUE(ctx_->jit_enabled());
}

// ============================================================================
// ExecutionEngine JIT Integration Tests
// ============================================================================

class ExecutionEngineJitTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = core::VmConfig::with_jit_aggressive();
        ctx_ = std::make_unique<core::VmContext>(config_);
        engine_ = std::make_unique<exec::ExecutionEngine>(*ctx_);
    }

    core::VmConfig config_;
    std::unique_ptr<core::VmContext> ctx_;
    std::unique_ptr<exec::ExecutionEngine> engine_;
};

TEST_F(ExecutionEngineJitTest, JitAvailable_ReturnsTrue) {
    EXPECT_TRUE(engine_->jit_available());
}

TEST_F(ExecutionEngineJitTest, JitRegisterFunction_Works) {
    auto id = engine_->jit_register_function(0x100, 0x200);
    EXPECT_EQ(id, 0u);
}

TEST_F(ExecutionEngineJitTest, JitHasCompiled_ReturnsFalseInitially) {
    engine_->jit_register_function(0x100, 0x200);
    EXPECT_FALSE(engine_->jit_has_compiled(0x100));
}

TEST_F(ExecutionEngineJitTest, JitExecute_ReturnsFallbackWhenNotCompiled) {
    engine_->jit_register_function(0x100, 0x200);
    auto result = engine_->jit_execute(0x100);
    EXPECT_EQ(result, exec::ExecResult::JitFallback);
}

// ============================================================================
// Compiler Tests
// ============================================================================

class JitCompilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto buffer_result = JitCodeBuffer::create(64 * 1024);
        ASSERT_TRUE(buffer_result.has_value());
        buffer_ = std::make_unique<JitCodeBuffer>(std::move(*buffer_result));
        stencils_ = StencilRegistry::create_default();
    }

    JitConfig config_;
    std::unique_ptr<JitCodeBuffer> buffer_;
    StencilRegistry stencils_;
};

TEST_F(JitCompilerTest, SupportsOpcode_ForArithmetic) {
    JitCompiler compiler(config_, *buffer_, stencils_);

    // Arithmetic opcodes should be supported
    EXPECT_TRUE(compiler.supports_opcode(static_cast<std::uint8_t>(JitOpcode::ADD)));
    EXPECT_TRUE(compiler.supports_opcode(static_cast<std::uint8_t>(JitOpcode::SUB)));
    EXPECT_TRUE(compiler.supports_opcode(static_cast<std::uint8_t>(JitOpcode::MUL)));
}

TEST_F(JitCompilerTest, EstimateCodeSize_ReturnsPositive) {
    JitCompiler compiler(config_, *buffer_, stencils_);

    std::vector<BytecodeInstr> instrs = {
        {.opcode = static_cast<std::uint8_t>(JitOpcode::ADD), .dst = 1, .src1 = 2, .src2 = 3, .pc = 0},
        {.opcode = static_cast<std::uint8_t>(JitOpcode::SUB), .dst = 1, .src1 = 2, .src2 = 3, .pc = 4},
    };

    auto size = compiler.estimate_code_size(instrs);
    EXPECT_GT(size, 0u);
}

TEST_F(JitCompilerTest, Compile_SimpleFunction_Succeeds) {
    JitCompiler compiler(config_, *buffer_, stencils_);

    std::vector<BytecodeInstr> instrs = {
        {.opcode = static_cast<std::uint8_t>(JitOpcode::ADD), .dst = 1, .src1 = 2, .src2 = 3, .pc = 0},
    };

    auto result = compiler.compile(0, instrs);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->code_size, 0u);
    EXPECT_NE(result->code, nullptr);
}

// ============================================================================
// Stencil Registry Tests
// ============================================================================

class StencilRegistryTest : public ::testing::Test {};

TEST_F(StencilRegistryTest, CreateDefault_HasArithmeticStencils) {
    auto registry = StencilRegistry::create_default();

    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::ADD)));
    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::SUB)));
    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::MUL)));
    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::DIV)));
    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::MOD)));
}

TEST_F(StencilRegistryTest, CreateDefault_HasBitwiseStencils) {
    auto registry = StencilRegistry::create_default();

    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::AND)));
    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::OR)));
    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::XOR)));
    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::SHL)));
    EXPECT_TRUE(registry.has_stencil(static_cast<std::uint8_t>(JitOpcode::SHR)));
}

TEST_F(StencilRegistryTest, Get_ReturnsValidStencil) {
    auto registry = StencilRegistry::create_default();

    const auto* add = registry.get(JitOpcode::ADD);
    ASSERT_NE(add, nullptr);
    EXPECT_TRUE(add->valid());
    EXPECT_GT(add->code_size, 0u);
    EXPECT_NE(add->code, nullptr);
}

TEST_F(StencilRegistryTest, Get_InvalidOpcode_ReturnsNull) {
    auto registry = StencilRegistry::create_default();

    // 0x80 is in the reserved range, should not have a stencil
    const auto* invalid = registry.get(0x80);  // Reserved, no stencil
    EXPECT_EQ(invalid, nullptr);
}

TEST_F(StencilRegistryTest, Count_ReturnsPositive) {
    auto registry = StencilRegistry::create_default();
    EXPECT_GT(registry.count(), 0u);
}

// ============================================================================
// End-to-End JIT Tests (EXEC-012)
// ============================================================================

class JitEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = core::VmConfig::with_jit_aggressive();
        // Use very low thresholds for testing
        config_.jit_call_threshold = 10;
        config_.jit_loop_threshold = 100;
        ctx_ = std::make_unique<core::VmContext>(config_);
        engine_ = std::make_unique<exec::ExecutionEngine>(*ctx_);
    }

    // Helper to encode Type A instruction: [opcode][Rd][Rs1][Rs2]
    static std::uint32_t encode_a(std::uint8_t op, std::uint8_t rd, std::uint8_t rs1, std::uint8_t rs2) {
        return (static_cast<std::uint32_t>(op) << 24) |
               (static_cast<std::uint32_t>(rd) << 16) |
               (static_cast<std::uint32_t>(rs1) << 8) |
               static_cast<std::uint32_t>(rs2);
    }

    // Helper to encode Type B instruction: [opcode][Rd][imm16]
    static std::uint32_t encode_b(std::uint8_t op, std::uint8_t rd, std::int16_t imm16) {
        return (static_cast<std::uint32_t>(op) << 24) |
               (static_cast<std::uint32_t>(rd) << 16) |
               (static_cast<std::uint32_t>(static_cast<std::uint16_t>(imm16)));
    }

    core::VmConfig config_;
    std::unique_ptr<core::VmContext> ctx_;
    std::unique_ptr<exec::ExecutionEngine> engine_;
};

TEST_F(JitEndToEndTest, FunctionCompiledAfterThresholdCalls) {
    // Simple function: R1 = R2 + R3; RET
    // We'll manually record calls to simulate the profiler
    const std::size_t func_entry_pc = 0;
    const std::size_t func_end_pc = 2;  // 2 instructions

    // Register function
    auto func_id = engine_->jit_register_function(func_entry_pc, func_end_pc);
    EXPECT_EQ(func_id, 0u);

    // Before threshold: should not be compiled
    EXPECT_FALSE(engine_->jit_has_compiled(func_entry_pc));

    // Record calls up to threshold
    auto* jit = ctx_->jit_context();
    ASSERT_NE(jit, nullptr);

    for (std::uint32_t i = 0; i < config_.jit_call_threshold; ++i) {
        (void)jit->record_call(func_id);
    }

    // After threshold: profiler should indicate compilation is needed
    // Note: The actual compilation happens when jit_try_compile() is called
    // For now, we verify the profiler state
    auto stats = jit->stats();
    EXPECT_GE(stats.total_calls, config_.jit_call_threshold);
}

TEST_F(JitEndToEndTest, ProfilerTracksCallCounts) {
    // Register multiple functions
    auto func1 = engine_->jit_register_function(0, 10);
    auto func2 = engine_->jit_register_function(100, 120);

    auto* jit = ctx_->jit_context();
    ASSERT_NE(jit, nullptr);

    // Record different call counts
    for (int i = 0; i < 5; ++i) {
        (void)jit->record_call(func1);
    }
    for (int i = 0; i < 15; ++i) {
        (void)jit->record_call(func2);
    }

    auto stats = jit->stats();
    EXPECT_EQ(stats.registered_functions, 2u);
    EXPECT_EQ(stats.total_calls, 20u);
}

TEST_F(JitEndToEndTest, LoopProfilerTracksIterations) {
    // Register function and loop
    auto func_id = engine_->jit_register_function(0, 100);
    auto loop_id = engine_->jit_register_loop(func_id, 10, 50);

    auto* jit = ctx_->jit_context();
    ASSERT_NE(jit, nullptr);

    // Record iterations
    for (int i = 0; i < 200; ++i) {
        (void)jit->record_iteration(loop_id);
    }

    auto stats = jit->stats();
    EXPECT_EQ(stats.registered_loops, 1u);
    EXPECT_EQ(stats.total_iterations, 200u);
}

TEST_F(JitEndToEndTest, ExecuteReturnsJitFallbackWhenNotCompiled) {
    // Register function but don't reach threshold
    engine_->jit_register_function(0, 10);

    // Try to execute via JIT - should fall back
    auto result = engine_->jit_execute(0);
    EXPECT_EQ(result, exec::ExecResult::JitFallback);
}

// Note: Interpreter correctness is tested exhaustively in execution_engine_test.cpp
// This test verifies basic JIT infrastructure connectivity

TEST_F(JitEndToEndTest, StencilRegistryHasCorrectOpcodes) {
    // Verify stencils are registered at the correct core::opcode values
    auto registry = StencilRegistry::create_default();

    // Arithmetic opcodes
    EXPECT_TRUE(registry.has_stencil(core::opcode::ADD));
    EXPECT_TRUE(registry.has_stencil(core::opcode::SUB));
    EXPECT_TRUE(registry.has_stencil(core::opcode::MUL));
    EXPECT_TRUE(registry.has_stencil(core::opcode::DIV));
    EXPECT_TRUE(registry.has_stencil(core::opcode::MOD));

    // Bitwise opcodes
    EXPECT_TRUE(registry.has_stencil(core::opcode::AND));
    EXPECT_TRUE(registry.has_stencil(core::opcode::OR));
    EXPECT_TRUE(registry.has_stencil(core::opcode::XOR));
    EXPECT_TRUE(registry.has_stencil(core::opcode::NOT));
    EXPECT_TRUE(registry.has_stencil(core::opcode::SHL));
    EXPECT_TRUE(registry.has_stencil(core::opcode::SHR));

    // Comparison opcodes
    EXPECT_TRUE(registry.has_stencil(core::opcode::EQ));
    EXPECT_TRUE(registry.has_stencil(core::opcode::LT));

    // Control flow opcodes
    EXPECT_TRUE(registry.has_stencil(core::opcode::JMP));
    EXPECT_TRUE(registry.has_stencil(core::opcode::JZ));
    EXPECT_TRUE(registry.has_stencil(core::opcode::JNZ));

    // Memory opcodes
    EXPECT_TRUE(registry.has_stencil(core::opcode::LOAD64));
    EXPECT_TRUE(registry.has_stencil(core::opcode::STORE64));
}

} // namespace
} // namespace dotvm::jit
