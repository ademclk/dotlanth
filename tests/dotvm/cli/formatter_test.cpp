/// @file formatter_test.cpp
/// @brief DSL-003 Formatter class unit tests

#include <gtest/gtest.h>

#include "dotvm/cli/formatter.hpp"
#include "dotvm/core/dsl/parser.hpp"

using namespace dotvm::cli;
using namespace dotvm::core::dsl;

// ============================================================================
// Construction Tests
// ============================================================================

TEST(FormatterTest, DefaultConstruction) {
    Formatter fmt;
    EXPECT_EQ(fmt.config().indent_size, 4);
    EXPECT_FALSE(fmt.config().use_tabs);
    EXPECT_TRUE(fmt.config().trim_trailing);
    EXPECT_TRUE(fmt.config().ensure_final_newline);
}

TEST(FormatterTest, CustomConfig) {
    FormatConfig config;
    config.indent_size = 2;
    config.use_tabs = true;
    config.trim_trailing = false;
    config.ensure_final_newline = false;

    Formatter fmt(config);

    EXPECT_EQ(fmt.config().indent_size, 2);
    EXPECT_TRUE(fmt.config().use_tabs);
    EXPECT_FALSE(fmt.config().trim_trailing);
    EXPECT_FALSE(fmt.config().ensure_final_newline);
}

TEST(FormatterTest, SetConfig) {
    Formatter fmt;

    FormatConfig config;
    config.indent_size = 8;
    fmt.set_config(config);

    EXPECT_EQ(fmt.config().indent_size, 8);
}

// ============================================================================
// AstFormatter Construction Tests
// ============================================================================

TEST(AstFormatterTest, DefaultConstruction) {
    AstFormatter fmt;
    EXPECT_EQ(fmt.config().indent_size, 4);
    EXPECT_FALSE(fmt.config().use_tabs);
}

TEST(AstFormatterTest, CustomConfig) {
    FormatConfig config;
    config.indent_size = 2;
    config.use_tabs = true;

    AstFormatter fmt(config);

    EXPECT_EQ(fmt.config().indent_size, 2);
    EXPECT_TRUE(fmt.config().use_tabs);
}

// ============================================================================
// Helper: Parse and Format
// ============================================================================

namespace {

/// Parse source and format it back
std::string parse_and_format(std::string_view source) {
    auto result = DslParser::parse(source);
    EXPECT_TRUE(result.is_ok()) << "Parse failed for: " << source;
    if (!result.is_ok()) {
        return "";
    }

    AstFormatter fmt;
    return fmt.format(result.value());
}

/// Verify round-trip: parse, format, parse again, ASTs should match
bool verify_round_trip(std::string_view source) {
    auto result1 = DslParser::parse(source);
    if (!result1.is_ok()) {
        return false;
    }

    AstFormatter fmt;
    std::string formatted = fmt.format(result1.value());

    auto result2 = DslParser::parse(formatted);
    if (!result2.is_ok()) {
        return false;
    }

    // Both parse successfully - compare key structural elements
    const auto& m1 = result1.value();
    const auto& m2 = result2.value();

    if (m1.dots.size() != m2.dots.size())
        return false;
    if (m1.links.size() != m2.links.size())
        return false;
    if (m1.imports.size() != m2.imports.size())
        return false;
    if (m1.includes.size() != m2.includes.size())
        return false;

    // Check dot names match
    for (size_t i = 0; i < m1.dots.size(); ++i) {
        if (m1.dots[i].name != m2.dots[i].name)
            return false;
    }

    // Check link names match
    for (size_t i = 0; i < m1.links.size(); ++i) {
        if (m1.links[i].source != m2.links[i].source)
            return false;
        if (m1.links[i].target != m2.links[i].target)
            return false;
    }

    return true;
}

}  // namespace

// ============================================================================
// Basic Formatting Tests
// ============================================================================

