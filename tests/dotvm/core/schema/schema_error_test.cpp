/// @file schema_error_test.cpp
/// @brief Unit tests for SchemaError enum

#include <format>
#include <string>

#include <gtest/gtest.h>

#include "dotvm/core/schema/schema_error.hpp"

namespace dotvm::core::schema {
namespace {

// ============================================================================
// Enum Value Tests
// ============================================================================

TEST(SchemaErrorTest, EnumValues) {
    // Type management errors (176-179)
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::TypeNotFound), 176);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::TypeAlreadyExists), 177);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::InvalidTypeName), 178);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::MaxTypesExceeded), 179);

    // Property errors (180-183)
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::PropertyNotFound), 180);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::PropertyAlreadyExists), 181);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::InvalidPropertyType), 182);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::RequiredPropertyMissing), 183);

    // Validation errors (184-187)
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::ValidationFailed), 184);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::RangeValidationFailed), 185);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::RegexValidationFailed), 186);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::EnumValidationFailed), 187);

    // Link errors (188-189)
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::LinkNotFound), 188);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::CardinalityViolation), 189);

    // Migration errors (190-191)
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::MigrationFailed), 190);
    EXPECT_EQ(static_cast<std::uint8_t>(SchemaError::VersionConflict), 191);
}

// ============================================================================
// to_string Tests
// ============================================================================

TEST(SchemaErrorTest, ToStringAllValues) {
    EXPECT_EQ(to_string(SchemaError::TypeNotFound), "TypeNotFound");
    EXPECT_EQ(to_string(SchemaError::TypeAlreadyExists), "TypeAlreadyExists");
    EXPECT_EQ(to_string(SchemaError::InvalidTypeName), "InvalidTypeName");
    EXPECT_EQ(to_string(SchemaError::MaxTypesExceeded), "MaxTypesExceeded");
    EXPECT_EQ(to_string(SchemaError::PropertyNotFound), "PropertyNotFound");
    EXPECT_EQ(to_string(SchemaError::PropertyAlreadyExists), "PropertyAlreadyExists");
    EXPECT_EQ(to_string(SchemaError::InvalidPropertyType), "InvalidPropertyType");
    EXPECT_EQ(to_string(SchemaError::RequiredPropertyMissing), "RequiredPropertyMissing");
    EXPECT_EQ(to_string(SchemaError::ValidationFailed), "ValidationFailed");
    EXPECT_EQ(to_string(SchemaError::RangeValidationFailed), "RangeValidationFailed");
    EXPECT_EQ(to_string(SchemaError::RegexValidationFailed), "RegexValidationFailed");
    EXPECT_EQ(to_string(SchemaError::EnumValidationFailed), "EnumValidationFailed");
    EXPECT_EQ(to_string(SchemaError::LinkNotFound), "LinkNotFound");
    EXPECT_EQ(to_string(SchemaError::CardinalityViolation), "CardinalityViolation");
    EXPECT_EQ(to_string(SchemaError::MigrationFailed), "MigrationFailed");
    EXPECT_EQ(to_string(SchemaError::VersionConflict), "VersionConflict");
}

// ============================================================================
// Category Helper Tests
// ============================================================================

TEST(SchemaErrorTest, IsValidationError) {
    EXPECT_FALSE(is_validation_error(SchemaError::TypeNotFound));
    EXPECT_FALSE(is_validation_error(SchemaError::PropertyNotFound));
    EXPECT_TRUE(is_validation_error(SchemaError::ValidationFailed));
    EXPECT_TRUE(is_validation_error(SchemaError::RangeValidationFailed));
    EXPECT_TRUE(is_validation_error(SchemaError::RegexValidationFailed));
    EXPECT_TRUE(is_validation_error(SchemaError::EnumValidationFailed));
    EXPECT_FALSE(is_validation_error(SchemaError::LinkNotFound));
    EXPECT_FALSE(is_validation_error(SchemaError::MigrationFailed));
}

TEST(SchemaErrorTest, IsTypeError) {
    EXPECT_TRUE(is_type_error(SchemaError::TypeNotFound));
    EXPECT_TRUE(is_type_error(SchemaError::TypeAlreadyExists));
    EXPECT_TRUE(is_type_error(SchemaError::InvalidTypeName));
    EXPECT_TRUE(is_type_error(SchemaError::MaxTypesExceeded));
    EXPECT_FALSE(is_type_error(SchemaError::PropertyNotFound));
    EXPECT_FALSE(is_type_error(SchemaError::ValidationFailed));
}

TEST(SchemaErrorTest, IsPropertyError) {
    EXPECT_FALSE(is_property_error(SchemaError::TypeNotFound));
    EXPECT_TRUE(is_property_error(SchemaError::PropertyNotFound));
    EXPECT_TRUE(is_property_error(SchemaError::PropertyAlreadyExists));
    EXPECT_TRUE(is_property_error(SchemaError::InvalidPropertyType));
    EXPECT_TRUE(is_property_error(SchemaError::RequiredPropertyMissing));
    EXPECT_FALSE(is_property_error(SchemaError::ValidationFailed));
}

// ============================================================================
// std::formatter Tests
// ============================================================================

TEST(SchemaErrorTest, Formatter) {
    EXPECT_EQ(std::format("{}", SchemaError::TypeNotFound), "TypeNotFound");
    EXPECT_EQ(std::format("{}", SchemaError::ValidationFailed), "ValidationFailed");
    EXPECT_EQ(std::format("Error: {}", SchemaError::MigrationFailed), "Error: MigrationFailed");
}

}  // namespace
}  // namespace dotvm::core::schema
