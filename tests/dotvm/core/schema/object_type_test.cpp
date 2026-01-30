/// @file object_type_test.cpp
/// @brief Unit tests for ObjectType and ObjectTypeBuilder

#include <gtest/gtest.h>

#include "dotvm/core/schema/object_type.hpp"

namespace dotvm::core::schema {
namespace {

// ============================================================================
// ObjectType Basic Tests
// ============================================================================

TEST(ObjectTypeTest, DefaultConstruction) {
    ObjectType type("TestType");
    EXPECT_EQ(type.name(), "TestType");
    EXPECT_EQ(type.property_count(), 0U);
    EXPECT_EQ(type.link_count(), 0U);
}

TEST(ObjectTypeTest, PropertyAccess) {
    auto type =
        ObjectTypeBuilder("User")
            .try_add_property(
                PropertyDefBuilder("name").with_type(PropertyType::String).required().build())
            .try_add_property(PropertyDefBuilder("age").with_type(PropertyType::Int64).build())
            .build();

    EXPECT_EQ(type.property_count(), 2U);
    EXPECT_TRUE(type.has_property("name"));
    EXPECT_TRUE(type.has_property("age"));
    EXPECT_FALSE(type.has_property("email"));

    const auto* name_prop = type.get_property("name");
    ASSERT_NE(name_prop, nullptr);
    EXPECT_EQ(name_prop->definition.name, "name");
    EXPECT_EQ(name_prop->definition.type, PropertyType::String);
    EXPECT_TRUE(name_prop->definition.required);

    const auto* missing = type.get_property("missing");
    EXPECT_EQ(missing, nullptr);
}

TEST(ObjectTypeTest, PropertyNames) {
    auto type = ObjectTypeBuilder("Product")
                    .try_add_property(PropertyDefBuilder("sku").build())
                    .try_add_property(PropertyDefBuilder("name").build())
                    .try_add_property(PropertyDefBuilder("price").build())
                    .build();

    auto names = type.property_names();
    EXPECT_EQ(names.size(), 3U);
    // Names are from unordered_map, so just check they exist
    EXPECT_TRUE(std::find(names.begin(), names.end(), "sku") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "name") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "price") != names.end());
}

TEST(ObjectTypeTest, LinkAccess) {
    auto type = ObjectTypeBuilder("Order")
                    .try_add_link(LinkDefBuilder("customer")
                                      .to("Customer")
                                      .with_cardinality(Cardinality::OneToOne)
                                      .required()
                                      .build())
                    .build();

    EXPECT_EQ(type.link_count(), 1U);
    EXPECT_TRUE(type.has_link("customer"));
    EXPECT_FALSE(type.has_link("items"));

    const auto* link = type.get_link("customer");
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->name, "customer");
    EXPECT_EQ(link->target_type, "Customer");
    EXPECT_EQ(link->cardinality, Cardinality::OneToOne);
    EXPECT_TRUE(link->required);
    EXPECT_EQ(link->source_type, "Order");  // Auto-set by builder

    const auto* missing = type.get_link("missing");
    EXPECT_EQ(missing, nullptr);
}

// ============================================================================
// ObjectTypeBuilder Tests
// ============================================================================

TEST(ObjectTypeBuilderTest, AddPropertyError) {
    // Empty name should fail
    auto result = ObjectTypeBuilder("Test").add_property(PropertyDefBuilder("").build());
    EXPECT_TRUE(result.is_err());
}

TEST(ObjectTypeBuilderTest, DuplicateProperty) {
    ObjectTypeBuilder builder("Test");
    auto first = builder.add_property(PropertyDefBuilder("name").build());
    EXPECT_TRUE(first.is_ok());

    auto second = builder.add_property(PropertyDefBuilder("name").build());
    EXPECT_TRUE(second.is_err());
    EXPECT_EQ(second.error(), SchemaError::PropertyAlreadyExists);
}

TEST(ObjectTypeBuilderTest, AddLinkError) {
    // Empty name should fail
    auto result = ObjectTypeBuilder("Test").add_link(LinkDefBuilder("").to("Target").build());
    EXPECT_TRUE(result.is_err());
}

TEST(ObjectTypeBuilderTest, DuplicateLink) {
    ObjectTypeBuilder builder("Test");
    auto first = builder.add_link(LinkDefBuilder("ref").to("Other").build());
    EXPECT_TRUE(first.is_ok());

    auto second = builder.add_link(LinkDefBuilder("ref").to("Another").build());
    EXPECT_TRUE(second.is_err());
}

TEST(ObjectTypeBuilderTest, PropertyWithValidators) {
    auto type =
        ObjectTypeBuilder("Product")
            .try_add_property(
                PropertyDefBuilder("price").with_type(PropertyType::Float64).required().build(),
                {range(0.0, 1000000.0)})
            .build();

    const auto* prop = type.get_property("price");
    ASSERT_NE(prop, nullptr);
    // Should have RangeValidator + auto-added RequiredValidator
    EXPECT_GE(prop->validators.size(), 1U);
}

// ============================================================================
// Validation Tests
// ============================================================================

TEST(ObjectTypeTest, ValidateDataAllValid) {
    auto type =
        ObjectTypeBuilder("User")
            .try_add_property(
                PropertyDefBuilder("name").with_type(PropertyType::String).required().build())
            .try_add_property(PropertyDefBuilder("age").with_type(PropertyType::Int64).build(),
                              {range(0.0, 150.0)})
            .build();

    std::unordered_map<std::string, Value> data;
    data["name"] = Value::from_handle(1, 0);  // String via handle
    data["age"] = Value::from_int(30);

    auto result = type.validate_data(data);
    EXPECT_TRUE(result.is_ok());
}

