/// @file vm_context_simd_test.cpp
/// @brief Unit tests for VmContext SIMD integration

#include <cstdint>

#include <gtest/gtest.h>

#include "dotvm/core/simd/cpu_features.hpp"
#include "dotvm/core/simd/vector_types.hpp"
#include "dotvm/core/vm_context.hpp"

namespace dotvm::core {
namespace {

// ============================================================================
// VmConfig SIMD Tests
// ============================================================================

class VmConfigSimdTest : public ::testing::Test {};

TEST_F(VmConfigSimdTest, DefaultConfig_SimdDisabled) {
    VmConfig config;
    EXPECT_FALSE(config.simd_enabled);
    EXPECT_EQ(config.simd_width, 0u);
}

TEST_F(VmConfigSimdTest, ScalarOnly_ExplicitlyDisablesSimd) {
    auto config = VmConfig::scalar_only();
    EXPECT_FALSE(config.simd_enabled);
    EXPECT_EQ(config.simd_width, 0u);
}

TEST_F(VmConfigSimdTest, Simd128_Enables128BitSimd) {
    auto config = VmConfig::simd128();
    EXPECT_TRUE(config.simd_enabled);
    EXPECT_EQ(config.simd_width, 128u);
}

TEST_F(VmConfigSimdTest, Simd256_Enables256BitSimd) {
    auto config = VmConfig::simd256();
    EXPECT_TRUE(config.simd_enabled);
    EXPECT_EQ(config.simd_width, 256u);
}

TEST_F(VmConfigSimdTest, Simd512_Enables512BitSimd) {
    auto config = VmConfig::simd512();
    EXPECT_TRUE(config.simd_enabled);
    EXPECT_EQ(config.simd_width, 512u);
}

TEST_F(VmConfigSimdTest, AutoDetect_EnablesSimdWithZeroWidth) {
    auto config = VmConfig::auto_detect();
    EXPECT_TRUE(config.simd_enabled);
    EXPECT_EQ(config.simd_width, 0u);  // 0 = auto-detect
}

TEST_F(VmConfigSimdTest, Equality_SimdFieldsIncluded) {
    VmConfig a = VmConfig::simd128();
    VmConfig b = VmConfig::simd128();
    EXPECT_EQ(a, b);

    VmConfig c = VmConfig::simd256();
    EXPECT_NE(a, c);
}

// ============================================================================
// VmContext with SIMD Disabled Tests
// ============================================================================

class VmContextSimdDisabledTest : public ::testing::Test {};

TEST_F(VmContextSimdDisabledTest, DefaultConstruction_SimdDisabled) {
    VmContext ctx;
    EXPECT_FALSE(ctx.simd_enabled());
    EXPECT_EQ(ctx.simd_alu(), nullptr);
}

TEST_F(VmContextSimdDisabledTest, ScalarOnly_SimdDisabled) {
    VmContext ctx{VmConfig::scalar_only()};
    EXPECT_FALSE(ctx.simd_enabled());
    EXPECT_EQ(ctx.simd_alu(), nullptr);
}

TEST_F(VmContextSimdDisabledTest, VecRegisters_StillAccessible) {
    VmContext ctx;  // SIMD disabled

    // Vector registers should still be accessible
    auto& vec_regs = ctx.vec_registers();
    EXPECT_EQ(vec_regs.size(), 32u);

    // V0 should be zero
    EXPECT_TRUE(vec_regs.read(0).is_zero());
}

TEST_F(VmContextSimdDisabledTest, Reset_ClearsVecRegisters) {
    VmContext ctx;

    // Write to vector register
    simd::VectorRegister reg;
    reg.qwords[0] = 0x123456789ABCDEF0ULL;
    ctx.vec_registers().write(1, reg);

    // Verify written
    EXPECT_FALSE(ctx.vec_registers().read(1).is_zero());

    // Reset
    ctx.reset();

    // V1 should be cleared
    EXPECT_TRUE(ctx.vec_registers().read(1).is_zero());
}

// ============================================================================
// VmContext with Auto-Detected SIMD Tests
// ============================================================================

class VmContextSimdAutoDetectTest : public ::testing::Test {};

TEST_F(VmContextSimdAutoDetectTest, AutoDetect_SimdEnabled) {
    VmContext ctx{VmConfig::auto_detect()};
    EXPECT_TRUE(ctx.simd_enabled());
}

TEST_F(VmContextSimdAutoDetectTest, AutoDetect_SimdAluNotNull) {
    VmContext ctx{VmConfig::auto_detect()};
    EXPECT_NE(ctx.simd_alu(), nullptr);
}

TEST_F(VmContextSimdAutoDetectTest, AutoDetect_HasValidArchitecture) {
    VmContext ctx{VmConfig::auto_detect()};

    auto arch = ctx.simd_architecture();
    // Should be one of the SIMD architectures (or Arch64 fallback)
    EXPECT_TRUE(arch == Architecture::Arch64 || arch == Architecture::Arch128 ||
                arch == Architecture::Arch256 || arch == Architecture::Arch512);
}

TEST_F(VmContextSimdAutoDetectTest, AutoDetect_ArchMatchesCpuCapabilities) {
    VmContext ctx{VmConfig::auto_detect()};

    const auto& features = simd::detect_cpu_features();
    auto detected_arch = ctx.simd_architecture();

    // The architecture should be supported by the CPU
    EXPECT_TRUE(features.supports_arch(detected_arch));
}

// ============================================================================
// VmContext with Forced SIMD Width Tests
// ============================================================================

class VmContextSimdForcedWidthTest : public ::testing::Test {};

TEST_F(VmContextSimdForcedWidthTest, Simd128_HasSimdAlu) {
    VmContext ctx{VmConfig::simd128()};
    EXPECT_TRUE(ctx.simd_enabled());
    EXPECT_NE(ctx.simd_alu(), nullptr);
}

TEST_F(VmContextSimdForcedWidthTest, Simd128_ArchitectureIsAtMost128) {
    VmContext ctx{VmConfig::simd128()};

    // If CPU supports 128-bit, should be Arch128
    // Otherwise falls back to what CPU supports
    auto arch = ctx.simd_architecture();
    auto width = arch_bit_width(arch);

    // Should not exceed requested width (unless CPU doesn't support it at all)
    const auto& features = simd::detect_cpu_features();
    if (features.supports_arch(Architecture::Arch128)) {
        EXPECT_LE(width, 128u);
    }
}

TEST_F(VmContextSimdForcedWidthTest, Simd256_RequestsHigherWidth) {
    VmContext ctx{VmConfig::simd256()};

    const auto& features = simd::detect_cpu_features();
    if (features.supports_arch(Architecture::Arch256)) {
        // If CPU supports 256-bit, should get it
        EXPECT_EQ(ctx.simd_architecture(), Architecture::Arch256);
    }
    // Otherwise falls back gracefully
}

TEST_F(VmContextSimdForcedWidthTest, Simd512_FallbackIfNotSupported) {
    VmContext ctx{VmConfig::simd512()};
    EXPECT_TRUE(ctx.simd_enabled());

    // ALU should exist even if 512 not supported
    EXPECT_NE(ctx.simd_alu(), nullptr);

    // Architecture should be supported by CPU
    const auto& features = simd::detect_cpu_features();
    EXPECT_TRUE(features.supports_arch(ctx.simd_architecture()));
}

// ============================================================================
// Vector Register Access Tests
// ============================================================================

class VmContextVecRegisterTest : public ::testing::Test {
protected:
    void SetUp() override { ctx_ = std::make_unique<VmContext>(VmConfig::simd128()); }

