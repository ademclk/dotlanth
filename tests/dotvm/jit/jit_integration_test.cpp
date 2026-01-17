/// @file jit_integration_test.cpp
/// @brief Integration tests for JIT compilation subsystem

#include <gtest/gtest.h>

#include "dotvm/core/vm_context.hpp"
#include "dotvm/exec/execution_engine.hpp"
#include "dotvm/jit/jit_compiler.hpp"
#include "dotvm/jit/jit_context.hpp"

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
        {.opcode = static_cast<std::uint8_t>(JitOpcode::ADD),
         .dst = 1,
         .src1 = 2,
         .src2 = 3,
         .pc = 0},
        {.opcode = static_cast<std::uint8_t>(JitOpcode::SUB),
         .dst = 1,
         .src1 = 2,
         .src2 = 3,
         .pc = 4},
    };

    auto size = compiler.estimate_code_size(instrs);
    EXPECT_GT(size, 0u);
}

TEST_F(JitCompilerTest, Compile_SimpleFunction_Succeeds) {
    JitCompiler compiler(config_, *buffer_, stencils_);

    std::vector<BytecodeInstr> instrs = {
        {.opcode = static_cast<std::uint8_t>(JitOpcode::ADD),
         .dst = 1,
         .src1 = 2,
         .src2 = 3,
         .pc = 0},
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

    // NOP (0x00) might not have a stencil
    const auto* invalid = registry.get(0xFE);  // Unlikely to have stencil
    EXPECT_EQ(invalid, nullptr);
}

TEST_F(StencilRegistryTest, Count_ReturnsPositive) {
    auto registry = StencilRegistry::create_default();
    EXPECT_GT(registry.count(), 0u);
}

}  // namespace
}  // namespace dotvm::jit