TEST(ObjectTypeTest, ValidateDataMissingRequired) {
    auto type =
        ObjectTypeBuilder("User")
            .try_add_property(
                PropertyDefBuilder("name").with_type(PropertyType::String).required().build())
            .build();

    std::unordered_map<std::string, Value> data;
    // Missing "name"

    auto result = type.validate_data(data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::RequiredPropertyMissing);
}

TEST(ObjectTypeTest, ValidateDataWrongType) {
    auto type =
        ObjectTypeBuilder("User")
            .try_add_property(PropertyDefBuilder("age").with_type(PropertyType::Int64).build())
            .build();

    std::unordered_map<std::string, Value> data;
    data["age"] = Value::from_float(30.5);  // Wrong type (Float instead of Int)

    auto result = type.validate_data(data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::InvalidPropertyType);
}

TEST(ObjectTypeTest, ValidateDataRangeViolation) {
    auto type =
        ObjectTypeBuilder("Product")
            .try_add_property(PropertyDefBuilder("price").with_type(PropertyType::Float64).build(),
                              {range(0.0, 100.0)})
            .build();

    std::unordered_map<std::string, Value> data;
    data["price"] = Value::from_float(150.0);  // Out of range

    auto result = type.validate_data(data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::RangeValidationFailed);
}

TEST(ObjectTypeTest, ValidateDataWithDefault) {
    auto type = ObjectTypeBuilder("Counter")
                    .try_add_property(PropertyDefBuilder("count")
                                          .with_type(PropertyType::Int64)
                                          .with_default(Value::from_int(0))
                                          .build())
                    .build();

    std::unordered_map<std::string, Value> data;
    // "count" not provided, will use default

    auto result = type.validate_data(data);
    EXPECT_TRUE(result.is_ok());
}

TEST(ObjectTypeTest, ValidateDataNilOptional) {
    auto type = ObjectTypeBuilder("User")
                    .try_add_property(PropertyDefBuilder("nickname")
                                          .with_type(PropertyType::String)
                                          .required(false)
                                          .build())
                    .build();

    std::unordered_map<std::string, Value> data;
    data["nickname"] = Value::nil();  // Explicitly nil

    auto result = type.validate_data(data);
    EXPECT_TRUE(result.is_ok());
}

TEST(ObjectTypeTest, ValidateProperty) {
    auto type =
        ObjectTypeBuilder("Product")
            .try_add_property(
                PropertyDefBuilder("price").with_type(PropertyType::Float64).required().build(),
                {range(0.0, 1000.0)})
            .build();

    // Valid value
    EXPECT_TRUE(type.validate_property("price", Value::from_float(50.0)).is_ok());

    // Required but nil
    auto nil_result = type.validate_property("price", Value::nil());
    EXPECT_TRUE(nil_result.is_err());
    EXPECT_EQ(nil_result.error(), SchemaError::RequiredPropertyMissing);

    // Out of range
    auto range_result = type.validate_property("price", Value::from_float(2000.0));
    EXPECT_TRUE(range_result.is_err());
    EXPECT_EQ(range_result.error(), SchemaError::RangeValidationFailed);

    // Wrong type
    auto type_result = type.validate_property("price", Value::from_int(50));
    EXPECT_TRUE(type_result.is_err());
    EXPECT_EQ(type_result.error(), SchemaError::InvalidPropertyType);

    // Non-existent property
    auto missing = type.validate_property("nonexistent", Value::from_int(1));
    EXPECT_TRUE(missing.is_err());
    EXPECT_EQ(missing.error(), SchemaError::PropertyNotFound);
}

// ============================================================================
// Complex Type Tests
// ============================================================================

TEST(ObjectTypeTest, WarehouseType) {
    auto warehouse =
        ObjectTypeBuilder("Warehouse")
            .try_add_property(
                PropertyDefBuilder("name").with_type(PropertyType::String).required().build())
            .try_add_property(PropertyDefBuilder("capacity").with_type(PropertyType::Int64).build(),
                              {range(0.0, 1000000.0)})
            .try_add_property(
                PropertyDefBuilder("latitude").with_type(PropertyType::Float64).build(),
                {range(-90.0, 90.0)})
            .try_add_property(
                PropertyDefBuilder("longitude").with_type(PropertyType::Float64).build(),
                {range(-180.0, 180.0)})
            .try_add_link(LinkDefBuilder("shipments")
                              .to("Shipment")
                              .with_cardinality(Cardinality::OneToMany)
                              .build())
            .build();

    EXPECT_EQ(warehouse.name(), "Warehouse");
    EXPECT_EQ(warehouse.property_count(), 4U);
    EXPECT_EQ(warehouse.link_count(), 1U);

    // Valid warehouse data
    std::unordered_map<std::string, Value> valid_data;
    valid_data["name"] = Value::from_handle(1, 0);
    valid_data["capacity"] = Value::from_int(50000);
    valid_data["latitude"] = Value::from_float(37.7749);
    valid_data["longitude"] = Value::from_float(-122.4194);

    EXPECT_TRUE(warehouse.validate_data(valid_data).is_ok());

    // Invalid latitude
    std::unordered_map<std::string, Value> invalid_data = valid_data;
    invalid_data["latitude"] = Value::from_float(100.0);  // Out of range

    auto result = warehouse.validate_data(invalid_data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::RangeValidationFailed);
}

}  // namespace
}  // namespace dotvm::core::schema
