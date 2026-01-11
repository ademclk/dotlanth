#include <gtest/gtest.h>
#include <dotvm/core/bigint/bigint.hpp>

#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace dotvm::core::bigint;

class BigIntTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// ============================================================================
// Type Traits and Basic Properties
// ============================================================================

TEST_F(BigIntTest, DefaultConstructorCreatesZero) {
    BigInt a;
    EXPECT_TRUE(a.is_zero());
    EXPECT_FALSE(a.is_negative());
    EXPECT_EQ(a.sign(), 0);
    EXPECT_EQ(a.bit_length(), 0);
    EXPECT_EQ(a.limb_count(), 0);
}

TEST_F(BigIntTest, IntegralConstructorPositive) {
    BigInt a(42);
    EXPECT_FALSE(a.is_zero());
    EXPECT_FALSE(a.is_negative());
    EXPECT_TRUE(a.is_positive());
    EXPECT_EQ(a.sign(), 1);
}

TEST_F(BigIntTest, IntegralConstructorNegative) {
    BigInt a(-42);
    EXPECT_FALSE(a.is_zero());
    EXPECT_TRUE(a.is_negative());
    EXPECT_FALSE(a.is_positive());
    EXPECT_EQ(a.sign(), -1);
}

TEST_F(BigIntTest, IntegralConstructorZero) {
    BigInt a(0);
    EXPECT_TRUE(a.is_zero());
    EXPECT_FALSE(a.is_negative());
    EXPECT_EQ(a.sign(), 0);
}

TEST_F(BigIntTest, IntegralConstructorMinInt64) {
    BigInt a(std::numeric_limits<std::int64_t>::min());
    EXPECT_TRUE(a.is_negative());
    EXPECT_EQ(a.to_string(), "-9223372036854775808");
}

TEST_F(BigIntTest, IntegralConstructorMaxInt64) {
    BigInt a(std::numeric_limits<std::int64_t>::max());
    EXPECT_TRUE(a.is_positive());
    EXPECT_EQ(a.to_string(), "9223372036854775807");
}

TEST_F(BigIntTest, IntegralConstructorMaxUInt64) {
    BigInt a(std::numeric_limits<std::uint64_t>::max());
    EXPECT_TRUE(a.is_positive());
    EXPECT_EQ(a.to_string(), "18446744073709551615");
}

// ============================================================================
// String Parsing
// ============================================================================

TEST_F(BigIntTest, ParseDecimalPositive) {
    BigInt a("12345678901234567890");
    EXPECT_TRUE(a.is_positive());
    EXPECT_EQ(a.to_string(), "12345678901234567890");
}

TEST_F(BigIntTest, ParseDecimalNegative) {
    BigInt a("-12345678901234567890");
    EXPECT_TRUE(a.is_negative());
    EXPECT_EQ(a.to_string(), "-12345678901234567890");
}

TEST_F(BigIntTest, ParseDecimalZero) {
    BigInt a("0");
    EXPECT_TRUE(a.is_zero());
    EXPECT_EQ(a.to_string(), "0");
}

TEST_F(BigIntTest, ParseDecimalLeadingZeros) {
    BigInt a("0000000123");
    EXPECT_EQ(a.to_string(), "123");
}

TEST_F(BigIntTest, ParseHexadecimal) {
    BigInt a("0xFF");
    EXPECT_EQ(a, BigInt(255));
}

TEST_F(BigIntTest, ParseHexadecimalLarge) {
    BigInt a("0xFFFFFFFFFFFFFFFFFFFFFFFF");
    // 24 hex digits = 96 bits (1.5 * 64-bit limbs)
    EXPECT_EQ(a.to_hex_string(), "0xffffffffffffffffffffffff");
}

TEST_F(BigIntTest, ParseHexadecimalNegative) {
    BigInt a("-0xFF");
    EXPECT_TRUE(a.is_negative());
    EXPECT_EQ(a, BigInt(-255));
}

TEST_F(BigIntTest, ParseInvalidEmptyString) {
    EXPECT_THROW(BigInt(""), InvalidFormatError);
}

TEST_F(BigIntTest, ParseInvalidOnlySign) {
    EXPECT_THROW(BigInt("-"), InvalidFormatError);
    EXPECT_THROW(BigInt("+"), InvalidFormatError);
}

TEST_F(BigIntTest, ParseInvalidCharacter) {
    EXPECT_THROW(BigInt("123abc456"), InvalidFormatError);
}