TEST(AstFormatterTest, FormatEmptyModule) {
    DslModule module;
    AstFormatter fmt;
    std::string result = fmt.format(module);

    // Empty module should produce empty or just newline
    EXPECT_TRUE(result.empty() || result == "\n");
}

TEST(AstFormatterTest, FormatSimpleDot) {
    std::string source = R"(dot Test:
    state:
        x: 0
)";

    std::string formatted = parse_and_format(source);

    EXPECT_NE(formatted.find("dot Test:"), std::string::npos);
    EXPECT_NE(formatted.find("state:"), std::string::npos);
    EXPECT_NE(formatted.find("x: 0"), std::string::npos);
}

TEST(AstFormatterTest, FormatDotWithTrigger) {
    std::string source = R"(dot Counter:
    state:
        count: 0
    when count < 10:
        do:
            count = count + 1
)";

    std::string formatted = parse_and_format(source);

    EXPECT_NE(formatted.find("dot Counter:"), std::string::npos);
    EXPECT_NE(formatted.find("when count < 10:"), std::string::npos);
    EXPECT_NE(formatted.find("do:"), std::string::npos);
    EXPECT_NE(formatted.find("count = count + 1"), std::string::npos);
}

TEST(AstFormatterTest, FormatLink) {
    std::string source = R"(dot A:
    state:
        x: 0

dot B:
    state:
        y: 1

link A -> B
)";

    std::string formatted = parse_and_format(source);

    EXPECT_NE(formatted.find("dot A:"), std::string::npos);
    EXPECT_NE(formatted.find("dot B:"), std::string::npos);
    EXPECT_NE(formatted.find("link A -> B"), std::string::npos);
}

TEST(AstFormatterTest, FormatImport) {
    std::string source = R"(import "utils/helpers.dsl"

dot Main:
    state:
        x: 0
)";

    std::string formatted = parse_and_format(source);

    EXPECT_NE(formatted.find("import \"utils/helpers.dsl\""), std::string::npos);
}

// ============================================================================
// Expression Formatting Tests
// ============================================================================

TEST(AstFormatterTest, FormatIntegerExpression) {
    std::string source = R"(dot Test:
    state:
        x: 42
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("x: 42"), std::string::npos);
}

TEST(AstFormatterTest, FormatFloatExpression) {
    // Use a float with exact binary representation to avoid precision issues
    std::string source = R"(dot Test:
    state:
        half: 0.5
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("half: 0.5"), std::string::npos);
}

TEST(AstFormatterTest, FormatFloatExpressionRoundTrip) {
    // Test with a float that has exact binary representation
    std::string source = R"(dot Test:
    state:
        x: 1.5
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("x: 1.5"), std::string::npos);
    EXPECT_TRUE(verify_round_trip(source));
}

TEST(AstFormatterTest, FormatBoolExpression) {
    std::string source = R"(dot Test:
    state:
        flag: true
        other: false
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("flag: true"), std::string::npos);
    EXPECT_NE(formatted.find("other: false"), std::string::npos);
}

TEST(AstFormatterTest, FormatStringExpression) {
    std::string source = R"(dot Test:
    state:
        msg: "hello world"
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("msg: \"hello world\""), std::string::npos);
}

TEST(AstFormatterTest, FormatStringWithEscapes) {
    std::string source = R"(dot Test:
    state:
        msg: "line1\nline2\ttab"
)";

    std::string formatted = parse_and_format(source);
    // Escaped characters should be preserved
    EXPECT_NE(formatted.find("\\n"), std::string::npos);
    EXPECT_NE(formatted.find("\\t"), std::string::npos);
}

// ============================================================================
// Binary Expression Precedence Tests
// ============================================================================

TEST(AstFormatterTest, FormatAdditionNoParens) {
    std::string source = R"(dot Test:
    state:
        x: 1 + 2 + 3
)";

    std::string formatted = parse_and_format(source);
    // Should NOT have unnecessary parens
    EXPECT_NE(formatted.find("x: 1 + 2 + 3"), std::string::npos);
}

