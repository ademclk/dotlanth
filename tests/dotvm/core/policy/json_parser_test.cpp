/// @file json_parser_test.cpp
/// @brief Unit tests for SEC-009 JSON parser

#include "dotvm/core/policy/json_parser.hpp"

#include <gtest/gtest.h>

namespace dotvm::core::policy {
namespace {

// ============================================================================
// Basic Value Tests
// ============================================================================

TEST(JsonParserTest, ParseNull) {
    auto result = JsonParser::parse("null");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_null());
}

TEST(JsonParserTest, ParseTrue) {
    auto result = JsonParser::parse("true");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_bool());
    EXPECT_TRUE(result.value().as_bool());
}

TEST(JsonParserTest, ParseFalse) {
    auto result = JsonParser::parse("false");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_bool());
    EXPECT_FALSE(result.value().as_bool());
}

// ============================================================================
// Number Tests
// ============================================================================

TEST(JsonParserTest, ParseInteger) {
    auto result = JsonParser::parse("42");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_int());
    EXPECT_EQ(result.value().as_int(), 42);
}

TEST(JsonParserTest, ParseNegativeInteger) {
    auto result = JsonParser::parse("-123");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_int());
    EXPECT_EQ(result.value().as_int(), -123);
}

TEST(JsonParserTest, ParseZero) {
    auto result = JsonParser::parse("0");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_int());
    EXPECT_EQ(result.value().as_int(), 0);
}

TEST(JsonParserTest, ParseFloat) {
    auto result = JsonParser::parse("3.14159");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_float());
    EXPECT_NEAR(result.value().as_float(), 3.14159, 0.00001);
}

TEST(JsonParserTest, ParseScientificNotation) {
    auto result = JsonParser::parse("1.5e10");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_float());
    EXPECT_NEAR(result.value().as_float(), 1.5e10, 1e6);
}

TEST(JsonParserTest, ParseNegativeExponent) {
    auto result = JsonParser::parse("1e-5");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_float());
    EXPECT_NEAR(result.value().as_float(), 1e-5, 1e-10);
}

// ============================================================================
// String Tests
// ============================================================================

TEST(JsonParserTest, ParseEmptyString) {
    auto result = JsonParser::parse("\"\"");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_string());
    EXPECT_EQ(result.value().as_string(), "");
}

TEST(JsonParserTest, ParseSimpleString) {
    auto result = JsonParser::parse("\"hello world\"");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_string());
    EXPECT_EQ(result.value().as_string(), "hello world");
}

TEST(JsonParserTest, ParseEscapedQuote) {
    auto result = JsonParser::parse("\"hello\\\"world\"");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_string());
    EXPECT_EQ(result.value().as_string(), "hello\"world");
}

TEST(JsonParserTest, ParseEscapedBackslash) {
    auto result = JsonParser::parse("\"path\\\\to\\\\file\"");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_string());
    EXPECT_EQ(result.value().as_string(), "path\\to\\file");
}

TEST(JsonParserTest, ParseEscapedNewline) {
    auto result = JsonParser::parse("\"line1\\nline2\"");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_string());
    EXPECT_EQ(result.value().as_string(), "line1\nline2");
}

TEST(JsonParserTest, ParseUnicodeEscape) {
    auto result = JsonParser::parse("\"hello\\u0041world\"");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_string());
    EXPECT_EQ(result.value().as_string(), "helloAworld");
}

// ============================================================================
// Array Tests
// ============================================================================

TEST(JsonParserTest, ParseEmptyArray) {
    auto result = JsonParser::parse("[]");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_array());
    EXPECT_EQ(result.value().size(), 0);
}

TEST(JsonParserTest, ParseArrayOfIntegers) {
    auto result = JsonParser::parse("[1, 2, 3]");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_array());
    EXPECT_EQ(result.value().size(), 3);
    EXPECT_EQ(result.value()[0].as_int(), 1);
    EXPECT_EQ(result.value()[1].as_int(), 2);
    EXPECT_EQ(result.value()[2].as_int(), 3);
}

TEST(JsonParserTest, ParseMixedArray) {
    auto result = JsonParser::parse("[1, \"two\", true, null]");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().size(), 4);
    EXPECT_TRUE(result.value()[0].is_int());
    EXPECT_TRUE(result.value()[1].is_string());
    EXPECT_TRUE(result.value()[2].is_bool());
    EXPECT_TRUE(result.value()[3].is_null());
}

