/// @file alu_test.cpp
/// @brief Unit tests for the Arithmetic Logic Unit

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "dotvm/core/alu.hpp"

namespace dotvm::core {
namespace {

// ============================================================================
// ALU Construction Tests
// ============================================================================

class ALUConstructionTest : public ::testing::Test {};

TEST_F(ALUConstructionTest, DefaultConstructor_UsesArch64) {
    ALU alu;
    EXPECT_EQ(alu.arch(), Architecture::Arch64);
}

TEST_F(ALUConstructionTest, ExplicitArch32) {
    ALU alu{Architecture::Arch32};
    EXPECT_EQ(alu.arch(), Architecture::Arch32);
}

TEST_F(ALUConstructionTest, SetArch_ChangesArchitecture) {
    ALU alu{Architecture::Arch64};
    alu.set_arch(Architecture::Arch32);
    EXPECT_EQ(alu.arch(), Architecture::Arch32);
}

// ============================================================================
// Addition Tests
// ============================================================================

class ALUAddTest : public ::testing::Test {};

TEST_F(ALUAddTest, Add_SimplePositive_BothArch) {
    ALU alu32{Architecture::Arch32};
    ALU alu64{Architecture::Arch64};

    auto a = Value::from_int(10);
    auto b = Value::from_int(20);

    EXPECT_EQ(alu32.add(a, b).as_integer(), 30);
    EXPECT_EQ(alu64.add(a, b).as_integer(), 30);
}

TEST_F(ALUAddTest, Add_SimpleNegative) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(-10);
    auto b = Value::from_int(-20);

    EXPECT_EQ(alu32.add(a, b).as_integer(), -30);
}

TEST_F(ALUAddTest, Add_Arch32_Overflow_Wraps) {
    ALU alu32{Architecture::Arch32};

    auto max = Value::from_int(std::numeric_limits<std::int32_t>::max());
    auto one = Value::from_int(1);

    // INT32_MAX + 1 should wrap to INT32_MIN
    auto result = alu32.add(max, one);
    EXPECT_EQ(result.as_integer(), std::numeric_limits<std::int32_t>::min());
}

TEST_F(ALUAddTest, Add_Arch64_NoWrap_InRange) {
    ALU alu64{Architecture::Arch64};

    auto max32 = Value::from_int(std::numeric_limits<std::int32_t>::max());
    auto one = Value::from_int(1);

    // In 64-bit mode, INT32_MAX + 1 should just be INT32_MAX + 1
    auto result = alu64.add(max32, one);
    EXPECT_EQ(result.as_integer(),
              static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1);
}

TEST_F(ALUAddTest, Add_Arch32_LargeValues_Truncated) {
    ALU alu32{Architecture::Arch32};

    // 0x1'0000'0000 (2^32) + 1 should become 1 after masking
    auto large = Value::from_int(0x1'0000'0000LL);
    auto one = Value::from_int(1);

    auto result = alu32.add(large, one);
    EXPECT_EQ(result.as_integer(), 1);
}

// ============================================================================
// Subtraction Tests
// ============================================================================

class ALUSubTest : public ::testing::Test {};

TEST_F(ALUSubTest, Sub_SimplePositive) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(30);
    auto b = Value::from_int(20);

    EXPECT_EQ(alu32.sub(a, b).as_integer(), 10);
}

TEST_F(ALUSubTest, Sub_Arch32_Underflow_Wraps) {
    ALU alu32{Architecture::Arch32};

    auto min = Value::from_int(std::numeric_limits<std::int32_t>::min());
    auto one = Value::from_int(1);

    // INT32_MIN - 1 should wrap to INT32_MAX
    auto result = alu32.sub(min, one);
    EXPECT_EQ(result.as_integer(), std::numeric_limits<std::int32_t>::max());
}

// ============================================================================
// Multiplication Tests
// ============================================================================

class ALUMulTest : public ::testing::Test {};

TEST_F(ALUMulTest, Mul_SimplePositive) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(6);
    auto b = Value::from_int(7);

    EXPECT_EQ(alu32.mul(a, b).as_integer(), 42);
}

TEST_F(ALUMulTest, Mul_Arch32_Overflow_Wraps) {
    ALU alu32{Architecture::Arch32};

    auto large = Value::from_int(0x10000);  // 65536
    auto result = alu32.mul(large, large);  // 65536 * 65536 = 2^32

    // 2^32 masked to 32 bits is 0
    EXPECT_EQ(result.as_integer(), 0);
}

