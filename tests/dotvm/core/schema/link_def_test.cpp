/// @file link_def_test.cpp
/// @brief Unit tests for LinkDef and Cardinality

#include <format>

#include <gtest/gtest.h>

#include "dotvm/core/schema/link_def.hpp"

namespace dotvm::core::schema {
namespace {

// ============================================================================
// Cardinality Tests
// ============================================================================

TEST(CardinalityTest, EnumValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(Cardinality::OneToOne), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(Cardinality::OneToMany), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(Cardinality::ManyToMany), 2);
}

TEST(CardinalityTest, ToString) {
    EXPECT_EQ(to_string(Cardinality::OneToOne), "OneToOne");
    EXPECT_EQ(to_string(Cardinality::OneToMany), "OneToMany");
    EXPECT_EQ(to_string(Cardinality::ManyToMany), "ManyToMany");
}

TEST(CardinalityTest, Formatter) {
    EXPECT_EQ(std::format("{}", Cardinality::OneToOne), "OneToOne");
    EXPECT_EQ(std::format("{}", Cardinality::ManyToMany), "ManyToMany");
}

TEST(CardinalityTest, MaxTargets) {
    EXPECT_EQ(max_targets(Cardinality::OneToOne), 1U);
    EXPECT_EQ(max_targets(Cardinality::OneToMany), 0U);   // 0 = unlimited
    EXPECT_EQ(max_targets(Cardinality::ManyToMany), 0U);  // 0 = unlimited
}

// ============================================================================
// LinkDef Tests
// ============================================================================

TEST(LinkDefTest, DefaultConstruction) {
    LinkDef link;
    EXPECT_TRUE(link.name.empty());
    EXPECT_TRUE(link.source_type.empty());
    EXPECT_TRUE(link.target_type.empty());
    EXPECT_EQ(link.cardinality, Cardinality::OneToOne);
    EXPECT_FALSE(link.required);
    EXPECT_TRUE(link.inverse_link.empty());
}

TEST(LinkDefTest, Equality) {
    LinkDef link1;
    link1.name = "customer";
    link1.source_type = "Order";
    link1.target_type = "Customer";
    link1.cardinality = Cardinality::OneToOne;
    link1.required = true;

    LinkDef link2;
    link2.name = "customer";
    link2.source_type = "Order";
    link2.target_type = "Customer";
    link2.cardinality = Cardinality::OneToOne;
    link2.required = true;

    EXPECT_EQ(link1, link2);

    link2.required = false;
    EXPECT_NE(link1, link2);
}

TEST(LinkDefTest, EqualityWithInverse) {
    LinkDef link1;
    link1.name = "orders";
    link1.source_type = "Customer";
    link1.target_type = "Order";
    link1.cardinality = Cardinality::OneToMany;
    link1.inverse_link = "customer";

    LinkDef link2;
    link2.name = "orders";
    link2.source_type = "Customer";
    link2.target_type = "Order";
    link2.cardinality = Cardinality::OneToMany;
    link2.inverse_link = "customer";

    EXPECT_EQ(link1, link2);

    link2.inverse_link = "different";
    EXPECT_NE(link1, link2);
}

// ============================================================================
// LinkDefBuilder Tests
// ============================================================================

TEST(LinkDefBuilderTest, BasicUsage) {
    auto link = LinkDefBuilder("customer").from("Order").to("Customer").build();

    EXPECT_EQ(link.name, "customer");
    EXPECT_EQ(link.source_type, "Order");
    EXPECT_EQ(link.target_type, "Customer");
    EXPECT_EQ(link.cardinality, Cardinality::OneToOne);
    EXPECT_FALSE(link.required);
}

TEST(LinkDefBuilderTest, WithCardinality) {
    auto link = LinkDefBuilder("items")
                    .from("Order")
                    .to("Product")
                    .with_cardinality(Cardinality::OneToMany)
                    .build();

    EXPECT_EQ(link.cardinality, Cardinality::OneToMany);
}

TEST(LinkDefBuilderTest, Required) {
    auto link = LinkDefBuilder("owner").from("Document").to("User").required().build();

    EXPECT_TRUE(link.required);
}

TEST(LinkDefBuilderTest, WithInverse) {
    auto link = LinkDefBuilder("orders")
                    .from("Customer")
                    .to("Order")
                    .with_cardinality(Cardinality::OneToMany)
                    .with_inverse("customer")
                    .build();

    EXPECT_EQ(link.inverse_link, "customer");
}

TEST(LinkDefBuilderTest, FullChain) {
    auto link = LinkDefBuilder("shipments")
                    .from("Warehouse")
                    .to("Shipment")
                    .with_cardinality(Cardinality::OneToMany)
                    .required(false)
                    .with_inverse("origin")
                    .build();

    EXPECT_EQ(link.name, "shipments");
    EXPECT_EQ(link.source_type, "Warehouse");
    EXPECT_EQ(link.target_type, "Shipment");
    EXPECT_EQ(link.cardinality, Cardinality::OneToMany);
    EXPECT_FALSE(link.required);
    EXPECT_EQ(link.inverse_link, "origin");
}

// ============================================================================
// Bidirectional Link Pattern Tests
// ============================================================================

TEST(LinkDefBuilderTest, BidirectionalLinks) {
    // Customer -> Orders (one-to-many)
    auto customer_orders = LinkDefBuilder("orders")
                               .from("Customer")
                               .to("Order")
                               .with_cardinality(Cardinality::OneToMany)
                               .with_inverse("customer")
                               .build();

    // Order -> Customer (many-to-one = one-to-one from Order's perspective)
    auto order_customer = LinkDefBuilder("customer")
                              .from("Order")
                              .to("Customer")
                              .with_cardinality(Cardinality::OneToOne)
                              .required(true)
                              .with_inverse("orders")
                              .build();

    EXPECT_EQ(customer_orders.inverse_link, "customer");
    EXPECT_EQ(order_customer.inverse_link, "orders");
    EXPECT_EQ(customer_orders.target_type, order_customer.source_type);
    EXPECT_EQ(customer_orders.source_type, order_customer.target_type);
}

}  // namespace
}  // namespace dotvm::core::schema