    std::unique_ptr<VmContext> ctx_;
};

TEST_F(VmContextVecRegisterTest, V0_AlwaysZero) {
    auto& regs = ctx_->vec_registers();

    // V0 should always be zero
    EXPECT_TRUE(regs.read(0).is_zero());

    // Writing to V0 should be ignored
    simd::VectorRegister reg;
    reg.qwords[0] = 0xFFFFFFFFFFFFFFFFULL;
    regs.write(0, reg);

    EXPECT_TRUE(regs.read(0).is_zero());
}

TEST_F(VmContextVecRegisterTest, Write_V1_ThroughV31) {
    auto& regs = ctx_->vec_registers();

    for (std::uint8_t i = 1; i < 32; ++i) {
        simd::VectorRegister reg;
        reg.qwords[0] = static_cast<std::uint64_t>(i);
        regs.write(i, reg);

        EXPECT_EQ(regs.read(i).qwords[0], static_cast<std::uint64_t>(i));
    }
}

TEST_F(VmContextVecRegisterTest, Read_128BitVector) {
    auto& regs = ctx_->vec_registers();

    // Set up a 128-bit vector
    simd::Vector128i32 v{1, 2, 3, 4};
    regs.write_v128i32(1, v);

    auto result = regs.read_v128i32(1);
    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[1], 2);
    EXPECT_EQ(result[2], 3);
    EXPECT_EQ(result[3], 4);
}

TEST_F(VmContextVecRegisterTest, ConstAccess_Works) {
    auto& regs = ctx_->vec_registers();

    simd::Vector128i32 v{10, 20, 30, 40};
    regs.write_v128i32(5, v);

    // Access through const context
    const VmContext& const_ctx = *ctx_;
    auto result = const_ctx.vec_registers().read_v128i32(5);

    EXPECT_EQ(result[0], 10);
    EXPECT_EQ(result[3], 40);
}

// ============================================================================
// SIMD ALU Access Tests
// ============================================================================

class VmContextSimdAluTest : public ::testing::Test {
protected:
    void SetUp() override { ctx_ = std::make_unique<VmContext>(VmConfig::auto_detect()); }

