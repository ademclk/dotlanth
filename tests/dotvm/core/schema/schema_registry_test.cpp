/// @file schema_registry_test.cpp
/// @brief Unit tests for SchemaRegistry

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/schema/schema_registry.hpp"

namespace dotvm::core::schema {
namespace {

// ============================================================================
// Basic Registry Tests
// ============================================================================

TEST(SchemaRegistryTest, DefaultConstruction) {
    SchemaRegistry registry;
    EXPECT_EQ(registry.type_count(), 0U);
}

TEST(SchemaRegistryTest, RegisterAndGet) {
    SchemaRegistry registry;

    auto type =
        ObjectTypeBuilder("User")
            .try_add_property(PropertyDefBuilder("name").with_type(PropertyType::String).build())
            .build();

    auto reg_result = registry.register_type(std::move(type));
    EXPECT_TRUE(reg_result.is_ok());
    EXPECT_EQ(registry.type_count(), 1U);
    EXPECT_TRUE(registry.has_type("User"));

    auto get_result = registry.get_type("User");
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(get_result.value()->name(), "User");
    EXPECT_EQ(get_result.value()->property_count(), 1U);
}

TEST(SchemaRegistryTest, RegisterEmptyName) {
    SchemaRegistry registry;

    auto type = ObjectTypeBuilder("").build();
    auto result = registry.register_type(std::move(type));

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::InvalidTypeName);
}

TEST(SchemaRegistryTest, RegisterDuplicate) {
    SchemaRegistry registry;

    auto type1 = ObjectTypeBuilder("User").build();
    EXPECT_TRUE(registry.register_type(std::move(type1)).is_ok());

    auto type2 = ObjectTypeBuilder("User").build();
    auto result = registry.register_type(std::move(type2));

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::TypeAlreadyExists);
}

TEST(SchemaRegistryTest, GetNonExistent) {
    SchemaRegistry registry;

    auto result = registry.get_type("NonExistent");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::TypeNotFound);
}

TEST(SchemaRegistryTest, HasType) {
    SchemaRegistry registry;

    EXPECT_FALSE(registry.has_type("User"));

    auto type = ObjectTypeBuilder("User").build();
    EXPECT_TRUE(registry.register_type(std::move(type)).is_ok());

    EXPECT_TRUE(registry.has_type("User"));
    EXPECT_FALSE(registry.has_type("Other"));
}

TEST(SchemaRegistryTest, TypeNames) {
    SchemaRegistry registry;

    auto type1 = ObjectTypeBuilder("Alpha").build();
    auto type2 = ObjectTypeBuilder("Beta").build();
    auto type3 = ObjectTypeBuilder("Gamma").build();

    EXPECT_TRUE(registry.register_type(std::move(type1)).is_ok());
    EXPECT_TRUE(registry.register_type(std::move(type2)).is_ok());
    EXPECT_TRUE(registry.register_type(std::move(type3)).is_ok());

    auto names = registry.type_names();
    EXPECT_EQ(names.size(), 3U);
    EXPECT_TRUE(std::find(names.begin(), names.end(), "Alpha") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "Beta") != names.end());
    EXPECT_TRUE(std::find(names.begin(), names.end(), "Gamma") != names.end());
}

// ============================================================================
// Unregister Tests
// ============================================================================

TEST(SchemaRegistryTest, UnregisterSuccess) {
    SchemaRegistry registry;

    auto type = ObjectTypeBuilder("Temp").build();
    EXPECT_TRUE(registry.register_type(std::move(type)).is_ok());
    EXPECT_TRUE(registry.has_type("Temp"));

    auto result = registry.unregister_type("Temp");
    EXPECT_TRUE(result.is_ok());
    EXPECT_FALSE(registry.has_type("Temp"));
    EXPECT_EQ(registry.type_count(), 0U);
}

TEST(SchemaRegistryTest, UnregisterNonExistent) {
    SchemaRegistry registry;

    auto result = registry.unregister_type("NonExistent");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::TypeNotFound);
}