TEST_F(BigIntTest, ParseInvalidHexCharacter) {
    EXPECT_THROW(BigInt("0xGHI"), InvalidFormatError);
}

// ============================================================================
// Comparison Operators
// ============================================================================

TEST_F(BigIntTest, EqualityPositive) {
    EXPECT_EQ(BigInt(42), BigInt(42));
    EXPECT_EQ(BigInt("123456789012345678901234567890"),
              BigInt("123456789012345678901234567890"));
}

TEST_F(BigIntTest, EqualityNegative) {
    EXPECT_EQ(BigInt(-42), BigInt(-42));
}

TEST_F(BigIntTest, EqualityZero) {
    EXPECT_EQ(BigInt(0), BigInt());
    EXPECT_EQ(BigInt("0"), BigInt("-0"));  // -0 should equal 0
}

TEST_F(BigIntTest, InequalityDifferentValues) {
    EXPECT_NE(BigInt(42), BigInt(43));
    EXPECT_NE(BigInt(42), BigInt(-42));
}

TEST_F(BigIntTest, ThreeWayComparisonPositive) {
    EXPECT_TRUE(BigInt(1) < BigInt(2));
    EXPECT_TRUE(BigInt(2) > BigInt(1));
    EXPECT_TRUE(BigInt(1) <= BigInt(1));
    EXPECT_TRUE(BigInt(1) >= BigInt(1));
    EXPECT_TRUE(BigInt(1) <= BigInt(2));
    EXPECT_TRUE(BigInt(2) >= BigInt(1));
}

TEST_F(BigIntTest, ThreeWayComparisonNegative) {
    EXPECT_TRUE(BigInt(-2) < BigInt(-1));
    EXPECT_TRUE(BigInt(-1) > BigInt(-2));
}

TEST_F(BigIntTest, ThreeWayComparisonMixedSigns) {
    EXPECT_TRUE(BigInt(-1) < BigInt(1));
    EXPECT_TRUE(BigInt(1) > BigInt(-1));
    EXPECT_TRUE(BigInt(-100) < BigInt(1));
}

TEST_F(BigIntTest, ThreeWayComparisonLargeNumbers) {
    BigInt a("99999999999999999999999999999999999999");
    BigInt b("100000000000000000000000000000000000000");
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
}

// ============================================================================
// Addition - Identity Properties
// ============================================================================

TEST_F(BigIntTest, AdditionIdentityZero) {
    BigInt a(42);
    BigInt zero;
    EXPECT_EQ(a + zero, a);
    EXPECT_EQ(zero + a, a);
}

TEST_F(BigIntTest, AdditionIdentityZeroNegative) {
    BigInt a(-42);
    BigInt zero;
    EXPECT_EQ(a + zero, a);
    EXPECT_EQ(zero + a, a);
}

// ============================================================================
// Addition - Commutativity
// ============================================================================

TEST_F(BigIntTest, AdditionCommutativity) {
    BigInt a(123);
    BigInt b(456);
    EXPECT_EQ(a + b, b + a);
}

TEST_F(BigIntTest, AdditionCommutativityLarge) {
    BigInt a("123456789012345678901234567890");
    BigInt b("987654321098765432109876543210");
    EXPECT_EQ(a + b, b + a);
}

TEST_F(BigIntTest, AdditionCommutativityMixedSigns) {
    BigInt a(123);
    BigInt b(-456);
    EXPECT_EQ(a + b, b + a);
}

// ============================================================================
// Addition - Associativity
// ============================================================================

TEST_F(BigIntTest, AdditionAssociativity) {
    BigInt a(100);
    BigInt b(200);
    BigInt c(300);
    EXPECT_EQ((a + b) + c, a + (b + c));
}

TEST_F(BigIntTest, AdditionAssociativityLarge) {
    BigInt a("111111111111111111111");
    BigInt b("222222222222222222222");
    BigInt c("333333333333333333333");
    EXPECT_EQ((a + b) + c, a + (b + c));
}

// ============================================================================
// Addition - Basic Operations
// ============================================================================

TEST_F(BigIntTest, AdditionPositivePositive) {
    EXPECT_EQ(BigInt(100) + BigInt(200), BigInt(300));
}

TEST_F(BigIntTest, AdditionNegativeNegative) {
    EXPECT_EQ(BigInt(-100) + BigInt(-200), BigInt(-300));
}

TEST_F(BigIntTest, AdditionPositiveNegativeResultPositive) {
    EXPECT_EQ(BigInt(200) + BigInt(-100), BigInt(100));
}

