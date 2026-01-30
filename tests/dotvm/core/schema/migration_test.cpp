/// @file migration_test.cpp
/// @brief Unit tests for MigrationVersion and MigrationManager

#include <format>

#include <gtest/gtest.h>

#include "dotvm/core/schema/migration.hpp"

namespace dotvm::core::schema {
namespace {

// ============================================================================
// MigrationVersion Tests
// ============================================================================

TEST(MigrationVersionTest, DefaultConstruction) {
    MigrationVersion v;
    EXPECT_EQ(v.major, 0);
    EXPECT_EQ(v.minor, 0);
    EXPECT_EQ(v.patch, 0);
}

TEST(MigrationVersionTest, Construction) {
    MigrationVersion v{1, 2, 3};
    EXPECT_EQ(v.major, 1);
    EXPECT_EQ(v.minor, 2);
    EXPECT_EQ(v.patch, 3);
}

TEST(MigrationVersionTest, Equality) {
    MigrationVersion v1{1, 2, 3};
    MigrationVersion v2{1, 2, 3};
    MigrationVersion v3{1, 2, 4};

    EXPECT_EQ(v1, v2);
    EXPECT_NE(v1, v3);
}

TEST(MigrationVersionTest, Comparison) {
    MigrationVersion v100{1, 0, 0};
    MigrationVersion v110{1, 1, 0};
    MigrationVersion v111{1, 1, 1};
    MigrationVersion v200{2, 0, 0};

    EXPECT_LT(v100, v110);
    EXPECT_LT(v110, v111);
    EXPECT_LT(v111, v200);
    EXPECT_GT(v200, v100);

    EXPECT_LE(v100, v100);
    EXPECT_LE(v100, v110);
    EXPECT_GE(v200, v200);
    EXPECT_GE(v200, v100);
}

TEST(MigrationVersionTest, SpaceshipOperator) {
    MigrationVersion v1{1, 0, 0};
    MigrationVersion v2{2, 0, 0};

    EXPECT_TRUE((v1 <=> v2) < 0);
    EXPECT_TRUE((v2 <=> v1) > 0);
    EXPECT_TRUE((v1 <=> v1) == 0);
}

TEST(MigrationVersionTest, ToString) {
    MigrationVersion v{1, 2, 3};
    EXPECT_EQ(v.to_string(), "1.2.3");

    MigrationVersion v0{0, 0, 0};
    EXPECT_EQ(v0.to_string(), "0.0.0");

    MigrationVersion v_large{100, 200, 300};
    EXPECT_EQ(v_large.to_string(), "100.200.300");
}

TEST(MigrationVersionTest, Formatter) {
    MigrationVersion v{1, 2, 3};
    EXPECT_EQ(std::format("{}", v), "1.2.3");
    EXPECT_EQ(std::format("Version: {}", v), "Version: 1.2.3");
}

TEST(MigrationVersionTest, ParseValid) {
    auto result = MigrationVersion::parse("1.2.3");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().major, 1);
    EXPECT_EQ(result.value().minor, 2);
    EXPECT_EQ(result.value().patch, 3);
}

TEST(MigrationVersionTest, ParseZero) {
    auto result = MigrationVersion::parse("0.0.0");
    ASSERT_TRUE(result.is_ok());
    MigrationVersion expected{0, 0, 0};
    EXPECT_EQ(result.value(), expected);
}

TEST(MigrationVersionTest, ParseInvalidNoDots) {
    auto result = MigrationVersion::parse("123");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::VersionConflict);
}

TEST(MigrationVersionTest, ParseInvalidOneDot) {
    auto result = MigrationVersion::parse("1.2");
    EXPECT_TRUE(result.is_err());
}

TEST(MigrationVersionTest, ParseInvalidNonNumeric) {
    auto result = MigrationVersion::parse("1.2.abc");
    EXPECT_TRUE(result.is_err());
}

// ============================================================================
// Migration Struct Tests
// ============================================================================

TEST(MigrationTest, IsReversible) {
    Migration reversible{.from = {0, 0, 0},
                         .to = {1, 0, 0},
                         .description = "Add User type",
                         .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; },
                         .down = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }};

    EXPECT_TRUE(reversible.is_reversible());

    Migration irreversible{.from = {0, 0, 0},
                           .to = {1, 0, 0},
                           .description = "Add User type",
                           .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; },
                           .down = nullptr};

    EXPECT_FALSE(irreversible.is_reversible());
}

// ============================================================================
// MigrationManager Tests
// ============================================================================

TEST(MigrationManagerTest, DefaultConstruction) {
    MigrationManager manager;
    MigrationVersion expected_v0{0, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v0);
    EXPECT_EQ(manager.latest_version(), expected_v0);
    EXPECT_EQ(manager.migration_count(), 0U);
}

