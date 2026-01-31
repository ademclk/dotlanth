/// @file link_manager_test.cpp
/// @brief Unit tests for LinkManager

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/link/link_key_encoder.hpp"
#include "dotvm/core/link/link_manager.hpp"
#include "dotvm/core/link/object_id.hpp"
#include "dotvm/core/schema/link_def.hpp"
#include "dotvm/core/schema/object_type.hpp"
#include "dotvm/core/schema/schema_registry.hpp"
#include "dotvm/core/state/state_backend.hpp"

namespace dotvm::core::link {
namespace {

[[nodiscard]] std::span<const std::byte> as_span(const std::vector<std::byte>& bytes) {
    return {bytes.data(), bytes.size()};
}

[[nodiscard]] std::uint64_t decode_u64_le(std::span<const std::byte> bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= (static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[i])) << (i * 8));
    }
    return value;
}

[[nodiscard]] std::array<std::byte, sizeof(std::uint64_t)> encode_u64_le(std::uint64_t value) {
    std::array<std::byte, sizeof(std::uint64_t)> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFU);
    }
    return out;
}

[[nodiscard]] std::shared_ptr<state::StateBackend> make_backend() {
    return std::shared_ptr<state::StateBackend>(state::create_state_backend());
}

// ============================================================================
// LinkManager Tests
// ============================================================================

TEST(LinkManagerTest, CreateLinkSucceedsForValidInputs) {
    schema::SchemaRegistryConfig config;
    config.validate_links_on_register = false;  // Allow inverse link cycles in tests
    auto registry = std::make_shared<schema::SchemaRegistry>(config);

    auto profile = schema::ObjectTypeBuilder("Profile")
                       .try_add_link(schema::LinkDefBuilder("user")
                                         .to("User")
                                         .with_cardinality(schema::Cardinality::OneToOne)
                                         .with_inverse("profile")
                                         .build())
                       .build();

    auto user = schema::ObjectTypeBuilder("User")
                    .try_add_link(schema::LinkDefBuilder("profile")
                                      .to("Profile")
                                      .with_cardinality(schema::Cardinality::OneToOne)
                                      .with_inverse("user")
                                      .build())
                    .build();

    EXPECT_TRUE(registry->register_type(profile).is_ok());
    EXPECT_TRUE(registry->register_type(user).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("User");
    ObjectId tgt = generator.generate("Profile");

    auto result = manager.create_link(src, "User", "profile", tgt);
    EXPECT_TRUE(result.is_ok());

    const auto forward_key = LinkKeyEncoder::encode_forward_key(src, "profile", tgt);
    EXPECT_TRUE(backend->exists(as_span(forward_key)));

    const auto inverse_index = LinkKeyEncoder::encode_inverse_key(tgt, "profile", src);
    EXPECT_TRUE(backend->exists(as_span(inverse_index)));

    const auto count_key = LinkKeyEncoder::encode_count_key(src, "profile");
    const auto count_result = backend->get(as_span(count_key));
    ASSERT_TRUE(count_result.is_ok());
    ASSERT_EQ(count_result.value().size(), sizeof(std::uint64_t));
    EXPECT_EQ(decode_u64_le(count_result.value()), 1U);

    const auto inverse_forward = LinkKeyEncoder::encode_forward_key(tgt, "user", src);
    EXPECT_TRUE(backend->exists(as_span(inverse_forward)));
}

TEST(LinkManagerTest, CreateLinkRejectsInvalidObjectIds) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToOne)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId valid_src = generator.generate("Source");
    ObjectId valid_tgt = generator.generate("Target");

    auto result = manager.create_link(ObjectId::invalid(), "Source", "rel", valid_tgt);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::InvalidObjectId);

    result = manager.create_link(valid_src, "Source", "rel", ObjectId::invalid());
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::InvalidObjectId);
}