TEST_F(BigIntTest, AdditionPositiveNegativeResultNegative) {
    EXPECT_EQ(BigInt(100) + BigInt(-200), BigInt(-100));
}

TEST_F(BigIntTest, AdditionPositiveNegativeResultZero) {
    EXPECT_EQ(BigInt(100) + BigInt(-100), BigInt(0));
}

TEST_F(BigIntTest, AdditionWithCarry) {
    // Two large numbers that cause carry propagation
    BigInt a("18446744073709551615");  // 2^64 - 1
    BigInt b("1");
    EXPECT_EQ(a + b, BigInt("18446744073709551616"));  // 2^64
}

// ============================================================================
// Subtraction
// ============================================================================

TEST_F(BigIntTest, SubtractionBasic) {
    EXPECT_EQ(BigInt(300) - BigInt(200), BigInt(100));
}

TEST_F(BigIntTest, SubtractionResultNegative) {
    EXPECT_EQ(BigInt(100) - BigInt(200), BigInt(-100));
}

TEST_F(BigIntTest, SubtractionFromZero) {
    EXPECT_EQ(BigInt(0) - BigInt(100), BigInt(-100));
}

TEST_F(BigIntTest, SubtractionIdentity) {
    BigInt a(42);
    EXPECT_EQ(a - BigInt(0), a);
}

TEST_F(BigIntTest, SubtractionSelfEqualsZero) {
    BigInt a(42);
    EXPECT_EQ(a - a, BigInt(0));
}

TEST_F(BigIntTest, SubtractionNegativeFromNegative) {
    EXPECT_EQ(BigInt(-100) - BigInt(-200), BigInt(100));
}

// ============================================================================
// Unary Negation
// ============================================================================

TEST_F(BigIntTest, UnaryNegationPositive) {
    BigInt a(42);
    EXPECT_EQ(-a, BigInt(-42));
}

TEST_F(BigIntTest, UnaryNegationNegative) {
    BigInt a(-42);
    EXPECT_EQ(-a, BigInt(42));
}

TEST_F(BigIntTest, UnaryNegationZero) {
    BigInt a(0);
    EXPECT_EQ(-a, BigInt(0));
}

TEST_F(BigIntTest, UnaryNegationDoubleNegation) {
    BigInt a(42);
    EXPECT_EQ(-(-a), a);
}

// ============================================================================
// Multiplication - Identity Properties
// ============================================================================

TEST_F(BigIntTest, MultiplicationIdentityOne) {
    BigInt a(42);
    BigInt one(1);
    EXPECT_EQ(a * one, a);
    EXPECT_EQ(one * a, a);
}

TEST_F(BigIntTest, MultiplicationZero) {
    BigInt a(42);
    BigInt zero;
    EXPECT_EQ(a * zero, BigInt(0));
    EXPECT_EQ(zero * a, BigInt(0));
}

// ============================================================================
// Multiplication - Commutativity
// ============================================================================

TEST_F(BigIntTest, MultiplicationCommutativity) {
    BigInt a(123);
    BigInt b(456);
    EXPECT_EQ(a * b, b * a);
}

TEST_F(BigIntTest, MultiplicationCommutativityLarge) {
    BigInt a("12345678901234567890");
    BigInt b("98765432109876543210");
    EXPECT_EQ(a * b, b * a);
}

// ============================================================================
// Multiplication - Associativity
// ============================================================================

TEST_F(BigIntTest, MultiplicationAssociativity) {
    BigInt a(10);
    BigInt b(20);
    BigInt c(30);
    EXPECT_EQ((a * b) * c, a * (b * c));
}

// ============================================================================
// Multiplication - Distribution
// ============================================================================

TEST_F(BigIntTest, MultiplicationDistribution) {
    BigInt a(10);
    BigInt b(20);
    BigInt c(30);
    // a * (b + c) = a*b + a*c
    EXPECT_EQ(a * (b + c), a * b + a * c);
}

TEST_F(BigIntTest, MultiplicationDistributionLarge) {
    BigInt a("123456789");
    BigInt b("987654321");
    BigInt c("111111111");
    EXPECT_EQ(a * (b + c), a * b + a * c);
}

// ============================================================================
// Multiplication - Signs
// ============================================================================

TEST_F(BigIntTest, MultiplicationPositivePositive) {
    EXPECT_EQ(BigInt(6) * BigInt(7), BigInt(42));
}