TEST_F(ALUMulTest, Mul_Arch64_LargeResult) {
    ALU alu64{Architecture::Arch64};

    auto large = Value::from_int(0x10000);  // 65536
    auto result = alu64.mul(large, large);  // 65536 * 65536 = 2^32

    EXPECT_EQ(result.as_integer(), 0x1'0000'0000LL);
}

// ============================================================================
// Division Tests
// ============================================================================

class ALUDivTest : public ::testing::Test {};

TEST_F(ALUDivTest, Div_SimplePositive) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(42);
    auto b = Value::from_int(7);

    EXPECT_EQ(alu32.div(a, b).as_integer(), 6);
}

TEST_F(ALUDivTest, Div_ByZero_ReturnsZero) {
    ALU alu32{Architecture::Arch32};
    ALU alu64{Architecture::Arch64};

    auto a = Value::from_int(42);
    auto zero = Value::from_int(0);

    EXPECT_EQ(alu32.div(a, zero).as_integer(), 0);
    EXPECT_EQ(alu64.div(a, zero).as_integer(), 0);
}

TEST_F(ALUDivTest, Div_Truncation) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(7);
    auto b = Value::from_int(2);

    EXPECT_EQ(alu32.div(a, b).as_integer(), 3);  // 7/2 = 3 (truncated)
}

// ============================================================================
// Modulo Tests
// ============================================================================

class ALUModTest : public ::testing::Test {};

TEST_F(ALUModTest, Mod_SimplePositive) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(7);
    auto b = Value::from_int(3);

    EXPECT_EQ(alu32.mod(a, b).as_integer(), 1);  // 7 % 3 = 1
}

TEST_F(ALUModTest, Mod_ByZero_ReturnsZero) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(42);
    auto zero = Value::from_int(0);

    EXPECT_EQ(alu32.mod(a, zero).as_integer(), 0);
}

// ============================================================================
// Negation and Absolute Value Tests
// ============================================================================

class ALUUnaryTest : public ::testing::Test {};

TEST_F(ALUUnaryTest, Neg_Positive) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(42);
    EXPECT_EQ(alu32.neg(a).as_integer(), -42);
}

TEST_F(ALUUnaryTest, Neg_Negative) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(-42);
    EXPECT_EQ(alu32.neg(a).as_integer(), 42);
}

TEST_F(ALUUnaryTest, Abs_Positive) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(42);
    EXPECT_EQ(alu32.abs(a).as_integer(), 42);
}

TEST_F(ALUUnaryTest, Abs_Negative) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(-42);
    EXPECT_EQ(alu32.abs(a).as_integer(), 42);
}

// ============================================================================
// Bitwise Operation Tests
// ============================================================================

class ALUBitwiseTest : public ::testing::Test {};

TEST_F(ALUBitwiseTest, And_Basic) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(0b1100);
    auto b = Value::from_int(0b1010);

    EXPECT_EQ(alu32.bit_and(a, b).as_integer(), 0b1000);
}

TEST_F(ALUBitwiseTest, Or_Basic) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(0b1100);
    auto b = Value::from_int(0b1010);

    EXPECT_EQ(alu32.bit_or(a, b).as_integer(), 0b1110);
}

TEST_F(ALUBitwiseTest, Xor_Basic) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(0b1100);
    auto b = Value::from_int(0b1010);

    EXPECT_EQ(alu32.bit_xor(a, b).as_integer(), 0b0110);
}

TEST_F(ALUBitwiseTest, Not_Arch32_MasksResult) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(0);
    auto result = alu32.bit_not(a);

    // NOT 0 in 32-bit should be -1 (all 32 bits set, sign-extended)
    EXPECT_EQ(result.as_integer(), -1);
}

// ============================================================================
// Shift Operation Tests
// ============================================================================

class ALUShiftTest : public ::testing::Test {};

TEST_F(ALUShiftTest, Shl_Basic) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(1);
    auto shift = Value::from_int(4);

    EXPECT_EQ(alu32.shl(a, shift).as_integer(), 16);
}