    std::unique_ptr<VmContext> ctx_;
};

TEST_F(VmContextSimdAluTest, SimdAlu_NotNull) {
    EXPECT_NE(ctx_->simd_alu(), nullptr);
}

TEST_F(VmContextSimdAluTest, SimdAlu_HasValidArch) {
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    auto arch = alu->arch();
    EXPECT_TRUE(is_valid_architecture(arch));
}

TEST_F(VmContextSimdAluTest, SimdAlu_Add128_Works) {
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    simd::Vector128i32 a{1, 2, 3, 4};
    simd::Vector128i32 b{10, 20, 30, 40};

    auto result = alu->add_v128i32(a, b);

    EXPECT_EQ(result[0], 11);
    EXPECT_EQ(result[1], 22);
    EXPECT_EQ(result[2], 33);
    EXPECT_EQ(result[3], 44);
}

TEST_F(VmContextSimdAluTest, SimdAlu_Sub128_Works) {
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    simd::Vector128i32 a{100, 200, 300, 400};
    simd::Vector128i32 b{10, 20, 30, 40};

    auto result = alu->sub_v128i32(a, b);

    EXPECT_EQ(result[0], 90);
    EXPECT_EQ(result[1], 180);
    EXPECT_EQ(result[2], 270);
    EXPECT_EQ(result[3], 360);
}

TEST_F(VmContextSimdAluTest, SimdAlu_Mul128_Works) {
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    simd::Vector128i32 a{2, 3, 4, 5};
    simd::Vector128i32 b{10, 10, 10, 10};

    auto result = alu->mul_v128i32(a, b);

    EXPECT_EQ(result[0], 20);
    EXPECT_EQ(result[1], 30);
    EXPECT_EQ(result[2], 40);
    EXPECT_EQ(result[3], 50);
}

TEST_F(VmContextSimdAluTest, SimdAlu_ConstAccess) {
    const VmContext& const_ctx = *ctx_;
    const auto* alu = const_ctx.simd_alu();

    EXPECT_NE(alu, nullptr);
    EXPECT_TRUE(is_valid_architecture(alu->arch()));
}

// ============================================================================
// SIMD ALU Register-Based Operations Tests
// ============================================================================

class VmContextSimdAluRegisterOpsTest : public ::testing::Test {
protected:
    void SetUp() override { ctx_ = std::make_unique<VmContext>(VmConfig::simd128()); }

