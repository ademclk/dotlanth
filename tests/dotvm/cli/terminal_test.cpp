/// @file terminal_test.cpp
/// @brief DSL-003 Terminal class unit tests

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dotvm/cli/terminal.hpp"

using namespace dotvm::cli;
using namespace dotvm::core::dsl;

// ============================================================================
// Terminal Construction Tests
// ============================================================================

TEST(TerminalTest, ConstructionWithNoColor) {
    std::ostringstream oss;
    Terminal term(oss, true);  // force_no_color = true
    EXPECT_FALSE(term.colors_enabled());
}

TEST(TerminalTest, ConstructionWithExplicitColor) {
    std::ostringstream oss;
    Terminal term(oss, true, true);  // colors_enabled = true, explicit_tag
    EXPECT_TRUE(term.colors_enabled());
}

TEST(TerminalTest, SetColorsEnabled) {
    std::ostringstream oss;
    Terminal term(oss, true);  // force_no_color
    EXPECT_FALSE(term.colors_enabled());

    term.set_colors_enabled(true);
    EXPECT_TRUE(term.colors_enabled());

    term.set_colors_enabled(false);
    EXPECT_FALSE(term.colors_enabled());
}

// ============================================================================
// Color Output Tests (Colors Disabled)
// ============================================================================

TEST(TerminalTest, ErrorWithoutColors) {
    std::ostringstream oss;
    Terminal term(oss, true);  // force_no_color
    term.error("test error");
    EXPECT_EQ(oss.str(), "test error");
}

TEST(TerminalTest, WarningWithoutColors) {
    std::ostringstream oss;
    Terminal term(oss, true);
    term.warning("test warning");
    EXPECT_EQ(oss.str(), "test warning");
}

TEST(TerminalTest, InfoWithoutColors) {
    std::ostringstream oss;
    Terminal term(oss, true);
    term.info("test info");
    EXPECT_EQ(oss.str(), "test info");
}

TEST(TerminalTest, SuccessWithoutColors) {
    std::ostringstream oss;
    Terminal term(oss, true);
    term.success("test success");
    EXPECT_EQ(oss.str(), "test success");
}

TEST(TerminalTest, PrintWithoutColors) {
    std::ostringstream oss;
    Terminal term(oss, true);
    term.print("test print");
    EXPECT_EQ(oss.str(), "test print");
}

TEST(TerminalTest, Newline) {
    std::ostringstream oss;
    Terminal term(oss, true);
    term.newline();
    EXPECT_EQ(oss.str(), "\n");
}

// ============================================================================
// Color Output Tests (Colors Enabled)
// ============================================================================

TEST(TerminalTest, ErrorWithColors) {
    std::ostringstream oss;
    Terminal term(oss, true, true);  // explicit colors_enabled = true
    term.error("test");
    std::string output = oss.str();
    // Should contain ANSI codes and the message
    EXPECT_TRUE(output.find("test") != std::string::npos);
    EXPECT_TRUE(output.find("\033[") != std::string::npos);  // ANSI escape
    EXPECT_TRUE(output.find(AnsiColor::BrightRed) != std::string::npos);
    EXPECT_TRUE(output.find(AnsiColor::Reset) != std::string::npos);
}

TEST(TerminalTest, WarningWithColors) {
    std::ostringstream oss;
    Terminal term(oss, true, true);
    term.warning("test");
    std::string output = oss.str();
    EXPECT_TRUE(output.find("test") != std::string::npos);
    EXPECT_TRUE(output.find(AnsiColor::BrightYellow) != std::string::npos);
}

TEST(TerminalTest, InfoWithColors) {
    std::ostringstream oss;
    Terminal term(oss, true, true);
    term.info("test");
    std::string output = oss.str();
    EXPECT_TRUE(output.find("test") != std::string::npos);
    EXPECT_TRUE(output.find(AnsiColor::BrightCyan) != std::string::npos);
}

TEST(TerminalTest, SuccessWithColors) {
    std::ostringstream oss;
    Terminal term(oss, true, true);
    term.success("test");
    std::string output = oss.str();
    EXPECT_TRUE(output.find("test") != std::string::npos);
    EXPECT_TRUE(output.find(AnsiColor::BrightGreen) != std::string::npos);
}

// ============================================================================
// Error Header Tests
// ============================================================================

TEST(TerminalTest, PrintErrorHeaderNoColors) {
    std::ostringstream oss;
    Terminal term(oss, true);

    SourceLocation loc = SourceLocation::at(10, 5, 100);
    term.print_error_header("test.dsl", loc, "error");

    std::string output = oss.str();
    // In spec format, print_error_header only outputs the "error: " label.
    // The filename and location are printed by print_error() with the "-->" arrow line.
    EXPECT_TRUE(output.find("error:") != std::string::npos);
    EXPECT_EQ(output, "error: ");
}

TEST(TerminalTest, PrintErrorHeaderWarning) {
    std::ostringstream oss;
    Terminal term(oss, true);

    SourceLocation loc = SourceLocation::at(1, 1, 0);
    term.print_error_header("file.dsl", loc, "warning");

    std::string output = oss.str();
    EXPECT_TRUE(output.find("warning:") != std::string::npos);
}

TEST(TerminalTest, PrintErrorHeaderNote) {
    std::ostringstream oss;
    Terminal term(oss, true);

    SourceLocation loc = SourceLocation::at(1, 1, 0);
    term.print_error_header("file.dsl", loc, "note");

    std::string output = oss.str();
    EXPECT_TRUE(output.find("note:") != std::string::npos);
}

// ============================================================================
// Source Snippet Tests
// ============================================================================

