/// @file doc_comment_test.cpp
/// @brief CLI-005 Doc comment parser tests

#include <gtest/gtest.h>

#include "dotvm/cli/doc/doc_comment.hpp"

using namespace dotvm::cli::doc;
using namespace dotvm::core::dsl;

// ============================================================================
// DocCommentParser::is_doc_comment Tests
// ============================================================================

TEST(DocCommentParserTest, IsDocComment_BriefTag_ReturnsTrue) {
    EXPECT_TRUE(DocCommentParser::is_doc_comment("# @brief This is a brief"));
}

TEST(DocCommentParserTest, IsDocComment_ParamTag_ReturnsTrue) {
    EXPECT_TRUE(DocCommentParser::is_doc_comment("# @param name Description"));
}

TEST(DocCommentParserTest, IsDocComment_ReturnTag_ReturnsTrue) {
    EXPECT_TRUE(DocCommentParser::is_doc_comment("# @return Description"));
}

TEST(DocCommentParserTest, IsDocComment_ReturnsTag_ReturnsTrue) {
    EXPECT_TRUE(DocCommentParser::is_doc_comment("# @returns Description"));
}

TEST(DocCommentParserTest, IsDocComment_NoteTag_ReturnsTrue) {
    EXPECT_TRUE(DocCommentParser::is_doc_comment("# @note Something important"));
}

TEST(DocCommentParserTest, IsDocComment_SeeTag_ReturnsTrue) {
    EXPECT_TRUE(DocCommentParser::is_doc_comment("# @see OtherEntity"));
}

TEST(DocCommentParserTest, IsDocComment_ExampleTag_ReturnsTrue) {
    EXPECT_TRUE(DocCommentParser::is_doc_comment("# @example usage here"));
}

TEST(DocCommentParserTest, IsDocComment_RegularComment_ReturnsFalse) {
    EXPECT_FALSE(DocCommentParser::is_doc_comment("# This is just a comment"));
}

TEST(DocCommentParserTest, IsDocComment_EmptyComment_ReturnsFalse) {
    EXPECT_FALSE(DocCommentParser::is_doc_comment("#"));
}

TEST(DocCommentParserTest, IsDocComment_WhitespaceOnly_ReturnsFalse) {
    EXPECT_FALSE(DocCommentParser::is_doc_comment("#    "));
}

// ============================================================================
// DocCommentParser::parse_line Tests
// ============================================================================

TEST(DocCommentParserTest, ParseLine_Brief_ExtractsBrief) {
    SourceSpan span{{1, 1, 0}, {1, 30, 29}};
    auto doc = DocCommentParser::parse_line("# @brief This is the brief description", span);

    EXPECT_EQ(doc.brief, "This is the brief description");
    EXPECT_TRUE(doc.params.empty());
}

TEST(DocCommentParserTest, ParseLine_Param_ExtractsNameAndDescription) {
    SourceSpan span{{1, 1, 0}, {1, 35, 34}};
    auto doc = DocCommentParser::parse_line("# @param count The number of items", span);

    ASSERT_EQ(doc.params.size(), 1);
    EXPECT_EQ(doc.params[0].name, "count");
    EXPECT_EQ(doc.params[0].description, "The number of items");
}

TEST(DocCommentParserTest, ParseLine_ParamNoDescription_ExtractsNameOnly) {
    SourceSpan span{{1, 1, 0}, {1, 15, 14}};
    auto doc = DocCommentParser::parse_line("# @param count", span);

    ASSERT_EQ(doc.params.size(), 1);
    EXPECT_EQ(doc.params[0].name, "count");
    EXPECT_TRUE(doc.params[0].description.empty());
}

TEST(DocCommentParserTest, ParseLine_Return_ExtractsDescription) {
    SourceSpan span{{1, 1, 0}, {1, 25, 24}};
    auto doc = DocCommentParser::parse_line("# @return The result value", span);

    EXPECT_EQ(doc.return_desc, "The result value");
}

