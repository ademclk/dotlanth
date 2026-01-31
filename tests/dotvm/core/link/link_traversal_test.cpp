/// @file link_traversal_test.cpp
/// @brief Unit tests for LinkManager traversal

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/link/link_manager.hpp"
#include "dotvm/core/link/object_id.hpp"
#include "dotvm/core/schema/link_def.hpp"
#include "dotvm/core/schema/object_type.hpp"
#include "dotvm/core/schema/schema_registry.hpp"
#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::link {
namespace {

[[nodiscard]] std::shared_ptr<state::StateBackend> make_backend() {
    return std::shared_ptr<state::StateBackend>(state::create_state_backend());
}

void sort_ids(std::vector<ObjectId>& ids) {
    std::sort(ids.begin(), ids.end(), [](const ObjectId& lhs, const ObjectId& rhs) {
        if (lhs.type_hash != rhs.type_hash) {
            return lhs.type_hash < rhs.type_hash;
        }
        return lhs.instance_id < rhs.instance_id;
    });
}

}  // namespace

TEST(LinkTraversalTest, SingleHopTraversalReturnsDirectTargets) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto customer = schema::ObjectTypeBuilder("Customer").build();
    auto order = schema::ObjectTypeBuilder("Order")
                     .try_add_link(schema::LinkDefBuilder("customer")
                                       .to("Customer")
                                       .with_cardinality(schema::Cardinality::OneToMany)
                                       .build())
                     .build();

    EXPECT_TRUE(registry->register_type(customer).is_ok());
    EXPECT_TRUE(registry->register_type(order).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId order_id = generator.generate("Order");
    ObjectId customer_a = generator.generate("Customer");
    ObjectId customer_b = generator.generate("Customer");

    EXPECT_TRUE(manager.create_link(order_id, "Order", "customer", customer_a).is_ok());
    EXPECT_TRUE(manager.create_link(order_id, "Order", "customer", customer_b).is_ok());

    std::array<std::string_view, 1> path{"customer"};
    auto result = manager.traverse(order_id, "Order", path);
    ASSERT_TRUE(result.is_ok());

    auto targets = result.value();
    sort_ids(targets);
    std::vector<ObjectId> expected{customer_a, customer_b};
    sort_ids(expected);
    EXPECT_EQ(targets, expected);
}

TEST(LinkTraversalTest, MultiHopTraversalFollowsChain) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto city = schema::ObjectTypeBuilder("City").build();
    auto address = schema::ObjectTypeBuilder("Address")
                       .try_add_link(schema::LinkDefBuilder("city")
                                         .to("City")
                                         .with_cardinality(schema::Cardinality::OneToOne)
                                         .build())
                       .build();
    auto customer = schema::ObjectTypeBuilder("Customer")
                        .try_add_link(schema::LinkDefBuilder("address")
                                          .to("Address")
                                          .with_cardinality(schema::Cardinality::OneToOne)
                                          .build())
                        .build();
    auto order = schema::ObjectTypeBuilder("Order")
                     .try_add_link(schema::LinkDefBuilder("customer")
                                       .to("Customer")
                                       .with_cardinality(schema::Cardinality::OneToOne)
                                       .build())
                     .build();

    EXPECT_TRUE(registry->register_type(city).is_ok());
    EXPECT_TRUE(registry->register_type(address).is_ok());
    EXPECT_TRUE(registry->register_type(customer).is_ok());
    EXPECT_TRUE(registry->register_type(order).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId order_id = generator.generate("Order");
    ObjectId customer_id = generator.generate("Customer");
    ObjectId address_id = generator.generate("Address");
    ObjectId city_id = generator.generate("City");

    EXPECT_TRUE(manager.create_link(order_id, "Order", "customer", customer_id).is_ok());
    EXPECT_TRUE(manager.create_link(customer_id, "Customer", "address", address_id).is_ok());
    EXPECT_TRUE(manager.create_link(address_id, "Address", "city", city_id).is_ok());

    std::array<std::string_view, 3> path{"customer", "address", "city"};
    auto result = manager.traverse(order_id, "Order", path);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().size(), 1U);
    EXPECT_EQ(result.value().front(), city_id);
}