TEST(SchemaRegistryTest, UnregisterWithReference) {
    SchemaRegistryConfig config;
    config.validate_links_on_register = false;  // Allow registering with missing targets
    SchemaRegistry registry(config);

    // Register target type
    auto target = ObjectTypeBuilder("Target").build();
    EXPECT_TRUE(registry.register_type(std::move(target)).is_ok());

    // Register type with link to target
    auto source = ObjectTypeBuilder("Source")
                      .try_add_link(LinkDefBuilder("ref").to("Target").build())
                      .build();
    EXPECT_TRUE(registry.register_type(std::move(source)).is_ok());

    // Cannot unregister Target because Source references it
    auto result = registry.unregister_type("Target");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::CardinalityViolation);

    // Can unregister Source first
    EXPECT_TRUE(registry.unregister_type("Source").is_ok());
    // Now can unregister Target
    EXPECT_TRUE(registry.unregister_type("Target").is_ok());
}

// ============================================================================
// Link Validation Tests
// ============================================================================

TEST(SchemaRegistryTest, LinkValidationOnRegister) {
    SchemaRegistry registry;  // validate_links_on_register = true by default

    // Try to register type with link to non-existent target
    auto order = ObjectTypeBuilder("Order")
                     .try_add_link(LinkDefBuilder("customer").to("Customer").build())
                     .build();

    auto result = registry.register_type(std::move(order));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::TypeNotFound);
}

TEST(SchemaRegistryTest, LinkValidationAllowSelfReference) {
    SchemaRegistry registry;

    // Self-referencing type should be allowed
    auto node = ObjectTypeBuilder("Node")
                    .try_add_link(LinkDefBuilder("parent").to("Node").build())
                    .try_add_link(LinkDefBuilder("children")
                                      .to("Node")
                                      .with_cardinality(Cardinality::OneToMany)
                                      .build())
                    .build();

    auto result = registry.register_type(std::move(node));
    EXPECT_TRUE(result.is_ok());
}

TEST(SchemaRegistryTest, LinkValidationDisabled) {
    SchemaRegistryConfig config;
    config.validate_links_on_register = false;
    SchemaRegistry registry(config);

    // Can register type with link to non-existent target
    auto order = ObjectTypeBuilder("Order")
                     .try_add_link(LinkDefBuilder("customer").to("NonExistent").build())
                     .build();

    auto result = registry.register_type(std::move(order));
    EXPECT_TRUE(result.is_ok());
}

TEST(SchemaRegistryTest, LinkValidationWithExistingTarget) {
    SchemaRegistry registry;

    // Register target first
    auto customer = ObjectTypeBuilder("Customer").build();
    EXPECT_TRUE(registry.register_type(std::move(customer)).is_ok());

    // Now can register with link
    auto order = ObjectTypeBuilder("Order")
                     .try_add_link(LinkDefBuilder("customer").to("Customer").build())
                     .build();

    auto result = registry.register_type(std::move(order));
    EXPECT_TRUE(result.is_ok());
}

// ============================================================================
// Validation Tests
// ============================================================================

TEST(SchemaRegistryTest, ValidateObject) {
    SchemaRegistry registry;

    auto type =
        ObjectTypeBuilder("Product")
            .try_add_property(
                PropertyDefBuilder("name").with_type(PropertyType::String).required().build())
            .try_add_property(PropertyDefBuilder("price").with_type(PropertyType::Float64).build(),
                              {range(0.0, 10000.0)})
            .build();
    EXPECT_TRUE(registry.register_type(std::move(type)).is_ok());

    // Valid data
    std::unordered_map<std::string, Value> valid_data;
    valid_data["name"] = Value::from_handle(1, 0);
    valid_data["price"] = Value::from_float(99.99);

    auto valid_result = registry.validate_object("Product", valid_data);
    EXPECT_TRUE(valid_result.is_ok());

    // Missing required field
    std::unordered_map<std::string, Value> invalid_data;
    invalid_data["price"] = Value::from_float(99.99);

    auto invalid_result = registry.validate_object("Product", invalid_data);
    EXPECT_TRUE(invalid_result.is_err());
    EXPECT_EQ(invalid_result.error(), SchemaError::RequiredPropertyMissing);

    // Non-existent type
    auto missing_result = registry.validate_object("NonExistent", valid_data);
    EXPECT_TRUE(missing_result.is_err());
    EXPECT_EQ(missing_result.error(), SchemaError::TypeNotFound);
}