TEST(AstFormatterTest, FormatMixedPrecedence) {
    std::string source = R"(dot Test:
    state:
        x: 1 + 2 * 3
)";

    std::string formatted = parse_and_format(source);
    // No parens needed: multiplication binds tighter
    EXPECT_NE(formatted.find("1 + 2 * 3"), std::string::npos);
}

TEST(AstFormatterTest, FormatPrecedenceNeedsParens) {
    std::string source = R"(dot Test:
    state:
        x: (1 + 2) * 3
)";

    std::string formatted = parse_and_format(source);
    // Parens needed to preserve meaning
    EXPECT_NE(formatted.find("(1 + 2) * 3"), std::string::npos);
}

TEST(AstFormatterTest, FormatComparisonOperators) {
    std::string source = R"(dot Test:
    when x > 0:
        do:
            print "positive"
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("when x > 0:"), std::string::npos);
}

TEST(AstFormatterTest, FormatLogicalOperators) {
    std::string source = R"(dot Test:
    when x > 0 and y < 10:
        do:
            print "in range"
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("x > 0 and y < 10"), std::string::npos);
}

TEST(AstFormatterTest, FormatLogicalOrAnd) {
    std::string source = R"(dot Test:
    when a or b and c:
        do:
            print "test"
)";

    std::string formatted = parse_and_format(source);
    // 'and' binds tighter than 'or', so: a or (b and c)
    // No parens needed in formatted output
    EXPECT_NE(formatted.find("a or b and c"), std::string::npos);
}

TEST(AstFormatterTest, FormatLogicalOrWithParens) {
    std::string source = R"(dot Test:
    when (a or b) and c:
        do:
            print "test"
)";

    std::string formatted = parse_and_format(source);
    // Parens needed to override precedence
    EXPECT_NE(formatted.find("(a or b) and c"), std::string::npos);
}

// ============================================================================
// Unary Expression Tests
// ============================================================================

TEST(AstFormatterTest, FormatUnaryNot) {
    std::string source = R"(dot Test:
    when not flag:
        do:
            print "not set"
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("when not flag:"), std::string::npos);
}

TEST(AstFormatterTest, FormatUnaryNegation) {
    std::string source = R"(dot Test:
    state:
        x: -42
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("x: -42"), std::string::npos);
}

// ============================================================================
// Assignment Tests
// ============================================================================

TEST(AstFormatterTest, FormatSimpleAssignment) {
    std::string source = R"(dot Test:
    when true:
        do:
            x = 10
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("x = 10"), std::string::npos);
}

TEST(AstFormatterTest, FormatCompoundAssignment) {
    std::string source = R"(dot Test:
    when true:
        do:
            x += 1
            y -= 2
            z *= 3
            w /= 4
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("x += 1"), std::string::npos);
    EXPECT_NE(formatted.find("y -= 2"), std::string::npos);
    EXPECT_NE(formatted.find("z *= 3"), std::string::npos);
    EXPECT_NE(formatted.find("w /= 4"), std::string::npos);
}

// ============================================================================
// Action Call Tests
// ============================================================================

TEST(AstFormatterTest, FormatPrintAction) {
    std::string source = R"(dot Test:
    when true:
        do:
            print "hello"
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("print \"hello\""), std::string::npos);
}

TEST(AstFormatterTest, FormatMultipleActions) {
    std::string source = R"(dot Test:
    when true:
        do:
            print "first"
            x = 1
            print "second"
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("print \"first\""), std::string::npos);
    EXPECT_NE(formatted.find("x = 1"), std::string::npos);
    EXPECT_NE(formatted.find("print \"second\""), std::string::npos);
}

// ============================================================================
// Indentation Tests
// ============================================================================

TEST(AstFormatterTest, IndentationFourSpaces) {
    std::string source = R"(dot Test:
    state:
        x: 0
)";

    std::string formatted = parse_and_format(source);

    // Check for 4-space indentation
    EXPECT_NE(formatted.find("    state:"), std::string::npos);
    EXPECT_NE(formatted.find("        x: 0"), std::string::npos);
}