TEST(LinkManagerTest, CreateLinkReturnsDefinitionNotFoundForMissingLink) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    EXPECT_TRUE(registry->register_type(schema::ObjectTypeBuilder("Source").build()).is_ok());
    EXPECT_TRUE(registry->register_type(schema::ObjectTypeBuilder("Target").build()).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt = generator.generate("Target");

    auto result = manager.create_link(src, "Source", "missing", tgt);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::LinkDefinitionNotFound);
}

TEST(LinkManagerTest, CreateLinkEnforcesOneToOneCardinality) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Account").build();
    auto source = schema::ObjectTypeBuilder("User")
                      .try_add_link(schema::LinkDefBuilder("account")
                                        .to("Account")
                                        .with_cardinality(schema::Cardinality::OneToOne)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("User");
    ObjectId tgt_a = generator.generate("Account");
    ObjectId tgt_b = generator.generate("Account");

    EXPECT_TRUE(manager.create_link(src, "User", "account", tgt_a).is_ok());

    auto result = manager.create_link(src, "User", "account", tgt_b);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::CardinalityViolation);
}

TEST(LinkManagerTest, CreateLinkRejectsDuplicateLinkInstances) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Group").build();
    auto source = schema::ObjectTypeBuilder("User")
                      .try_add_link(schema::LinkDefBuilder("member_of")
                                        .to("Group")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("User");
    ObjectId tgt = generator.generate("Group");

    EXPECT_TRUE(manager.create_link(src, "User", "member_of", tgt).is_ok());

    auto result = manager.create_link(src, "User", "member_of", tgt);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::LinkInstanceAlreadyExists);
}

TEST(LinkManagerTest, CreateLinkRejectsCountOverflow) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt = generator.generate("Target");

    const auto count_key = LinkKeyEncoder::encode_count_key(src, "rel");
    const auto encoded = encode_u64_le(std::numeric_limits<std::uint64_t>::max());
    auto put_result = backend->put(as_span(count_key),
                                   std::span<const std::byte>(encoded.data(), encoded.size()));
    ASSERT_TRUE(put_result.is_ok());

    auto result = manager.create_link(src, "Source", "rel", tgt);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::CardinalityViolation);

    const auto forward_key = LinkKeyEncoder::encode_forward_key(src, "rel", tgt);
    EXPECT_FALSE(backend->exists(as_span(forward_key)));

    auto count_result = backend->get(as_span(count_key));
    ASSERT_TRUE(count_result.is_ok());
    EXPECT_EQ(decode_u64_le(count_result.value()), std::numeric_limits<std::uint64_t>::max());
}

TEST(LinkManagerTest, CreateLinkRollsBackOnInverseCardinalityViolation) {
    schema::SchemaRegistryConfig config;
    config.validate_links_on_register = false;
    auto registry = std::make_shared<schema::SchemaRegistry>(config);

    auto type_a = schema::ObjectTypeBuilder("A")
                      .try_add_link(schema::LinkDefBuilder("b")
                                        .to("B")
                                        .with_cardinality(schema::Cardinality::OneToOne)
                                        .with_inverse("a")
                                        .build())
                      .build();

    auto type_b = schema::ObjectTypeBuilder("B")
                      .try_add_link(schema::LinkDefBuilder("a")
                                        .to("A")
                                        .with_cardinality(schema::Cardinality::OneToOne)
                                        .with_inverse("b")
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(type_a).is_ok());
    EXPECT_TRUE(registry->register_type(type_b).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId a1 = generator.generate("A");
    ObjectId a2 = generator.generate("A");
    ObjectId b1 = generator.generate("B");

    EXPECT_TRUE(manager.create_link(a1, "A", "b", b1).is_ok());

    auto result = manager.create_link(a2, "A", "b", b1);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), LinkError::CardinalityViolation);

    const auto forward_key = LinkKeyEncoder::encode_forward_key(a2, "b", b1);
    EXPECT_FALSE(backend->exists(as_span(forward_key)));

    const auto inverse_key = LinkKeyEncoder::encode_inverse_key(b1, "b", a2);
    EXPECT_FALSE(backend->exists(as_span(inverse_key)));

    const auto count_key = LinkKeyEncoder::encode_count_key(a2, "b");
    auto count_result = backend->get(as_span(count_key));
    EXPECT_TRUE(count_result.is_err());
    EXPECT_EQ(count_result.error(), state::StateBackendError::KeyNotFound);
}

