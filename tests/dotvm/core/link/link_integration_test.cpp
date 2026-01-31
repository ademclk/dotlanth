/// @file link_integration_test.cpp
/// @brief Integration tests for LinkManager using supply chain domain

#include <algorithm>
#include <array>
#include <latch>
#include <memory>
#include <thread>
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

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

/// @brief IDs for supply chain entities
struct SupplyChainIds {
    ObjectId warehouse_a;
    ObjectId warehouse_b;
    ObjectId product_1;
    ObjectId product_2;
    ObjectId product_3;
    ObjectId shipment_1;
    ObjectId shipment_2;
    ObjectId order_1;
    ObjectId order_2;
};

/// @brief Create all test IDs
[[nodiscard]] SupplyChainIds generate_ids(ObjectIdGenerator& gen) {
    return {
        .warehouse_a = gen.generate("Warehouse"),
        .warehouse_b = gen.generate("Warehouse"),
        .product_1 = gen.generate("Product"),
        .product_2 = gen.generate("Product"),
        .product_3 = gen.generate("Product"),
        .shipment_1 = gen.generate("Shipment"),
        .shipment_2 = gen.generate("Shipment"),
        .order_1 = gen.generate("Order"),
        .order_2 = gen.generate("Order"),
    };
}

/// @brief Register supply chain types in dependency order
void register_supply_chain_types(schema::SchemaRegistry& registry) {
    // Warehouse (no dependencies)
    auto warehouse = schema::ObjectTypeBuilder("Warehouse").build();
    EXPECT_TRUE(registry.register_type(warehouse).is_ok());

    // Product (no dependencies)
    auto product = schema::ObjectTypeBuilder("Product").build();
    EXPECT_TRUE(registry.register_type(product).is_ok());

    // Shipment -> Warehouse (origin, destination)
    auto shipment = schema::ObjectTypeBuilder("Shipment")
                        .try_add_link(schema::LinkDefBuilder("origin")
                                          .to("Warehouse")
                                          .with_cardinality(schema::Cardinality::OneToOne)
                                          .build())
                        .try_add_link(schema::LinkDefBuilder("destination")
                                          .to("Warehouse")
                                          .with_cardinality(schema::Cardinality::OneToOne)
                                          .build())
                        .build();
    EXPECT_TRUE(registry.register_type(shipment).is_ok());

    // Order -> Product (items), Order -> Shipment (shipments)
    auto order = schema::ObjectTypeBuilder("Order")
                     .try_add_link(schema::LinkDefBuilder("items")
                                       .to("Product")
                                       .with_cardinality(schema::Cardinality::OneToMany)
                                       .build())
                     .try_add_link(schema::LinkDefBuilder("shipments")
                                       .to("Shipment")
                                       .with_cardinality(schema::Cardinality::OneToMany)
                                       .build())
                     .build();
    EXPECT_TRUE(registry.register_type(order).is_ok());
}

/// @brief Create state backend
[[nodiscard]] std::shared_ptr<state::StateBackend> make_backend() {
    return std::shared_ptr<state::StateBackend>(state::create_state_backend());
}

/// @brief Sort ObjectIds for comparison
void sort_ids(std::vector<ObjectId>& ids) {
    std::sort(ids.begin(), ids.end(), [](const ObjectId& lhs, const ObjectId& rhs) {
        if (lhs.type_hash != rhs.type_hash) {
            return lhs.type_hash < rhs.type_hash;
        }
        return lhs.instance_id < rhs.instance_id;
    });
}

