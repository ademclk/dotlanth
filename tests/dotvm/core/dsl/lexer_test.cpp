/// @file lexer_test.cpp
/// @brief DSL-001 Lexer unit tests

#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/dsl/lexer.hpp"

using namespace dotvm::core::dsl;

// ============================================================================
// Helper Functions
// ============================================================================

/// Collect all tokens from source
std::vector<Token> tokenize(std::string_view source) {
    Lexer lexer{source};
    std::vector<Token> tokens;
    while (true) {
        Token token = lexer.next_token();
        tokens.push_back(token);
        if (token.type == TokenType::Eof)
            break;
    }
    return tokens;
}

/// Check token sequence matches expected types
void check_token_types(const std::vector<Token>& tokens, const std::vector<TokenType>& expected) {
    ASSERT_EQ(tokens.size(), expected.size()) << "Token count mismatch";
    for (size_t i = 0; i < tokens.size(); ++i) {
        EXPECT_EQ(tokens[i].type, expected[i])
            << "Token " << i << ": expected " << to_string(expected[i]) << ", got "
            << to_string(tokens[i].type);
    }
}

// ============================================================================
// Basic Token Tests
// ============================================================================

TEST(LexerTest, EmptySource) {
    auto tokens = tokenize("");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TokenType::Eof);
}

TEST(LexerTest, WhitespaceOnly) {
    auto tokens = tokenize("   \t   ");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, TokenType::Eof);
}

TEST(LexerTest, Newlines) {
    auto tokens = tokenize("\n\n\n");
    // Multiple newlines should produce Newline tokens
    check_token_types(tokens,
                      {TokenType::Newline, TokenType::Newline, TokenType::Newline, TokenType::Eof});
}

TEST(LexerTest, Comment) {
    auto tokens = tokenize("# this is a comment\n");
    // Comment is skipped, only newline remains
    check_token_types(tokens, {TokenType::Newline, TokenType::Eof});
}

TEST(LexerTest, CommentAtEof) {
    auto tokens = tokenize("# comment without newline");
    check_token_types(tokens, {TokenType::Eof});
}

// ============================================================================
// Identifier and Keyword Tests
// ============================================================================

TEST(LexerTest, Identifiers) {
    auto tokens = tokenize("foo bar _baz qux123\n");
    check_token_types(tokens, {TokenType::Identifier, TokenType::Identifier, TokenType::Identifier,
                               TokenType::Identifier, TokenType::Newline, TokenType::Eof});
    EXPECT_EQ(tokens[0].lexeme, "foo");
    EXPECT_EQ(tokens[1].lexeme, "bar");
    EXPECT_EQ(tokens[2].lexeme, "_baz");
    EXPECT_EQ(tokens[3].lexeme, "qux123");
}

TEST(LexerTest, Keywords) {
    auto tokens = tokenize("dot when do state link import true false and or not include\n");
    check_token_types(tokens,
                      {TokenType::KwDot, TokenType::KwWhen, TokenType::KwDo, TokenType::KwState,
                       TokenType::KwLink, TokenType::KwImport, TokenType::KwTrue,
                       TokenType::KwFalse, TokenType::KwAnd, TokenType::KwOr, TokenType::KwNot,
                       TokenType::KwInclude, TokenType::Newline, TokenType::Eof});
}

TEST(LexerTest, IncludeKeyword) {
    auto tokens = tokenize("include: \"stdlib/common.dsl\"\n");
    check_token_types(tokens, {TokenType::KwInclude, TokenType::Colon, TokenType::String,
                               TokenType::Newline, TokenType::Eof});
    EXPECT_EQ(tokens[2].lexeme, "stdlib/common.dsl");
}

TEST(LexerTest, KeywordLikeIdentifiers) {
    // Identifiers that start like keywords but aren't
    auto tokens = tokenize("dots whens doing\n");
    check_token_types(tokens, {TokenType::Identifier, TokenType::Identifier, TokenType::Identifier,
                               TokenType::Newline, TokenType::Eof});
}

// ============================================================================
// Number Tests
// ============================================================================

TEST(LexerTest, Integers) {
    auto tokens = tokenize("0 42 123456\n");
    check_token_types(tokens, {TokenType::Integer, TokenType::Integer, TokenType::Integer,
                               TokenType::Newline, TokenType::Eof});
    EXPECT_EQ(tokens[0].lexeme, "0");
    EXPECT_EQ(tokens[1].lexeme, "42");
    EXPECT_EQ(tokens[2].lexeme, "123456");
}