TEST(LinkManagerTest, HasLinkReturnsTrueForExistingLink) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt = generator.generate("Target");

    EXPECT_TRUE(manager.create_link(src, "Source", "rel", tgt).is_ok());
    EXPECT_TRUE(manager.has_link(src, "rel", tgt));
}

TEST(LinkManagerTest, HasLinkReturnsFalseForMissingLink) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt = generator.generate("Target");

    EXPECT_FALSE(manager.has_link(src, "rel", tgt));
}

TEST(LinkManagerTest, GetLinksReturnsAllTargetsForSource) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt1 = generator.generate("Target");
    ObjectId tgt2 = generator.generate("Target");
    ObjectId tgt3 = generator.generate("Target");

    EXPECT_TRUE(manager.create_link(src, "Source", "rel", tgt1).is_ok());
    EXPECT_TRUE(manager.create_link(src, "Source", "rel", tgt2).is_ok());
    EXPECT_TRUE(manager.create_link(src, "Source", "rel", tgt3).is_ok());

    auto result = manager.get_links(src, "rel");
    ASSERT_TRUE(result.is_ok());

    auto links = result.value();
    std::vector<ObjectId> expected{tgt1, tgt2, tgt3};
    std::sort(links.begin(), links.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(links, expected);
}

TEST(LinkManagerTest, GetLinksReturnsEmptyVectorWhenNoLinksExist) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");

    auto result = manager.get_links(src, "rel");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().empty());
}

TEST(LinkManagerTest, GetInverseReturnsAllSourcesForTarget) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src1 = generator.generate("Source");
    ObjectId src2 = generator.generate("Source");
    ObjectId src3 = generator.generate("Source");
    ObjectId tgt1 = generator.generate("Target");
    ObjectId tgt2 = generator.generate("Target");

    EXPECT_TRUE(manager.create_link(src1, "Source", "rel", tgt1).is_ok());
    EXPECT_TRUE(manager.create_link(src2, "Source", "rel", tgt1).is_ok());
    EXPECT_TRUE(manager.create_link(src3, "Source", "rel", tgt2).is_ok());

    auto result = manager.get_inverse(tgt1, "rel");
    ASSERT_TRUE(result.is_ok());

    auto sources = result.value();
    std::vector<ObjectId> expected{src1, src2};
    std::sort(sources.begin(), sources.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(sources, expected);
}

TEST(LinkManagerTest, GetInverseReturnsEmptyVectorWhenNoIncomingLinksExist) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId tgt = generator.generate("Target");

    auto result = manager.get_inverse(tgt, "rel");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().empty());
}

TEST(LinkManagerTest, GetInverseHandlesMultipleSourcesPointingToSameTarget) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId tgt = generator.generate("Target");
    ObjectId src1 = generator.generate("Source");
    ObjectId src2 = generator.generate("Source");
    ObjectId src3 = generator.generate("Source");
    ObjectId src4 = generator.generate("Source");

    EXPECT_TRUE(manager.create_link(src1, "Source", "rel", tgt).is_ok());
    EXPECT_TRUE(manager.create_link(src2, "Source", "rel", tgt).is_ok());
    EXPECT_TRUE(manager.create_link(src3, "Source", "rel", tgt).is_ok());
    EXPECT_TRUE(manager.create_link(src4, "Source", "rel", tgt).is_ok());

    auto result = manager.get_inverse(tgt, "rel");
    ASSERT_TRUE(result.is_ok());

    auto sources = result.value();
    std::vector<ObjectId> expected{src1, src2, src3, src4};
    std::sort(sources.begin(), sources.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(sources, expected);
}

