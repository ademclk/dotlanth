/// @file vm_context_test.cpp
/// @brief Unit tests for the VM execution context

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "dotvm/core/vm_context.hpp"

namespace dotvm::core {
namespace {

// ============================================================================
// VmConfig Tests
// ============================================================================

class VmConfigTest : public ::testing::Test {};

TEST_F(VmConfigTest, DefaultConfig_IsArch64) {
    VmConfig config;
    EXPECT_EQ(config.arch, Architecture::Arch64);
    EXPECT_FALSE(config.strict_overflow);
}

TEST_F(VmConfigTest, ForArch_CreatesConfigWithArch) {
    auto config32 = VmConfig::for_arch(Architecture::Arch32);
    EXPECT_EQ(config32.arch, Architecture::Arch32);

    auto config64 = VmConfig::for_arch(Architecture::Arch64);
    EXPECT_EQ(config64.arch, Architecture::Arch64);
}

TEST_F(VmConfigTest, Arch32_CreatesArch32Config) {
    auto config = VmConfig::arch32();
    EXPECT_EQ(config.arch, Architecture::Arch32);
}

TEST_F(VmConfigTest, Arch64_CreatesArch64Config) {
    auto config = VmConfig::arch64();
    EXPECT_EQ(config.arch, Architecture::Arch64);
}

TEST_F(VmConfigTest, Equality_SameConfigs) {
    VmConfig a{.arch = Architecture::Arch32, .strict_overflow = true};
    VmConfig b{.arch = Architecture::Arch32, .strict_overflow = true};
    EXPECT_EQ(a, b);
}

TEST_F(VmConfigTest, Equality_DifferentConfigs) {
    VmConfig a{.arch = Architecture::Arch32};
    VmConfig b{.arch = Architecture::Arch64};
    EXPECT_NE(a, b);
}

// ============================================================================
// VmContext Construction Tests
// ============================================================================

class VmContextConstructionTest : public ::testing::Test {};

TEST_F(VmContextConstructionTest, DefaultConstruction) {
    VmContext ctx;
    EXPECT_EQ(ctx.arch(), Architecture::Arch64);
    EXPECT_FALSE(ctx.is_arch32());
    EXPECT_TRUE(ctx.is_arch64());
}

TEST_F(VmContextConstructionTest, ConstructWithConfig) {
    VmContext ctx{VmConfig::arch32()};
    EXPECT_EQ(ctx.arch(), Architecture::Arch32);
    EXPECT_TRUE(ctx.is_arch32());
    EXPECT_FALSE(ctx.is_arch64());
}

TEST_F(VmContextConstructionTest, ConstructWithArchitecture) {
    VmContext ctx{Architecture::Arch32};
    EXPECT_EQ(ctx.arch(), Architecture::Arch32);
}

TEST_F(VmContextConstructionTest, ConfigAccessible) {
    VmConfig config{
        .arch = Architecture::Arch32,
        .strict_overflow = true
    };
    VmContext ctx{config};

    EXPECT_EQ(ctx.config().arch, Architecture::Arch32);
    EXPECT_TRUE(ctx.config().strict_overflow);
}

// ============================================================================
// Value Creation Tests
// ============================================================================

class VmContextValueCreationTest : public ::testing::Test {};

TEST_F(VmContextValueCreationTest, MakeInt_Arch32_MasksValue) {
    VmContext ctx{Architecture::Arch32};

    auto val = ctx.make_int(0x1'0000'0000LL);
    EXPECT_EQ(val.as_integer(), 0);  // Masked to 32 bits
}

TEST_F(VmContextValueCreationTest, MakeInt_Arch32_SmallValue_Unchanged) {
    VmContext ctx{Architecture::Arch32};

    auto val = ctx.make_int(42);
    EXPECT_EQ(val.as_integer(), 42);
}

TEST_F(VmContextValueCreationTest, MakeInt_Arch64_PreservesLargeValue) {
    VmContext ctx{Architecture::Arch64};

    auto val = ctx.make_int(0x1'0000'0000LL);
    EXPECT_EQ(val.as_integer(), 0x1'0000'0000LL);
}

TEST_F(VmContextValueCreationTest, MaskValue_Arch32_MasksInteger) {
    VmContext ctx{Architecture::Arch32};

    auto val = Value::from_int(0x1'0000'0000LL);
    auto masked = ctx.mask_value(val);
    EXPECT_EQ(masked.as_integer(), 0);
}

TEST_F(VmContextValueCreationTest, MaskValue_NonInteger_Unchanged) {
    VmContext ctx{Architecture::Arch32};

    auto val = Value::from_float(3.14159);
    auto masked = ctx.mask_value(val);
    EXPECT_DOUBLE_EQ(masked.as_float(), 3.14159);
}

// ============================================================================
// Address Computation Tests
// ============================================================================

class VmContextAddressTest : public ::testing::Test {};

TEST_F(VmContextAddressTest, MaskAddress_Arch32_Truncates) {
    VmContext ctx{Architecture::Arch32};

    auto addr = ctx.mask_address(0x1'0000'0000ULL);
    EXPECT_EQ(addr, 0u);
}

TEST_F(VmContextAddressTest, MaskAddress_Arch64_Preserves) {
    VmContext ctx{Architecture::Arch64};

    auto addr = ctx.mask_address(0x1'0000'0000ULL);
    EXPECT_EQ(addr, 0x1'0000'0000ULL);
}

TEST_F(VmContextAddressTest, ComputeAddress_Arch32) {
    VmContext ctx{Architecture::Arch32};

    // 0xFFFF'0000 + 0x2'0000 = 0x1'0001'0000 -> masked to 0x0001'0000
    auto addr = ctx.compute_address(0xFFFF'0000, 0x2'0000);
    EXPECT_EQ(addr, 0x0001'0000u);
}

TEST_F(VmContextAddressTest, IsValidAddress_Arch32) {
    VmContext ctx{Architecture::Arch32};

    EXPECT_TRUE(ctx.is_valid_address(0));
    EXPECT_TRUE(ctx.is_valid_address(0xFFFF'FFFFU));
    EXPECT_FALSE(ctx.is_valid_address(0x1'0000'0000ULL));
}

TEST_F(VmContextAddressTest, IsValidAddress_Arch64) {
    VmContext ctx{Architecture::Arch64};

    EXPECT_TRUE(ctx.is_valid_address(0x1'0000'0000ULL));
    EXPECT_TRUE(ctx.is_valid_address(0xFFFF'FFFF'FFFFULL));
    EXPECT_FALSE(ctx.is_valid_address(0x1'0000'0000'0000ULL));  // 49 bits
}

// ============================================================================
// Component Access Tests
// ============================================================================

class VmContextComponentsTest : public ::testing::Test {};

TEST_F(VmContextComponentsTest, Registers_WriteRead) {
    VmContext ctx{Architecture::Arch32};

    ctx.registers().write(1, Value::from_int(42));
    EXPECT_EQ(ctx.registers().read(1).as_integer(), 42);
}

TEST_F(VmContextComponentsTest, Registers_MaskingApplied) {
    VmContext ctx{Architecture::Arch32};

    ctx.registers().write(1, Value::from_int(0x1'0000'0000LL));
    EXPECT_EQ(ctx.registers().read(1).as_integer(), 0);  // Masked
}

TEST_F(VmContextComponentsTest, Memory_Allocate) {
    VmContext ctx;

    auto result = ctx.memory().allocate(4096);
    EXPECT_EQ(result.second, MemoryError::Success);
    EXPECT_NE(result.first.index, mem_config::INVALID_INDEX);

    // Clean up
    [[maybe_unused]] auto err = ctx.memory().deallocate(result.first);
}

TEST_F(VmContextComponentsTest, ALU_UsesContextArch) {
    VmContext ctx{Architecture::Arch32};

    auto a = Value::from_int(std::numeric_limits<std::int32_t>::max());
    auto b = Value::from_int(1);

    // Should wrap due to Arch32
    auto result = ctx.alu().add(a, b);
    EXPECT_EQ(result.as_integer(), std::numeric_limits<std::int32_t>::min());
}

TEST_F(VmContextComponentsTest, ConstAccess) {
    const VmContext ctx{Architecture::Arch32};

    // Should compile - const access
    EXPECT_EQ(ctx.registers().read(0).as_float(), 0.0);
    EXPECT_EQ(ctx.alu().arch(), Architecture::Arch32);
}

// ============================================================================
// Reset Tests
// ============================================================================

class VmContextResetTest : public ::testing::Test {};

TEST_F(VmContextResetTest, Reset_ClearsRegisters) {
    VmContext ctx{Architecture::Arch32};

    ctx.registers().write(1, Value::from_int(42));
    ctx.registers().write(10, Value::from_int(100));

    ctx.reset();

    // Registers should be cleared
    EXPECT_EQ(ctx.registers().read(1).as_float(), 0.0);
    EXPECT_EQ(ctx.registers().read(10).as_float(), 0.0);
}

TEST_F(VmContextResetTest, Reset_PreservesConfig) {
    VmContext ctx{Architecture::Arch32};

    ctx.reset();

    EXPECT_EQ(ctx.arch(), Architecture::Arch32);
}

// ============================================================================
// Stats Tests
// ============================================================================

class VmContextStatsTest : public ::testing::Test {};

TEST_F(VmContextStatsTest, Stats_InitiallyZero) {
    VmContext ctx;

    auto stats = ctx.stats();
    EXPECT_EQ(stats.active_allocations, 0u);
    EXPECT_EQ(stats.total_allocated_bytes, 0u);
}

TEST_F(VmContextStatsTest, Stats_AfterAllocation) {
    VmContext ctx;

    auto result = ctx.memory().allocate(4096);
    EXPECT_EQ(result.second, MemoryError::Success);

    auto stats = ctx.stats();
    EXPECT_EQ(stats.active_allocations, 1u);
    EXPECT_GE(stats.total_allocated_bytes, 4096u);

    [[maybe_unused]] auto dealloc_err = ctx.memory().deallocate(result.first);
}

// ============================================================================
// Integration Tests
// ============================================================================

class VmContextIntegrationTest : public ::testing::Test {};

TEST_F(VmContextIntegrationTest, FullWorkflow_Arch32) {
    VmContext ctx{Architecture::Arch32};

    // Create values
    auto a = ctx.make_int(100);
    auto b = ctx.make_int(200);

    // Store in registers
    ctx.registers().write(1, a);
    ctx.registers().write(2, b);

    // Perform arithmetic
    auto sum = ctx.alu().add(ctx.registers().read(1), ctx.registers().read(2));
    ctx.registers().write(3, sum);

    EXPECT_EQ(ctx.registers().read(3).as_integer(), 300);
}

TEST_F(VmContextIntegrationTest, OverflowBehavior_Arch32) {
    VmContext ctx{Architecture::Arch32};

    // Set up for overflow
    auto max = ctx.make_int(std::numeric_limits<std::int32_t>::max());
    auto one = ctx.make_int(1);

    ctx.registers().write(1, max);
    ctx.registers().write(2, one);

    // Add with overflow
    auto result = ctx.alu().add(ctx.registers().read(1), ctx.registers().read(2));

    // Should wrap to INT32_MIN
    EXPECT_EQ(result.as_integer(), std::numeric_limits<std::int32_t>::min());
}

}  // namespace
}  // namespace dotvm::core