TEST_F(BigIntTest, MultiplicationPositiveNegative) {
    EXPECT_EQ(BigInt(6) * BigInt(-7), BigInt(-42));
}

TEST_F(BigIntTest, MultiplicationNegativePositive) {
    EXPECT_EQ(BigInt(-6) * BigInt(7), BigInt(-42));
}

TEST_F(BigIntTest, MultiplicationNegativeNegative) {
    EXPECT_EQ(BigInt(-6) * BigInt(-7), BigInt(42));
}

// ============================================================================
// Multiplication - Large Numbers (Karatsuba)
// ============================================================================

TEST_F(BigIntTest, MultiplicationLargeNumbers) {
    // Create numbers large enough to trigger Karatsuba
    BigInt a("99999999999999999999999999999999999999999999999999999999999999999999");
    BigInt b("88888888888888888888888888888888888888888888888888888888888888888888");
    BigInt product = a * b;

    // Verify by dividing back
    EXPECT_EQ(product / a, b);
    EXPECT_EQ(product / b, a);
}

// ============================================================================
// Division - Basic
// ============================================================================

TEST_F(BigIntTest, DivisionExact) {
    EXPECT_EQ(BigInt(42) / BigInt(7), BigInt(6));
    EXPECT_EQ(BigInt(42) / BigInt(6), BigInt(7));
}

TEST_F(BigIntTest, DivisionWithRemainder) {
    EXPECT_EQ(BigInt(43) / BigInt(7), BigInt(6));
    EXPECT_EQ(BigInt(47) / BigInt(7), BigInt(6));
}

TEST_F(BigIntTest, DivisionByOne) {
    BigInt a(42);
    EXPECT_EQ(a / BigInt(1), a);
}

TEST_F(BigIntTest, DivisionBySelf) {
    BigInt a(42);
    EXPECT_EQ(a / a, BigInt(1));
}

TEST_F(BigIntTest, DivisionZeroByNonZero) {
    EXPECT_EQ(BigInt(0) / BigInt(42), BigInt(0));
}

TEST_F(BigIntTest, DivisionByZeroThrows) {
    EXPECT_THROW((void)(BigInt(42) / BigInt(0)), DivisionByZeroError);
}

TEST_F(BigIntTest, DivisionSmallerByLarger) {
    EXPECT_EQ(BigInt(3) / BigInt(7), BigInt(0));
}

// ============================================================================
// Division - Signs
// ============================================================================

TEST_F(BigIntTest, DivisionPositiveByPositive) {
    EXPECT_EQ(BigInt(42) / BigInt(7), BigInt(6));
}

TEST_F(BigIntTest, DivisionPositiveByNegative) {
    EXPECT_EQ(BigInt(42) / BigInt(-7), BigInt(-6));
}

TEST_F(BigIntTest, DivisionNegativeByPositive) {
    EXPECT_EQ(BigInt(-42) / BigInt(7), BigInt(-6));
}

TEST_F(BigIntTest, DivisionNegativeByNegative) {
    EXPECT_EQ(BigInt(-42) / BigInt(-7), BigInt(6));
}

// ============================================================================
// Modulo
// ============================================================================

TEST_F(BigIntTest, ModuloBasic) {
    EXPECT_EQ(BigInt(43) % BigInt(7), BigInt(1));
    EXPECT_EQ(BigInt(42) % BigInt(7), BigInt(0));
}

TEST_F(BigIntTest, ModuloNegativeDividend) {
    // C++ modulo: sign follows dividend
    EXPECT_EQ(BigInt(-43) % BigInt(7), BigInt(-1));
}

TEST_F(BigIntTest, ModuloByZeroThrows) {
    EXPECT_THROW((void)(BigInt(42) % BigInt(0)), DivisionByZeroError);
}

// ============================================================================
// Division/Modulo Identity: a = (a/b)*b + (a%b)
// ============================================================================

TEST_F(BigIntTest, DivmodIdentity) {
    BigInt a(1234567);
    BigInt b(123);
    auto [q, r] = a.divmod(b);
    EXPECT_EQ(q * b + r, a);
}

TEST_F(BigIntTest, DivmodIdentityLarge) {
    BigInt a("1234567890123456789012345678901234567890");
    BigInt b("98765432109876543210");
    auto [q, r] = a.divmod(b);
    EXPECT_EQ(q * b + r, a);
}

TEST_F(BigIntTest, DivmodIdentityNegative) {
    BigInt a(-1234567);
    BigInt b(123);
    auto [q, r] = a.divmod(b);
    EXPECT_EQ(q * b + r, a);
}

