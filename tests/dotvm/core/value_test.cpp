#include <cmath>
#include <format>
#include <limits>
#include <vector>

#include <dotvm/core/value.hpp>

#include <gtest/gtest.h>

using namespace dotvm::core;

class ValueTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// ============================================================================
// Size and Type Trait Guarantees
// ============================================================================

TEST_F(ValueTest, SizeIs8Bytes) {
    EXPECT_EQ(sizeof(Value), 8);
}

TEST_F(ValueTest, HandleSizeIs8Bytes) {
    EXPECT_EQ(sizeof(Handle), 8);
}

TEST_F(ValueTest, TriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<Value>);
    static_assert(std::is_trivially_destructible_v<Value>);
}

TEST_F(ValueTest, AtomicLockFree) {
    static_assert(std::atomic<Value>::is_always_lock_free);
}

// ============================================================================
// Float Encoding/Decoding
// ============================================================================

TEST_F(ValueTest, FloatRoundTrip) {
    std::vector<double> test_values = {0.0,
                                       -0.0,
                                       1.0,
                                       -1.0,
                                       std::numeric_limits<double>::min(),
                                       std::numeric_limits<double>::max(),
                                       std::numeric_limits<double>::epsilon(),
                                       3.14159265358979323846,
                                       std::numeric_limits<double>::infinity(),
                                       -std::numeric_limits<double>::infinity()};

    for (double d : test_values) {
        Value v = Value::from_float(d);
        EXPECT_TRUE(v.is_float()) << "Failed for: " << d;
        EXPECT_EQ(v.as_float(), d) << "Failed for: " << d;
        EXPECT_EQ(v.type(), ValueType::Float);
    }
}

TEST_F(ValueTest, FloatNegativeZero) {
    double neg_zero = -0.0;
    Value v = Value::from_float(neg_zero);
    EXPECT_TRUE(v.is_float());
    EXPECT_EQ(std::signbit(v.as_float()), std::signbit(neg_zero));
    // -0.0 and 0.0 compare equal in IEEE 754
    EXPECT_EQ(v.as_float(), 0.0);
}

TEST_F(ValueTest, FloatSubnormal) {
    double subnormal = std::numeric_limits<double>::denorm_min();
    Value v = Value::from_float(subnormal);
    EXPECT_TRUE(v.is_float());
    EXPECT_EQ(v.as_float(), subnormal);
}

TEST_F(ValueTest, FloatQuietNaN) {
    double qnan = std::numeric_limits<double>::quiet_NaN();
    Value v = Value::from_float(qnan);
    // Must be detected as a float
    EXPECT_TRUE(v.is_float());
    // Result must be a NaN
    EXPECT_TRUE(std::isnan(v.as_float()));
}

TEST_F(ValueTest, FloatSignalingNaN) {
    double snan = std::numeric_limits<double>::signaling_NaN();
    Value v = Value::from_float(snan);
    // Must be detected as a float (even if canonicalized)
    EXPECT_TRUE(v.is_float());
    // Result must be a NaN (may be converted to quiet NaN)
    EXPECT_TRUE(std::isnan(v.as_float()));
}

TEST_F(ValueTest, FloatNaNCanonicalizedWhenConflicting) {
    // Create a NaN with bits that would conflict with INTEGER tag
    // INTEGER_PREFIX = 0x7FF9'0001'0000'0000
    // We create a NaN with similar pattern
    std::uint64_t conflicting_nan_bits = 0x7FF9'0001'0000'0000ULL;
    double conflicting_nan = std::bit_cast<double>(conflicting_nan_bits);

    Value v = Value::from_float(conflicting_nan);

    // Critical: must NOT be misdetected as integer
    EXPECT_FALSE(v.is_integer()) << "Conflicting NaN was misdetected as integer!";
    // Should be detected as float
    EXPECT_TRUE(v.is_float());
    // Should still be a NaN
    EXPECT_TRUE(std::isnan(v.as_float()));
}

TEST_F(ValueTest, FloatNaNNonConflictingPreserved) {
    // A quiet NaN with zero tag bits should be preserved exactly
    std::uint64_t safe_nan_bits = nan_box::QNAN_PREFIX;  // 0x7FF8'0000'0000'0000
    double safe_nan = std::bit_cast<double>(safe_nan_bits);

    Value v = Value::from_float(safe_nan);

    EXPECT_TRUE(v.is_float());
    EXPECT_TRUE(std::isnan(v.as_float()));
    // This specific NaN should be preserved
    EXPECT_EQ(v.raw_bits(), safe_nan_bits);
}