TEST(MigrationManagerTest, RegisterMigration) {
    MigrationManager manager;

    auto result = manager.register_migration({.from = {0, 0, 0},
                                              .to = {1, 0, 0},
                                              .description = "Initial schema",
                                              .up = [](SchemaRegistry& reg) {
                                                  auto type = ObjectTypeBuilder("User").build();
                                                  return reg.register_type(std::move(type));
                                              }});

    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(manager.migration_count(), 1U);
    MigrationVersion expected_v1{1, 0, 0};
    EXPECT_EQ(manager.latest_version(), expected_v1);
}

TEST(MigrationManagerTest, RegisterMigrationChain) {
    MigrationManager manager;

    EXPECT_TRUE(
        manager
            .register_migration({.from = {0, 0, 0},
                                 .to = {1, 0, 0},
                                 .description = "V1",
                                 .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }})
            .is_ok());

    EXPECT_TRUE(
        manager
            .register_migration({.from = {1, 0, 0},
                                 .to = {1, 1, 0},
                                 .description = "V1.1",
                                 .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }})
            .is_ok());

    EXPECT_TRUE(
        manager
            .register_migration({.from = {1, 1, 0},
                                 .to = {2, 0, 0},
                                 .description = "V2",
                                 .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }})
            .is_ok());

    EXPECT_EQ(manager.migration_count(), 3U);
    MigrationVersion expected_v2{2, 0, 0};
    EXPECT_EQ(manager.latest_version(), expected_v2);
}

TEST(MigrationManagerTest, RegisterMigrationVersionConflict) {
    MigrationManager manager;

    EXPECT_TRUE(
        manager
            .register_migration({.from = {0, 0, 0},
                                 .to = {1, 0, 0},
                                 .description = "V1",
                                 .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }})
            .is_ok());

    // Try to register migration that doesn't chain properly
    auto result = manager.register_migration(
        {.from = {0, 0, 0},  // Should be 1.0.0
         .to = {2, 0, 0},
         .description = "V2",
         .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }});

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::VersionConflict);
}

TEST(MigrationManagerTest, RegisterMigrationNotFromZero) {
    MigrationManager manager;

    // First migration must start from 0.0.0
    auto result = manager.register_migration(
        {.from = {1, 0, 0}, .to = {2, 0, 0}, .description = "V2", .up = [](SchemaRegistry&) {
             return Result<void, SchemaError>{};
         }});

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::VersionConflict);
}

TEST(MigrationManagerTest, RegisterMigrationVersionBackward) {
    MigrationManager manager;

    // Target version must be greater than source
    auto result = manager.register_migration(
        {.from = {0, 0, 0}, .to = {0, 0, 0}, .description = "No-op", .up = [](SchemaRegistry&) {
             return Result<void, SchemaError>{};
         }});

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::VersionConflict);
}

// ============================================================================
// Migration Execution Tests
// ============================================================================

TEST(MigrationManagerTest, MigrateForward) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration({.from = {0, 0, 0},
                                         .to = {1, 0, 0},
                                         .description = "Add User",
                                         .up =
                                             [](SchemaRegistry& reg) {
                                                 auto type = ObjectTypeBuilder("User").build();
                                                 return reg.register_type(std::move(type));
                                             }})
                    .is_ok());

    auto result = manager.migrate_to(registry, {1, 0, 0});
    EXPECT_TRUE(result.is_ok());
    MigrationVersion expected_v1{1, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v1);
    EXPECT_TRUE(registry.has_type("User"));
}

TEST(MigrationManagerTest, MigrateToLatest) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration({.from = {0, 0, 0},
                                         .to = {1, 0, 0},
                                         .description = "Add User",
                                         .up =
                                             [](SchemaRegistry& reg) {
                                                 auto type = ObjectTypeBuilder("User").build();
                                                 return reg.register_type(std::move(type));
                                             }})
                    .is_ok());

    EXPECT_TRUE(manager
                    .register_migration({.from = {1, 0, 0},
                                         .to = {2, 0, 0},
                                         .description = "Add Product",
                                         .up =
                                             [](SchemaRegistry& reg) {
                                                 auto type = ObjectTypeBuilder("Product").build();
                                                 return reg.register_type(std::move(type));
                                             }})
                    .is_ok());

    auto result = manager.migrate_to_latest(registry);
    EXPECT_TRUE(result.is_ok());
    MigrationVersion expected_v2{2, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v2);
    EXPECT_TRUE(registry.has_type("User"));
    EXPECT_TRUE(registry.has_type("Product"));
}