/// @brief Assert two ObjectId vectors are equal (unordered)
void assert_ids_equal_unordered(std::vector<ObjectId> actual, std::vector<ObjectId> expected) {
    sort_ids(actual);
    sort_ids(expected);
    EXPECT_EQ(actual, expected);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(LinkIntegrationTest, BasicLifecycleWorkflow) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Create order with items: O1 -> [P1, P2]
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "items", ids.product_1).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "items", ids.product_2).is_ok());

    // Create shipment: S1 -> origin:W_A, destination:W_B
    EXPECT_TRUE(manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_1, "Shipment", "destination", ids.warehouse_b).is_ok());

    // Link order to shipment: O1 -> S1
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "shipments", ids.shipment_1).is_ok());

    // Verify has_link
    EXPECT_TRUE(manager.has_link(ids.order_1, "items", ids.product_1));
    EXPECT_TRUE(manager.has_link(ids.order_1, "items", ids.product_2));
    EXPECT_FALSE(manager.has_link(ids.order_1, "items", ids.product_3));
    EXPECT_TRUE(manager.has_link(ids.order_1, "shipments", ids.shipment_1));
    EXPECT_TRUE(manager.has_link(ids.shipment_1, "origin", ids.warehouse_a));
    EXPECT_TRUE(manager.has_link(ids.shipment_1, "destination", ids.warehouse_b));

    // Verify get_links
    auto items_result = manager.get_links(ids.order_1, "items");
    ASSERT_TRUE(items_result.is_ok());
    assert_ids_equal_unordered(items_result.value(), {ids.product_1, ids.product_2});

    auto shipments_result = manager.get_links(ids.order_1, "shipments");
    ASSERT_TRUE(shipments_result.is_ok());
    EXPECT_EQ(shipments_result.value().size(), 1U);
    EXPECT_EQ(shipments_result.value().front(), ids.shipment_1);

    // Verify get_link_count
    auto items_count = manager.get_link_count(ids.order_1, "items");
    ASSERT_TRUE(items_count.is_ok());
    EXPECT_EQ(items_count.value(), 2U);

    auto shipments_count = manager.get_link_count(ids.order_1, "shipments");
    ASSERT_TRUE(shipments_count.is_ok());
    EXPECT_EQ(shipments_count.value(), 1U);
}

TEST(LinkIntegrationTest, BidirectionalQueries) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Setup: Two shipments from same origin (W_A)
    // S1: W_A -> W_B
    // S2: W_A -> W_B
    EXPECT_TRUE(manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_1, "Shipment", "destination", ids.warehouse_b).is_ok());
    EXPECT_TRUE(manager.create_link(ids.shipment_2, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_2, "Shipment", "destination", ids.warehouse_b).is_ok());

    // Setup: O1 -> [S1], O2 -> [S2]
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "shipments", ids.shipment_1).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_2, "Order", "shipments", ids.shipment_2).is_ok());

    // Setup: O1 -> [P1, P2], O2 -> [P2, P3]
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "items", ids.product_1).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "items", ids.product_2).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_2, "Order", "items", ids.product_2).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_2, "Order", "items", ids.product_3).is_ok());

    // Query: All shipments originating from W_A
    auto origin_inverse = manager.get_inverse(ids.warehouse_a, "origin");
    ASSERT_TRUE(origin_inverse.is_ok());
    assert_ids_equal_unordered(origin_inverse.value(), {ids.shipment_1, ids.shipment_2});

    // Query: All shipments destined to W_B
    auto dest_inverse = manager.get_inverse(ids.warehouse_b, "destination");
    ASSERT_TRUE(dest_inverse.is_ok());
    assert_ids_equal_unordered(dest_inverse.value(), {ids.shipment_1, ids.shipment_2});

    // Query: All orders containing P2 (should be both O1 and O2)
    auto product_inverse = manager.get_inverse(ids.product_2, "items");
    ASSERT_TRUE(product_inverse.is_ok());
    assert_ids_equal_unordered(product_inverse.value(), {ids.order_1, ids.order_2});

    // Query: All orders containing P1 (should be only O1)
    auto p1_inverse = manager.get_inverse(ids.product_1, "items");
    ASSERT_TRUE(p1_inverse.is_ok());
    EXPECT_EQ(p1_inverse.value().size(), 1U);
    EXPECT_EQ(p1_inverse.value().front(), ids.order_1);

    // Query: Orders using S1 (should be only O1)
    auto s1_inverse = manager.get_inverse(ids.shipment_1, "shipments");
    ASSERT_TRUE(s1_inverse.is_ok());
    EXPECT_EQ(s1_inverse.value().size(), 1U);
    EXPECT_EQ(s1_inverse.value().front(), ids.order_1);
}