TEST(DocCommentParserTest, ParseLine_Note_ExtractsNote) {
    SourceSpan span{{1, 1, 0}, {1, 25, 24}};
    auto doc = DocCommentParser::parse_line("# @note Thread safety warning", span);

    ASSERT_EQ(doc.notes.size(), 1);
    EXPECT_EQ(doc.notes[0], "Thread safety warning");
}

TEST(DocCommentParserTest, ParseLine_See_ExtractsReference) {
    SourceSpan span{{1, 1, 0}, {1, 20, 19}};
    auto doc = DocCommentParser::parse_line("# @see OtherEntity", span);

    ASSERT_EQ(doc.see_also.size(), 1);
    EXPECT_EQ(doc.see_also[0], "OtherEntity");
}

TEST(DocCommentParserTest, ParseLine_RegularComment_ReturnsEmptyDoc) {
    SourceSpan span{{1, 1, 0}, {1, 20, 19}};
    auto doc = DocCommentParser::parse_line("# This is not a doc", span);

    EXPECT_TRUE(doc.empty());
}

// ============================================================================
// DocCommentParser::parse_block Tests
// ============================================================================

TEST(DocCommentParserTest, ParseBlock_MultipleTags_MergesCorrectly) {
    std::vector<std::string_view> lines = {
        "# @brief Agent for counting",
        "# @param initial The starting count",
        "# @param max Maximum allowed value",
    };
    SourceSpan span{{1, 1, 0}, {3, 35, 100}};

    auto doc = DocCommentParser::parse_block(lines, span);

    EXPECT_EQ(doc.brief, "Agent for counting");
    ASSERT_EQ(doc.params.size(), 2);
    EXPECT_EQ(doc.params[0].name, "initial");
    EXPECT_EQ(doc.params[0].description, "The starting count");
    EXPECT_EQ(doc.params[1].name, "max");
    EXPECT_EQ(doc.params[1].description, "Maximum allowed value");
}

TEST(DocCommentParserTest, ParseBlock_MultipleNotes_CollectsAll) {
    std::vector<std::string_view> lines = {
        "# @note First note",
        "# @note Second note",
    };
    SourceSpan span{{1, 1, 0}, {2, 20, 40}};

    auto doc = DocCommentParser::parse_block(lines, span);

    ASSERT_EQ(doc.notes.size(), 2);
    EXPECT_EQ(doc.notes[0], "First note");
    EXPECT_EQ(doc.notes[1], "Second note");
}

TEST(DocCommentParserTest, ParseBlock_EmptyLines_ProducesEmptyDoc) {
    std::vector<std::string_view> lines;
    SourceSpan span{{1, 1, 0}, {1, 1, 0}};

    auto doc = DocCommentParser::parse_block(lines, span);

    EXPECT_TRUE(doc.empty());
}

// ============================================================================
// DocComment Methods Tests
// ============================================================================

TEST(DocCommentTest, Empty_NoContent_ReturnsTrue) {
    DocComment doc;
    EXPECT_TRUE(doc.empty());
}

TEST(DocCommentTest, Empty_WithBrief_ReturnsFalse) {
    DocComment doc;
    doc.brief = "Has content";
    EXPECT_FALSE(doc.empty());
}

TEST(DocCommentTest, Empty_WithParam_ReturnsFalse) {
    DocComment doc;
    doc.params.push_back({"name", "desc"});
    EXPECT_FALSE(doc.empty());
}

TEST(DocCommentTest, HasBrief_WithBrief_ReturnsTrue) {
    DocComment doc;
    doc.brief = "Something";
    EXPECT_TRUE(doc.has_brief());
}

TEST(DocCommentTest, HasBrief_NoBrief_ReturnsFalse) {
    DocComment doc;
    EXPECT_FALSE(doc.has_brief());
}

TEST(DocCommentTest, HasParams_WithParams_ReturnsTrue) {
    DocComment doc;
    doc.params.push_back({"name", "desc"});
    EXPECT_TRUE(doc.has_params());
}

TEST(DocCommentTest, HasParams_NoParams_ReturnsFalse) {
    DocComment doc;
    EXPECT_FALSE(doc.has_params());
}