TEST(LinkManagerTest, GetLinkCountMatchesNumberOfLinks) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt1 = generator.generate("Target");
    ObjectId tgt2 = generator.generate("Target");

    EXPECT_TRUE(manager.create_link(src, "Source", "rel", tgt1).is_ok());
    EXPECT_TRUE(manager.create_link(src, "Source", "rel", tgt2).is_ok());

    auto count_result = manager.get_link_count(src, "rel");
    ASSERT_TRUE(count_result.is_ok());
    EXPECT_EQ(count_result.value(), 2ULL);
}

TEST(LinkManagerTest, RemoveLinkSucceedsAndLinkNoLongerExists) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt = generator.generate("Target");

    ASSERT_TRUE(manager.create_link(src, "Source", "rel", tgt).is_ok());
    EXPECT_TRUE(manager.has_link(src, "rel", tgt));

    auto remove_result = manager.remove_link(src, "Source", "rel", tgt);
    EXPECT_TRUE(remove_result.is_ok());
    EXPECT_FALSE(manager.has_link(src, "rel", tgt));

    const auto forward_key = LinkKeyEncoder::encode_forward_key(src, "rel", tgt);
    EXPECT_FALSE(backend->exists(as_span(forward_key)));
}

TEST(LinkManagerTest, RemoveLinkReturnsNotFoundForMissingLink) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt = generator.generate("Target");

    auto remove_result = manager.remove_link(src, "Source", "rel", tgt);
    EXPECT_TRUE(remove_result.is_err());
    EXPECT_EQ(remove_result.error(), LinkError::LinkInstanceNotFound);
}

TEST(LinkManagerTest, RemoveLinkRemovesInverseIndex) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt = generator.generate("Target");

    ASSERT_TRUE(manager.create_link(src, "Source", "rel", tgt).is_ok());

    const auto inverse_key = LinkKeyEncoder::encode_inverse_key(tgt, "rel", src);
    EXPECT_TRUE(backend->exists(as_span(inverse_key)));

    auto remove_result = manager.remove_link(src, "Source", "rel", tgt);
    EXPECT_TRUE(remove_result.is_ok());
    EXPECT_FALSE(backend->exists(as_span(inverse_key)));
}