// ============================================================================
// Integer Encoding/Decoding
// ============================================================================

TEST_F(ValueTest, IntegerRoundTrip) {
    std::vector<std::int64_t> test_values = {0,
                                             1,
                                             -1,
                                             42,
                                             -42,
                                             (1LL << 47) - 1,  // Max 48-bit positive
                                             -(1LL << 47),     // Min 48-bit negative
                                             12345678901234LL,
                                             -12345678901234LL};

    for (std::int64_t i : test_values) {
        Value v = Value::from_int(i);
        EXPECT_TRUE(v.is_integer()) << "Failed for: " << i;
        EXPECT_EQ(v.as_integer(), i) << "Failed for: " << i;
        EXPECT_EQ(v.type(), ValueType::Integer);
    }
}

TEST_F(ValueTest, IntegerExactMaxPositive) {
    constexpr std::int64_t max_val = (1LL << 47) - 1;  // 140737488355327
    Value v = Value::from_int(max_val);
    EXPECT_TRUE(v.is_integer());
    EXPECT_EQ(v.as_integer(), max_val);
}

TEST_F(ValueTest, IntegerExactMinNegative) {
    constexpr std::int64_t min_val = -(1LL << 47);  // -140737488355328
    Value v = Value::from_int(min_val);
    EXPECT_TRUE(v.is_integer());
    EXPECT_EQ(v.as_integer(), min_val);
}

TEST_F(ValueTest, IntegerOverflowPositive) {
    // Value one past max 48-bit positive
    constexpr std::int64_t too_large = 1LL << 47;
    Value v = Value::from_int(too_large);
    EXPECT_TRUE(v.is_integer());
    // This value is truncated - document the wrapping behavior
    // (1LL << 47) with sign extension becomes -(1LL << 47)
    EXPECT_EQ(v.as_integer(), -(1LL << 47));
}

TEST_F(ValueTest, IntegerOverflowNegative) {
    // Value one past min 48-bit negative
    constexpr std::int64_t too_small = -(1LL << 47) - 1;
    Value v = Value::from_int(too_small);
    EXPECT_TRUE(v.is_integer());
    // Truncation wraps around
    EXPECT_EQ(v.as_integer(), (1LL << 47) - 1);
}

TEST_F(ValueTest, IntegerSignExtension) {
    // Test that negative values are properly sign-extended
    Value v = Value::from_int(-1);
    EXPECT_EQ(v.as_integer(), -1);

    // Test boundary of sign bit
    Value v2 = Value::from_int(-(1LL << 46));  // Large negative within range
    EXPECT_EQ(v2.as_integer(), -(1LL << 46));
}

// ============================================================================
// Boolean Encoding/Decoding
// ============================================================================

TEST_F(ValueTest, BoolRoundTrip) {
    Value t = Value::from_bool(true);
    Value f = Value::from_bool(false);

    EXPECT_TRUE(t.is_bool());
    EXPECT_TRUE(f.is_bool());
    EXPECT_TRUE(t.as_bool());
    EXPECT_FALSE(f.as_bool());
    EXPECT_EQ(t.type(), ValueType::Bool);
    EXPECT_EQ(f.type(), ValueType::Bool);
}

// ============================================================================
// Handle Encoding/Decoding
// ============================================================================

TEST_F(ValueTest, HandleRoundTrip) {
    Handle h1{.index = 0, .generation = 0};
    Handle h2{.index = 12345, .generation = 678};

    for (Handle h : {h1, h2}) {
        Value v = Value::from_handle(h);
        EXPECT_TRUE(v.is_handle());
        Handle result = v.as_handle();
        EXPECT_EQ(result.index, h.index);
        EXPECT_EQ(result.generation, h.generation);
        EXPECT_EQ(v.type(), ValueType::Handle);
    }
}

TEST_F(ValueTest, HandleFromTwoArgs) {
    Value v = Value::from_handle(100, 5);
    EXPECT_TRUE(v.is_handle());
    Handle h = v.as_handle();
    EXPECT_EQ(h.index, 100u);
    EXPECT_EQ(h.generation, 5u);
}