TEST(LinkIntegrationTest, MultiHopTraversalOrderToWarehouse) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Setup: O1 -> S1 -> W_A (origin), W_B (destination)
    EXPECT_TRUE(manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_1, "Shipment", "destination", ids.warehouse_b).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "shipments", ids.shipment_1).is_ok());

    // Traverse: Order -> shipments -> destination (should return W_B)
    std::array<std::string_view, 2> path_to_dest{"shipments", "destination"};
    auto dest_result = manager.traverse(ids.order_1, "Order", path_to_dest);
    ASSERT_TRUE(dest_result.is_ok());
    EXPECT_EQ(dest_result.value().size(), 1U);
    EXPECT_EQ(dest_result.value().front(), ids.warehouse_b);

    // Traverse: Order -> shipments -> origin (should return W_A)
    std::array<std::string_view, 2> path_to_origin{"shipments", "origin"};
    auto origin_result = manager.traverse(ids.order_1, "Order", path_to_origin);
    ASSERT_TRUE(origin_result.is_ok());
    EXPECT_EQ(origin_result.value().size(), 1U);
    EXPECT_EQ(origin_result.value().front(), ids.warehouse_a);
}

TEST(LinkIntegrationTest, MultiHopTraversalWithMultipleShipments) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Setup: O2 -> [S1, S2]
    // S1: W_A -> W_B
    // S2: W_B -> W_A (reversed)
    EXPECT_TRUE(manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_1, "Shipment", "destination", ids.warehouse_b).is_ok());
    EXPECT_TRUE(manager.create_link(ids.shipment_2, "Shipment", "origin", ids.warehouse_b).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_2, "Shipment", "destination", ids.warehouse_a).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_2, "Order", "shipments", ids.shipment_1).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_2, "Order", "shipments", ids.shipment_2).is_ok());

    // Traverse: O2 -> shipments -> destination
    // Should return both W_A and W_B (S1.dest=W_B, S2.dest=W_A)
    std::array<std::string_view, 2> path{"shipments", "destination"};
    auto result = manager.traverse(ids.order_2, "Order", path);
    ASSERT_TRUE(result.is_ok());
    assert_ids_equal_unordered(result.value(), {ids.warehouse_a, ids.warehouse_b});

    // Traverse: O2 -> shipments -> origin
    // Should return both W_A and W_B (S1.origin=W_A, S2.origin=W_B)
    std::array<std::string_view, 2> origin_path{"shipments", "origin"};
    auto origin_result = manager.traverse(ids.order_2, "Order", origin_path);
    ASSERT_TRUE(origin_result.is_ok());
    assert_ids_equal_unordered(origin_result.value(), {ids.warehouse_a, ids.warehouse_b});
}

TEST(LinkIntegrationTest, CascadeDeleteOrderLinks) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Setup: O1 -> [P1, P2] (items), O1 -> [S1] (shipments)
    // S1 -> W_A (origin), W_B (destination)
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "items", ids.product_1).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "items", ids.product_2).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "shipments", ids.shipment_1).is_ok());
    EXPECT_TRUE(manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_1, "Shipment", "destination", ids.warehouse_b).is_ok());

    // Verify initial state
    EXPECT_TRUE(manager.has_link(ids.order_1, "items", ids.product_1));
    EXPECT_TRUE(manager.has_link(ids.order_1, "shipments", ids.shipment_1));
    EXPECT_TRUE(manager.has_link(ids.shipment_1, "origin", ids.warehouse_a));

    // Remove all outgoing links from O1
    auto remove_result = manager.remove_all_links_from(ids.order_1, "Order", CascadePolicy::None);
    EXPECT_TRUE(remove_result.is_ok());

    // Verify O1 links removed
    EXPECT_FALSE(manager.has_link(ids.order_1, "items", ids.product_1));
    EXPECT_FALSE(manager.has_link(ids.order_1, "items", ids.product_2));
    EXPECT_FALSE(manager.has_link(ids.order_1, "shipments", ids.shipment_1));

    // Verify shipment warehouse links remain intact (not cascaded)
    EXPECT_TRUE(manager.has_link(ids.shipment_1, "origin", ids.warehouse_a));
    EXPECT_TRUE(manager.has_link(ids.shipment_1, "destination", ids.warehouse_b));

    // Verify link counts
    auto items_count = manager.get_link_count(ids.order_1, "items");
    ASSERT_TRUE(items_count.is_ok());
    EXPECT_EQ(items_count.value(), 0U);
}