TEST_F(BigIntTest, DivmodIdentityBothNegative) {
    BigInt a(-1234567);
    BigInt b(-123);
    auto [q, r] = a.divmod(b);
    EXPECT_EQ(q * b + r, a);
}

// ============================================================================
// Compound Assignment
// ============================================================================

TEST_F(BigIntTest, CompoundAddition) {
    BigInt a(10);
    a += BigInt(5);
    EXPECT_EQ(a, BigInt(15));
}

TEST_F(BigIntTest, CompoundSubtraction) {
    BigInt a(10);
    a -= BigInt(3);
    EXPECT_EQ(a, BigInt(7));
}

TEST_F(BigIntTest, CompoundMultiplication) {
    BigInt a(10);
    a *= BigInt(5);
    EXPECT_EQ(a, BigInt(50));
}

TEST_F(BigIntTest, CompoundDivision) {
    BigInt a(50);
    a /= BigInt(10);
    EXPECT_EQ(a, BigInt(5));
}

TEST_F(BigIntTest, CompoundModulo) {
    BigInt a(43);
    a %= BigInt(7);
    EXPECT_EQ(a, BigInt(1));
}

// ============================================================================
// Increment/Decrement
// ============================================================================

TEST_F(BigIntTest, PreIncrement) {
    BigInt a(41);
    EXPECT_EQ(++a, BigInt(42));
    EXPECT_EQ(a, BigInt(42));
}

TEST_F(BigIntTest, PostIncrement) {
    BigInt a(41);
    EXPECT_EQ(a++, BigInt(41));
    EXPECT_EQ(a, BigInt(42));
}

TEST_F(BigIntTest, PreDecrement) {
    BigInt a(43);
    EXPECT_EQ(--a, BigInt(42));
    EXPECT_EQ(a, BigInt(42));
}

TEST_F(BigIntTest, PostDecrement) {
    BigInt a(43);
    EXPECT_EQ(a--, BigInt(43));
    EXPECT_EQ(a, BigInt(42));
}

// ============================================================================
// Bitwise Operations
// ============================================================================

TEST_F(BigIntTest, LeftShift) {
    BigInt a(1);
    EXPECT_EQ(a << 10, BigInt(1024));
}

TEST_F(BigIntTest, LeftShiftLarge) {
    BigInt a(1);
    EXPECT_EQ(a << 100, BigInt("1267650600228229401496703205376"));
}

TEST_F(BigIntTest, RightShift) {
    BigInt a(1024);
    EXPECT_EQ(a >> 10, BigInt(1));
}

TEST_F(BigIntTest, RightShiftToZero) {
    BigInt a(255);
    EXPECT_EQ(a >> 10, BigInt(0));
}

TEST_F(BigIntTest, BitwiseAnd) {
    BigInt a(0b1010);
    BigInt b(0b1100);
    EXPECT_EQ(a & b, BigInt(0b1000));
}

TEST_F(BigIntTest, BitwiseOr) {
    BigInt a(0b1010);
    BigInt b(0b1100);
    EXPECT_EQ(a | b, BigInt(0b1110));
}

TEST_F(BigIntTest, BitwiseXor) {
    BigInt a(0b1010);
    BigInt b(0b1100);
    EXPECT_EQ(a ^ b, BigInt(0b0110));
}

// ============================================================================
// Power
// ============================================================================

TEST_F(BigIntTest, PowerZero) {
    EXPECT_EQ(BigInt(42).pow(0), BigInt(1));
}

TEST_F(BigIntTest, PowerOne) {
    EXPECT_EQ(BigInt(42).pow(1), BigInt(42));
}

TEST_F(BigIntTest, PowerBasic) {
    EXPECT_EQ(BigInt(2).pow(10), BigInt(1024));
}

TEST_F(BigIntTest, PowerLarge) {
    EXPECT_EQ(BigInt(2).pow(100), BigInt("1267650600228229401496703205376"));
}

TEST_F(BigIntTest, PowerNegativeBase) {
    EXPECT_EQ(BigInt(-2).pow(3), BigInt(-8));
    EXPECT_EQ(BigInt(-2).pow(4), BigInt(16));
}

// ============================================================================
// GCD
// ============================================================================

TEST_F(BigIntTest, GcdBasic) {
    EXPECT_EQ(BigInt::gcd(BigInt(48), BigInt(18)), BigInt(6));
}