TEST(MigrationManagerTest, MigratePartial) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration({.from = {0, 0, 0},
                                         .to = {1, 0, 0},
                                         .description = "Add User",
                                         .up =
                                             [](SchemaRegistry& reg) {
                                                 auto type = ObjectTypeBuilder("User").build();
                                                 return reg.register_type(std::move(type));
                                             }})
                    .is_ok());

    EXPECT_TRUE(manager
                    .register_migration({.from = {1, 0, 0},
                                         .to = {2, 0, 0},
                                         .description = "Add Product",
                                         .up =
                                             [](SchemaRegistry& reg) {
                                                 auto type = ObjectTypeBuilder("Product").build();
                                                 return reg.register_type(std::move(type));
                                             }})
                    .is_ok());

    // Migrate only to 1.0.0
    auto result = manager.migrate_to(registry, {1, 0, 0});
    EXPECT_TRUE(result.is_ok());
    MigrationVersion expected_v1{1, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v1);
    EXPECT_TRUE(registry.has_type("User"));
    EXPECT_FALSE(registry.has_type("Product"));
}

TEST(MigrationManagerTest, MigrateAlreadyAtTarget) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration({.from = {0, 0, 0},
                                         .to = {1, 0, 0},
                                         .description = "Add User",
                                         .up =
                                             [](SchemaRegistry& reg) {
                                                 auto type = ObjectTypeBuilder("User").build();
                                                 return reg.register_type(std::move(type));
                                             }})
                    .is_ok());

    EXPECT_TRUE(manager.migrate_to(registry, {1, 0, 0}).is_ok());

    // Migrating to same version is a no-op
    auto result = manager.migrate_to(registry, {1, 0, 0});
    EXPECT_TRUE(result.is_ok());
}

// ============================================================================
// Rollback Tests
// ============================================================================

TEST(MigrationManagerTest, RollbackReversible) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration(
                        {.from = {0, 0, 0},
                         .to = {1, 0, 0},
                         .description = "Add User",
                         .up =
                             [](SchemaRegistry& reg) {
                                 auto type = ObjectTypeBuilder("User").build();
                                 return reg.register_type(std::move(type));
                             },
                         .down = [](SchemaRegistry& reg) { return reg.unregister_type("User"); }})
                    .is_ok());

    // Migrate forward
    EXPECT_TRUE(manager.migrate_to(registry, {1, 0, 0}).is_ok());
    EXPECT_TRUE(registry.has_type("User"));

    // Rollback
    auto result = manager.rollback_to(registry, {0, 0, 0});
    EXPECT_TRUE(result.is_ok());
    MigrationVersion expected_v0{0, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v0);
    EXPECT_FALSE(registry.has_type("User"));
}

TEST(MigrationManagerTest, RollbackIrreversible) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration({
                        .from = {0, 0, 0},
                        .to = {1, 0, 0},
                        .description = "Add User",
                        .up =
                            [](SchemaRegistry& reg) {
                                auto type = ObjectTypeBuilder("User").build();
                                return reg.register_type(std::move(type));
                            }
                        // No .down = irreversible
                    })
                    .is_ok());

    EXPECT_TRUE(manager.migrate_to(registry, {1, 0, 0}).is_ok());

    // Cannot rollback irreversible migration
    auto result = manager.rollback_to(registry, {0, 0, 0});
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::MigrationFailed);
}

TEST(MigrationManagerTest, RollbackMultiple) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration(
                        {.from = {0, 0, 0},
                         .to = {1, 0, 0},
                         .description = "Add User",
                         .up =
                             [](SchemaRegistry& reg) {
                                 auto type = ObjectTypeBuilder("User").build();
                                 return reg.register_type(std::move(type));
                             },
                         .down = [](SchemaRegistry& reg) { return reg.unregister_type("User"); }})
                    .is_ok());

    EXPECT_TRUE(
        manager
            .register_migration(
                {.from = {1, 0, 0},
                 .to = {2, 0, 0},
                 .description = "Add Product",
                 .up =
                     [](SchemaRegistry& reg) {
                         auto type = ObjectTypeBuilder("Product").build();
                         return reg.register_type(std::move(type));
                     },
                 .down = [](SchemaRegistry& reg) { return reg.unregister_type("Product"); }})
            .is_ok());

    EXPECT_TRUE(manager.migrate_to_latest(registry).is_ok());
    EXPECT_TRUE(registry.has_type("User"));
    EXPECT_TRUE(registry.has_type("Product"));

    // Rollback to 0.0.0
    auto result = manager.rollback_to(registry, {0, 0, 0});
    EXPECT_TRUE(result.is_ok());
    MigrationVersion expected_v0{0, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v0);
    EXPECT_FALSE(registry.has_type("User"));
    EXPECT_FALSE(registry.has_type("Product"));
}

