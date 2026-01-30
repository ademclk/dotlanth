/// @file property_type_test.cpp
/// @brief Unit tests for PropertyType and PropertyDef

#include <format>

#include <gtest/gtest.h>

#include "dotvm/core/schema/property_type.hpp"

namespace dotvm::core::schema {
namespace {

// ============================================================================
// PropertyType Enum Tests
// ============================================================================

TEST(PropertyTypeTest, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(PropertyType::Int64), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(PropertyType::Float64), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(PropertyType::Boolean), 2);
    EXPECT_EQ(static_cast<std::uint8_t>(PropertyType::String), 3);
    EXPECT_EQ(static_cast<std::uint8_t>(PropertyType::DateTime), 4);
    EXPECT_EQ(static_cast<std::uint8_t>(PropertyType::Handle), 5);
}

TEST(PropertyTypeTest, ToString) {
    EXPECT_EQ(to_string(PropertyType::Int64), "Int64");
    EXPECT_EQ(to_string(PropertyType::Float64), "Float64");
    EXPECT_EQ(to_string(PropertyType::Boolean), "Boolean");
    EXPECT_EQ(to_string(PropertyType::String), "String");
    EXPECT_EQ(to_string(PropertyType::DateTime), "DateTime");
    EXPECT_EQ(to_string(PropertyType::Handle), "Handle");
}

TEST(PropertyTypeTest, Formatter) {
    EXPECT_EQ(std::format("{}", PropertyType::Int64), "Int64");
    EXPECT_EQ(std::format("{}", PropertyType::String), "String");
}

// ============================================================================
// Value Type Mapping Tests
// ============================================================================

TEST(PropertyTypeTest, ToValueType) {
    EXPECT_EQ(to_value_type(PropertyType::Int64), ValueType::Integer);
    EXPECT_EQ(to_value_type(PropertyType::Float64), ValueType::Float);
    EXPECT_EQ(to_value_type(PropertyType::Boolean), ValueType::Bool);
    EXPECT_EQ(to_value_type(PropertyType::String), ValueType::Handle);     // String via handle
    EXPECT_EQ(to_value_type(PropertyType::DateTime), ValueType::Integer);  // Epoch nanos
    EXPECT_EQ(to_value_type(PropertyType::Handle), ValueType::Handle);
}

// ============================================================================
// Value Compatibility Tests
// ============================================================================

TEST(PropertyTypeTest, IsCompatibleNil) {
    // Nil is always compatible
    EXPECT_TRUE(is_compatible(Value::nil(), PropertyType::Int64));
    EXPECT_TRUE(is_compatible(Value::nil(), PropertyType::Float64));
    EXPECT_TRUE(is_compatible(Value::nil(), PropertyType::Boolean));
    EXPECT_TRUE(is_compatible(Value::nil(), PropertyType::String));
    EXPECT_TRUE(is_compatible(Value::nil(), PropertyType::DateTime));
    EXPECT_TRUE(is_compatible(Value::nil(), PropertyType::Handle));
}

TEST(PropertyTypeTest, IsCompatibleInt64) {
    auto int_val = Value::from_int(42);
    EXPECT_TRUE(is_compatible(int_val, PropertyType::Int64));
    EXPECT_TRUE(is_compatible(int_val, PropertyType::DateTime));  // DateTime is Int64
    EXPECT_FALSE(is_compatible(int_val, PropertyType::Float64));
    EXPECT_FALSE(is_compatible(int_val, PropertyType::Boolean));
    EXPECT_FALSE(is_compatible(int_val, PropertyType::String));
    EXPECT_FALSE(is_compatible(int_val, PropertyType::Handle));
}

TEST(PropertyTypeTest, IsCompatibleFloat64) {
    auto float_val = Value::from_float(3.14);
    EXPECT_TRUE(is_compatible(float_val, PropertyType::Float64));
    EXPECT_FALSE(is_compatible(float_val, PropertyType::Int64));
    EXPECT_FALSE(is_compatible(float_val, PropertyType::Boolean));
}

TEST(PropertyTypeTest, IsCompatibleBool) {
    auto bool_val = Value::from_bool(true);
    EXPECT_TRUE(is_compatible(bool_val, PropertyType::Boolean));
    EXPECT_FALSE(is_compatible(bool_val, PropertyType::Int64));
    EXPECT_FALSE(is_compatible(bool_val, PropertyType::Float64));
}

TEST(PropertyTypeTest, IsCompatibleHandle) {
    auto handle_val = Value::from_handle(1, 0);
    EXPECT_TRUE(is_compatible(handle_val, PropertyType::Handle));
    EXPECT_TRUE(is_compatible(handle_val, PropertyType::String));  // String via handle
    EXPECT_FALSE(is_compatible(handle_val, PropertyType::Int64));
    EXPECT_FALSE(is_compatible(handle_val, PropertyType::Float64));
}

// ============================================================================
// PropertyDef Tests
// ============================================================================

TEST(PropertyDefTest, DefaultConstruction) {
    PropertyDef def;
    EXPECT_TRUE(def.name.empty());
    EXPECT_EQ(def.type, PropertyType::String);
    EXPECT_FALSE(def.required);
    EXPECT_FALSE(def.default_value.has_value());
    EXPECT_TRUE(def.target_type.empty());
}

TEST(PropertyDefTest, Equality) {
    PropertyDef def1;
    def1.name = "test";
    def1.type = PropertyType::Int64;
    def1.required = true;

    PropertyDef def2;
    def2.name = "test";
    def2.type = PropertyType::Int64;
    def2.required = true;

    EXPECT_EQ(def1, def2);

    def2.required = false;
    EXPECT_NE(def1, def2);
}

// ============================================================================
// PropertyDefBuilder Tests
// ============================================================================

TEST(PropertyDefBuilderTest, BasicUsage) {
    auto prop = PropertyDefBuilder("age").with_type(PropertyType::Int64).required().build();

    EXPECT_EQ(prop.name, "age");
    EXPECT_EQ(prop.type, PropertyType::Int64);
    EXPECT_TRUE(prop.required);
}

TEST(PropertyDefBuilderTest, WithDefault) {
    auto prop = PropertyDefBuilder("count")
                    .with_type(PropertyType::Int64)
                    .with_default(Value::from_int(0))
                    .build();

    EXPECT_EQ(prop.name, "count");
    ASSERT_TRUE(prop.default_value.has_value());
    EXPECT_TRUE(prop.default_value->is_integer());
    EXPECT_EQ(prop.default_value->as_integer(), 0);
}

TEST(PropertyDefBuilderTest, WithTargetType) {
    auto prop = PropertyDefBuilder("owner")
                    .with_type(PropertyType::Handle)
                    .with_target_type("User")
                    .build();

    EXPECT_EQ(prop.name, "owner");
    EXPECT_EQ(prop.type, PropertyType::Handle);
    EXPECT_EQ(prop.target_type, "User");
}

TEST(PropertyDefBuilderTest, ChainedCalls) {
    auto prop = PropertyDefBuilder("price")
                    .with_type(PropertyType::Float64)
                    .required(true)
                    .with_default(Value::from_float(0.0))
                    .build();

    EXPECT_EQ(prop.name, "price");
    EXPECT_EQ(prop.type, PropertyType::Float64);
    EXPECT_TRUE(prop.required);
    ASSERT_TRUE(prop.default_value.has_value());
}

}  // namespace
}  // namespace dotvm::core::schema