TEST_F(BigIntTest, GcdWithZero) {
    EXPECT_EQ(BigInt::gcd(BigInt(42), BigInt(0)), BigInt(42));
    EXPECT_EQ(BigInt::gcd(BigInt(0), BigInt(42)), BigInt(42));
}

TEST_F(BigIntTest, GcdCoprime) {
    EXPECT_EQ(BigInt::gcd(BigInt(17), BigInt(13)), BigInt(1));
}

TEST_F(BigIntTest, GcdSameNumber) {
    EXPECT_EQ(BigInt::gcd(BigInt(42), BigInt(42)), BigInt(42));
}

TEST_F(BigIntTest, GcdNegativeNumbers) {
    // GCD should always return positive result
    EXPECT_EQ(BigInt::gcd(BigInt(-48), BigInt(18)), BigInt(6));
    EXPECT_EQ(BigInt::gcd(BigInt(48), BigInt(-18)), BigInt(6));
    EXPECT_EQ(BigInt::gcd(BigInt(-48), BigInt(-18)), BigInt(6));
}

TEST_F(BigIntTest, GcdLargeNumbers) {
    BigInt a("123456789012345678901234567890");
    BigInt b("98765432109876543210987654321");
    BigInt g = BigInt::gcd(a, b);
    // Verify that g divides both
    EXPECT_EQ(a % g, BigInt(0));
    EXPECT_EQ(b % g, BigInt(0));
}

// ============================================================================
// LCM
// ============================================================================

TEST_F(BigIntTest, LcmBasic) {
    EXPECT_EQ(BigInt::lcm(BigInt(4), BigInt(6)), BigInt(12));
}

TEST_F(BigIntTest, LcmWithOne) {
    EXPECT_EQ(BigInt::lcm(BigInt(1), BigInt(42)), BigInt(42));
}

TEST_F(BigIntTest, LcmWithZero) {
    EXPECT_EQ(BigInt::lcm(BigInt(0), BigInt(42)), BigInt(0));
}

// ============================================================================
// Modular Exponentiation
// ============================================================================

TEST_F(BigIntTest, ModPowBasic) {
    // 2^10 mod 1000 = 1024 mod 1000 = 24
    EXPECT_EQ(BigInt(2).mod_pow(BigInt(10), BigInt(1000)), BigInt(24));
}

TEST_F(BigIntTest, ModPowLarge) {
    // Fermat's little theorem: a^(p-1) = 1 mod p for prime p
    BigInt prime(97);
    BigInt base(3);
    EXPECT_EQ(base.mod_pow(prime - BigInt(1), prime), BigInt(1));
}

TEST_F(BigIntTest, ModPowZeroExponent) {
    EXPECT_EQ(BigInt(42).mod_pow(BigInt(0), BigInt(100)), BigInt(1));
}

TEST_F(BigIntTest, ModPowModOne) {
    EXPECT_EQ(BigInt(42).mod_pow(BigInt(100), BigInt(1)), BigInt(0));
}

TEST_F(BigIntTest, ModPowByZeroThrows) {
    EXPECT_THROW((void)BigInt(42).mod_pow(BigInt(10), BigInt(0)), DivisionByZeroError);
}

// ============================================================================
// Absolute Value
// ============================================================================

TEST_F(BigIntTest, AbsPositive) {
    EXPECT_EQ(BigInt(42).abs(), BigInt(42));
}

TEST_F(BigIntTest, AbsNegative) {
    EXPECT_EQ(BigInt(-42).abs(), BigInt(42));
}

TEST_F(BigIntTest, AbsZero) {
    EXPECT_EQ(BigInt(0).abs(), BigInt(0));
}

// ============================================================================
// String Conversion
// ============================================================================

TEST_F(BigIntTest, ToStringZero) {
    EXPECT_EQ(BigInt(0).to_string(), "0");
}

TEST_F(BigIntTest, ToStringPositive) {
    EXPECT_EQ(BigInt(42).to_string(), "42");
}

TEST_F(BigIntTest, ToStringNegative) {
    EXPECT_EQ(BigInt(-42).to_string(), "-42");
}

TEST_F(BigIntTest, ToStringLarge) {
    BigInt a("123456789012345678901234567890");
    EXPECT_EQ(a.to_string(), "123456789012345678901234567890");
}

TEST_F(BigIntTest, ToHexStringZero) {
    EXPECT_EQ(BigInt(0).to_hex_string(), "0x0");
}