TEST(LinkManagerTest, RemoveLinkDecrementsCountCorrectly) {
    auto registry = std::make_shared<schema::SchemaRegistry>();

    auto target = schema::ObjectTypeBuilder("Target").build();
    auto source = schema::ObjectTypeBuilder("Source")
                      .try_add_link(schema::LinkDefBuilder("rel")
                                        .to("Target")
                                        .with_cardinality(schema::Cardinality::OneToMany)
                                        .build())
                      .build();

    EXPECT_TRUE(registry->register_type(target).is_ok());
    EXPECT_TRUE(registry->register_type(source).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("Source");
    ObjectId tgt1 = generator.generate("Target");
    ObjectId tgt2 = generator.generate("Target");

    ASSERT_TRUE(manager.create_link(src, "Source", "rel", tgt1).is_ok());
    ASSERT_TRUE(manager.create_link(src, "Source", "rel", tgt2).is_ok());

    auto count_result = manager.get_link_count(src, "rel");
    ASSERT_TRUE(count_result.is_ok());
    EXPECT_EQ(count_result.value(), 2ULL);

    auto remove_result = manager.remove_link(src, "Source", "rel", tgt1);
    EXPECT_TRUE(remove_result.is_ok());

    count_result = manager.get_link_count(src, "rel");
    ASSERT_TRUE(count_result.is_ok());
    EXPECT_EQ(count_result.value(), 1ULL);

    remove_result = manager.remove_link(src, "Source", "rel", tgt2);
    EXPECT_TRUE(remove_result.is_ok());

    count_result = manager.get_link_count(src, "rel");
    ASSERT_TRUE(count_result.is_ok());
    EXPECT_EQ(count_result.value(), 0ULL);
}

TEST(LinkManagerTest, RemoveLinkRemovesAutoManagedInverseLink) {
    schema::SchemaRegistryConfig config;
    config.validate_links_on_register = false;
    auto registry = std::make_shared<schema::SchemaRegistry>(config);

    auto profile = schema::ObjectTypeBuilder("Profile")
                       .try_add_link(schema::LinkDefBuilder("user")
                                         .to("User")
                                         .with_cardinality(schema::Cardinality::OneToOne)
                                         .with_inverse("profile")
                                         .build())
                       .build();

    auto user = schema::ObjectTypeBuilder("User")
                    .try_add_link(schema::LinkDefBuilder("profile")
                                      .to("Profile")
                                      .with_cardinality(schema::Cardinality::OneToOne)
                                      .with_inverse("user")
                                      .build())
                    .build();

    EXPECT_TRUE(registry->register_type(profile).is_ok());
    EXPECT_TRUE(registry->register_type(user).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("User");
    ObjectId tgt = generator.generate("Profile");

    ASSERT_TRUE(manager.create_link(src, "User", "profile", tgt).is_ok());
    EXPECT_TRUE(manager.has_link(tgt, "user", src));

    auto remove_result = manager.remove_link(src, "User", "profile", tgt);
    EXPECT_TRUE(remove_result.is_ok());
    EXPECT_FALSE(manager.has_link(tgt, "user", src));
}

TEST(LinkManagerTest, RemoveLinkRollsBackWhenInverseRemovalFails) {
    schema::SchemaRegistryConfig config;
    config.validate_links_on_register = false;
    auto registry = std::make_shared<schema::SchemaRegistry>(config);

    auto profile = schema::ObjectTypeBuilder("Profile")
                       .try_add_link(schema::LinkDefBuilder("user")
                                         .to("User")
                                         .with_cardinality(schema::Cardinality::OneToOne)
                                         .with_inverse("profile")
                                         .build())
                       .build();

    auto user = schema::ObjectTypeBuilder("User")
                    .try_add_link(schema::LinkDefBuilder("profile")
                                      .to("Profile")
                                      .with_cardinality(schema::Cardinality::OneToOne)
                                      .with_inverse("user")
                                      .build())
                    .build();

    EXPECT_TRUE(registry->register_type(profile).is_ok());
    EXPECT_TRUE(registry->register_type(user).is_ok());

    auto backend = make_backend();
    LinkManager manager(registry, backend);

    ObjectIdGenerator generator;
    ObjectId src = generator.generate("User");
    ObjectId tgt = generator.generate("Profile");

    ASSERT_TRUE(manager.create_link(src, "User", "profile", tgt).is_ok());

    const auto inverse_count_key = LinkKeyEncoder::encode_count_key(tgt, "user");
    auto remove_count_result = backend->remove(as_span(inverse_count_key));
    ASSERT_TRUE(remove_count_result.is_ok());

    auto remove_result = manager.remove_link(src, "User", "profile", tgt);
    EXPECT_TRUE(remove_result.is_err());
    EXPECT_EQ(remove_result.error(), LinkError::IndexCorruption);

    EXPECT_TRUE(manager.has_link(src, "profile", tgt));
    auto count_result = manager.get_link_count(src, "profile");
    ASSERT_TRUE(count_result.is_ok());
    EXPECT_EQ(count_result.value(), 1ULL);

    const auto inverse_key = LinkKeyEncoder::encode_inverse_key(tgt, "profile", src);
    EXPECT_TRUE(backend->exists(as_span(inverse_key)));
}

}  // namespace
}  // namespace dotvm::core::link