TEST(LexerTest, Floats) {
    auto tokens = tokenize("3.14 0.5 1e10 2.5e-3\n");
    check_token_types(tokens, {TokenType::Float, TokenType::Float, TokenType::Float,
                               TokenType::Float, TokenType::Newline, TokenType::Eof});
    EXPECT_EQ(tokens[0].lexeme, "3.14");
    EXPECT_EQ(tokens[1].lexeme, "0.5");
    EXPECT_EQ(tokens[2].lexeme, "1e10");
    EXPECT_EQ(tokens[3].lexeme, "2.5e-3");
}

// ============================================================================
// String Tests
// ============================================================================

TEST(LexerTest, SimpleString) {
    auto tokens = tokenize("\"hello world\"\n");
    check_token_types(tokens, {TokenType::String, TokenType::Newline, TokenType::Eof});
    EXPECT_EQ(tokens[0].lexeme, "hello world");
}

TEST(LexerTest, EmptyString) {
    auto tokens = tokenize("\"\"\n");
    check_token_types(tokens, {TokenType::String, TokenType::Newline, TokenType::Eof});
    EXPECT_EQ(tokens[0].lexeme, "");
}

TEST(LexerTest, StringWithEscapes) {
    auto tokens = tokenize("\"line1\\nline2\\ttab\"\n");
    check_token_types(tokens, {TokenType::String, TokenType::Newline, TokenType::Eof});
    EXPECT_EQ(tokens[0].lexeme, "line1\\nline2\\ttab");
}

TEST(LexerTest, InterpolatedString) {
    auto tokens = tokenize("\"Hello ${name}!\"\n");
    check_token_types(tokens, {TokenType::StringStart, TokenType::Identifier, TokenType::StringEnd,
                               TokenType::Newline, TokenType::Eof});
    EXPECT_EQ(tokens[0].lexeme, "Hello ");
    EXPECT_EQ(tokens[1].lexeme, "name");
    EXPECT_EQ(tokens[2].lexeme, "!");
}

TEST(LexerTest, MultipleInterpolations) {
    auto tokens = tokenize("\"${a} and ${b}\"\n");
    check_token_types(tokens, {TokenType::StringStart, TokenType::Identifier,
                               TokenType::StringMiddle, TokenType::Identifier, TokenType::StringEnd,
                               TokenType::Newline, TokenType::Eof});
    EXPECT_EQ(tokens[0].lexeme, "");
    EXPECT_EQ(tokens[1].lexeme, "a");
    EXPECT_EQ(tokens[2].lexeme, " and ");
    EXPECT_EQ(tokens[3].lexeme, "b");
    EXPECT_EQ(tokens[4].lexeme, "");
}

TEST(LexerTest, InterpolatedMemberAccess) {
    auto tokens = tokenize("\"Value: ${state.counter}\"\n");
    // Note: 'state' is a keyword (KwState), which is correct behavior
    // The parser handles this by allowing keywords as expression primaries
    check_token_types(tokens, {TokenType::StringStart, TokenType::KwState, TokenType::Dot,
                               TokenType::Identifier, TokenType::StringEnd, TokenType::Newline,
                               TokenType::Eof});
}

// ============================================================================
// Operator Tests
// ============================================================================

TEST(LexerTest, SingleCharOperators) {
    auto tokens = tokenize("+ - * / % = < >\n");
    check_token_types(tokens, {TokenType::Plus, TokenType::Minus, TokenType::Star, TokenType::Slash,
                               TokenType::Percent, TokenType::Equals, TokenType::Less,
                               TokenType::Greater, TokenType::Newline, TokenType::Eof});
}

TEST(LexerTest, TwoCharOperators) {
    auto tokens = tokenize("+= -= *= /= == != <= >= ->\n");
    check_token_types(tokens, {TokenType::PlusEquals, TokenType::MinusEquals, TokenType::StarEquals,
                               TokenType::SlashEquals, TokenType::EqualEqual, TokenType::NotEqual,
                               TokenType::LessEqual, TokenType::GreaterEqual, TokenType::Arrow,
                               TokenType::Newline, TokenType::Eof});
}