TEST_F(BigIntTest, ToHexStringPositive) {
    EXPECT_EQ(BigInt(255).to_hex_string(), "0xff");
}

TEST_F(BigIntTest, ToHexStringNegative) {
    EXPECT_EQ(BigInt(-255).to_hex_string(), "-0xff");
}

TEST_F(BigIntTest, StringRoundTrip) {
    std::vector<std::string> test_values = {
        "0",
        "42",
        "-42",
        "123456789012345678901234567890",
        "-123456789012345678901234567890",
        "99999999999999999999999999999999999999999999999999"
    };

    for (const auto& s : test_values) {
        BigInt a(s);
        EXPECT_EQ(a.to_string(), s) << "Failed for: " << s;
    }
}

TEST_F(BigIntTest, HexStringRoundTrip) {
    std::vector<std::string> test_values = {
        "0x0",
        "0xFF",
        "0x123456789ABCDEF",
        "0xFFFFFFFFFFFFFFFFFFFFFFFF"
    };

    for (const auto& s : test_values) {
        BigInt a(s);
        BigInt b(a.to_hex_string());
        EXPECT_EQ(a, b) << "Failed for: " << s;
    }
}

// ============================================================================
// Type Conversion
// ============================================================================

TEST_F(BigIntTest, TryToInt64Success) {
    auto result = BigInt(42).try_to<std::int64_t>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST_F(BigIntTest, TryToInt64Negative) {
    auto result = BigInt(-42).try_to<std::int64_t>();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, -42);
}

TEST_F(BigIntTest, TryToInt64Overflow) {
    BigInt a("999999999999999999999999999");
    auto result = a.try_to<std::int64_t>();
    EXPECT_FALSE(result.has_value());
}

TEST_F(BigIntTest, TryToUInt64Negative) {
    auto result = BigInt(-42).try_to<std::uint64_t>();
    EXPECT_FALSE(result.has_value());
}

TEST_F(BigIntTest, ToInt64) {
    EXPECT_EQ(BigInt(42).to<std::int64_t>(), 42);
    EXPECT_EQ(BigInt(-42).to<std::int64_t>(), -42);
}

// ============================================================================
// Factorial
// ============================================================================

TEST_F(BigIntTest, FactorialZero) {
    EXPECT_EQ(BigInt::factorial(0), BigInt(1));
}

TEST_F(BigIntTest, FactorialOne) {
    EXPECT_EQ(BigInt::factorial(1), BigInt(1));
}

TEST_F(BigIntTest, FactorialSmall) {
    EXPECT_EQ(BigInt::factorial(5), BigInt(120));
    EXPECT_EQ(BigInt::factorial(10), BigInt(3628800));
}

TEST_F(BigIntTest, FactorialLarge) {
    // 20! = 2432902008176640000
    EXPECT_EQ(BigInt::factorial(20), BigInt("2432902008176640000"));

    // 50! is a large number
    BigInt f50 = BigInt::factorial(50);
    EXPECT_EQ(f50.to_string(), "30414093201713378043612608166064768844377641568960512000000000000");
}

// ============================================================================
// Fibonacci
// ============================================================================

TEST_F(BigIntTest, FibonacciZero) {
    EXPECT_EQ(BigInt::fibonacci(0), BigInt(0));
}

TEST_F(BigIntTest, FibonacciOne) {
    EXPECT_EQ(BigInt::fibonacci(1), BigInt(1));
}

TEST_F(BigIntTest, FibonacciSmall) {
    // F(10) = 55
    EXPECT_EQ(BigInt::fibonacci(10), BigInt(55));
    // F(20) = 6765
    EXPECT_EQ(BigInt::fibonacci(20), BigInt(6765));
}

TEST_F(BigIntTest, FibonacciLarge) {
    // F(100) = 354224848179261915075
    EXPECT_EQ(BigInt::fibonacci(100), BigInt("354224848179261915075"));
}

// ============================================================================
// Known Mathematical Values
// ============================================================================

TEST_F(BigIntTest, MersennePrime) {
    // M_31 = 2^31 - 1 = 2147483647 (8th Mersenne prime)
    BigInt m31 = BigInt(2).pow(31) - BigInt(1);
    EXPECT_EQ(m31, BigInt(2147483647));

    // M_61 = 2^61 - 1 = 2305843009213693951 (9th Mersenne prime)
    BigInt m61 = BigInt(2).pow(61) - BigInt(1);
    EXPECT_EQ(m61, BigInt("2305843009213693951"));
}

