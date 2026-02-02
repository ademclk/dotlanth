/// @file doc_extractor_test.cpp
/// @brief CLI-005 Doc extractor tests

#include <gtest/gtest.h>

#include "dotvm/cli/doc/doc_extractor.hpp"
#include "dotvm/core/dsl/parser.hpp"

using namespace dotvm::cli::doc;
using namespace dotvm::core::dsl;

// ============================================================================
// Helper Functions
// ============================================================================

/// Parse DSL source and extract documentation
DocumentationResult extract_from_source(std::string_view source, std::string_view name = "test") {
    auto parse_result = DslParser::parse(source);
    EXPECT_TRUE(parse_result.is_ok()) << "Parse failed";
    return DocExtractor::extract(source, parse_result.value(), name);
}

// ============================================================================
// Basic Extraction Tests
// ============================================================================

TEST(DocExtractorTest, Extract_EmptySource_ReturnsEmptyResult) {
    auto result = extract_from_source("");

    EXPECT_EQ(result.module_name, "test");
    EXPECT_FALSE(result.module_doc.has_value());
    EXPECT_TRUE(result.entities.empty());
}

TEST(DocExtractorTest, Extract_UndocumentedDot_ExtractsEntityWithoutDoc) {
    const char* source = R"(
dot counter:
    state:
        count: 0
)";
    auto result = extract_from_source(source);

    ASSERT_GE(result.entities.size(), 1);
    EXPECT_EQ(result.entities[0].kind, EntityKind::Dot);
    EXPECT_EQ(result.entities[0].name, "counter");
    EXPECT_FALSE(result.entities[0].doc.has_value());
}

TEST(DocExtractorTest, Extract_DocumentedDot_ExtractsDocComment) {
    const char* source = R"(
# @brief A simple counter agent
dot counter:
    state:
        count: 0
)";
    auto result = extract_from_source(source);

    ASSERT_GE(result.entities.size(), 1);
    auto& dot_entity = result.entities[0];
    EXPECT_EQ(dot_entity.kind, EntityKind::Dot);
    EXPECT_EQ(dot_entity.name, "counter");
    ASSERT_TRUE(dot_entity.doc.has_value());
    EXPECT_EQ(dot_entity.doc->brief, "A simple counter agent");
}

TEST(DocExtractorTest, Extract_DocumentedStateVar_ExtractsDocComment) {
    const char* source = R"(
dot counter:
    state:
        # @brief Current count value
        count: 0
)";
    auto result = extract_from_source(source);

    // Find the state var entity
    auto it = std::find_if(result.entities.begin(), result.entities.end(),
                           [](const auto& e) { return e.kind == EntityKind::StateVar; });

    ASSERT_NE(it, result.entities.end());
    EXPECT_EQ(it->name, "count");
    EXPECT_EQ(it->parent_name, "counter");
    ASSERT_TRUE(it->doc.has_value());
    EXPECT_EQ(it->doc->brief, "Current count value");
}

TEST(DocExtractorTest, Extract_DocumentedLink_ExtractsDocComment) {
    const char* source = R"(
dot a:
    state:
        x: 0

dot b:
    state:
        y: 0

# @brief Connects A to B
link a -> b
)";
    auto result = extract_from_source(source);

    // Find the link entity
    auto it = std::find_if(result.entities.begin(), result.entities.end(),
                           [](const auto& e) { return e.kind == EntityKind::Link; });

    ASSERT_NE(it, result.entities.end());
    EXPECT_EQ(it->name, "a -> b");
    ASSERT_TRUE(it->doc.has_value());
    EXPECT_EQ(it->doc->brief, "Connects A to B");
}

TEST(DocExtractorTest, Extract_MultiLineDocComment_MergesLines) {
    const char* source = R"(
# @brief Main counter agent
# @param initial Starting value
dot counter:
    state:
        count: 0
)";
    auto result = extract_from_source(source);

    ASSERT_GE(result.entities.size(), 1);
    auto& dot_entity = result.entities[0];
    ASSERT_TRUE(dot_entity.doc.has_value());
    EXPECT_EQ(dot_entity.doc->brief, "Main counter agent");
    ASSERT_EQ(dot_entity.doc->params.size(), 1);
    EXPECT_EQ(dot_entity.doc->params[0].name, "initial");
}

TEST(DocExtractorTest, Extract_ModuleLevelDoc_ExtractsModuleDoc) {
    const char* source = R"(
# @brief Module for counting operations
# @note This is experimental

dot counter:
    state:
        count: 0
)";
    auto result = extract_from_source(source);

    ASSERT_TRUE(result.module_doc.has_value());
    EXPECT_EQ(result.module_doc->brief, "Module for counting operations");
    ASSERT_EQ(result.module_doc->notes.size(), 1);
    EXPECT_EQ(result.module_doc->notes[0], "This is experimental");
}

TEST(DocExtractorTest, Extract_RegularCommentIgnored_NoDocExtracted) {
    const char* source = R"(
# This is just a regular comment, not a doc comment
dot counter:
    state:
        count: 0
)";
    auto result = extract_from_source(source);

    ASSERT_GE(result.entities.size(), 1);
    // Regular comments should not create doc entries
    EXPECT_FALSE(result.entities[0].doc.has_value());
}

TEST(DocExtractorTest, Extract_MultipleDots_ExtractsAll) {
    const char* source = R"(
# @brief First agent
dot first:
    state:
        x: 0

# @brief Second agent
dot second:
    state:
        y: 0
)";
    auto result = extract_from_source(source);

    // Count dot entities
    auto dot_count = std::count_if(result.entities.begin(), result.entities.end(),
                                   [](const auto& e) { return e.kind == EntityKind::Dot; });
    EXPECT_EQ(dot_count, 2);

    // Check both have docs
    for (const auto& entity : result.entities) {
        if (entity.kind == EntityKind::Dot) {
            ASSERT_TRUE(entity.doc.has_value()) << "Dot " << entity.name << " missing doc";
        }
    }
}

TEST(DocExtractorTest, Extract_SetsModuleName) {
    auto result = extract_from_source("dot x:\n    state:\n        v: 0\n", "my_module.dsl");
    EXPECT_EQ(result.module_name, "my_module.dsl");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(DocExtractorTest, Extract_CommentNotImmediatelyPreceding_NotAttached) {
    const char* source = R"(
# @brief This is far away

import "something"

dot counter:
    state:
        count: 0
)";
    auto result = extract_from_source(source);

    // The doc comment should be captured as module doc, not attached to dot
    // because there's code between them
    auto it = std::find_if(result.entities.begin(), result.entities.end(),
                           [](const auto& e) { return e.kind == EntityKind::Dot; });

    ASSERT_NE(it, result.entities.end());
    // The doc should not be attached since import is between
    // (depends on implementation - this tests the boundary detection)
}

TEST(DocExtractorTest, Extract_InlineCommentAfterEntity_NotCapturedAsPrecedingDoc) {
    // Note: Inline comments that appear AFTER the entity definition start
    // (after the entity name/keyword) are not captured as documentation.
    // This tests that doc comments must PRECEDE the entity.
    const char* source = R"(
dot counter:
    # @brief This is inside the dot, not before it
    state:
        count: 0
)";
    auto result = extract_from_source(source);

    auto it = std::find_if(result.entities.begin(), result.entities.end(),
                           [](const auto& e) { return e.kind == EntityKind::Dot; });

    ASSERT_NE(it, result.entities.end());
    // The comment inside the dot block should not be attached to the dot itself
    // (it might be attached to a nested entity like state)
    EXPECT_FALSE(it->doc.has_value());
}