    std::unique_ptr<VmContext> ctx_;
};

TEST_F(VmContextSimdAluRegisterOpsTest, VAdd_RegisterBased) {
    auto& regs = ctx_->vec_registers();
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    // Load source vectors into registers
    regs.write_v128i32(1, simd::Vector128i32{1, 2, 3, 4});
    regs.write_v128i32(2, simd::Vector128i32{10, 20, 30, 40});

    // Perform add: V3 = V1 + V2
    alu->vadd_i32(regs, 3, 1, 2);

    auto result = regs.read_v128i32(3);
    EXPECT_EQ(result[0], 11);
    EXPECT_EQ(result[1], 22);
    EXPECT_EQ(result[2], 33);
    EXPECT_EQ(result[3], 44);
}

TEST_F(VmContextSimdAluRegisterOpsTest, VSub_RegisterBased) {
    auto& regs = ctx_->vec_registers();
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    regs.write_v128i32(1, simd::Vector128i32{100, 200, 300, 400});
    regs.write_v128i32(2, simd::Vector128i32{10, 20, 30, 40});

    // V3 = V1 - V2
    alu->vsub_i32(regs, 3, 1, 2);

    auto result = regs.read_v128i32(3);
    EXPECT_EQ(result[0], 90);
    EXPECT_EQ(result[1], 180);
    EXPECT_EQ(result[2], 270);
    EXPECT_EQ(result[3], 360);
}

TEST_F(VmContextSimdAluRegisterOpsTest, VMul_RegisterBased) {
    auto& regs = ctx_->vec_registers();
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    regs.write_v128i32(1, simd::Vector128i32{2, 3, 4, 5});
    regs.write_v128i32(2, simd::Vector128i32{10, 10, 10, 10});

    // V3 = V1 * V2
    alu->vmul_i32(regs, 3, 1, 2);

    auto result = regs.read_v128i32(3);
    EXPECT_EQ(result[0], 20);
    EXPECT_EQ(result[1], 30);
    EXPECT_EQ(result[2], 40);
    EXPECT_EQ(result[3], 50);
}

TEST_F(VmContextSimdAluRegisterOpsTest, WriteToV0_Ignored) {
    auto& regs = ctx_->vec_registers();
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    regs.write_v128i32(1, simd::Vector128i32{1, 2, 3, 4});
    regs.write_v128i32(2, simd::Vector128i32{10, 20, 30, 40});

    // Try to write to V0 - should be ignored
    alu->vadd_i32(regs, 0, 1, 2);

    // V0 should still be zero
    EXPECT_TRUE(regs.read(0).is_zero());
}

// ============================================================================
// Float Vector Operations Tests
// ============================================================================

class VmContextSimdFloatTest : public ::testing::Test {
protected:
    void SetUp() override { ctx_ = std::make_unique<VmContext>(VmConfig::auto_detect()); }