TEST(MigrationManagerTest, RollbackPartial) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration(
                        {.from = {0, 0, 0},
                         .to = {1, 0, 0},
                         .description = "Add User",
                         .up =
                             [](SchemaRegistry& reg) {
                                 auto type = ObjectTypeBuilder("User").build();
                                 return reg.register_type(std::move(type));
                             },
                         .down = [](SchemaRegistry& reg) { return reg.unregister_type("User"); }})
                    .is_ok());

    EXPECT_TRUE(
        manager
            .register_migration(
                {.from = {1, 0, 0},
                 .to = {2, 0, 0},
                 .description = "Add Product",
                 .up =
                     [](SchemaRegistry& reg) {
                         auto type = ObjectTypeBuilder("Product").build();
                         return reg.register_type(std::move(type));
                     },
                 .down = [](SchemaRegistry& reg) { return reg.unregister_type("Product"); }})
            .is_ok());

    EXPECT_TRUE(manager.migrate_to_latest(registry).is_ok());

    // Rollback only to 1.0.0
    auto result = manager.rollback_to(registry, {1, 0, 0});
    EXPECT_TRUE(result.is_ok());
    MigrationVersion expected_v1{1, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v1);
    EXPECT_TRUE(registry.has_type("User"));
    EXPECT_FALSE(registry.has_type("Product"));
}

TEST(MigrationManagerTest, Reset) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration({.from = {0, 0, 0},
                                         .to = {1, 0, 0},
                                         .description = "Add User",
                                         .up =
                                             [](SchemaRegistry& reg) {
                                                 auto type = ObjectTypeBuilder("User").build();
                                                 return reg.register_type(std::move(type));
                                             }})
                    .is_ok());

    EXPECT_TRUE(manager.migrate_to(registry, {1, 0, 0}).is_ok());

    manager.reset();
    MigrationVersion expected_v0{0, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v0);
    // Note: registry still has the type - reset only affects version tracking
    EXPECT_TRUE(registry.has_type("User"));
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(MigrationManagerTest, MigrationFailure) {
    MigrationManager manager;
    SchemaRegistry registry;

    EXPECT_TRUE(manager
                    .register_migration(
                        {.from = {0, 0, 0},
                         .to = {1, 0, 0},
                         .description = "Will fail",
                         .up = [](SchemaRegistry&) { return SchemaError::MigrationFailed; }})
                    .is_ok());

    auto result = manager.migrate_to(registry, {1, 0, 0});
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::MigrationFailed);
}

TEST(MigrationManagerTest, MigrateToInvalidVersion) {
    MigrationManager manager;
    SchemaRegistry registry;

    // Register migrations for 0.0.0 -> 1.0.0 -> 2.0.0
    EXPECT_TRUE(
        manager
            .register_migration({.from = {0, 0, 0},
                                 .to = {1, 0, 0},
                                 .description = "V1",
                                 .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }})
            .is_ok());

    EXPECT_TRUE(
        manager
            .register_migration({.from = {1, 0, 0},
                                 .to = {2, 0, 0},
                                 .description = "V2",
                                 .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }})
            .is_ok());

    // Try to migrate to 1.5.0 which doesn't exist
    auto result = manager.migrate_to(registry, {1, 5, 0});
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::VersionConflict);

    // Version should remain at 0.0.0 since migration was rejected
    MigrationVersion expected_v0{0, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v0);
}

TEST(MigrationManagerTest, RollbackToInvalidVersion) {
    MigrationManager manager;
    SchemaRegistry registry;

    // Register reversible migrations
    EXPECT_TRUE(manager
                    .register_migration(
                        {.from = {0, 0, 0},
                         .to = {1, 0, 0},
                         .description = "V1",
                         .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; },
                         .down = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }})
                    .is_ok());

    EXPECT_TRUE(manager
                    .register_migration(
                        {.from = {1, 0, 0},
                         .to = {2, 0, 0},
                         .description = "V2",
                         .up = [](SchemaRegistry&) { return Result<void, SchemaError>{}; },
                         .down = [](SchemaRegistry&) { return Result<void, SchemaError>{}; }})
                    .is_ok());

    // Migrate to 2.0.0
    EXPECT_TRUE(manager.migrate_to_latest(registry).is_ok());

    // Try to rollback to 0.5.0 which doesn't exist
    auto result = manager.rollback_to(registry, {0, 5, 0});
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), SchemaError::VersionConflict);

    // Version should remain at 2.0.0 since rollback was rejected
    MigrationVersion expected_v2{2, 0, 0};
    EXPECT_EQ(manager.current_version(), expected_v2);
}

}  // namespace
}  // namespace dotvm::core::schema