TEST_F(ALUShiftTest, Shl_Arch32_ShiftAmountMasked) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(1);
    auto shift = Value::from_int(32);  // Should be masked to 0

    EXPECT_EQ(alu32.shl(a, shift).as_integer(), 1);  // 1 << 0 = 1
}

TEST_F(ALUShiftTest, Shl_Arch32_ShiftAmountMasked_33) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(1);
    auto shift = Value::from_int(33);  // Should be masked to 1

    EXPECT_EQ(alu32.shl(a, shift).as_integer(), 2);  // 1 << 1 = 2
}

TEST_F(ALUShiftTest, Shr_Logical_Unsigned) {
    ALU alu32{Architecture::Arch32};

    // -1 in 32-bit is 0xFFFF'FFFF
    auto a = Value::from_int(-1, Architecture::Arch32);
    auto shift = Value::from_int(1);

    auto result = alu32.shr(a, shift);
    // Logical shift right of 0xFFFF'FFFF by 1 = 0x7FFF'FFFF = INT32_MAX
    EXPECT_EQ(result.as_integer(), std::numeric_limits<std::int32_t>::max());
}

TEST_F(ALUShiftTest, Sar_Arithmetic_Signed) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(-4);
    auto shift = Value::from_int(1);

    auto result = alu32.sar(a, shift);
    // Arithmetic shift right of -4 by 1 = -2
    EXPECT_EQ(result.as_integer(), -2);
}

TEST_F(ALUShiftTest, Sar_PositiveValue) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(8);
    auto shift = Value::from_int(2);

    EXPECT_EQ(alu32.sar(a, shift).as_integer(), 2);  // 8 >> 2 = 2
}

// ============================================================================
// Comparison Operation Tests
// ============================================================================

class ALUCompareTest : public ::testing::Test {};

TEST_F(ALUCompareTest, CmpEq_Equal) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(42);
    auto b = Value::from_int(42);

    EXPECT_EQ(alu32.cmp_eq(a, b).as_integer(), 1);
}

TEST_F(ALUCompareTest, CmpEq_NotEqual) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(42);
    auto b = Value::from_int(43);

    EXPECT_EQ(alu32.cmp_eq(a, b).as_integer(), 0);
}

TEST_F(ALUCompareTest, CmpNe_NotEqual) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(42);
    auto b = Value::from_int(43);

    EXPECT_EQ(alu32.cmp_ne(a, b).as_integer(), 1);
}

TEST_F(ALUCompareTest, CmpLt_LessThan) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(10);
    auto b = Value::from_int(20);

    EXPECT_EQ(alu32.cmp_lt(a, b).as_integer(), 1);
    EXPECT_EQ(alu32.cmp_lt(b, a).as_integer(), 0);
}

TEST_F(ALUCompareTest, CmpLt_Signed_Negative) {
    ALU alu32{Architecture::Arch32};

    auto neg = Value::from_int(-1);
    auto pos = Value::from_int(1);

    // -1 < 1 (signed)
    EXPECT_EQ(alu32.cmp_lt(neg, pos).as_integer(), 1);
}

TEST_F(ALUCompareTest, CmpLtu_Unsigned) {
    ALU alu32{Architecture::Arch32};

    auto neg = Value::from_int(-1, Architecture::Arch32);  // 0xFFFF'FFFF as unsigned
    auto pos = Value::from_int(1);

    // As unsigned, 0xFFFF'FFFF > 1
    EXPECT_EQ(alu32.cmp_ltu(neg, pos).as_integer(), 0);
    EXPECT_EQ(alu32.cmp_gtu(neg, pos).as_integer(), 1);
}

// ============================================================================
// Cross-Architecture Consistency Tests
// ============================================================================

class ALUCrossArchTest : public ::testing::Test {};

TEST_F(ALUCrossArchTest, SameResult_WhenWithinBothRanges) {
    ALU alu32{Architecture::Arch32};
    ALU alu64{Architecture::Arch64};

    auto a = Value::from_int(100);
    auto b = Value::from_int(200);

    // For values within 32-bit range, results should be the same
    EXPECT_EQ(alu32.add(a, b).as_integer(), alu64.add(a, b).as_integer());
    EXPECT_EQ(alu32.sub(b, a).as_integer(), alu64.sub(b, a).as_integer());
    EXPECT_EQ(alu32.mul(a, b).as_integer(), alu64.mul(a, b).as_integer());
}