TEST(TerminalTest, PrintSourceSnippetSimple) {
    std::ostringstream oss;
    Terminal term(oss, true);  // no colors

    std::string source = "dot MyDot:\n    state:\n        count: 0\n";
    SourceSpan span = SourceSpan::from(SourceLocation::at(1, 5, 4), SourceLocation::at(1, 10, 9));

    term.print_source_snippet(source, span);

    std::string output = oss.str();
    // Should contain line number and source line
    EXPECT_TRUE(output.find("1") != std::string::npos);
    EXPECT_TRUE(output.find("dot MyDot:") != std::string::npos);
    // Should contain caret
    EXPECT_TRUE(output.find("^") != std::string::npos);
}

TEST(TerminalTest, PrintSourceSnippetSecondLine) {
    std::ostringstream oss;
    Terminal term(oss, true);

    std::string source = "line one\nline two\nline three\n";
    SourceSpan span = SourceSpan::from(SourceLocation::at(2, 6, 14), SourceLocation::at(2, 9, 17));

    term.print_source_snippet(source, span);

    std::string output = oss.str();
    // Should show line 2
    EXPECT_TRUE(output.find("2") != std::string::npos);
    EXPECT_TRUE(output.find("line two") != std::string::npos);
    EXPECT_TRUE(output.find("^") != std::string::npos);
}

TEST(TerminalTest, PrintSourceSnippetWithMessage) {
    std::ostringstream oss;
    Terminal term(oss, true);

    std::string source = "foo bar baz\n";
    SourceSpan span = SourceSpan::from(SourceLocation::at(1, 5, 4), SourceLocation::at(1, 8, 7));

    term.print_source_snippet(source, span, "expected identifier");

    std::string output = oss.str();
    EXPECT_TRUE(output.find("expected identifier") != std::string::npos);
}

TEST(TerminalTest, PrintSourceSnippetEmptySource) {
    std::ostringstream oss;
    Terminal term(oss, true);

    std::string source;
    SourceSpan span = SourceSpan::from(SourceLocation::at(1, 1, 0), SourceLocation::at(1, 1, 0));

    term.print_source_snippet(source, span);

    std::string output = oss.str();
    // Should produce no output for empty source
    EXPECT_TRUE(output.empty());
}

TEST(TerminalTest, PrintSourceSnippetInvalidLine) {
    std::ostringstream oss;
    Terminal term(oss, true);

    std::string source = "only one line\n";
    SourceSpan span = SourceSpan::from(SourceLocation::at(5, 1, 0), SourceLocation::at(5, 1, 0));

    term.print_source_snippet(source, span);

    std::string output = oss.str();
    // Should produce no output for invalid line
    EXPECT_TRUE(output.empty());
}

// ============================================================================
// Full Error Print Tests
// ============================================================================

TEST(TerminalTest, PrintErrorComplete) {
    std::ostringstream oss;
    Terminal term(oss, true);

    std::string source = "dot BadDot:\n    state:\n        x: @invalid\n";
    SourceSpan span =
        SourceSpan::from(SourceLocation::at(3, 12, 35), SourceLocation::at(3, 13, 36));

    term.print_error("test.dsl", source, span, "error", "unexpected character '@'");

    std::string output = oss.str();

    // Check header components
    EXPECT_TRUE(output.find("test.dsl") != std::string::npos);
    EXPECT_TRUE(output.find("3") != std::string::npos);   // line
    EXPECT_TRUE(output.find("12") != std::string::npos);  // column
    EXPECT_TRUE(output.find("error:") != std::string::npos);
    EXPECT_TRUE(output.find("unexpected character '@'") != std::string::npos);

    // Check snippet
    EXPECT_TRUE(output.find("x: @invalid") != std::string::npos);
    EXPECT_TRUE(output.find("^") != std::string::npos);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(TerminalTest, LineWithTabs) {
    std::ostringstream oss;
    Terminal term(oss, true);

    std::string source = "\t\tindented\n";
    SourceSpan span = SourceSpan::from(SourceLocation::at(1, 3, 2), SourceLocation::at(1, 11, 10));

    term.print_source_snippet(source, span);

    std::string output = oss.str();
    EXPECT_TRUE(output.find("indented") != std::string::npos);
    EXPECT_TRUE(output.find("^") != std::string::npos);
}

TEST(TerminalTest, LastLineNoNewline) {
    std::ostringstream oss;
    Terminal term(oss, true);

    std::string source = "no trailing newline";
    SourceSpan span = SourceSpan::from(SourceLocation::at(1, 1, 0), SourceLocation::at(1, 3, 2));

    term.print_source_snippet(source, span);

    std::string output = oss.str();
    EXPECT_TRUE(output.find("no trailing newline") != std::string::npos);
}

TEST(TerminalTest, MultipleNewlines) {
    std::ostringstream oss;
    Terminal term(oss, true);

    std::string source = "\n\n\nline four\n";
    SourceSpan span = SourceSpan::from(SourceLocation::at(4, 1, 3), SourceLocation::at(4, 5, 7));

    term.print_source_snippet(source, span);

    std::string output = oss.str();
    EXPECT_TRUE(output.find("4") != std::string::npos);
    EXPECT_TRUE(output.find("line four") != std::string::npos);
}

// ============================================================================
// Exit Code Tests
// ============================================================================

TEST(ExitCodeTest, Values) {
    EXPECT_EQ(static_cast<int>(ExitCode::Success), 0);
    EXPECT_EQ(static_cast<int>(ExitCode::ParseError), 1);
    EXPECT_EQ(static_cast<int>(ExitCode::CompilationError), 2);
    EXPECT_EQ(static_cast<int>(ExitCode::FileNotFound), 3);
    EXPECT_EQ(static_cast<int>(ExitCode::IoError), 4);
    EXPECT_EQ(static_cast<int>(ExitCode::CircularInclude), 5);
}