TEST(LinkIntegrationTest, RemoveAllLinksToWarehouse) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Setup: S1 -> W_A (origin), W_B (destination)
    //        S2 -> W_A (origin), W_B (destination)
    EXPECT_TRUE(manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_1, "Shipment", "destination", ids.warehouse_b).is_ok());
    EXPECT_TRUE(manager.create_link(ids.shipment_2, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_2, "Shipment", "destination", ids.warehouse_b).is_ok());

    // Orders linked to shipments
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "shipments", ids.shipment_1).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_2, "Order", "shipments", ids.shipment_2).is_ok());

    // Remove all links TO W_B (destination links)
    auto remove_result = manager.remove_all_links_to(ids.warehouse_b, "Warehouse");
    EXPECT_TRUE(remove_result.is_ok());

    // Destination links should be removed
    EXPECT_FALSE(manager.has_link(ids.shipment_1, "destination", ids.warehouse_b));
    EXPECT_FALSE(manager.has_link(ids.shipment_2, "destination", ids.warehouse_b));

    // Origin links should remain (different link type to different target)
    EXPECT_TRUE(manager.has_link(ids.shipment_1, "origin", ids.warehouse_a));
    EXPECT_TRUE(manager.has_link(ids.shipment_2, "origin", ids.warehouse_a));

    // Order -> Shipment links should remain
    EXPECT_TRUE(manager.has_link(ids.order_1, "shipments", ids.shipment_1));
    EXPECT_TRUE(manager.has_link(ids.order_2, "shipments", ids.shipment_2));
}

TEST(LinkIntegrationTest, BulkUnlinkShipment) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Setup: S1 -> W_A, W_B (origin, destination)
    //        O1 -> S1 (shipments)
    EXPECT_TRUE(manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_1, "Shipment", "destination", ids.warehouse_b).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "shipments", ids.shipment_1).is_ok());

    // Remove all links FROM S1 (origin and destination)
    auto remove_result =
        manager.remove_all_links_from(ids.shipment_1, "Shipment", CascadePolicy::None);
    EXPECT_TRUE(remove_result.is_ok());

    // Shipment links removed
    EXPECT_FALSE(manager.has_link(ids.shipment_1, "origin", ids.warehouse_a));
    EXPECT_FALSE(manager.has_link(ids.shipment_1, "destination", ids.warehouse_b));

    // Order -> Shipment link should remain (different direction)
    EXPECT_TRUE(manager.has_link(ids.order_1, "shipments", ids.shipment_1));
}