TEST(JsonParserTest, ParseNestedArrays) {
    auto result = JsonParser::parse("[[1, 2], [3, 4]]");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().size(), 2);
    EXPECT_TRUE(result.value()[0].is_array());
    EXPECT_TRUE(result.value()[1].is_array());
    EXPECT_EQ(result.value()[0][0].as_int(), 1);
    EXPECT_EQ(result.value()[1][1].as_int(), 4);
}

// ============================================================================
// Object Tests
// ============================================================================

TEST(JsonParserTest, ParseEmptyObject) {
    auto result = JsonParser::parse("{}");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_object());
    EXPECT_EQ(result.value().size(), 0);
}

TEST(JsonParserTest, ParseSimpleObject) {
    auto result = JsonParser::parse("{\"name\": \"test\", \"value\": 42}");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_object());
    EXPECT_EQ(result.value()["name"].as_string(), "test");
    EXPECT_EQ(result.value()["value"].as_int(), 42);
}

TEST(JsonParserTest, ParseNestedObject) {
    auto result = JsonParser::parse("{\"outer\": {\"inner\": 123}}");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value()["outer"].is_object());
    EXPECT_EQ(result.value()["outer"]["inner"].as_int(), 123);
}

TEST(JsonParserTest, ObjectContains) {
    auto result = JsonParser::parse("{\"exists\": 1}");
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().contains("exists"));
    EXPECT_FALSE(result.value().contains("missing"));
}

// ============================================================================
// Whitespace Tests
// ============================================================================

TEST(JsonParserTest, WhitespaceAroundValues) {
    auto result = JsonParser::parse("  { \"key\"  :  \"value\"  }  ");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value()["key"].as_string(), "value");
}

TEST(JsonParserTest, NewlinesInJson) {
    auto result = JsonParser::parse("{\n  \"key\": \"value\"\n}");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value()["key"].as_string(), "value");
}

// ============================================================================
// Error Tests
// ============================================================================

TEST(JsonParserTest, ErrorEmptyInput) {
    auto result = JsonParser::parse("");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::UnexpectedEof);
}

TEST(JsonParserTest, ErrorInvalidCharacter) {
    auto result = JsonParser::parse("@invalid");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::InvalidCharacter);
}

TEST(JsonParserTest, ErrorUnterminatedString) {
    auto result = JsonParser::parse("\"unclosed");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::UnexpectedEof);
}

TEST(JsonParserTest, ErrorMissingColon) {
    auto result = JsonParser::parse("{\"key\" \"value\"}");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::JsonSyntaxError);
}

TEST(JsonParserTest, ErrorTrailingComma) {
    auto result = JsonParser::parse("[1, 2, 3,]");
    ASSERT_TRUE(result.is_err());
}

TEST(JsonParserTest, ErrorExtraContent) {
    auto result = JsonParser::parse("42 extra");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::JsonSyntaxError);
}

TEST(JsonParserTest, ErrorNestingTooDeep) {
    // Create deeply nested array
    std::string deep = "";
    for (int i = 0; i < 100; ++i) {
        deep += "[";
    }
    for (int i = 0; i < 100; ++i) {
        deep += "]";
    }
    auto result = JsonParser::parse(deep);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::NestingTooDeep);
}

// ============================================================================
// Policy DSL Example Tests
// ============================================================================

TEST(JsonParserTest, ParsePolicyRule) {
    const char* policy_json = R"({
        "rules": [
            {
                "id": 1,
                "priority": 100,
                "if": {
                    "opcode": "STATE_PUT",
                    "key_prefix": "/admin"
                },
                "then": {
                    "action": "RequireCapability",
                    "capability": "Admin"
                }
            }
        ],
        "default_action": "Allow"
    })";

    auto result = JsonParser::parse(policy_json);
    ASSERT_TRUE(result.is_ok()) << result.error().message;

    const JsonValue& root = result.value();
    EXPECT_TRUE(root.contains("rules"));
    EXPECT_TRUE(root.contains("default_action"));

    const JsonValue& rules = root["rules"];
    EXPECT_EQ(rules.size(), 1);

    const JsonValue& rule = rules[0];
    EXPECT_EQ(rule["id"].as_int(), 1);
    EXPECT_EQ(rule["priority"].as_int(), 100);
    EXPECT_EQ(rule["if"]["opcode"].as_string(), "STATE_PUT");
    EXPECT_EQ(rule["if"]["key_prefix"].as_string(), "/admin");
    EXPECT_EQ(rule["then"]["action"].as_string(), "RequireCapability");
}

}  // namespace
}  // namespace dotvm::core::policy