TEST_F(ALUCrossArchTest, DifferentResult_WhenOverflow) {
    ALU alu32{Architecture::Arch32};
    ALU alu64{Architecture::Arch64};

    auto max32 = Value::from_int(std::numeric_limits<std::int32_t>::max());
    auto one = Value::from_int(1);

    // INT32_MAX + 1 wraps in 32-bit, doesn't in 64-bit
    auto result32 = alu32.add(max32, one);
    auto result64 = alu64.add(max32, one);

    EXPECT_NE(result32.as_integer(), result64.as_integer());
    EXPECT_EQ(result32.as_integer(), std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(result64.as_integer(),
              static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1);
}

// ============================================================================
// Constexpr Tests
// ============================================================================

class ALUConstexprTest : public ::testing::Test {};

TEST_F(ALUConstexprTest, AllOperationsAreConstexpr) {
    constexpr ALU alu{Architecture::Arch32};
    constexpr auto a = Value::from_int(10);
    constexpr auto b = Value::from_int(3);

    constexpr auto add_result = alu.add(a, b);
    constexpr auto sub_result = alu.sub(a, b);
    constexpr auto mul_result = alu.mul(a, b);
    constexpr auto div_result = alu.div(a, b);
    constexpr auto mod_result = alu.mod(a, b);

    EXPECT_EQ(add_result.as_integer(), 13);
    EXPECT_EQ(sub_result.as_integer(), 7);
    EXPECT_EQ(mul_result.as_integer(), 30);
    EXPECT_EQ(div_result.as_integer(), 3);
    EXPECT_EQ(mod_result.as_integer(), 1);
}

// ============================================================================
// Rotate Left (ROL) Tests
// ============================================================================

class ALURolTest : public ::testing::Test {};

TEST_F(ALURolTest, Rol_Arch32_Basic) {
    ALU alu32{Architecture::Arch32};

    // ROL(0x80000001, 1) should give 0x00000003
    auto a = Value::from_int(static_cast<std::int32_t>(0x80000001));
    auto shift = Value::from_int(1);

    auto result = alu32.rol(a, shift);
    EXPECT_EQ(result.as_integer(), 0x00000003);
}

TEST_F(ALURolTest, Rol_Arch32_ZeroRotation) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(0x12345678);
    auto shift = Value::from_int(0);

    auto result = alu32.rol(a, shift);
    EXPECT_EQ(result.as_integer(), 0x12345678);
}

TEST_F(ALURolTest, Rol_Arch32_FullRotation) {
    ALU alu32{Architecture::Arch32};

    // Rotating by 32 should give same result (identity)
    auto a = Value::from_int(0x12345678);
    auto shift = Value::from_int(32);

    auto result = alu32.rol(a, shift);
    EXPECT_EQ(result.as_integer(), 0x12345678);
}

TEST_F(ALURolTest, Rol_Arch32_HalfRotation) {
    ALU alu32{Architecture::Arch32};

    // ROL(0x0000FFFF, 16) should give 0xFFFF0000
    auto a = Value::from_int(0x0000FFFF);
    auto shift = Value::from_int(16);

    auto result = alu32.rol(a, shift);
    EXPECT_EQ(result.as_integer(), static_cast<std::int32_t>(0xFFFF0000));
}

TEST_F(ALURolTest, Rol_Arch64_Basic) {
    ALU alu64{Architecture::Arch64};

    // Simple test: ROL(0x0000000000000001, 4) should give 0x0000000000000010
    // This tests basic left rotation without worrying about wrap-around
    auto a = Value::from_int(1);
    auto shift = Value::from_int(4);

    auto result = alu64.rol(a, shift);
    EXPECT_EQ(result.as_integer(), 0x10);
}

TEST_F(ALURolTest, Rol_Arch64_FullRotation) {
    ALU alu64{Architecture::Arch64};

    // Rotating by 64 should give same result (identity)
    // Use a value that fits in 48-bit range
    auto a = Value::from_int(0x123456789ABCLL);  // 48-bit compatible value
    auto shift = Value::from_int(64);

    auto result = alu64.rol(a, shift);
    // rotate % 64 = 0, so should return same value
    EXPECT_EQ(result.as_integer(), 0x123456789ABCLL);
}