TEST(SchemaRegistryTest, ValidateLink) {
    SchemaRegistry registry;

    auto customer = ObjectTypeBuilder("Customer").build();
    auto order = ObjectTypeBuilder("Order")
                     .try_add_link(LinkDefBuilder("customer").to("Customer").build())
                     .build();

    EXPECT_TRUE(registry.register_type(std::move(customer)).is_ok());
    EXPECT_TRUE(registry.register_type(std::move(order)).is_ok());

    // Valid link validation
    auto valid = registry.validate_link("Order", "customer", "Customer");
    EXPECT_TRUE(valid.is_ok());

    // Wrong target type
    auto wrong_target = registry.validate_link("Order", "customer", "Product");
    EXPECT_TRUE(wrong_target.is_err());
    EXPECT_EQ(wrong_target.error(), SchemaError::InvalidPropertyType);

    // Non-existent link
    auto no_link = registry.validate_link("Order", "items", "Product");
    EXPECT_TRUE(no_link.is_err());
    EXPECT_EQ(no_link.error(), SchemaError::LinkNotFound);

    // Non-existent source type
    auto no_source = registry.validate_link("Invoice", "customer", "Customer");
    EXPECT_TRUE(no_source.is_err());
    EXPECT_EQ(no_source.error(), SchemaError::TypeNotFound);
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST(SchemaRegistryTest, MaxTypesLimit) {
    SchemaRegistryConfig config;
    config.max_types = 2;
    SchemaRegistry registry(config);

    auto type1 = ObjectTypeBuilder("Type1").build();
    auto type2 = ObjectTypeBuilder("Type2").build();
    auto type3 = ObjectTypeBuilder("Type3").build();

    EXPECT_TRUE(registry.register_type(std::move(type1)).is_ok());
    EXPECT_TRUE(registry.register_type(std::move(type2)).is_ok());

    auto result = registry.register_type(std::move(type3));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::MaxTypesExceeded);
}

TEST(SchemaRegistryTest, Clear) {
    SchemaRegistry registry;

    auto type1 = ObjectTypeBuilder("Type1").build();
    auto type2 = ObjectTypeBuilder("Type2").build();

    EXPECT_TRUE(registry.register_type(std::move(type1)).is_ok());
    EXPECT_TRUE(registry.register_type(std::move(type2)).is_ok());
    EXPECT_EQ(registry.type_count(), 2U);

    registry.clear();

    EXPECT_EQ(registry.type_count(), 0U);
    EXPECT_FALSE(registry.has_type("Type1"));
    EXPECT_FALSE(registry.has_type("Type2"));
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST(SchemaRegistryTest, ConcurrentReads) {
    SchemaRegistry registry;

    // Pre-populate with some types
    for (int i = 0; i < 10; ++i) {
        auto type = ObjectTypeBuilder("Type" + std::to_string(i)).build();
        EXPECT_TRUE(registry.register_type(std::move(type)).is_ok());
    }

    std::atomic<int> read_count{0};
    std::vector<std::thread> threads;

    // Multiple threads reading concurrently
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&registry, &read_count]() {
            for (int i = 0; i < 100; ++i) {
                (void)registry.type_count();
                (void)registry.has_type("Type5");
                (void)registry.type_names();
                read_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(read_count.load(), 400);
}

TEST(SchemaRegistryTest, ConcurrentReadWrite) {
    SchemaRegistry registry;

    std::atomic<int> success_count{0};
    std::atomic<int> read_count{0};

    // Writer thread
    std::thread writer([&registry, &success_count]() {
        for (int i = 0; i < 50; ++i) {
            auto type = ObjectTypeBuilder("ConcurrentType" + std::to_string(i)).build();
            if (registry.register_type(std::move(type)).is_ok()) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    // Reader threads
    std::vector<std::thread> readers;
    for (int t = 0; t < 3; ++t) {
        readers.emplace_back([&registry, &read_count]() {
            for (int i = 0; i < 100; ++i) {
                (void)registry.type_count();
                (void)registry.type_names();
                read_count.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });
    }

    writer.join();
    for (auto& reader : readers) {
        reader.join();
    }

    EXPECT_EQ(success_count.load(), 50);
    EXPECT_EQ(read_count.load(), 300);
    EXPECT_EQ(registry.type_count(), 50U);
}

}  // namespace
}  // namespace dotvm::core::schema