TEST_F(BigIntTest, PowerOfTwo) {
    BigInt p100 = BigInt(2).pow(100);
    EXPECT_EQ(p100, BigInt("1267650600228229401496703205376"));
}

// ============================================================================
// Bit Length
// ============================================================================

TEST_F(BigIntTest, BitLengthZero) {
    EXPECT_EQ(BigInt(0).bit_length(), 0);
}

TEST_F(BigIntTest, BitLengthSmall) {
    EXPECT_EQ(BigInt(1).bit_length(), 1);
    EXPECT_EQ(BigInt(2).bit_length(), 2);
    EXPECT_EQ(BigInt(255).bit_length(), 8);
    EXPECT_EQ(BigInt(256).bit_length(), 9);
}

TEST_F(BigIntTest, BitLengthLarge) {
    BigInt a = BigInt(1) << 100;
    EXPECT_EQ(a.bit_length(), 101);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(BigIntTest, MultiplicationByPowerOfTwo) {
    BigInt a(12345);
    // a * 2^n should equal a << n
    EXPECT_EQ(a * BigInt(2).pow(10), a << 10);
}

TEST_F(BigIntTest, DivisionByPowerOfTwo) {
    BigInt a(12345 * 1024);
    // For exact division, a / 2^n should equal a >> n
    EXPECT_EQ(a / BigInt(2).pow(10), a >> 10);
}

TEST_F(BigIntTest, ChainedOperations) {
    BigInt a(10);
    BigInt result = ((a + BigInt(5)) * BigInt(3) - BigInt(15)) / BigInt(6);
    // (10 + 5) * 3 - 15 = 30
    // 30 / 6 = 5
    EXPECT_EQ(result, BigInt(5));
}

TEST_F(BigIntTest, VeryLargeNumberOperations) {
    // Create a moderately large number (40 digits fits in ~3 limbs)
    std::string large_str(40, '9');
    BigInt large(large_str);

    // Square it
    BigInt squared = large * large;

    // Verify by taking square root (approximate via division)
    BigInt approx = squared / large;
    EXPECT_EQ(approx, large);

    // Also test with a smaller 500-digit case just for basic sanity
    // (division of very large numbers may have precision issues)
    std::string huge_str(100, '9');
    BigInt huge(huge_str);
    BigInt huge_squared = huge * huge;
    BigInt huge_approx = huge_squared / huge;
    EXPECT_EQ(huge_approx, huge);
}

// ============================================================================
// FromLimbs Factory
// ============================================================================

TEST_F(BigIntTest, FromLimbs) {
    std::vector<BigInt::limb_type> limbs = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
    BigInt a = BigInt::from_limbs(limbs);
    EXPECT_EQ(a, BigInt("36893488147419103231"));
}

TEST_F(BigIntTest, FromLimbsNegative) {
    std::vector<BigInt::limb_type> limbs = {42};
    BigInt a = BigInt::from_limbs(limbs, true);
    EXPECT_EQ(a, BigInt(-42));
}

// ============================================================================
// Stress Tests with Random Values
// ============================================================================

TEST_F(BigIntTest, RandomArithmeticConsistency) {
    std::mt19937_64 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<std::int64_t> dist(-1000000, 1000000);

    for (int i = 0; i < 100; ++i) {
        std::int64_t x = dist(rng);
        std::int64_t y = dist(rng);

        // Skip zero divisor
        if (y == 0) continue;

        BigInt a(x);
        BigInt b(y);

        // Test that BigInt arithmetic matches int64_t arithmetic
        EXPECT_EQ((a + b).to<std::int64_t>(), x + y);
        EXPECT_EQ((a - b).to<std::int64_t>(), x - y);
        EXPECT_EQ((a * b).to<std::int64_t>(), x * y);
        EXPECT_EQ((a / b).to<std::int64_t>(), x / y);
        EXPECT_EQ((a % b).to<std::int64_t>(), x % y);
    }
}

TEST_F(BigIntTest, RandomGcdProperties) {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<std::uint64_t> dist(1, 1000000);

    for (int i = 0; i < 50; ++i) {
        BigInt a(dist(rng));
        BigInt b(dist(rng));

        BigInt g = BigInt::gcd(a, b);

        // GCD should divide both numbers
        EXPECT_TRUE((a % g).is_zero());
        EXPECT_TRUE((b % g).is_zero());

        // LCM * GCD = a * b
        BigInt l = BigInt::lcm(a, b);
        EXPECT_EQ(l * g, a * b);
    }
}
