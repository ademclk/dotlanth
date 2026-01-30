/// @file validator_test.cpp
/// @brief Unit tests for Validator types

#include <gtest/gtest.h>

#include "dotvm/core/schema/validator.hpp"

namespace dotvm::core::schema {
namespace {

// ============================================================================
// RangeValidator Tests
// ============================================================================

TEST(RangeValidatorTest, DefaultConstruction) {
    RangeValidator v;
    EXPECT_FALSE(v.min.has_value());
    EXPECT_FALSE(v.max.has_value());
    EXPECT_TRUE(v.min_inclusive);
    EXPECT_TRUE(v.max_inclusive);
}

TEST(RangeValidatorTest, BothBoundsInclusive) {
    RangeValidator v{.min = 0.0, .max = 100.0};

    EXPECT_TRUE(v.validate(0.0));     // At min
    EXPECT_TRUE(v.validate(50.0));    // Middle
    EXPECT_TRUE(v.validate(100.0));   // At max
    EXPECT_FALSE(v.validate(-1.0));   // Below min
    EXPECT_FALSE(v.validate(101.0));  // Above max
}

TEST(RangeValidatorTest, MinExclusive) {
    RangeValidator v{.min = 0.0, .max = 100.0, .min_inclusive = false};

    EXPECT_FALSE(v.validate(0.0));   // At min (exclusive)
    EXPECT_TRUE(v.validate(0.1));    // Just above min
    EXPECT_TRUE(v.validate(100.0));  // At max (inclusive)
}

TEST(RangeValidatorTest, MaxExclusive) {
    RangeValidator v{.min = 0.0, .max = 100.0, .max_inclusive = false};

    EXPECT_TRUE(v.validate(0.0));     // At min (inclusive)
    EXPECT_FALSE(v.validate(100.0));  // At max (exclusive)
    EXPECT_TRUE(v.validate(99.9));    // Just below max
}

TEST(RangeValidatorTest, MinOnly) {
    RangeValidator v{.min = 0.0};

    EXPECT_TRUE(v.validate(0.0));
    EXPECT_TRUE(v.validate(1000000.0));
    EXPECT_FALSE(v.validate(-1.0));
}

TEST(RangeValidatorTest, MaxOnly) {
    RangeValidator v{.max = 100.0};

    EXPECT_TRUE(v.validate(-1000000.0));
    EXPECT_TRUE(v.validate(100.0));
    EXPECT_FALSE(v.validate(101.0));
}

TEST(RangeValidatorTest, NoBounds) {
    RangeValidator v;

    EXPECT_TRUE(v.validate(-1e100));
    EXPECT_TRUE(v.validate(0.0));
    EXPECT_TRUE(v.validate(1e100));
}

// ============================================================================
// RegexValidator Tests
// ============================================================================

TEST(RegexValidatorTest, CreateValid) {
    auto result = RegexValidator::create("[a-z]+");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().pattern, "[a-z]+");
}

TEST(RegexValidatorTest, CreateInvalid) {
    auto result = RegexValidator::create("[invalid(");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::RegexValidationFailed);
}

TEST(RegexValidatorTest, ValidateMatch) {
    auto result = RegexValidator::create("[a-z]+");
    ASSERT_TRUE(result.is_ok());

    const auto& v = result.value();
    EXPECT_TRUE(v.validate("hello"));
    EXPECT_TRUE(v.validate("abc"));
    EXPECT_FALSE(v.validate("123"));
    EXPECT_FALSE(v.validate("Hello"));  // Capital letter
    EXPECT_FALSE(v.validate(""));       // Empty string
}

TEST(RegexValidatorTest, SkuPattern) {
    auto result = RegexValidator::create("SKU-[A-Z]{3}-[0-9]{4}");
    ASSERT_TRUE(result.is_ok());

    const auto& v = result.value();
    EXPECT_TRUE(v.validate("SKU-ABC-1234"));
    EXPECT_FALSE(v.validate("SKU-abc-1234"));  // Lowercase
    EXPECT_FALSE(v.validate("SKU-AB-1234"));   // Too few letters
    EXPECT_FALSE(v.validate("sku-ABC-1234"));  // Wrong prefix case
}

TEST(RegexValidatorTest, EmailPattern) {
    auto result = RegexValidator::create("[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}");
    ASSERT_TRUE(result.is_ok());

    const auto& v = result.value();
    EXPECT_TRUE(v.validate("test@example.com"));
    EXPECT_TRUE(v.validate("user.name@domain.co.uk"));
    EXPECT_FALSE(v.validate("invalid"));
    EXPECT_FALSE(v.validate("@example.com"));
}

// ============================================================================
// EnumValidator Tests
// ============================================================================

TEST(EnumValidatorTest, BasicValidation) {
    EnumValidator v{.allowed_values = {"red", "green", "blue"}};

    EXPECT_TRUE(v.validate("red"));
    EXPECT_TRUE(v.validate("green"));
    EXPECT_TRUE(v.validate("blue"));
    EXPECT_FALSE(v.validate("yellow"));
    EXPECT_FALSE(v.validate("RED"));  // Case sensitive
}

TEST(EnumValidatorTest, EmptyAllowed) {
    EnumValidator v{.allowed_values = {}};

    EXPECT_FALSE(v.validate("anything"));
}