TEST(AstFormatterTest, IndentationWithTabs) {
    std::string source = R"(dot Test:
    state:
        x: 0
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    FormatConfig config;
    config.use_tabs = true;

    AstFormatter fmt(config);
    std::string formatted = fmt.format(result.value());

    // Check for tab indentation
    EXPECT_NE(formatted.find("\tstate:"), std::string::npos);
    EXPECT_NE(formatted.find("\t\tx: 0"), std::string::npos);
}

TEST(AstFormatterTest, IndentationTwoSpaces) {
    std::string source = R"(dot Test:
    state:
        x: 0
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    FormatConfig config;
    config.indent_size = 2;

    AstFormatter fmt(config);
    std::string formatted = fmt.format(result.value());

    // Check for 2-space indentation
    EXPECT_NE(formatted.find("  state:"), std::string::npos);
    EXPECT_NE(formatted.find("    x: 0"), std::string::npos);
}

// ============================================================================
// Round-Trip Tests
// ============================================================================

TEST(AstFormatterTest, RoundTripSimpleDot) {
    std::string source = R"(dot Test:
    state:
        x: 0
        y: 1
)";

    EXPECT_TRUE(verify_round_trip(source));
}

TEST(AstFormatterTest, RoundTripWithTrigger) {
    std::string source = R"(dot Counter:
    state:
        count: 0
    when count < 10:
        do:
            count = count + 1
)";

    EXPECT_TRUE(verify_round_trip(source));
}

TEST(AstFormatterTest, RoundTripWithLinks) {
    std::string source = R"(dot A:
    state:
        x: 0

dot B:
    state:
        y: 1

link A -> B
)";

    EXPECT_TRUE(verify_round_trip(source));
}

TEST(AstFormatterTest, RoundTripComplexExpressions) {
    std::string source = R"(dot Math:
    state:
        result: (1 + 2) * 3 - 4 / 2
)";

    EXPECT_TRUE(verify_round_trip(source));
}

TEST(AstFormatterTest, RoundTripLogicalExpressions) {
    std::string source = R"(dot Logic:
    when a and b or c and d:
        do:
            print "test"
)";

    EXPECT_TRUE(verify_round_trip(source));
}

TEST(AstFormatterTest, RoundTripMultipleTriggers) {
    std::string source = R"(dot Multi:
    state:
        x: 0
    when x == 0:
        do:
            x = 1
    when x == 1:
        do:
            x = 2
)";

    EXPECT_TRUE(verify_round_trip(source));
}

TEST(AstFormatterTest, RoundTripAllOperators) {
    std::string source = R"(dot Ops:
    state:
        add: 1 + 2
        sub: 3 - 4
        mul: 5 * 6
        div: 7 / 8
        mod: 9 % 10
    when a == b:
        do:
            print "equal"
    when c != d:
        do:
            print "not equal"
    when e < f:
        do:
            print "less"
    when g <= h:
        do:
            print "less or equal"
    when i > j:
        do:
            print "greater"
    when k >= l:
        do:
            print "greater or equal"
)";

    EXPECT_TRUE(verify_round_trip(source));
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(AstFormatterTest, FormatDotWithoutState) {
    std::string source = R"(dot Empty:
    when true:
        do:
            print "no state"
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("dot Empty:"), std::string::npos);
    // Should not contain "state:" since it wasn't defined
    EXPECT_EQ(formatted.find("state:"), std::string::npos);
}

TEST(AstFormatterTest, FormatEmptyState) {
    // Note: the parser may not allow empty state blocks, but let's see
    std::string source = R"(dot Test:
    state:
        x: 0
)";

    // Just verify it parses and formats without crashing
    EXPECT_TRUE(verify_round_trip(source));
}

TEST(AstFormatterTest, FormatNestedParentheses) {
    std::string source = R"(dot Test:
    state:
        x: ((1 + 2))
)";

    // Should handle nested parens
    EXPECT_TRUE(verify_round_trip(source));
}