TEST(LinkIntegrationTest, ConcurrentAccessPatterns) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Pre-generate unique temp product IDs to avoid collision with ids.product_*
    // (ObjectIdGenerator starts at 1 per-instance, so local_gen would collide)
    // Use "Product" type since that's what Order.items links to (schema validation)
    constexpr int kIterations = 100;
    std::vector<ObjectId> temp_products;
    temp_products.reserve(kIterations);
    for (int i = 0; i < kIterations; ++i) {
        temp_products.push_back(gen.generate("Product"));
    }

    // Pre-create the baseline link
    EXPECT_TRUE(manager.create_link(ids.order_2, "Order", "items", ids.product_1).is_ok());

    std::atomic<int> writer_count{0};
    std::atomic<int> reader_count{0};

    // Synchronization barrier to ensure true concurrent start
    std::latch start_barrier(3);

    // Thread A: Writer - adds/removes items using pre-generated unique IDs
    std::thread writer([&] {
        start_barrier.arrive_and_wait();
        for (int i = 0; i < kIterations; ++i) {
            ObjectId temp_product = temp_products[static_cast<std::size_t>(i)];
            auto create_result = manager.create_link(ids.order_2, "Order", "items", temp_product);
            if (create_result.is_ok()) {
                ++writer_count;
                // Remove immediately
                std::ignore = manager.remove_link(ids.order_2, "Order", "items", temp_product,
                                                  CascadePolicy::None);
            }
        }
    });

    // Thread B: Reader - reads link count
    std::thread reader_count_thread([&] {
        start_barrier.arrive_and_wait();
        for (int i = 0; i < kIterations; ++i) {
            auto count_result = manager.get_link_count(ids.order_2, "items");
            if (count_result.is_ok()) {
                ++reader_count;
            }
        }
    });

    // Thread C: Reader - reads inverse
    std::thread reader_inverse_thread([&] {
        start_barrier.arrive_and_wait();
        for (int i = 0; i < kIterations; ++i) {
            auto inverse_result = manager.get_inverse(ids.product_1, "items");
            // Should always succeed
            EXPECT_TRUE(inverse_result.is_ok());
        }
    });

    writer.join();
    reader_count_thread.join();
    reader_inverse_thread.join();

    // All operations should have completed without crashes
    EXPECT_GT(writer_count.load(), 0);
    EXPECT_GT(reader_count.load(), 0);

    // Final state: order_2 should have exactly product_1 (all temp products removed)
    EXPECT_TRUE(manager.has_link(ids.order_2, "items", ids.product_1));

    auto final_links = manager.get_links(ids.order_2, "items");
    ASSERT_TRUE(final_links.is_ok());
    EXPECT_EQ(final_links.value().size(), 1U);
    EXPECT_EQ(final_links.value().front(), ids.product_1);

    auto final_count = manager.get_link_count(ids.order_2, "items");
    ASSERT_TRUE(final_count.is_ok());
    EXPECT_EQ(final_count.value(), 1U);
}

TEST(LinkIntegrationTest, ComplexQueryOrdersForWarehouse) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Setup complex scenario:
    // O1 -> S1 -> W_A (origin), W_B (dest)
    // O2 -> S2 -> W_B (origin), W_A (dest)
    EXPECT_TRUE(manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_a).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_1, "Shipment", "destination", ids.warehouse_b).is_ok());
    EXPECT_TRUE(manager.create_link(ids.shipment_2, "Shipment", "origin", ids.warehouse_b).is_ok());
    EXPECT_TRUE(
        manager.create_link(ids.shipment_2, "Shipment", "destination", ids.warehouse_a).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "shipments", ids.shipment_1).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_2, "Order", "shipments", ids.shipment_2).is_ok());

    // Query: Find all shipments originating FROM W_A
    auto from_wa = manager.get_inverse(ids.warehouse_a, "origin");
    ASSERT_TRUE(from_wa.is_ok());
    EXPECT_EQ(from_wa.value().size(), 1U);
    EXPECT_EQ(from_wa.value().front(), ids.shipment_1);

    // Query: Find all shipments going TO W_A
    auto to_wa = manager.get_inverse(ids.warehouse_a, "destination");
    ASSERT_TRUE(to_wa.is_ok());
    EXPECT_EQ(to_wa.value().size(), 1U);
    EXPECT_EQ(to_wa.value().front(), ids.shipment_2);

    // Query: Find all orders related to W_A (via shipment origin or destination)
    // Step 1: Get shipments from/to W_A
    std::vector<ObjectId> wa_shipments;
    wa_shipments.insert(wa_shipments.end(), from_wa.value().begin(), from_wa.value().end());
    wa_shipments.insert(wa_shipments.end(), to_wa.value().begin(), to_wa.value().end());

    // Step 2: Get orders for each shipment
    std::vector<ObjectId> wa_orders;
    for (const auto& shipment_id : wa_shipments) {
        auto orders = manager.get_inverse(shipment_id, "shipments");
        if (orders.is_ok()) {
            wa_orders.insert(wa_orders.end(), orders.value().begin(), orders.value().end());
        }
    }

    // Should find both orders (O1 originates from W_A, O2 delivers to W_A)
    assert_ids_equal_unordered(wa_orders, {ids.order_1, ids.order_2});
}

