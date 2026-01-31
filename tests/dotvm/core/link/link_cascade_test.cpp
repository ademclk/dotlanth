/// @file link_cascade_test.cpp
/// @brief Unit tests for LinkManager bulk removal and cascade behavior

#include <memory>
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

}  // namespace

TEST(LinkCascadeTest, RemoveAllLinksFromRemovesOutgoingLinks) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto group = schema::ObjectTypeBuilder("Group").build();
    auto project = schema::ObjectTypeBuilder("Project").build();
    auto user = schema::ObjectTypeBuilder("User")
                    .try_add_link(schema::LinkDefBuilder("groups")
                                      .to("Group")
                                      .with_cardinality(schema::Cardinality::OneToMany)
                                      .build())
                    .try_add_link(schema::LinkDefBuilder("projects")
                                      .to("Project")
                                      .with_cardinality(schema::Cardinality::OneToMany)
                                      .build())
                    .build();

    EXPECT_TRUE(registry->register_type(group).is_ok());
    EXPECT_TRUE(registry->register_type(project).is_ok());
    EXPECT_TRUE(registry->register_type(user).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId user_id = generator.generate("User");
    ObjectId group_a = generator.generate("Group");
    ObjectId group_b = generator.generate("Group");
    ObjectId project_a = generator.generate("Project");
    ObjectId project_b = generator.generate("Project");

    EXPECT_TRUE(manager.create_link(user_id, "User", "groups", group_a).is_ok());
    EXPECT_TRUE(manager.create_link(user_id, "User", "groups", group_b).is_ok());
    EXPECT_TRUE(manager.create_link(user_id, "User", "projects", project_a).is_ok());
    EXPECT_TRUE(manager.create_link(user_id, "User", "projects", project_b).is_ok());

    auto result = manager.remove_all_links_from(user_id, "User");
    EXPECT_TRUE(result.is_ok());

    auto groups = manager.get_links(user_id, "groups");
    ASSERT_TRUE(groups.is_ok());
    EXPECT_TRUE(groups.value().empty());

    auto projects = manager.get_links(user_id, "projects");
    ASSERT_TRUE(projects.is_ok());
    EXPECT_TRUE(projects.value().empty());

    EXPECT_FALSE(manager.has_link(user_id, "groups", group_a));
    EXPECT_FALSE(manager.has_link(user_id, "groups", group_b));
    EXPECT_FALSE(manager.has_link(user_id, "projects", project_a));
    EXPECT_FALSE(manager.has_link(user_id, "projects", project_b));
}

TEST(LinkCascadeTest, RemoveAllLinksFromRejectsInvalidObjectId) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    auto user = schema::ObjectTypeBuilder("User").build();
    EXPECT_TRUE(registry->register_type(user).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    auto result = manager.remove_all_links_from(ObjectId::invalid(), "User");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::InvalidObjectId);
}