TEST(EnumValidatorTest, SingleValue) {
    EnumValidator v{.allowed_values = {"only"}};

    EXPECT_TRUE(v.validate("only"));
    EXPECT_FALSE(v.validate("other"));
}

// ============================================================================
// RequiredValidator Tests
// ============================================================================

TEST(RequiredValidatorTest, NilFails) {
    RequiredValidator v;

    EXPECT_FALSE(v.validate(Value::nil()));
}

TEST(RequiredValidatorTest, NonNilPasses) {
    RequiredValidator v;

    EXPECT_TRUE(v.validate(Value::from_int(0)));
    EXPECT_TRUE(v.validate(Value::from_int(42)));
    EXPECT_TRUE(v.validate(Value::from_float(0.0)));
    EXPECT_TRUE(v.validate(Value::from_bool(false)));
    EXPECT_TRUE(v.validate(Value::from_handle(0, 0)));
}

// ============================================================================
// validate_value Function Tests
// ============================================================================

TEST(ValidateValueTest, RequiredWithNil) {
    Validator v = RequiredValidator{};
    auto result = validate_value(Value::nil(), v);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::RequiredPropertyMissing);
}

TEST(ValidateValueTest, RequiredWithValue) {
    Validator v = RequiredValidator{};
    auto result = validate_value(Value::from_int(42), v);
    EXPECT_TRUE(result.is_ok());
}

TEST(ValidateValueTest, RangeWithInRange) {
    Validator v = RangeValidator{.min = 0.0, .max = 100.0};
    auto result = validate_value(Value::from_int(50), v);
    EXPECT_TRUE(result.is_ok());
}

TEST(ValidateValueTest, RangeWithOutOfRange) {
    Validator v = RangeValidator{.min = 0.0, .max = 100.0};
    auto result = validate_value(Value::from_int(150), v);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::RangeValidationFailed);
}

TEST(ValidateValueTest, RangeWithFloat) {
    Validator v = RangeValidator{.min = 0.0, .max = 100.0};
    auto result = validate_value(Value::from_float(50.5), v);
    EXPECT_TRUE(result.is_ok());
}

TEST(ValidateValueTest, RangeWithNil) {
    Validator v = RangeValidator{.min = 0.0, .max = 100.0};
    auto result = validate_value(Value::nil(), v);
    EXPECT_TRUE(result.is_ok());  // Nil passes range validation
}

TEST(ValidateValueTest, RangeWithNonNumeric) {
    Validator v = RangeValidator{.min = 0.0, .max = 100.0};
    auto result = validate_value(Value::from_bool(true), v);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::InvalidPropertyType);
}

TEST(ValidateValueTest, EnumWithValid) {
    Validator v = EnumValidator{.allowed_values = {"1", "2", "3"}};
    auto result = validate_value(Value::from_int(2), v);
    EXPECT_TRUE(result.is_ok());
}

TEST(ValidateValueTest, EnumWithInvalid) {
    Validator v = EnumValidator{.allowed_values = {"1", "2", "3"}};
    auto result = validate_value(Value::from_int(5), v);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::EnumValidationFailed);
}

// ============================================================================
// Multiple Validators Tests
// ============================================================================

TEST(ValidateValueTest, MultipleValidatorsAllPass) {
    std::vector<Validator> validators = {RequiredValidator{},
                                         RangeValidator{.min = 0.0, .max = 100.0}};

    auto result = validate_value(Value::from_int(50), validators);
    EXPECT_TRUE(result.is_ok());
}

TEST(ValidateValueTest, MultipleValidatorsFirstFails) {
    std::vector<Validator> validators = {RequiredValidator{},
                                         RangeValidator{.min = 0.0, .max = 100.0}};

    auto result = validate_value(Value::nil(), validators);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::RequiredPropertyMissing);
}

TEST(ValidateValueTest, MultipleValidatorsSecondFails) {
    std::vector<Validator> validators = {RequiredValidator{},
                                         RangeValidator{.min = 0.0, .max = 100.0}};

    auto result = validate_value(Value::from_int(150), validators);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::RangeValidationFailed);
}

// ============================================================================
// Builder Function Tests
// ============================================================================

TEST(ValidatorBuildersTest, Range) {
    auto v = range(0.0, 100.0);
    EXPECT_EQ(v.min, 0.0);
    EXPECT_EQ(v.max, 100.0);
    EXPECT_TRUE(v.min_inclusive);
    EXPECT_TRUE(v.max_inclusive);
}

TEST(ValidatorBuildersTest, MinValue) {
    auto v = min_value(10.0);
    EXPECT_EQ(v.min, 10.0);
    EXPECT_FALSE(v.max.has_value());
}

TEST(ValidatorBuildersTest, MaxValue) {
    auto v = max_value(100.0);
    EXPECT_FALSE(v.min.has_value());
    EXPECT_EQ(v.max, 100.0);
}

TEST(ValidatorBuildersTest, OneOf) {
    auto v = one_of({"a", "b", "c"});
    EXPECT_EQ(v.allowed_values.size(), 3U);
    EXPECT_EQ(v.allowed_values[0], "a");
}

TEST(ValidatorBuildersTest, Required) {
    auto v = required();
    EXPECT_FALSE(v.validate(Value::nil()));
    EXPECT_TRUE(v.validate(Value::from_int(1)));
}

}  // namespace
}  // namespace dotvm::core::schema
