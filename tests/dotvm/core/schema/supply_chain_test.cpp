/// @file supply_chain_test.cpp
/// @brief Integration tests using supply chain domain types

#include <gtest/gtest.h>

#include "dotvm/core/schema/migration.hpp"
#include "dotvm/core/schema/schema_json.hpp"
#include "dotvm/core/schema/schema_registry.hpp"

namespace dotvm::core::schema {
namespace {

// ============================================================================
// Supply Chain Type Definitions
// ============================================================================

/// @brief Create the Warehouse type
ObjectType create_warehouse_type() {
    return ObjectTypeBuilder("Warehouse")
        .try_add_property(
            PropertyDefBuilder("name").with_type(PropertyType::String).required().build())
        .try_add_property(PropertyDefBuilder("capacity")
                              .with_type(PropertyType::Int64)
                              .with_default(Value::from_int(0))
                              .build(),
                          {range(0.0, 10000000.0)})
        .try_add_property(PropertyDefBuilder("latitude").with_type(PropertyType::Float64).build(),
                          {range(-90.0, 90.0)})
        .try_add_property(PropertyDefBuilder("longitude").with_type(PropertyType::Float64).build(),
                          {range(-180.0, 180.0)})
        .try_add_property(PropertyDefBuilder("status").with_type(PropertyType::String).build())
        .build();
}

/// @brief Create the Product type with SKU validation
ObjectType create_product_type() {
    auto sku_regex = RegexValidator::create("SKU-[A-Z]{3}-[0-9]{4}");
    std::vector<Validator> sku_validators;
    if (sku_regex.is_ok()) {
        sku_validators.push_back(sku_regex.value());
    }

    return ObjectTypeBuilder("Product")
        .try_add_property(
            PropertyDefBuilder("sku").with_type(PropertyType::String).required().build(),
            std::move(sku_validators))
        .try_add_property(
            PropertyDefBuilder("name").with_type(PropertyType::String).required().build())
        .try_add_property(PropertyDefBuilder("price").with_type(PropertyType::Float64).build(),
                          {range(0.0, 1000000.0)})
        .try_add_property(
            PropertyDefBuilder("created_at").with_type(PropertyType::DateTime).build())
        .build();
}

/// @brief Create the Shipment type
ObjectType create_shipment_type() {
    return ObjectTypeBuilder("Shipment")
        .try_add_property(PropertyDefBuilder("tracking_number")
                              .with_type(PropertyType::String)
                              .required()
                              .build())
        .try_add_property(PropertyDefBuilder("status").with_type(PropertyType::String).build())
        .try_add_property(
            PropertyDefBuilder("shipped_at").with_type(PropertyType::DateTime).build())
        .try_add_property(
            PropertyDefBuilder("delivered_at").with_type(PropertyType::DateTime).build())
        .try_add_link(LinkDefBuilder("origin")
                          .to("Warehouse")
                          .with_cardinality(Cardinality::OneToOne)
                          .required()
                          .build())
        .try_add_link(LinkDefBuilder("destination")
                          .to("Warehouse")
                          .with_cardinality(Cardinality::OneToOne)
                          .required()
                          .build())
        .build();
}

/// @brief Create the Order type
ObjectType create_order_type() {
    return ObjectTypeBuilder("Order")
        .try_add_property(
            PropertyDefBuilder("order_number").with_type(PropertyType::String).required().build())
        .try_add_property(
            PropertyDefBuilder("customer_id").with_type(PropertyType::String).required().build())
        .try_add_property(
            PropertyDefBuilder("total_amount").with_type(PropertyType::Float64).build(),
            {min_value(0.0)})
        .try_add_property(PropertyDefBuilder("status").with_type(PropertyType::String).build())
        .try_add_link(
            LinkDefBuilder("items").to("Product").with_cardinality(Cardinality::OneToMany).build())
        .try_add_link(LinkDefBuilder("shipments")
                          .to("Shipment")
                          .with_cardinality(Cardinality::OneToMany)
                          .build())
        .build();
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(SupplyChainTest, RegisterAllTypes) {
    SchemaRegistryConfig config;
    config.validate_links_on_register = false;  // Register types independently
    SchemaRegistry registry(config);

    EXPECT_TRUE(registry.register_type(create_warehouse_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_product_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_shipment_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_order_type()).is_ok());

    EXPECT_EQ(registry.type_count(), 4U);
    EXPECT_TRUE(registry.has_type("Warehouse"));
    EXPECT_TRUE(registry.has_type("Product"));
    EXPECT_TRUE(registry.has_type("Shipment"));
    EXPECT_TRUE(registry.has_type("Order"));
}

TEST(SupplyChainTest, RegisterWithLinkValidation) {
    SchemaRegistry registry;  // validate_links_on_register = true

    // Must register in dependency order
    EXPECT_TRUE(registry.register_type(create_warehouse_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_product_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_shipment_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_order_type()).is_ok());

    EXPECT_EQ(registry.type_count(), 4U);
}

TEST(SupplyChainTest, ValidateWarehouseData) {
    SchemaRegistry registry;
    EXPECT_TRUE(registry.register_type(create_warehouse_type()).is_ok());

    // Valid warehouse
    std::unordered_map<std::string, Value> valid_data;
    valid_data["name"] = Value::from_handle(1, 0);  // String via handle
    valid_data["capacity"] = Value::from_int(50000);
    valid_data["latitude"] = Value::from_float(37.7749);
    valid_data["longitude"] = Value::from_float(-122.4194);
    valid_data["status"] = Value::from_handle(2, 0);

    auto valid_result = registry.validate_object("Warehouse", valid_data);
    EXPECT_TRUE(valid_result.is_ok());

    // Invalid latitude
    std::unordered_map<std::string, Value> invalid_data = valid_data;
    invalid_data["latitude"] = Value::from_float(100.0);  // Out of range

    auto invalid_result = registry.validate_object("Warehouse", invalid_data);
    EXPECT_TRUE(invalid_result.is_err());
    EXPECT_EQ(invalid_result.error(), SchemaError::RangeValidationFailed);

    // Missing required name
    std::unordered_map<std::string, Value> missing_data;
    missing_data["capacity"] = Value::from_int(50000);

    auto missing_result = registry.validate_object("Warehouse", missing_data);
    EXPECT_TRUE(missing_result.is_err());
    EXPECT_EQ(missing_result.error(), SchemaError::RequiredPropertyMissing);
}

TEST(SupplyChainTest, ValidateProductData) {
    SchemaRegistry registry;
    EXPECT_TRUE(registry.register_type(create_product_type()).is_ok());

    // Valid product with valid SKU
    std::unordered_map<std::string, Value> valid_data;
    valid_data["sku"] = Value::from_handle(1, 0);
    valid_data["name"] = Value::from_handle(2, 0);
    valid_data["price"] = Value::from_float(29.99);
    valid_data["created_at"] = Value::from_int(1704067200000000000LL);  // Epoch nanos

    auto valid_result = registry.validate_object("Product", valid_data);
    EXPECT_TRUE(valid_result.is_ok());

    // Invalid price (negative)
    std::unordered_map<std::string, Value> invalid_data = valid_data;
    invalid_data["price"] = Value::from_float(-10.0);

    auto invalid_result = registry.validate_object("Product", invalid_data);
    EXPECT_TRUE(invalid_result.is_err());
    EXPECT_EQ(invalid_result.error(), SchemaError::RangeValidationFailed);
}

TEST(SupplyChainTest, ValidateOrderData) {
    SchemaRegistryConfig config;
    config.validate_links_on_register = false;
    SchemaRegistry registry(config);

    EXPECT_TRUE(registry.register_type(create_order_type()).is_ok());

    // Valid order
    std::unordered_map<std::string, Value> valid_data;
    valid_data["order_number"] = Value::from_handle(1, 0);
    valid_data["customer_id"] = Value::from_handle(2, 0);
    valid_data["total_amount"] = Value::from_float(150.00);
    valid_data["status"] = Value::from_handle(3, 0);

    auto valid_result = registry.validate_object("Order", valid_data);
    EXPECT_TRUE(valid_result.is_ok());

    // Invalid total (negative)
    std::unordered_map<std::string, Value> invalid_data = valid_data;
    invalid_data["total_amount"] = Value::from_float(-50.0);

    auto invalid_result = registry.validate_object("Order", invalid_data);
    EXPECT_TRUE(invalid_result.is_err());
}

// ============================================================================
// Migration Tests
// ============================================================================

TEST(SupplyChainTest, MigrationWorkflow) {
    MigrationManager manager;
    SchemaRegistry registry;

    // V1: Add Warehouse and Product
    EXPECT_TRUE(
        manager
            .register_migration({.from = {0, 0, 0},
                                 .to = {1, 0, 0},
                                 .description = "Add Warehouse and Product types",
                                 .up = [](SchemaRegistry& reg) -> Result<void, SchemaError> {
                                     auto result = reg.register_type(create_warehouse_type());
                                     if (result.is_err())
                                         return result.error();
                                     return reg.register_type(create_product_type());
                                 },
                                 .down = [](SchemaRegistry& reg) -> Result<void, SchemaError> {
                                     auto result = reg.unregister_type("Product");
                                     if (result.is_err())
                                         return result.error();
                                     return reg.unregister_type("Warehouse");
                                 }})
            .is_ok());

    // V2: Add Shipment (depends on Warehouse)
    EXPECT_TRUE(
        manager
            .register_migration({.from = {1, 0, 0},
                                 .to = {2, 0, 0},
                                 .description = "Add Shipment type",
                                 .up = [](SchemaRegistry& reg) -> Result<void, SchemaError> {
                                     return reg.register_type(create_shipment_type());
                                 },
                                 .down = [](SchemaRegistry& reg) -> Result<void, SchemaError> {
                                     return reg.unregister_type("Shipment");
                                 }})
            .is_ok());

    // V3: Add Order (depends on Product and Shipment)
    EXPECT_TRUE(
        manager
            .register_migration({.from = {2, 0, 0},
                                 .to = {3, 0, 0},
                                 .description = "Add Order type",
                                 .up = [](SchemaRegistry& reg) -> Result<void, SchemaError> {
                                     return reg.register_type(create_order_type());
                                 },
                                 .down = [](SchemaRegistry& reg) -> Result<void, SchemaError> {
                                     return reg.unregister_type("Order");
                                 }})
            .is_ok());

    // Migrate to latest
    EXPECT_TRUE(manager.migrate_to_latest(registry).is_ok());
    MigrationVersion expected_v3{3, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v3);
    EXPECT_EQ(registry.type_count(), 4U);

    // Validate data with full schema
    std::unordered_map<std::string, Value> order_data;
    order_data["order_number"] = Value::from_handle(1, 0);
    order_data["customer_id"] = Value::from_handle(2, 0);
    order_data["total_amount"] = Value::from_float(99.99);

    EXPECT_TRUE(registry.validate_object("Order", order_data).is_ok());

    // Rollback to V1
    EXPECT_TRUE(manager.rollback_to(registry, {1, 0, 0}).is_ok());
    MigrationVersion expected_v1{1, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v1);
    EXPECT_EQ(registry.type_count(), 2U);
    EXPECT_TRUE(registry.has_type("Warehouse"));
    EXPECT_TRUE(registry.has_type("Product"));
    EXPECT_FALSE(registry.has_type("Shipment"));
    EXPECT_FALSE(registry.has_type("Order"));
}

// ============================================================================
// JSON Serialization Tests
// ============================================================================

TEST(SupplyChainTest, JsonRoundtrip) {
    SchemaExport original;
    original.version = {1, 0, 0};
    original.types.push_back(create_warehouse_type());
    original.types.push_back(create_product_type());

    // Export to JSON
    std::string json = export_schema_json(original);
    EXPECT_FALSE(json.empty());
    EXPECT_TRUE(json.find("Warehouse") != std::string::npos);
    EXPECT_TRUE(json.find("Product") != std::string::npos);

    // Import from JSON
    auto import_result = import_schema_json(json);
    ASSERT_TRUE(import_result.is_ok());

    const auto& imported = import_result.value();
    MigrationVersion expected_v1{1, 0, 0};
    EXPECT_EQ(imported.version, expected_v1);
    EXPECT_EQ(imported.types.size(), 2U);

    // Find imported types
    bool found_warehouse = false;
    bool found_product = false;
    for (const auto& type : imported.types) {
        if (type.name() == "Warehouse") {
            found_warehouse = true;
            EXPECT_TRUE(type.has_property("name"));
            EXPECT_TRUE(type.has_property("capacity"));
            EXPECT_TRUE(type.has_property("latitude"));
            EXPECT_TRUE(type.has_property("longitude"));
        } else if (type.name() == "Product") {
            found_product = true;
            EXPECT_TRUE(type.has_property("sku"));
            EXPECT_TRUE(type.has_property("name"));
            EXPECT_TRUE(type.has_property("price"));
        }
    }
    EXPECT_TRUE(found_warehouse);
    EXPECT_TRUE(found_product);
}

TEST(SupplyChainTest, ExportRegistry) {
    SchemaRegistryConfig config;
    config.validate_links_on_register = false;
    SchemaRegistry registry(config);

    EXPECT_TRUE(registry.register_type(create_warehouse_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_product_type()).is_ok());

    std::string json = export_registry_json(registry, {1, 0, 0});
    EXPECT_FALSE(json.empty());
    EXPECT_TRUE(json.find("dotvm-schema/1.0") != std::string::npos);
}

TEST(SupplyChainTest, ImportToRegistry) {
    // Create JSON schema
    SchemaExport schema;
    schema.version = {1, 0, 0};
    schema.types.push_back(create_warehouse_type());

    std::string json = export_schema_json(schema);

    // Import to fresh registry
    SchemaRegistry registry;
    auto result = import_registry_json(registry, json);

    ASSERT_TRUE(result.is_ok());
    MigrationVersion expected_v1{1, 0, 0};
    EXPECT_EQ(result.value(), expected_v1);
    EXPECT_TRUE(registry.has_type("Warehouse"));
}

// ============================================================================
// Link Validation Tests
// ============================================================================

TEST(SupplyChainTest, ValidateLinkTypes) {
    SchemaRegistry registry;

    EXPECT_TRUE(registry.register_type(create_warehouse_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_product_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_shipment_type()).is_ok());
    EXPECT_TRUE(registry.register_type(create_order_type()).is_ok());

    // Valid link: Shipment.origin -> Warehouse
    auto valid = registry.validate_link("Shipment", "origin", "Warehouse");
    EXPECT_TRUE(valid.is_ok());

    // Invalid link target: Shipment.origin should be Warehouse, not Product
    auto invalid = registry.validate_link("Shipment", "origin", "Product");
    EXPECT_TRUE(invalid.is_err());
    EXPECT_EQ(invalid.error(), SchemaError::InvalidPropertyType);

    // Non-existent link
    auto missing = registry.validate_link("Warehouse", "nonexistent", "Product");
    EXPECT_TRUE(missing.is_err());
    EXPECT_EQ(missing.error(), SchemaError::LinkNotFound);
}

// ============================================================================
// Complex Validation Scenarios
// ============================================================================

TEST(SupplyChainTest, DefaultValues) {
    SchemaRegistry registry;
    EXPECT_TRUE(registry.register_type(create_warehouse_type()).is_ok());

    // Warehouse with only required fields - capacity should use default
    std::unordered_map<std::string, Value> minimal_data;
    minimal_data["name"] = Value::from_handle(1, 0);

    auto result = registry.validate_object("Warehouse", minimal_data);
    EXPECT_TRUE(result.is_ok());
}

TEST(SupplyChainTest, NullableFields) {
    SchemaRegistryConfig config;
    config.validate_links_on_register = false;  // Shipment has links to Warehouse
    SchemaRegistry registry(config);
    EXPECT_TRUE(registry.register_type(create_shipment_type()).is_ok());

    // Shipment with optional delivered_at as nil
    std::unordered_map<std::string, Value> data;
    data["tracking_number"] = Value::from_handle(1, 0);
    data["status"] = Value::from_handle(2, 0);
    data["shipped_at"] = Value::from_int(1704067200000000000LL);
    data["delivered_at"] = Value::nil();  // Not yet delivered

    auto result = registry.validate_object("Shipment", data);
    EXPECT_TRUE(result.is_ok());
}

}  // namespace
}  // namespace dotvm::core::schema