TEST_F(ALURolTest, Rol_NegativeShift_WrapsCorrectly) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(0x12345678);
    // -1 mod 32 = 31
    auto shift_neg = Value::from_int(-1);
    auto shift_31 = Value::from_int(31);

    auto result_neg = alu32.rol(a, shift_neg);
    auto result_31 = alu32.rol(a, shift_31);

    EXPECT_EQ(result_neg.as_integer(), result_31.as_integer());
}

// ============================================================================
// Rotate Right (ROR) Tests
// ============================================================================

class ALURorTest : public ::testing::Test {};

TEST_F(ALURorTest, Ror_Arch32_Basic) {
    ALU alu32{Architecture::Arch32};

    // ROR(0x00000003, 1) should give 0x80000001
    auto a = Value::from_int(0x00000003);
    auto shift = Value::from_int(1);

    auto result = alu32.ror(a, shift);
    EXPECT_EQ(result.as_integer(), static_cast<std::int32_t>(0x80000001));
}

TEST_F(ALURorTest, Ror_Arch32_ZeroRotation) {
    ALU alu32{Architecture::Arch32};

    auto a = Value::from_int(0x12345678);
    auto shift = Value::from_int(0);

    auto result = alu32.ror(a, shift);
    EXPECT_EQ(result.as_integer(), 0x12345678);
}

TEST_F(ALURorTest, Ror_Arch32_FullRotation) {
    ALU alu32{Architecture::Arch32};

    // Rotating by 32 should give same result (identity)
    auto a = Value::from_int(0x12345678);
    auto shift = Value::from_int(32);

    auto result = alu32.ror(a, shift);
    EXPECT_EQ(result.as_integer(), 0x12345678);
}

TEST_F(ALURorTest, Ror_Arch32_HalfRotation) {
    ALU alu32{Architecture::Arch32};

    // ROR(0xFFFF0000, 16) should give 0x0000FFFF
    auto a = Value::from_int(static_cast<std::int32_t>(0xFFFF0000));
    auto shift = Value::from_int(16);

    auto result = alu32.ror(a, shift);
    EXPECT_EQ(result.as_integer(), 0x0000FFFF);
}

TEST_F(ALURorTest, Ror_Arch64_Basic) {
    ALU alu64{Architecture::Arch64};

    // Simple test: ROR(0x10, 4) should give 0x1
    // This tests basic right rotation without worrying about wrap-around
    auto a = Value::from_int(0x10LL);
    auto shift = Value::from_int(4);

    auto result = alu64.ror(a, shift);
    EXPECT_EQ(result.as_integer(), 0x1LL);
}

TEST_F(ALURorTest, Ror_Arch64_FullRotation) {
    ALU alu64{Architecture::Arch64};

    // Rotating by 64 should give same result (identity)
    // Use a value that fits in 48-bit range
    auto a = Value::from_int(0x123456789ABCLL);  // 48-bit compatible value
    auto shift = Value::from_int(64);

    auto result = alu64.ror(a, shift);
    // rotate % 64 = 0, so should return same value
    EXPECT_EQ(result.as_integer(), 0x123456789ABCLL);
}

TEST_F(ALURorTest, Ror_Inverse_Of_Rol) {
    ALU alu32{Architecture::Arch32};

    auto original = Value::from_int(0x12345678);
    auto shift = Value::from_int(7);

    // ROL then ROR by same amount should give original
    auto rotated = alu32.rol(original, shift);
    auto back = alu32.ror(rotated, shift);

    EXPECT_EQ(back.as_integer(), original.as_integer());
}

// ============================================================================
// Rotate Constexpr Tests
// ============================================================================

class ALURotateConstexprTest : public ::testing::Test {};

TEST_F(ALURotateConstexprTest, RotateOperationsAreConstexpr) {
    constexpr ALU alu{Architecture::Arch32};
    constexpr auto a = Value::from_int(0x80000001);
    constexpr auto shift = Value::from_int(1);

    constexpr auto rol_result = alu.rol(a, shift);
    constexpr auto ror_result = alu.ror(a, shift);

    EXPECT_EQ(rol_result.as_integer(), 0x00000003);
    EXPECT_EQ(ror_result.as_integer(), static_cast<std::int32_t>(0xC0000000));
}

}  // namespace
}  // namespace dotvm::core
