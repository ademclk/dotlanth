/// @file interop_32_64_test.cpp
/// @brief Interoperability tests between 32-bit and 64-bit modes
///
/// These tests verify that 32-bit bytecode can be executed correctly
/// within a 64-bit VM, and that the architecture boundary behaviors
/// are consistent across all components.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "dotvm/core/arch_config.hpp"
#include "dotvm/core/alu.hpp"
#include "dotvm/core/register_file.hpp"
#include "dotvm/core/memory.hpp"
#include "dotvm/core/vm_context.hpp"
#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/instruction.hpp"

namespace dotvm::core {
namespace {

// ============================================================================
// Value Interoperability Tests
// ============================================================================

class ValueInteropTest : public ::testing::Test {};

TEST_F(ValueInteropTest, ValueCreatedInArch64_MaskedForArch32) {
    // Create a value that's valid in 64-bit but exceeds 32-bit range
    Value val = Value::from_int(0x1'0000'0000LL);

    // When masked for Arch32, should become 0
    Value masked = val.mask_to_arch(Architecture::Arch32);
    EXPECT_EQ(masked.as_integer(), 0);

    // When masked for Arch64, should be unchanged
    Value unchanged = val.mask_to_arch(Architecture::Arch64);
    EXPECT_EQ(unchanged.as_integer(), 0x1'0000'0000LL);
}

TEST_F(ValueInteropTest, ValueFromIntArch_DirectCreation) {
    // Create value directly with architecture
    Value v32 = Value::from_int(0x1'8000'0000LL, Architecture::Arch32);
    Value v64 = Value::from_int(0x1'8000'0000LL, Architecture::Arch64);

    // 32-bit version should be sign-extended from bit 31
    EXPECT_EQ(v32.as_integer(), std::numeric_limits<std::int32_t>::min());

    // 64-bit version should be unchanged
    EXPECT_EQ(v64.as_integer(), 0x1'8000'0000LL);
}

TEST_F(ValueInteropTest, NegativeOneConsistent) {
    // -1 should be the same in both architectures
    Value v32 = Value::from_int(-1, Architecture::Arch32);
    Value v64 = Value::from_int(-1, Architecture::Arch64);

    EXPECT_EQ(v32.as_integer(), -1);
    EXPECT_EQ(v64.as_integer(), -1);
}

// ============================================================================
// ALU Cross-Architecture Tests
// ============================================================================

class ALUInteropTest : public ::testing::Test {};

TEST_F(ALUInteropTest, SameResult_SmallValues) {
    ALU alu32{Architecture::Arch32};
    ALU alu64{Architecture::Arch64};

    // Small values should produce same results
    auto a = Value::from_int(1000);
    auto b = Value::from_int(2000);

    EXPECT_EQ(alu32.add(a, b).as_integer(), alu64.add(a, b).as_integer());
    EXPECT_EQ(alu32.sub(b, a).as_integer(), alu64.sub(b, a).as_integer());
    EXPECT_EQ(alu32.mul(a, b).as_integer(), alu64.mul(a, b).as_integer());
}

TEST_F(ALUInteropTest, DifferentResult_Overflow) {
    ALU alu32{Architecture::Arch32};
    ALU alu64{Architecture::Arch64};

    auto max32 = Value::from_int(std::numeric_limits<std::int32_t>::max());
    auto one = Value::from_int(1);

    auto result32 = alu32.add(max32, one);
    auto result64 = alu64.add(max32, one);

    // 32-bit wraps, 64-bit doesn't
    EXPECT_EQ(result32.as_integer(), std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(result64.as_integer(),
              static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1);
}

TEST_F(ALUInteropTest, Shift_DifferentBoundaries) {
    ALU alu32{Architecture::Arch32};
    ALU alu64{Architecture::Arch64};

    auto one = Value::from_int(1);

    // Shifting by 32 in Arch32 should wrap to shift by 0
    auto shift32 = Value::from_int(32);
    EXPECT_EQ(alu32.shl(one, shift32).as_integer(), 1);  // 1 << 0 = 1

    // Shifting by 32 in Arch64 should work
    EXPECT_EQ(alu64.shl(one, shift32).as_integer(), 0x1'0000'0000LL);
}

// ============================================================================
// Register File Interoperability Tests
// ============================================================================

class RegisterFileInteropTest : public ::testing::Test {};

TEST_F(RegisterFileInteropTest, WriteInArch64_ReadInArch32Mode) {
    ArchRegisterFile rf{Architecture::Arch64};

    // Write a large value in 64-bit mode
    rf.write(1, Value::from_int(0x1'0000'0000LL));
    EXPECT_EQ(rf.read(1).as_integer(), 0x1'0000'0000LL);

    // Switch to 32-bit mode - existing values stay but new writes are masked
    rf.set_arch(Architecture::Arch32);

    // Existing value is unchanged when read
    EXPECT_EQ(rf.read(1).as_integer(), 0x1'0000'0000LL);

    // New writes are masked
    rf.write(2, Value::from_int(0x1'0000'0000LL));
    EXPECT_EQ(rf.read(2).as_integer(), 0);
}

TEST_F(RegisterFileInteropTest, SameOperationsBothModes) {
    ArchRegisterFile rf32{Architecture::Arch32};
    ArchRegisterFile rf64{Architecture::Arch64};

    // Small values behave identically
    rf32.write(1, Value::from_int(42));
    rf64.write(1, Value::from_int(42));

    EXPECT_EQ(rf32.read(1).as_integer(), rf64.read(1).as_integer());
}

// ============================================================================
// Address Space Interoperability Tests
// ============================================================================

class AddressInteropTest : public ::testing::Test {};

TEST_F(AddressInteropTest, AddressComputation_Arch32_Wraps) {
    // Address at 4GB boundary should wrap in 32-bit mode
    auto addr = MemoryManager::compute_address(
        0xFFFF'0000, 0x2'0000, Architecture::Arch32);

    // 0xFFFF'0000 + 0x2'0000 = 0x1'0001'0000 -> masked to 0x0001'0000
    EXPECT_EQ(addr, 0x0001'0000u);
}

TEST_F(AddressInteropTest, AddressComputation_Arch64_NoWrap) {
    auto addr = MemoryManager::compute_address(
        0xFFFF'0000, 0x2'0000, Architecture::Arch64);

    // In 64-bit mode, no wrapping
    EXPECT_EQ(addr, 0x1'0001'0000u);
}

TEST_F(AddressInteropTest, AddressRange_Arch32_Limited) {
    // 4GB exactly is out of range for 32-bit
    EXPECT_FALSE(MemoryManager::address_in_range(
        0x1'0000'0000ULL, 0, Architecture::Arch32));

    // Just under 4GB is valid
    EXPECT_TRUE(MemoryManager::address_in_range(
        0xFFFF'FFFF, 0, Architecture::Arch32));
}

TEST_F(AddressInteropTest, AddressRange_Arch64_Extended) {
    // Values above 4GB are valid in 64-bit mode
    EXPECT_TRUE(MemoryManager::address_in_range(
        0x1'0000'0000ULL, 0, Architecture::Arch64));

    // Up to 48-bit range
    EXPECT_TRUE(MemoryManager::address_in_range(
        0xFFFF'FFFF'FFFFULL, 0, Architecture::Arch64));
}

// ============================================================================
// VmContext Mode Switching Tests
// ============================================================================

class VmContextModeTest : public ::testing::Test {};

TEST_F(VmContextModeTest, ContextsOperateIndependently) {
    VmContext ctx32{Architecture::Arch32};
    VmContext ctx64{Architecture::Arch64};

    // Same large value
    auto val = ctx32.make_int(0x1'0000'0000LL);
    auto val64 = ctx64.make_int(0x1'0000'0000LL);

    // 32-bit context masks it
    EXPECT_EQ(val.as_integer(), 0);

    // 64-bit context preserves it
    EXPECT_EQ(val64.as_integer(), 0x1'0000'0000LL);
}

TEST_F(VmContextModeTest, ALU_FollowsContextArchitecture) {
    VmContext ctx32{Architecture::Arch32};

    auto max = Value::from_int(std::numeric_limits<std::int32_t>::max());
    auto one = Value::from_int(1);

    // Use context's ALU
    auto result = ctx32.alu().add(max, one);

    // Should wrap
    EXPECT_EQ(result.as_integer(), std::numeric_limits<std::int32_t>::min());
}

// ============================================================================
// Bytecode Header Extraction Tests
// ============================================================================

class BytecodeHeaderInteropTest : public ::testing::Test {};

TEST_F(BytecodeHeaderInteropTest, ExtractConfig_Arch32) {
    auto header = make_header(
        Architecture::Arch32,
        bytecode::FLAG_DEBUG,
        0, 48, 0, 48, 0
    );

    auto config = extract_vm_config(header);
    EXPECT_EQ(config.arch, Architecture::Arch32);
    EXPECT_TRUE(config.strict_overflow);  // From FLAG_DEBUG
}

TEST_F(BytecodeHeaderInteropTest, ExtractConfig_Arch64) {
    auto header = make_header(
        Architecture::Arch64,
        bytecode::FLAG_NONE,
        0, 48, 0, 48, 0
    );

    auto config = extract_vm_config(header);
    EXPECT_EQ(config.arch, Architecture::Arch64);
    EXPECT_FALSE(config.strict_overflow);
}

// ============================================================================
// End-to-End Interoperability Tests
// ============================================================================

class EndToEndInteropTest : public ::testing::Test {};

TEST_F(EndToEndInteropTest, FullProgram_Arch32_Overflow) {
    VmContext ctx{Architecture::Arch32};

    // Simulate a program that adds two numbers causing overflow
    ctx.registers().write(1, Value::from_int(std::numeric_limits<std::int32_t>::max()));
    ctx.registers().write(2, Value::from_int(100));

    auto r1 = ctx.registers().read(1);
    auto r2 = ctx.registers().read(2);
    auto result = ctx.alu().add(r1, r2);

    ctx.registers().write(3, result);

    // Result should wrap around
    // INT32_MAX + 100 wraps to INT32_MIN + 99
    std::int64_t expected = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) + 99;
    EXPECT_EQ(ctx.registers().read(3).as_integer(), expected);
}

TEST_F(EndToEndInteropTest, FullProgram_Arch64_NoOverflow) {
    VmContext ctx{Architecture::Arch64};

    // Same program in 64-bit mode
    ctx.registers().write(1, Value::from_int(std::numeric_limits<std::int32_t>::max()));
    ctx.registers().write(2, Value::from_int(100));

    auto r1 = ctx.registers().read(1);
    auto r2 = ctx.registers().read(2);
    auto result = ctx.alu().add(r1, r2);

    ctx.registers().write(3, result);

    // No overflow in 64-bit mode
    auto expected = static_cast<std::int64_t>(
        std::numeric_limits<std::int32_t>::max()) + 100;
    EXPECT_EQ(ctx.registers().read(3).as_integer(), expected);
}

TEST_F(EndToEndInteropTest, MixedValues_SameBehavior) {
    VmContext ctx32{Architecture::Arch32};
    VmContext ctx64{Architecture::Arch64};

    // Operations on values within 32-bit range should be identical
    auto a = Value::from_int(1000);
    auto b = Value::from_int(2000);

    ctx32.registers().write(1, a);
    ctx32.registers().write(2, b);
    ctx64.registers().write(1, a);
    ctx64.registers().write(2, b);

    auto sum32 = ctx32.alu().add(ctx32.registers().read(1), ctx32.registers().read(2));
    auto sum64 = ctx64.alu().add(ctx64.registers().read(1), ctx64.registers().read(2));

    EXPECT_EQ(sum32.as_integer(), sum64.as_integer());
    EXPECT_EQ(sum32.as_integer(), 3000);
}

// ============================================================================
// Encoding Preservation Tests
// ============================================================================

class EncodingPreservationTest : public ::testing::Test {};

TEST_F(EncodingPreservationTest, InstructionFormat_SameAcrossArchitectures) {
    // Instructions are 32-bit in both modes - just the value handling differs

    // Type A instruction encoding should be identical
    auto instr32 = encode_type_a(0x00, 1, 2, 3);  // ADD R1, R2, R3
    auto instr64 = encode_type_a(0x00, 1, 2, 3);

    EXPECT_EQ(instr32, instr64);

    // Type B instruction encoding
    auto imm32 = encode_type_b(0x80, 1, 0x1234);  // LOADI R1, 0x1234
    auto imm64 = encode_type_b(0x80, 1, 0x1234);

    EXPECT_EQ(imm32, imm64);
}

TEST_F(EncodingPreservationTest, ConstantPool_IntegerRange_Arch32) {
    // Integers in constant pool are 64-bit, but should be validated
    // for the target architecture

    // Value within 32-bit range is valid for both
    EXPECT_TRUE(arch_config::fits_in_arch(42, Architecture::Arch32));
    EXPECT_TRUE(arch_config::fits_in_arch(42, Architecture::Arch64));

    // Value exceeding 32-bit range
    EXPECT_FALSE(arch_config::fits_in_arch(0x1'0000'0000LL, Architecture::Arch32));
    EXPECT_TRUE(arch_config::fits_in_arch(0x1'0000'0000LL, Architecture::Arch64));
}

}  // namespace
}  // namespace dotvm::core