TEST(LinkIntegrationTest, RemoveSingleLinkPreservesOthers) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Setup: O1 -> [P1, P2, P3]
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "items", ids.product_1).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "items", ids.product_2).is_ok());
    EXPECT_TRUE(manager.create_link(ids.order_1, "Order", "items", ids.product_3).is_ok());

    // Verify initial count
    auto count_before = manager.get_link_count(ids.order_1, "items");
    ASSERT_TRUE(count_before.is_ok());
    EXPECT_EQ(count_before.value(), 3U);

    // Remove single link: O1 -> P2
    auto remove_result =
        manager.remove_link(ids.order_1, "Order", "items", ids.product_2, CascadePolicy::None);
    EXPECT_TRUE(remove_result.is_ok());

    // Verify P1 and P3 remain, P2 removed
    EXPECT_TRUE(manager.has_link(ids.order_1, "items", ids.product_1));
    EXPECT_FALSE(manager.has_link(ids.order_1, "items", ids.product_2));
    EXPECT_TRUE(manager.has_link(ids.order_1, "items", ids.product_3));

    // Verify count
    auto count_after = manager.get_link_count(ids.order_1, "items");
    ASSERT_TRUE(count_after.is_ok());
    EXPECT_EQ(count_after.value(), 2U);

    // Verify inverse index updated
    auto p2_inverse = manager.get_inverse(ids.product_2, "items");
    ASSERT_TRUE(p2_inverse.is_ok());
    EXPECT_TRUE(p2_inverse.value().empty());
}

// ============================================================================
// Negative / Error Case Tests
// ============================================================================

TEST(LinkIntegrationTest, InvalidLinkNameReturnsError) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Attempt to create link with non-existent link name
    auto result = manager.create_link(ids.order_1, "Order", "nonexistent_link", ids.product_1);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::LinkDefinitionNotFound);
}

TEST(LinkIntegrationTest, RemoveNonExistentLinkReturnsError) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Attempt to remove a link that was never created
    auto result =
        manager.remove_link(ids.order_1, "Order", "items", ids.product_1, CascadePolicy::None);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::LinkInstanceNotFound);
}

TEST(LinkIntegrationTest, OneToOneCardinalityEnforced) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Shipment.origin is OneToOne - first link should succeed
    auto first_result = manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_a);
    EXPECT_TRUE(first_result.is_ok());

    // Second link to different warehouse should fail (OneToOne violation)
    auto second_result = manager.create_link(ids.shipment_1, "Shipment", "origin", ids.warehouse_b);
    EXPECT_TRUE(second_result.is_err());
    EXPECT_EQ(second_result.error(), LinkError::CardinalityViolation);

    // The original link should still exist
    EXPECT_TRUE(manager.has_link(ids.shipment_1, "origin", ids.warehouse_a));
    EXPECT_FALSE(manager.has_link(ids.shipment_1, "origin", ids.warehouse_b));
}

TEST(LinkIntegrationTest, DuplicateLinkReturnsError) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Create link: O1 -> P1
    auto first_result = manager.create_link(ids.order_1, "Order", "items", ids.product_1);
    EXPECT_TRUE(first_result.is_ok());

    // Attempt to create the same link again (duplicate)
    auto duplicate_result = manager.create_link(ids.order_1, "Order", "items", ids.product_1);
    EXPECT_TRUE(duplicate_result.is_err());
    EXPECT_EQ(duplicate_result.error(), LinkError::LinkInstanceAlreadyExists);

    // Should still have exactly one link
    auto count = manager.get_link_count(ids.order_1, "items");
    ASSERT_TRUE(count.is_ok());
    EXPECT_EQ(count.value(), 1U);
}

TEST(LinkIntegrationTest, InvalidObjectIdReturnsError) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    register_supply_chain_types(*registry);

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator gen;
    auto ids = generate_ids(gen);

    // Attempt to create link with invalid source ID
    ObjectId invalid_source = ObjectId::invalid();
    auto result = manager.create_link(invalid_source, "Order", "items", ids.product_1);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::InvalidObjectId);

    // Attempt to create link with invalid target ID
    ObjectId invalid_target = ObjectId::invalid();
    auto result2 = manager.create_link(ids.order_1, "Order", "items", invalid_target);
    EXPECT_TRUE(result2.is_err());
    EXPECT_EQ(result2.error(), LinkError::InvalidObjectId);
}

}  // namespace
}  // namespace dotvm::core::link