    std::unique_ptr<VmContext> ctx_;
};

TEST_F(VmContextSimdFloatTest, AddFloat128_Works) {
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    simd::Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd::Vector128f32 b{0.5f, 0.5f, 0.5f, 0.5f};

    auto result = alu->add_v128f32(a, b);

    EXPECT_FLOAT_EQ(result[0], 1.5f);
    EXPECT_FLOAT_EQ(result[1], 2.5f);
    EXPECT_FLOAT_EQ(result[2], 3.5f);
    EXPECT_FLOAT_EQ(result[3], 4.5f);
}

TEST_F(VmContextSimdFloatTest, MulFloat128_Works) {
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    simd::Vector128f32 a{2.0f, 3.0f, 4.0f, 5.0f};
    simd::Vector128f32 b{0.5f, 0.5f, 0.5f, 0.5f};

    auto result = alu->mul_v128f32(a, b);

    EXPECT_FLOAT_EQ(result[0], 1.0f);
    EXPECT_FLOAT_EQ(result[1], 1.5f);
    EXPECT_FLOAT_EQ(result[2], 2.0f);
    EXPECT_FLOAT_EQ(result[3], 2.5f);
}

TEST_F(VmContextSimdFloatTest, DotProduct128_Works) {
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    simd::Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd::Vector128f32 b{1.0f, 1.0f, 1.0f, 1.0f};

    float result = alu->dot_v128f32(a, b);

    // 1*1 + 2*1 + 3*1 + 4*1 = 10
    EXPECT_FLOAT_EQ(result, 10.0f);
}

TEST_F(VmContextSimdFloatTest, FMA128_Works) {
    auto* alu = ctx_->simd_alu();
    ASSERT_NE(alu, nullptr);

    simd::Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    simd::Vector128f32 b{2.0f, 2.0f, 2.0f, 2.0f};
    simd::Vector128f32 c{10.0f, 10.0f, 10.0f, 10.0f};

    // a*b + c
    auto result = alu->fma_v128f32(a, b, c);

    EXPECT_FLOAT_EQ(result[0], 12.0f);  // 1*2 + 10
    EXPECT_FLOAT_EQ(result[1], 14.0f);  // 2*2 + 10
    EXPECT_FLOAT_EQ(result[2], 16.0f);  // 3*2 + 10
    EXPECT_FLOAT_EQ(result[3], 18.0f);  // 4*2 + 10
}

// ============================================================================
// Integration Tests: VmContext Full Workflow
// ============================================================================

class VmContextSimdIntegrationTest : public ::testing::Test {};

TEST_F(VmContextSimdIntegrationTest, FullWorkflow_ScalarAndSimd) {
    // Create context with both scalar and SIMD capabilities
    VmContext ctx{VmConfig::auto_detect()};

    // Use scalar registers
    ctx.registers().write(1, Value::from_int(100));
    ctx.registers().write(2, Value::from_int(200));
    auto scalar_sum = ctx.alu().add(ctx.registers().read(1), ctx.registers().read(2));
    ctx.registers().write(3, scalar_sum);

    EXPECT_EQ(ctx.registers().read(3).as_integer(), 300);

    // Use vector registers (if SIMD available)
    if (ctx.simd_enabled() && ctx.simd_alu() != nullptr) {
        ctx.vec_registers().write_v128i32(1, simd::Vector128i32{10, 20, 30, 40});
        ctx.vec_registers().write_v128i32(2, simd::Vector128i32{1, 2, 3, 4});

        ctx.simd_alu()->vadd_i32(ctx.vec_registers(), 3, 1, 2);

        auto vec_result = ctx.vec_registers().read_v128i32(3);
        EXPECT_EQ(vec_result[0], 11);
        EXPECT_EQ(vec_result[3], 44);
    }
}

TEST_F(VmContextSimdIntegrationTest, BackwardCompatibility_NoSimd) {
    // Default config should work exactly as before
    VmContext ctx;

    EXPECT_FALSE(ctx.simd_enabled());
    EXPECT_EQ(ctx.arch(), Architecture::Arch64);

    // All existing scalar operations should work
    ctx.registers().write(1, Value::from_int(42));
    EXPECT_EQ(ctx.registers().read(1).as_integer(), 42);

    auto result = ctx.memory().allocate(4096);
    EXPECT_TRUE(result.has_value());

    [[maybe_unused]] auto err = ctx.memory().deallocate(*result);
}

TEST_F(VmContextSimdIntegrationTest, Reset_ClearsBothRegisterFiles) {
    VmContext ctx{VmConfig::auto_detect()};

    // Write to both scalar and vector registers
    ctx.registers().write(1, Value::from_int(42));
    ctx.vec_registers().write_v128i32(1, simd::Vector128i32{1, 2, 3, 4});

    // Verify written
    EXPECT_EQ(ctx.registers().read(1).as_integer(), 42);
    EXPECT_FALSE(ctx.vec_registers().read(1).is_zero());

    // Reset
    ctx.reset();

    // Both should be cleared
    EXPECT_EQ(ctx.registers().read(1).as_float(), 0.0);
    EXPECT_TRUE(ctx.vec_registers().read(1).is_zero());
}

}  // namespace
}  // namespace dotvm::core
