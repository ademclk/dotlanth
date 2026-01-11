/// @file arch_config_test.cpp
/// @brief Unit tests for architecture configuration and masking operations

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "dotvm/core/arch_config.hpp"

namespace dotvm::core {
namespace {

using namespace arch_config;

// ============================================================================
// Architecture Detection Tests
// ============================================================================

class ArchDetectionTest : public ::testing::Test {};

TEST_F(ArchDetectionTest, IsArch32_ReturnsTrueForArch32) {
    EXPECT_TRUE(is_arch32(Architecture::Arch32));
    EXPECT_FALSE(is_arch32(Architecture::Arch64));
}

TEST_F(ArchDetectionTest, IsArch64_ReturnsTrueForArch64) {
    EXPECT_TRUE(is_arch64(Architecture::Arch64));
    EXPECT_FALSE(is_arch64(Architecture::Arch32));
}

TEST_F(ArchDetectionTest, IntWidth_Returns32ForArch32) {
    EXPECT_EQ(int_width(Architecture::Arch32), 32u);
    EXPECT_EQ(int_width(Architecture::Arch64), 48u);
}

TEST_F(ArchDetectionTest, AddrWidth_Returns32ForArch32) {
    EXPECT_EQ(addr_width(Architecture::Arch32), 32u);
    EXPECT_EQ(addr_width(Architecture::Arch64), 48u);
}

// ============================================================================
// Integer Masking Tests - 32-bit Mode
// ============================================================================

class MaskInt32Test : public ::testing::Test {};

TEST_F(MaskInt32Test, PositiveValueWithin32Bits_Unchanged) {
    EXPECT_EQ(mask_int32(0), 0);
    EXPECT_EQ(mask_int32(1), 1);
    EXPECT_EQ(mask_int32(42), 42);
    EXPECT_EQ(mask_int32(INT32_MAX_VAL), INT32_MAX_VAL);
}

TEST_F(MaskInt32Test, NegativeValueWithin32Bits_Unchanged) {
    EXPECT_EQ(mask_int32(-1), -1);
    EXPECT_EQ(mask_int32(-42), -42);
    EXPECT_EQ(mask_int32(INT32_MIN_VAL), INT32_MIN_VAL);
}

TEST_F(MaskInt32Test, ValueExceeding32Bits_Truncated) {
    // 0x1'0000'0000 (2^32) should become 0 after masking
    constexpr std::int64_t two_pow_32 = 0x1'0000'0000LL;
    EXPECT_EQ(mask_int32(two_pow_32), 0);
}

TEST_F(MaskInt32Test, ValueExceeding32BitsWithLowerBits_TruncatedWithSignExtension) {
    // 0x1'0000'0001 should become 1
    constexpr std::int64_t val1 = 0x1'0000'0001LL;
    EXPECT_EQ(mask_int32(val1), 1);

    // 0x1'8000'0000 should become -2147483648 (INT32_MIN) due to sign extension
    constexpr std::int64_t val2 = 0x1'8000'0000LL;
    EXPECT_EQ(mask_int32(val2), INT32_MIN_VAL);
}

TEST_F(MaskInt32Test, SignExtension_NegativeValuesCorrect) {
    // 0xFFFF'FFFF should become -1 (sign-extended)
    constexpr std::int64_t all_ones_32 = 0xFFFF'FFFFLL;
    EXPECT_EQ(mask_int32(all_ones_32), -1);

    // 0x8000'0000 should become INT32_MIN (sign-extended)
    constexpr std::int64_t min_32 = 0x8000'0000LL;
    EXPECT_EQ(mask_int32(min_32), INT32_MIN_VAL);
}

TEST_F(MaskInt32Test, OverflowWrap_INT32_MAX_Plus1_BecomesINT32_MIN) {
    // INT32_MAX + 1 should wrap to INT32_MIN
    constexpr std::int64_t overflow = static_cast<std::int64_t>(INT32_MAX_VAL) + 1;
    EXPECT_EQ(mask_int32(overflow), INT32_MIN_VAL);
}

TEST_F(MaskInt32Test, UnderflowWrap_INT32_MIN_Minus1_BecomesINT32_MAX) {
    // INT32_MIN - 1 should wrap to INT32_MAX
    constexpr std::int64_t underflow = static_cast<std::int64_t>(INT32_MIN_VAL) - 1;
    EXPECT_EQ(mask_int32(underflow), INT32_MAX_VAL);
}

// ============================================================================
// Architecture-Aware Integer Masking Tests
// ============================================================================

class MaskIntArchTest : public ::testing::Test {};

TEST_F(MaskIntArchTest, Arch32_MasksTo32Bits) {
    constexpr std::int64_t large_val = 0x1'2345'6789LL;
    // Lower 32 bits: 0x2345'6789 - this is positive (bit 31 is 0)
    EXPECT_EQ(mask_int(large_val, Architecture::Arch32), 0x2345'6789LL);
}

TEST_F(MaskIntArchTest, Arch64_ReturnsUnchanged) {
    constexpr std::int64_t large_val = 0x1'2345'6789LL;
    EXPECT_EQ(mask_int(large_val, Architecture::Arch64), large_val);
}

TEST_F(MaskIntArchTest, Arch32_NegativeValue_SignExtended) {
    // Value where lower 32 bits have bit 31 set
    constexpr std::int64_t val = 0xFFFF'FFFFLL;  // -1 in 32-bit
    EXPECT_EQ(mask_int(val, Architecture::Arch32), -1);
}

TEST_F(MaskIntArchTest, Arch64_NegativeValue_Unchanged) {
    constexpr std::int64_t val = -1;
    EXPECT_EQ(mask_int(val, Architecture::Arch64), -1);
}

// ============================================================================
// Unsigned Integer Masking Tests
// ============================================================================

class MaskUintTest : public ::testing::Test {};

TEST_F(MaskUintTest, Arch32_MasksTo32Bits_NoSignExtension) {
    constexpr std::uint64_t val = 0x1'FFFF'FFFFULL;
    EXPECT_EQ(mask_uint(val, Architecture::Arch32), 0xFFFF'FFFFULL);
}

TEST_F(MaskUintTest, Arch64_ReturnsUnchanged) {
    constexpr std::uint64_t val = 0x1'FFFF'FFFFULL;
    EXPECT_EQ(mask_uint(val, Architecture::Arch64), val);
}

TEST_F(MaskUintTest, Arch32_HighBitSet_NoSignExtension) {
    // Unlike signed, 0x8000'0000 stays as-is (no sign extension)
    constexpr std::uint64_t val = 0x8000'0000ULL;
    EXPECT_EQ(mask_uint32(val), 0x8000'0000ULL);
}

// ============================================================================
// Address Masking Tests
// ============================================================================

class MaskAddrTest : public ::testing::Test {};

TEST_F(MaskAddrTest, Arch32_TruncatesTo4GB) {
    constexpr std::uint64_t addr_5GB = 0x1'4000'0000ULL;  // 5 GB
    EXPECT_EQ(mask_addr(addr_5GB, Architecture::Arch32), 0x4000'0000ULL);  // 1 GB
}

TEST_F(MaskAddrTest, Arch32_MaxAddress) {
    EXPECT_EQ(mask_addr(ADDR32_MAX, Architecture::Arch32), ADDR32_MAX);
}

TEST_F(MaskAddrTest, Arch32_AddressAt4GB_BecomesZero) {
    constexpr std::uint64_t addr_4GB = 0x1'0000'0000ULL;  // 4 GB exactly
    EXPECT_EQ(mask_addr(addr_4GB, Architecture::Arch32), 0);
}

TEST_F(MaskAddrTest, Arch64_MasksTo48Bits) {
    constexpr std::uint64_t large_addr = 0xFFFF'1234'5678'9ABCULL;
    // Should mask to 48-bit canonical form
    EXPECT_EQ(mask_addr(large_addr, Architecture::Arch64), 0x1234'5678'9ABCULL);
}

TEST_F(MaskAddrTest, Arch64_48BitAddress_Unchanged) {
    constexpr std::uint64_t addr = 0x0000'FFFF'FFFF'FFFFULL;
    EXPECT_EQ(mask_addr(addr, Architecture::Arch64), addr);
}

// ============================================================================
// Range Validation Tests
// ============================================================================

class FitsInRangeTest : public ::testing::Test {};

TEST_F(FitsInRangeTest, FitsInInt32_BoundaryValues) {
    EXPECT_TRUE(fits_in_int32(0));
    EXPECT_TRUE(fits_in_int32(INT32_MAX_VAL));
    EXPECT_TRUE(fits_in_int32(INT32_MIN_VAL));

    EXPECT_FALSE(fits_in_int32(static_cast<std::int64_t>(INT32_MAX_VAL) + 1));
    EXPECT_FALSE(fits_in_int32(static_cast<std::int64_t>(INT32_MIN_VAL) - 1));
}

TEST_F(FitsInRangeTest, FitsInInt48_BoundaryValues) {
    EXPECT_TRUE(fits_in_int48(0));
    EXPECT_TRUE(fits_in_int48(INT48_MAX));
    EXPECT_TRUE(fits_in_int48(INT48_MIN));

    EXPECT_FALSE(fits_in_int48(INT48_MAX + 1));
    EXPECT_FALSE(fits_in_int48(INT48_MIN - 1));
}

TEST_F(FitsInRangeTest, FitsInArch_UsesCorrectRange) {
    // Value that fits in 48-bit but not 32-bit
    constexpr std::int64_t val = 0x1'0000'0000LL;  // 2^32

    EXPECT_FALSE(fits_in_arch(val, Architecture::Arch32));
    EXPECT_TRUE(fits_in_arch(val, Architecture::Arch64));
}

TEST_F(FitsInRangeTest, AddrFitsInArch_ChecksCorrectRange) {
    constexpr std::uint64_t addr_5GB = 0x1'4000'0000ULL;

    EXPECT_FALSE(addr_fits_in_arch(addr_5GB, Architecture::Arch32));
    EXPECT_TRUE(addr_fits_in_arch(addr_5GB, Architecture::Arch64));
}

// ============================================================================
// Shift Amount Masking Tests
// ============================================================================

class MaskShiftTest : public ::testing::Test {};

TEST_F(MaskShiftTest, Arch32_MasksTo0_31) {
    EXPECT_EQ(mask_shift(0, Architecture::Arch32), 0);
    EXPECT_EQ(mask_shift(31, Architecture::Arch32), 31);
    EXPECT_EQ(mask_shift(32, Architecture::Arch32), 0);   // Wraps
    EXPECT_EQ(mask_shift(33, Architecture::Arch32), 1);   // Wraps
    EXPECT_EQ(mask_shift(63, Architecture::Arch32), 31);  // Wraps
}

TEST_F(MaskShiftTest, Arch64_MasksTo0_47) {
    EXPECT_EQ(mask_shift(0, Architecture::Arch64), 0);
    EXPECT_EQ(mask_shift(47, Architecture::Arch64), 47);
    EXPECT_EQ(mask_shift(48, Architecture::Arch64), 0);   // 48 % 48 = 0
    EXPECT_EQ(mask_shift(49, Architecture::Arch64), 1);   // 49 % 48 = 1
    EXPECT_EQ(mask_shift(63, Architecture::Arch64), 15);  // 63 % 48 = 15 (wraps to 0-47 range)
}

TEST_F(MaskShiftTest, NegativeShift_MasksToPositive) {
    // Negative shifts should wrap to positive values
    EXPECT_EQ(mask_shift(-1, Architecture::Arch32), 31);  // -1 & 31 = 31 (bitwise AND)
    EXPECT_EQ(mask_shift(-1, Architecture::Arch64), 47);  // ((-1 % 48) + 48) % 48 = 47
}

// ============================================================================
// Constexpr Validation Tests
// ============================================================================

class ConstexprTest : public ::testing::Test {};

TEST_F(ConstexprTest, AllFunctionsAreConstexpr) {
    // These should all compile as constexpr
    constexpr bool is32 = is_arch32(Architecture::Arch32);
    constexpr bool is64 = is_arch64(Architecture::Arch64);
    constexpr std::size_t iw = int_width(Architecture::Arch32);
    constexpr std::size_t aw = addr_width(Architecture::Arch32);
    constexpr std::int64_t mi32 = mask_int32(42);
    constexpr std::int64_t mi = mask_int(42, Architecture::Arch32);
    constexpr std::uint64_t mu = mask_uint(42, Architecture::Arch32);
    constexpr std::uint64_t ma = mask_addr(42, Architecture::Arch32);
    constexpr bool fi32 = fits_in_int32(42);
    constexpr bool fi48 = fits_in_int48(42);
    constexpr bool fia = fits_in_arch(42, Architecture::Arch32);
    constexpr bool afa = addr_fits_in_arch(42, Architecture::Arch32);
    constexpr std::int64_t ms = mask_shift(10, Architecture::Arch32);

    EXPECT_TRUE(is32);
    EXPECT_TRUE(is64);
    EXPECT_EQ(iw, 32u);
    EXPECT_EQ(aw, 32u);
    EXPECT_EQ(mi32, 42);
    EXPECT_EQ(mi, 42);
    EXPECT_EQ(mu, 42u);
    EXPECT_EQ(ma, 42u);
    EXPECT_TRUE(fi32);
    EXPECT_TRUE(fi48);
    EXPECT_TRUE(fia);
    EXPECT_TRUE(afa);
    EXPECT_EQ(ms, 10);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

class EdgeCaseTest : public ::testing::Test {};

TEST_F(EdgeCaseTest, Zero_HandledCorrectly) {
    EXPECT_EQ(mask_int(0, Architecture::Arch32), 0);
    EXPECT_EQ(mask_int(0, Architecture::Arch64), 0);
    EXPECT_EQ(mask_addr(0, Architecture::Arch32), 0u);
    EXPECT_EQ(mask_addr(0, Architecture::Arch64), 0u);
}

TEST_F(EdgeCaseTest, AllBitsSet_HandledCorrectly) {
    constexpr std::int64_t all_bits = -1;  // 0xFFFF'FFFF'FFFF'FFFF as signed

    // In Arch32, should become -1 (lower 32 bits are all 1s, sign-extended)
    EXPECT_EQ(mask_int(all_bits, Architecture::Arch32), -1);

    // In Arch64, should remain -1
    EXPECT_EQ(mask_int(all_bits, Architecture::Arch64), -1);
}

TEST_F(EdgeCaseTest, LargestPositive32BitValue_PreservedInArch32) {
    EXPECT_EQ(mask_int(INT32_MAX_VAL, Architecture::Arch32), INT32_MAX_VAL);
}

TEST_F(EdgeCaseTest, SmallestNegative32BitValue_PreservedInArch32) {
    EXPECT_EQ(mask_int(INT32_MIN_VAL, Architecture::Arch32), INT32_MIN_VAL);
}

}  // namespace
}  // namespace dotvm::core