TEST_F(ValueTest, HandleMaxGeneration) {
    // Maximum 16-bit generation value
    Handle h{.index = 0, .generation = 0xFFFF};
    Value v = Value::from_handle(h);
    Handle result = v.as_handle();
    EXPECT_EQ(result.generation, 0xFFFFu);
}

TEST_F(ValueTest, HandleGenerationTruncationAssertsInDebug) {
    // Generation value exceeding 16 bits triggers assertion in debug builds
    // In release builds, value is silently truncated to lower 16 bits
#ifdef NDEBUG
    // Release build: truncation happens silently
    Handle h{.index = 0, .generation = 0x1'0000};  // 17 bits
    Value v = Value::from_handle(h);
    Handle result = v.as_handle();
    // Only lower 16 bits preserved
    EXPECT_EQ(result.generation, 0u);
#else
    // Debug build: assertion fires
    EXPECT_DEATH(
        {
            [[maybe_unused]] Value v =
                Value::from_handle(Handle{.index = 0, .generation = 0x1'0000});
        },
        "Handle generation exceeds 16-bit NaN-boxing limit");
#endif
}

TEST_F(ValueTest, HandleMaxIndex) {
    // Maximum 32-bit index
    Handle h{.index = 0xFFFF'FFFF, .generation = 0};
    Value v = Value::from_handle(h);
    EXPECT_EQ(v.as_handle().index, 0xFFFF'FFFFu);
}

TEST_F(ValueTest, HandleFitsInValue) {
    Handle valid{.index = 100, .generation = 0xFFFF};
    Handle invalid{.index = 100, .generation = 0x1'0000};

    EXPECT_TRUE(valid.fits_in_value());
    EXPECT_FALSE(invalid.fits_in_value());
}

// ============================================================================
// Pointer Encoding/Decoding
// ============================================================================

TEST_F(ValueTest, PointerBasic) {
    void* ptr = reinterpret_cast<void*>(0x1234'5678);
    Value v{ptr};
    EXPECT_TRUE(v.is_pointer());
    EXPECT_EQ(v.as_pointer(), ptr);
}

TEST_F(ValueTest, PointerUserSpace) {
    // Canonical x86-64 user-space address (bit 47 = 0)
    void* ptr = reinterpret_cast<void*>(0x0000'7FFF'FFFF'FFFF);
    Value v{ptr};
    EXPECT_TRUE(v.is_pointer());
    EXPECT_EQ(v.as_pointer(), ptr);
}

TEST_F(ValueTest, PointerKernelSpace) {
    // Canonical x86-64 kernel-space address (bit 47 = 1)
    // These addresses have sign-extension in upper 16 bits
    void* ptr = reinterpret_cast<void*>(0xFFFF'8000'0000'0000ULL);
    Value v{ptr};
    EXPECT_TRUE(v.is_pointer());
    // After truncation and sign-extension, should recover original
    EXPECT_EQ(v.as_pointer(), ptr);
}

TEST_F(ValueTest, PointerNullptr) {
    void* ptr = nullptr;
    Value v{ptr};
    EXPECT_TRUE(v.is_pointer());
    EXPECT_EQ(v.as_pointer(), nullptr);
}

// ============================================================================
// Nil Value
// ============================================================================

TEST_F(ValueTest, NilValue) {
    Value nil = Value::nil();
    Value default_val;

    EXPECT_TRUE(nil.is_nil());
    EXPECT_TRUE(default_val.is_nil());
    EXPECT_EQ(nil.type(), ValueType::Nil);
}

// ============================================================================
// Zero Value
// ============================================================================

TEST_F(ValueTest, ZeroValue) {
    Value z = Value::zero();
    EXPECT_TRUE(z.is_float());
    EXPECT_EQ(z.as_float(), 0.0);
}

// ============================================================================
// Type Discrimination
// ============================================================================

TEST_F(ValueTest, TypeDiscrimination) {
    Value f = Value::from_float(1.5);
    Value i = Value::from_int(42);
    Value b = Value::from_bool(true);
    Value h = Value::from_handle({1, 2});
    Value n = Value::nil();

    // Float
    EXPECT_TRUE(f.is_float());
    EXPECT_FALSE(f.is_integer());
    EXPECT_FALSE(f.is_bool());
    EXPECT_FALSE(f.is_handle());
    EXPECT_FALSE(f.is_nil());
    EXPECT_FALSE(f.is_pointer());

    // Integer
    EXPECT_FALSE(i.is_float());
    EXPECT_TRUE(i.is_integer());
    EXPECT_FALSE(i.is_bool());
    EXPECT_FALSE(i.is_handle());
    EXPECT_FALSE(i.is_nil());
    EXPECT_FALSE(i.is_pointer());

    // Bool
    EXPECT_FALSE(b.is_float());
    EXPECT_FALSE(b.is_integer());
    EXPECT_TRUE(b.is_bool());
    EXPECT_FALSE(b.is_handle());
    EXPECT_FALSE(b.is_nil());
    EXPECT_FALSE(b.is_pointer());

    // Handle
    EXPECT_FALSE(h.is_float());
    EXPECT_FALSE(h.is_integer());
    EXPECT_FALSE(h.is_bool());
    EXPECT_TRUE(h.is_handle());
    EXPECT_FALSE(h.is_nil());
    EXPECT_FALSE(h.is_pointer());

    // Nil
    EXPECT_FALSE(n.is_float());
    EXPECT_FALSE(n.is_integer());
    EXPECT_FALSE(n.is_bool());
    EXPECT_FALSE(n.is_handle());
    EXPECT_TRUE(n.is_nil());
    EXPECT_FALSE(n.is_pointer());
}

TEST_F(ValueTest, IsNumeric) {
    EXPECT_TRUE(Value::from_float(1.0).is_numeric());
    EXPECT_TRUE(Value::from_int(1).is_numeric());
    EXPECT_FALSE(Value::from_bool(true).is_numeric());
    EXPECT_FALSE(Value::nil().is_numeric());
    EXPECT_FALSE(Value::from_handle({1, 1}).is_numeric());
}

// ============================================================================
// Truthiness
// ============================================================================

TEST_F(ValueTest, Truthiness) {
    EXPECT_FALSE(Value::nil().is_truthy());
    EXPECT_FALSE(Value::from_bool(false).is_truthy());
    EXPECT_TRUE(Value::from_bool(true).is_truthy());
    EXPECT_FALSE(Value::from_int(0).is_truthy());
    EXPECT_TRUE(Value::from_int(1).is_truthy());
    EXPECT_TRUE(Value::from_int(-1).is_truthy());
    EXPECT_FALSE(Value::from_float(0.0).is_truthy());
    EXPECT_TRUE(Value::from_float(0.1).is_truthy());
    EXPECT_TRUE(Value::from_handle({1, 1}).is_truthy());
}

// ============================================================================
// Raw Bits Access
// ============================================================================

TEST_F(ValueTest, RawBitsAccess) {
    Value v = Value::from_int(42);
    std::uint64_t bits = v.raw_bits();
    Value v2 = Value::from_raw(bits);
    EXPECT_EQ(v, v2);
    EXPECT_EQ(v2.as_integer(), 42);
}

// ============================================================================
// Equality
// ============================================================================

TEST_F(ValueTest, Equality) {
    EXPECT_EQ(Value::from_int(42), Value::from_int(42));
    EXPECT_NE(Value::from_int(42), Value::from_int(43));
    EXPECT_EQ(Value::from_float(3.14), Value::from_float(3.14));
    EXPECT_EQ(Value::nil(), Value::nil());
    EXPECT_NE(Value::from_int(0), Value::from_float(0.0));
}

// ============================================================================
// Constexpr Tests (Compile-Time Verification)
// ============================================================================

TEST_F(ValueTest, ConstexprConstruction) {
    constexpr Value cv_float = Value::from_float(3.14);
    constexpr Value cv_int = Value::from_int(42);
    constexpr Value cv_bool = Value::from_bool(true);
    constexpr Value cv_nil = Value::nil();
    constexpr Value cv_zero = Value::zero();

    static_assert(cv_float.is_float());
    static_assert(cv_int.is_integer());
    static_assert(cv_bool.is_bool());
    static_assert(cv_nil.is_nil());
    static_assert(cv_zero.is_float());

    // Verify at runtime too
    EXPECT_TRUE(cv_float.is_float());
    EXPECT_TRUE(cv_int.is_integer());
    EXPECT_TRUE(cv_bool.is_bool());
    EXPECT_TRUE(cv_nil.is_nil());
    EXPECT_TRUE(cv_zero.is_float());
}

// ============================================================================
// Arithmetic Operations
// ============================================================================

TEST_F(ValueTest, ArithmeticAddFloat) {
    Value a = Value::from_float(1.5);
    Value b = Value::from_float(2.5);
    Value result = value_ops::add(a, b);
    EXPECT_TRUE(result.is_float());
    EXPECT_EQ(result.as_float(), 4.0);
}

TEST_F(ValueTest, ArithmeticAddInteger) {
    Value a = Value::from_int(10);
    Value b = Value::from_int(20);
    Value result = value_ops::add(a, b);
    EXPECT_TRUE(result.is_integer());
    EXPECT_EQ(result.as_integer(), 30);
}

TEST_F(ValueTest, ArithmeticAddMixed) {
    Value a = Value::from_float(1.5);
    Value b = Value::from_int(2);
    Value result = value_ops::add(a, b);
    // Mixed promotes to float
    EXPECT_TRUE(result.is_float());
    EXPECT_EQ(result.as_float(), 3.5);
}

TEST_F(ValueTest, ArithmeticAddInvalidTypes) {
    Value a = Value::from_int(1);
    Value b = Value::from_bool(true);
    Value result = value_ops::add(a, b);
    EXPECT_TRUE(result.is_nil());
}

TEST_F(ValueTest, ArithmeticSub) {
    EXPECT_EQ(value_ops::sub(Value::from_float(5.0), Value::from_float(3.0)).as_float(), 2.0);
    EXPECT_EQ(value_ops::sub(Value::from_int(10), Value::from_int(3)).as_integer(), 7);
}

TEST_F(ValueTest, ArithmeticMul) {
    EXPECT_EQ(value_ops::mul(Value::from_float(2.0), Value::from_float(3.0)).as_float(), 6.0);
    EXPECT_EQ(value_ops::mul(Value::from_int(4), Value::from_int(5)).as_integer(), 20);
}

TEST_F(ValueTest, ArithmeticDiv) {
    EXPECT_EQ(value_ops::div(Value::from_float(6.0), Value::from_float(2.0)).as_float(), 3.0);
    EXPECT_EQ(value_ops::div(Value::from_int(10), Value::from_int(3)).as_integer(),
              3);  // Truncates
}

TEST_F(ValueTest, ArithmeticDivByZero) {
    // Integer division by zero returns nil
    Value result = value_ops::div(Value::from_int(10), Value::from_int(0));
    EXPECT_TRUE(result.is_nil());

    // Float division by zero returns infinity
    Value float_result = value_ops::div(Value::from_float(1.0), Value::from_float(0.0));
    EXPECT_TRUE(std::isinf(float_result.as_float()));
}

TEST_F(ValueTest, ArithmeticMod) {
    EXPECT_EQ(value_ops::mod(Value::from_int(10), Value::from_int(3)).as_integer(), 1);
    EXPECT_EQ(value_ops::mod(Value::from_int(-10), Value::from_int(3)).as_integer(), -1);
    // Mod only works on integers
    EXPECT_TRUE(value_ops::mod(Value::from_float(10.0), Value::from_float(3.0)).is_nil());
}

TEST_F(ValueTest, ArithmeticModByZero) {
    Value result = value_ops::mod(Value::from_int(10), Value::from_int(0));
    EXPECT_TRUE(result.is_nil());
}

TEST_F(ValueTest, ArithmeticNeg) {
    EXPECT_EQ(value_ops::neg(Value::from_float(5.0)).as_float(), -5.0);
    EXPECT_EQ(value_ops::neg(Value::from_int(42)).as_integer(), -42);
    EXPECT_TRUE(value_ops::neg(Value::from_bool(true)).is_nil());
}

TEST_F(ValueTest, ArithmeticConstexpr) {
    constexpr Value a = Value::from_int(10);
    constexpr Value b = Value::from_int(5);
    constexpr Value sum = value_ops::add(a, b);
    static_assert(sum.is_integer());
    static_assert(sum.as_integer() == 15);
}

// ============================================================================
// Comparison Operations
// ============================================================================

TEST_F(ValueTest, CompareFloats) {
    EXPECT_EQ(value_ops::compare(Value::from_float(1.0), Value::from_float(2.0)),
              std::partial_ordering::less);
    EXPECT_EQ(value_ops::compare(Value::from_float(2.0), Value::from_float(1.0)),
              std::partial_ordering::greater);
    EXPECT_EQ(value_ops::compare(Value::from_float(1.0), Value::from_float(1.0)),
              std::partial_ordering::equivalent);
}

TEST_F(ValueTest, CompareFloatNaN) {
    double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(value_ops::compare(Value::from_float(nan), Value::from_float(1.0)),
              std::partial_ordering::unordered);
}

TEST_F(ValueTest, CompareIntegers) {
    EXPECT_TRUE(value_ops::less_than(Value::from_int(1), Value::from_int(2)));
    EXPECT_TRUE(value_ops::greater_than(Value::from_int(2), Value::from_int(1)));
    EXPECT_TRUE(value_ops::less_equal(Value::from_int(1), Value::from_int(1)));
    EXPECT_TRUE(value_ops::greater_equal(Value::from_int(2), Value::from_int(2)));
}

TEST_F(ValueTest, CompareMixedNumeric) {
    // Integer and float comparison (promotes to float)
    EXPECT_TRUE(value_ops::less_than(Value::from_int(1), Value::from_float(1.5)));
    EXPECT_TRUE(value_ops::greater_than(Value::from_float(2.5), Value::from_int(2)));
}

TEST_F(ValueTest, CompareDifferentTypes) {
    // Different types are unordered
    EXPECT_EQ(value_ops::compare(Value::from_int(1), Value::from_bool(true)),
              std::partial_ordering::unordered);
}

TEST_F(ValueTest, CompareBools) {
    EXPECT_TRUE(value_ops::less_than(Value::from_bool(false), Value::from_bool(true)));
    EXPECT_FALSE(value_ops::less_than(Value::from_bool(true), Value::from_bool(false)));
}

// ============================================================================
// String Representation
// ============================================================================

TEST_F(ValueTest, ToStringFloat) {
    std::string s = to_string(Value::from_float(3.14));
    EXPECT_FALSE(s.empty());
    EXPECT_NE(s.find("3.14"), std::string::npos);
}

TEST_F(ValueTest, ToStringInteger) {
    EXPECT_EQ(to_string(Value::from_int(42)), "42");
    EXPECT_EQ(to_string(Value::from_int(-123)), "-123");
}

TEST_F(ValueTest, ToStringBool) {
    EXPECT_EQ(to_string(Value::from_bool(true)), "true");
    EXPECT_EQ(to_string(Value::from_bool(false)), "false");
}

TEST_F(ValueTest, ToStringNil) {
    EXPECT_EQ(to_string(Value::nil()), "nil");
}

TEST_F(ValueTest, ToStringHandle) {
    std::string s = to_string(Value::from_handle(100, 5));
    EXPECT_NE(s.find("100"), std::string::npos);
    EXPECT_NE(s.find("5"), std::string::npos);
}

TEST_F(ValueTest, StdFormat) {
    Value v = Value::from_int(42);
    std::string formatted = std::format("{}", v);
    EXPECT_EQ(formatted, "42");
}

TEST_F(ValueTest, TypeNameFormat) {
    EXPECT_EQ(std::format("{}", ValueType::Integer), "Integer");
    EXPECT_EQ(std::format("{}", ValueType::Float), "Float");
    EXPECT_EQ(std::format("{}", ValueType::Nil), "Nil");
}

// ============================================================================
// Type Name Helper
// ============================================================================

TEST_F(ValueTest, TypeName) {
    EXPECT_EQ(type_name(ValueType::Float), "Float");
    EXPECT_EQ(type_name(ValueType::Integer), "Integer");
    EXPECT_EQ(type_name(ValueType::Bool), "Bool");
    EXPECT_EQ(type_name(ValueType::Handle), "Handle");
    EXPECT_EQ(type_name(ValueType::Nil), "Nil");
    EXPECT_EQ(type_name(ValueType::Pointer), "Pointer");
}

// ============================================================================
// as_number Helper
// ============================================================================

TEST_F(ValueTest, AsNumber) {
    EXPECT_EQ(Value::from_float(3.14).as_number(), 3.14);
    EXPECT_EQ(Value::from_int(42).as_number(), 42.0);
}