TEST(LinkCascadeTest, RemoveAllLinksToRemovesIncomingLinks) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto book = schema::ObjectTypeBuilder("Book").build();
    auto author = schema::ObjectTypeBuilder("Author")
                      .try_add_link(schema::LinkDefBuilder("books")
                                        .to("Book")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();
    auto publisher = schema::ObjectTypeBuilder("Publisher")
                         .try_add_link(schema::LinkDefBuilder("catalog")
                                           .to("Book")
                                           .with_cardinality(schema::Cardinality::OneToMany)
                                           .build())
                         .build();

    EXPECT_TRUE(registry->register_type(book).is_ok());
    EXPECT_TRUE(registry->register_type(author).is_ok());
    EXPECT_TRUE(registry->register_type(publisher).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId book_id = generator.generate("Book");
    ObjectId author_a = generator.generate("Author");
    ObjectId author_b = generator.generate("Author");
    ObjectId publisher_id = generator.generate("Publisher");

    EXPECT_TRUE(manager.create_link(author_a, "Author", "books", book_id).is_ok());
    EXPECT_TRUE(manager.create_link(author_b, "Author", "books", book_id).is_ok());
    EXPECT_TRUE(manager.create_link(publisher_id, "Publisher", "catalog", book_id).is_ok());

    auto result = manager.remove_all_links_to(book_id, "Book");
    EXPECT_TRUE(result.is_ok());

    auto author_a_books = manager.get_links(author_a, "books");
    ASSERT_TRUE(author_a_books.is_ok());
    EXPECT_TRUE(author_a_books.value().empty());

    auto author_b_books = manager.get_links(author_b, "books");
    ASSERT_TRUE(author_b_books.is_ok());
    EXPECT_TRUE(author_b_books.value().empty());

    auto publisher_books = manager.get_links(publisher_id, "catalog");
    ASSERT_TRUE(publisher_books.is_ok());
    EXPECT_TRUE(publisher_books.value().empty());

    EXPECT_FALSE(manager.has_link(author_a, "books", book_id));
    EXPECT_FALSE(manager.has_link(author_b, "books", book_id));
    EXPECT_FALSE(manager.has_link(publisher_id, "catalog", book_id));
}

TEST(LinkCascadeTest, RemoveAllLinksToRejectsInvalidObjectId) {
    auto registry = std::make_shared<schema::SchemaRegistry>();
    auto book = schema::ObjectTypeBuilder("Book").build();
    EXPECT_TRUE(registry->register_type(book).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    auto result = manager.remove_all_links_to(ObjectId::invalid(), "Book");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::InvalidObjectId);
}

TEST(LinkCascadeTest, CascadeNoneLeavesTargetsIntact) {
    schema::SchemaRegistryConfig config;
    config.validate_links_on_register = false;
    auto registry = std::make_shared<schema::SchemaRegistry>(config);

    auto toy = schema::ObjectTypeBuilder("Toy").build();
    auto child = schema::ObjectTypeBuilder("Child")
                     .try_add_link(schema::LinkDefBuilder("parent")
                                       .to("Parent")
                                       .with_cardinality(schema::Cardinality::OneToOne)
                                       .with_inverse("child")
                                       .build())
                     .try_add_link(schema::LinkDefBuilder("toy")
                                       .to("Toy")
                                       .with_cardinality(schema::Cardinality::OneToOne)
                                       .build())
                     .build();
    auto parent = schema::ObjectTypeBuilder("Parent")
                      .try_add_link(schema::LinkDefBuilder("child")
                                        .to("Child")
                                        .with_cardinality(schema::Cardinality::OneToOne)
                                        .with_inverse("parent")
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(toy).is_ok());
    EXPECT_TRUE(registry->register_type(child).is_ok());
    EXPECT_TRUE(registry->register_type(parent).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId parent_id = generator.generate("Parent");
    ObjectId child_id = generator.generate("Child");
    ObjectId toy_id = generator.generate("Toy");

    EXPECT_TRUE(manager.create_link(parent_id, "Parent", "child", child_id).is_ok());
    EXPECT_TRUE(manager.create_link(child_id, "Child", "toy", toy_id).is_ok());

    auto result = manager.remove_all_links_from(parent_id, "Parent", CascadePolicy::None);
    EXPECT_TRUE(result.is_ok());

    EXPECT_TRUE(manager.has_link(child_id, "toy", toy_id));
}

TEST(LinkCascadeTest, BulkRemovalZeroesLinkCounts) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto group = schema::ObjectTypeBuilder("Group").build();
    auto project = schema::ObjectTypeBuilder("Project").build();
    auto user = schema::ObjectTypeBuilder("User")
                    .try_add_link(schema::LinkDefBuilder("groups")
                                      .to("Group")
                                      .with_cardinality(schema::Cardinality::OneToMany)
                                      .build())
                    .try_add_link(schema::LinkDefBuilder("projects")
                                      .to("Project")
                                      .with_cardinality(schema::Cardinality::OneToMany)
                                      .build())
                    .build();

    EXPECT_TRUE(registry->register_type(group).is_ok());
    EXPECT_TRUE(registry->register_type(project).is_ok());
    EXPECT_TRUE(registry->register_type(user).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId user_id = generator.generate("User");
    ObjectId group_a = generator.generate("Group");
    ObjectId group_b = generator.generate("Group");
    ObjectId project_a = generator.generate("Project");

    EXPECT_TRUE(manager.create_link(user_id, "User", "groups", group_a).is_ok());
    EXPECT_TRUE(manager.create_link(user_id, "User", "groups", group_b).is_ok());
    EXPECT_TRUE(manager.create_link(user_id, "User", "projects", project_a).is_ok());

    auto group_count = manager.get_link_count(user_id, "groups");
    ASSERT_TRUE(group_count.is_ok());
    EXPECT_EQ(group_count.value(), 2U);

    auto project_count = manager.get_link_count(user_id, "projects");
    ASSERT_TRUE(project_count.is_ok());
    EXPECT_EQ(project_count.value(), 1U);

    auto result = manager.remove_all_links_from(user_id, "User");
    EXPECT_TRUE(result.is_ok());

    group_count = manager.get_link_count(user_id, "groups");
    ASSERT_TRUE(group_count.is_ok());
    EXPECT_EQ(group_count.value(), 0U);

    project_count = manager.get_link_count(user_id, "projects");
    ASSERT_TRUE(project_count.is_ok());
    EXPECT_EQ(project_count.value(), 0U);
}

}  // namespace dotvm::core::link