TEST(LexerTest, Punctuation) {
    auto tokens = tokenize(": , . ( )\n");
    check_token_types(tokens,
                      {TokenType::Colon, TokenType::Comma, TokenType::Dot, TokenType::LParen,
                       TokenType::RParen, TokenType::Newline, TokenType::Eof});
}

// ============================================================================
// Indentation Tests
// ============================================================================

TEST(LexerTest, BasicIndent) {
    auto tokens = tokenize("foo:\n    bar\n");
    check_token_types(tokens, {TokenType::Identifier, TokenType::Colon, TokenType::Newline,
                               TokenType::Indent, TokenType::Identifier, TokenType::Newline,
                               TokenType::Dedent, TokenType::Eof});
}

TEST(LexerTest, MultipleIndentLevels) {
    auto tokens = tokenize("a:\n    b:\n        c\n");
    check_token_types(tokens,
                      {TokenType::Identifier, TokenType::Colon, TokenType::Newline,
                       TokenType::Indent, TokenType::Identifier, TokenType::Colon,
                       TokenType::Newline, TokenType::Indent, TokenType::Identifier,
                       TokenType::Newline, TokenType::Dedent, TokenType::Dedent, TokenType::Eof});
}

TEST(LexerTest, MultipleDedents) {
    auto tokens = tokenize("a:\n    b:\n        c\nd\n");
    check_token_types(tokens, {TokenType::Identifier, TokenType::Colon, TokenType::Newline,
                               TokenType::Indent, TokenType::Identifier, TokenType::Colon,
                               TokenType::Newline, TokenType::Indent, TokenType::Identifier,
                               TokenType::Newline, TokenType::Dedent, TokenType::Dedent,
                               TokenType::Identifier, TokenType::Newline, TokenType::Eof});
}

TEST(LexerTest, BlankLinesInIndentation) {
    auto tokens = tokenize("a:\n    b\n\n    c\n");
    // Blank line should not affect indentation
    check_token_types(tokens, {TokenType::Identifier, TokenType::Colon, TokenType::Newline,
                               TokenType::Indent, TokenType::Identifier, TokenType::Newline,
                               TokenType::Newline, TokenType::Identifier, TokenType::Newline,
                               TokenType::Dedent, TokenType::Eof});
}

// ============================================================================
// Location Tracking Tests
// ============================================================================

TEST(LexerTest, LineTracking) {
    Lexer lexer{"foo\nbar\nbaz"};

    Token t1 = lexer.next_token();
    EXPECT_EQ(t1.span.start.line, 1);

    (void)lexer.next_token();  // newline

    Token t2 = lexer.next_token();
    EXPECT_EQ(t2.span.start.line, 2);

    (void)lexer.next_token();  // newline

    Token t3 = lexer.next_token();
    EXPECT_EQ(t3.span.start.line, 3);
}

TEST(LexerTest, ColumnTracking) {
    Lexer lexer{"foo bar"};

    Token t1 = lexer.next_token();
    EXPECT_EQ(t1.span.start.column, 1);

    Token t2 = lexer.next_token();
    EXPECT_EQ(t2.span.start.column, 5);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(LexerTest, UnterminatedString) {
    Lexer lexer{"\"unterminated"};
    (void)lexer.next_token();
    EXPECT_TRUE(lexer.has_errors());
}

TEST(LexerTest, InvalidCharacter) {
    Lexer lexer{"@invalid"};
    (void)lexer.next_token();
    EXPECT_TRUE(lexer.has_errors());
}

TEST(LexerTest, InvalidEscapeSequence) {
    Lexer lexer{"\"\\x\""};
    (void)lexer.next_token();
    EXPECT_TRUE(lexer.has_errors());
}

// ============================================================================
// Peek Tests
// ============================================================================

TEST(LexerTest, PeekDoesNotAdvance) {
    Lexer lexer{"foo bar"};

    const Token& peeked1 = lexer.peek();
    EXPECT_EQ(peeked1.type, TokenType::Identifier);
    EXPECT_EQ(peeked1.lexeme, "foo");

    const Token& peeked2 = lexer.peek();
    EXPECT_EQ(peeked2.lexeme, "foo");  // Same token

    Token consumed = lexer.next_token();
    EXPECT_EQ(consumed.lexeme, "foo");

    Token next = lexer.next_token();
    EXPECT_EQ(next.lexeme, "bar");
}
