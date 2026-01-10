#include <gtest/gtest.h>
#include <dotvm/core/value.hpp>

#include <limits>
#include <vector>

using namespace dotvm::core;

class ValueTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// Size guarantees
TEST_F(ValueTest, SizeIs8Bytes) {
    EXPECT_EQ(sizeof(Value), 8);
}

TEST_F(ValueTest, HandleSizeIs8Bytes) {
    EXPECT_EQ(sizeof(Handle), 8);
}

// Float encoding/decoding
TEST_F(ValueTest, FloatRoundTrip) {
    std::vector<double> test_values = {
        0.0, -0.0, 1.0, -1.0,
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::epsilon(),
        3.14159265358979323846,
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };

    for (double d : test_values) {
        Value v = Value::from_float(d);
        EXPECT_TRUE(v.is_float()) << "Failed for: " << d;
        EXPECT_EQ(v.as_float(), d) << "Failed for: " << d;
        EXPECT_EQ(v.type(), ValueType::Float);
    }
}

// Integer encoding/decoding
TEST_F(ValueTest, IntegerRoundTrip) {
    std::vector<std::int64_t> test_values = {
        0, 1, -1, 42, -42,
        (1LL << 47) - 1,   // Max 48-bit positive
        -(1LL << 47),      // Min 48-bit negative
        12345678901234LL,
        -12345678901234LL
    };

    for (std::int64_t i : test_values) {
        Value v = Value::from_int(i);
        EXPECT_TRUE(v.is_integer()) << "Failed for: " << i;
        EXPECT_EQ(v.as_integer(), i) << "Failed for: " << i;
        EXPECT_EQ(v.type(), ValueType::Integer);
    }
}

// Boolean encoding/decoding
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

// Handle encoding/decoding
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

// Nil value
TEST_F(ValueTest, NilValue) {
    Value nil = Value::nil();
    Value default_val;

    EXPECT_TRUE(nil.is_nil());
    EXPECT_TRUE(default_val.is_nil());
    EXPECT_EQ(nil.type(), ValueType::Nil);
}

// Zero value
TEST_F(ValueTest, ZeroValue) {
    Value z = Value::zero();
    EXPECT_TRUE(z.is_float());
    EXPECT_EQ(z.as_float(), 0.0);
}

// Type discrimination
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

    // Integer
    EXPECT_FALSE(i.is_float());
    EXPECT_TRUE(i.is_integer());
    EXPECT_FALSE(i.is_bool());
    EXPECT_FALSE(i.is_handle());
    EXPECT_FALSE(i.is_nil());

    // Bool
    EXPECT_FALSE(b.is_float());
    EXPECT_FALSE(b.is_integer());
    EXPECT_TRUE(b.is_bool());
    EXPECT_FALSE(b.is_handle());
    EXPECT_FALSE(b.is_nil());

    // Handle
    EXPECT_FALSE(h.is_float());
    EXPECT_FALSE(h.is_integer());
    EXPECT_FALSE(h.is_bool());
    EXPECT_TRUE(h.is_handle());
    EXPECT_FALSE(h.is_nil());

    // Nil
    EXPECT_FALSE(n.is_float());
    EXPECT_FALSE(n.is_integer());
    EXPECT_FALSE(n.is_bool());
    EXPECT_FALSE(n.is_handle());
    EXPECT_TRUE(n.is_nil());
}

// Truthiness
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

// Raw bits access
TEST_F(ValueTest, RawBitsAccess) {
    Value v = Value::from_int(42);
    std::uint64_t bits = v.raw_bits();
    Value v2 = Value::from_raw(bits);
    EXPECT_EQ(v, v2);
    EXPECT_EQ(v2.as_integer(), 42);
}

// Equality
TEST_F(ValueTest, Equality) {
    EXPECT_EQ(Value::from_int(42), Value::from_int(42));
    EXPECT_NE(Value::from_int(42), Value::from_int(43));
    EXPECT_EQ(Value::from_float(3.14), Value::from_float(3.14));
    EXPECT_EQ(Value::nil(), Value::nil());
    EXPECT_NE(Value::from_int(0), Value::from_float(0.0));
}

// Constexpr tests (compile-time verification)
TEST_F(ValueTest, ConstexprConstruction) {
    constexpr Value cv_float = Value::from_float(3.14);
    constexpr Value cv_int = Value::from_int(42);
    constexpr Value cv_bool = Value::from_bool(true);
    constexpr Value cv_nil = Value::nil();

    static_assert(cv_float.is_float());
    static_assert(cv_int.is_integer());
    static_assert(cv_bool.is_bool());
    static_assert(cv_nil.is_nil());

    // Verify at runtime too
    EXPECT_TRUE(cv_float.is_float());
    EXPECT_TRUE(cv_int.is_integer());
    EXPECT_TRUE(cv_bool.is_bool());
    EXPECT_TRUE(cv_nil.is_nil());
}

// Trivially copyable check
TEST_F(ValueTest, TriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<Value>);
    static_assert(std::is_trivially_destructible_v<Value>);
}