TEST(LinkTraversalTest, EmptyPathReturnsError) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    EXPECT_TRUE(registry->register_type(schema::ObjectTypeBuilder("Order").build()).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId order_id = generator.generate("Order");

    std::vector<std::string_view> path;
    auto result = manager.traverse(order_id, "Order", path);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::TraversalPathEmpty);
}

TEST(LinkTraversalTest, InvalidLinkNameReturnsError) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto customer = schema::ObjectTypeBuilder("Customer").build();
    auto order = schema::ObjectTypeBuilder("Order")
                     .try_add_link(schema::LinkDefBuilder("customer")
                                       .to("Customer")
                                       .with_cardinality(schema::Cardinality::OneToOne)
                                       .build())
                     .build();

    EXPECT_TRUE(registry->register_type(customer).is_ok());
    EXPECT_TRUE(registry->register_type(order).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId order_id = generator.generate("Order");

    std::array<std::string_view, 1> path{"missing"};
    auto result = manager.traverse(order_id, "Order", path);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::TraversalPathInvalid);
}

TEST(LinkTraversalTest, DepthLimitExceededReturnsError) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto customer = schema::ObjectTypeBuilder("Customer").build();
    auto order = schema::ObjectTypeBuilder("Order")
                     .try_add_link(schema::LinkDefBuilder("customer")
                                       .to("Customer")
                                       .with_cardinality(schema::Cardinality::OneToOne)
                                       .build())
                     .build();

    EXPECT_TRUE(registry->register_type(customer).is_ok());
    EXPECT_TRUE(registry->register_type(order).is_ok());

    auto backend = make_backend();
    LinkManagerConfig config = LinkManagerConfig::defaults();
    config.max_traversal_depth = 1;
    LinkManager manager(registry, backend, config);

    ObjectIdGenerator generator;
    ObjectId order_id = generator.generate("Order");

    std::array<std::string_view, 2> path{"customer", "customer"};
    auto result = manager.traverse(order_id, "Order", path);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::TraversalDepthExceeded);
}

TEST(LinkTraversalTest, EmptyIntermediateResultsReturnEmptyVector) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto address = schema::ObjectTypeBuilder("Address").build();
    auto customer = schema::ObjectTypeBuilder("Customer")
                        .try_add_link(schema::LinkDefBuilder("address")
                                          .to("Address")
                                          .with_cardinality(schema::Cardinality::OneToOne)
                                          .build())
                        .build();
    auto order = schema::ObjectTypeBuilder("Order")
                     .try_add_link(schema::LinkDefBuilder("customer")
                                       .to("Customer")
                                       .with_cardinality(schema::Cardinality::OneToOne)
                                       .build())
                     .build();

    EXPECT_TRUE(registry->register_type(address).is_ok());
    EXPECT_TRUE(registry->register_type(customer).is_ok());
    EXPECT_TRUE(registry->register_type(order).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId order_id = generator.generate("Order");
    ObjectId customer_id = generator.generate("Customer");

    EXPECT_TRUE(manager.create_link(order_id, "Order", "customer", customer_id).is_ok());

    std::array<std::string_view, 2> path{"customer", "address"};
    auto result = manager.traverse(order_id, "Order", path);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().empty());
}

TEST(LinkTraversalTest, CycleTraversalCompletesWithinDepth) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto node = schema::ObjectTypeBuilder("Node")
                    .try_add_link(schema::LinkDefBuilder("next")
                                      .to("Node")
                                      .with_cardinality(schema::Cardinality::OneToOne)
                                      .build())
                    .build();

    EXPECT_TRUE(registry->register_type(node).is_ok());

    auto backend = make_backend();
    LinkManagerConfig config = LinkManagerConfig::defaults();
    config.max_traversal_depth = 4;
    LinkManager manager(registry, backend, config);

    ObjectIdGenerator generator;
    ObjectId node_a = generator.generate("Node");
    ObjectId node_b = generator.generate("Node");

    EXPECT_TRUE(manager.create_link(node_a, "Node", "next", node_b).is_ok());
    EXPECT_TRUE(manager.create_link(node_b, "Node", "next", node_a).is_ok());

    std::array<std::string_view, 4> path{"next", "next", "next", "next"};
    auto result = manager.traverse(node_a, "Node", path);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().size(), 1U);
    EXPECT_EQ(result.value().front(), node_a);
}

}  // namespace dotvm::core::link