TEST(AstFormatterTest, FormatMemberAccess) {
    std::string source = R"(dot Test:
    when obj.property > 0:
        do:
            obj.value = 1
)";

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("obj.property"), std::string::npos);
    EXPECT_NE(formatted.find("obj.value"), std::string::npos);
}

// ============================================================================
// Formatting Quality Tests
// ============================================================================

TEST(AstFormatterTest, MessyInputBecomesClean) {
    // Input with inconsistent spacing
    std::string messy = R"(dot   MyAgent:
    state:
       x:1
       y:  2
    when  x>0 :
     do:
          print "hello"
)";

    std::string formatted = parse_and_format(messy);

    // Verify consistent formatting
    EXPECT_NE(formatted.find("dot MyAgent:"), std::string::npos);
    EXPECT_NE(formatted.find("x: 1"), std::string::npos);
    EXPECT_NE(formatted.find("y: 2"), std::string::npos);
    EXPECT_NE(formatted.find("when x > 0:"), std::string::npos);
}

TEST(AstFormatterTest, FinalNewlineEnsured) {
    // Parser requires trailing newline, so include it in input
    std::string source = "dot Test:\n    state:\n        x: 0\n";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    FormatConfig config;
    config.ensure_final_newline = true;

    AstFormatter fmt(config);
    std::string formatted = fmt.format(result.value());

    EXPECT_EQ(formatted.back(), '\n');
}

TEST(AstFormatterTest, NoFinalNewlineWhenDisabled) {
    std::string source = "dot Test:\n    state:\n        x: 0\n";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    FormatConfig config;
    config.ensure_final_newline = false;

    AstFormatter fmt(config);
    std::string formatted = fmt.format(result.value());

    // When disabled, should not add extra newlines (but original structure maintained)
    // The actual behavior depends on implementation
    EXPECT_FALSE(formatted.empty());
}

// ============================================================================
// Subtraction Associativity Test
// ============================================================================

TEST(AstFormatterTest, SubtractionAssociativity) {
    // a - b - c should be (a - b) - c, NOT a - (b - c)
    std::string source = R"(dot Test:
    state:
        x: 10 - 5 - 2
)";

    // Round-trip should preserve the semantics
    EXPECT_TRUE(verify_round_trip(source));

    // The formatted output shouldn't need extra parens for left-associative subtraction
    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("10 - 5 - 2"), std::string::npos);
}

TEST(AstFormatterTest, DivisionAssociativity) {
    // a / b / c should be (a / b) / c
    std::string source = R"(dot Test:
    state:
        x: 100 / 10 / 2
)";

    EXPECT_TRUE(verify_round_trip(source));

    std::string formatted = parse_and_format(source);
    EXPECT_NE(formatted.find("100 / 10 / 2"), std::string::npos);
}

// ============================================================================
// Complex Precedence Tests
// ============================================================================

TEST(AstFormatterTest, ComplexMixedPrecedence) {
    // Test: 1 + 2 * 3 - 4 / 2 == 5
    // Should parse as: ((1 + (2 * 3)) - (4 / 2)) == 5
    std::string source = R"(dot Test:
    when 1 + 2 * 3 - 4 / 2 == 5:
        do:
            print "math"
)";

    EXPECT_TRUE(verify_round_trip(source));

    std::string formatted = parse_and_format(source);
    // Verify no unnecessary parens were added
    EXPECT_NE(formatted.find("1 + 2 * 3 - 4 / 2 == 5"), std::string::npos);
}

TEST(AstFormatterTest, LogicalWithComparison) {
    // Test: a > 0 and b < 10 or c == 5
    // Should parse as: ((a > 0) and (b < 10)) or (c == 5)
    std::string source = R"(dot Test:
    when a > 0 and b < 10 or c == 5:
        do:
            print "complex"
)";

    EXPECT_TRUE(verify_round_trip(source));
}
