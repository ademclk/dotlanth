/// @file command_parser_test.cpp
/// @brief Unit tests for CommandParser - TOOL-011 Debug Client

#include <gtest/gtest.h>

#include "dotvm/debugger/command_parser.hpp"

namespace dotvm::debugger {
namespace {

// ============================================================================
// Tokenizer Tests
// ============================================================================

TEST(CommandParserTest, ParseEmptyInput) {
    CommandParser parser;
    auto result = parser.parse("");
    EXPECT_TRUE(result.tokens.empty());
    EXPECT_TRUE(result.command.empty());
}

TEST(CommandParserTest, ParseWhitespaceOnly) {
    CommandParser parser;
    auto result = parser.parse("   \t  ");
    EXPECT_TRUE(result.tokens.empty());
    EXPECT_TRUE(result.command.empty());
}

TEST(CommandParserTest, ParseSingleCommand) {
    CommandParser parser;
    auto result = parser.parse("run");
    ASSERT_EQ(result.tokens.size(), 1);
    EXPECT_EQ(result.tokens[0], "run");
    EXPECT_EQ(result.command, "run");
}

TEST(CommandParserTest, ParseCommandWithArgs) {
    CommandParser parser;
    auto result = parser.parse("breakpoint set 0x10");
    ASSERT_EQ(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[0], "breakpoint");
    EXPECT_EQ(result.tokens[1], "set");
    EXPECT_EQ(result.tokens[2], "0x10");
    EXPECT_EQ(result.command, "breakpoint");
}

TEST(CommandParserTest, ParseCommandWithExtraWhitespace) {
    CommandParser parser;
    auto result = parser.parse("  run   --debug   ");
    ASSERT_EQ(result.tokens.size(), 2);
    EXPECT_EQ(result.tokens[0], "run");
    EXPECT_EQ(result.tokens[1], "--debug");
}

TEST(CommandParserTest, ParseQuotedStrings) {
    CommandParser parser;
    auto result = parser.parse(R"(breakpoint condition 1 "r1 > 10")");
    ASSERT_EQ(result.tokens.size(), 4);
    EXPECT_EQ(result.tokens[0], "breakpoint");
    EXPECT_EQ(result.tokens[1], "condition");
    EXPECT_EQ(result.tokens[2], "1");
    EXPECT_EQ(result.tokens[3], "r1 > 10");  // Quotes stripped
}

TEST(CommandParserTest, ParseSingleQuotedStrings) {
    CommandParser parser;
    auto result = parser.parse("breakpoint condition 1 'r2 == 0'");
    ASSERT_EQ(result.tokens.size(), 4);
    EXPECT_EQ(result.tokens[3], "r2 == 0");
}

// ============================================================================
// Alias Expansion Tests
// ============================================================================

TEST(CommandParserTest, AliasRun) {
    CommandParser parser;
    auto result = parser.parse("r");
    EXPECT_EQ(result.command, "run");
    EXPECT_EQ(result.original_command, "r");
}

TEST(CommandParserTest, AliasContinue) {
    CommandParser parser;
    auto result = parser.parse("c");
    EXPECT_EQ(result.command, "continue");
    EXPECT_EQ(result.original_command, "c");
}

TEST(CommandParserTest, AliasStep) {
    CommandParser parser;
    auto result = parser.parse("s");
    EXPECT_EQ(result.command, "step");
    EXPECT_EQ(result.original_command, "s");
}

TEST(CommandParserTest, AliasNext) {
    CommandParser parser;
    auto result = parser.parse("n");
    EXPECT_EQ(result.command, "next");
    EXPECT_EQ(result.original_command, "n");
}

TEST(CommandParserTest, AliasFinish) {
    CommandParser parser;
    auto result = parser.parse("fin");
    EXPECT_EQ(result.command, "finish");
    EXPECT_EQ(result.original_command, "fin");
}

TEST(CommandParserTest, AliasQuit) {
    CommandParser parser;
    auto result = parser.parse("q");
    EXPECT_EQ(result.command, "quit");
    EXPECT_EQ(result.original_command, "q");
}

TEST(CommandParserTest, AliasBreakpointShort) {
    CommandParser parser;
    auto result = parser.parse("b 0x10");
    EXPECT_EQ(result.command, "breakpoint");
    ASSERT_GE(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[1], "set");  // 'b <addr>' expands to 'breakpoint set <addr>'
    EXPECT_EQ(result.tokens[2], "0x10");
}

TEST(CommandParserTest, AliasBreakpointList) {
    CommandParser parser;
    auto result = parser.parse("bl");
    EXPECT_EQ(result.command, "breakpoint");
    ASSERT_GE(result.tokens.size(), 2);
    EXPECT_EQ(result.tokens[1], "list");
}

TEST(CommandParserTest, AliasBreakpointDelete) {
    CommandParser parser;
    auto result = parser.parse("bd 1");
    EXPECT_EQ(result.command, "breakpoint");
    ASSERT_GE(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[1], "delete");
    EXPECT_EQ(result.tokens[2], "1");
}

TEST(CommandParserTest, AliasBreakpointEnable) {
    CommandParser parser;
    auto result = parser.parse("be 1");
    EXPECT_EQ(result.command, "breakpoint");
    ASSERT_GE(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[1], "enable");
}

TEST(CommandParserTest, AliasBreakpointDisable) {
    CommandParser parser;
    auto result = parser.parse("bdi 1");
    EXPECT_EQ(result.command, "breakpoint");
    ASSERT_GE(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[1], "disable");
}

TEST(CommandParserTest, AliasRegister) {
    CommandParser parser;
    auto result = parser.parse("reg");
    EXPECT_EQ(result.command, "register");
    EXPECT_EQ(result.original_command, "reg");
}

TEST(CommandParserTest, AliasMemory) {
    CommandParser parser;
    auto result = parser.parse("x 0 0 16");
    EXPECT_EQ(result.command, "memory");
    // 'x <h> <off> <sz>' expands to 'memory read <h> <off> <sz>'
    ASSERT_EQ(result.tokens.size(), 5);
    EXPECT_EQ(result.tokens[1], "read");
}

TEST(CommandParserTest, AliasDisassemble) {
    CommandParser parser;
    auto result = parser.parse("dis");
    EXPECT_EQ(result.command, "disassemble");
    EXPECT_EQ(result.original_command, "dis");
}

TEST(CommandParserTest, AliasBacktrace) {
    CommandParser parser;
    auto result = parser.parse("bt");
    EXPECT_EQ(result.command, "backtrace");
    EXPECT_EQ(result.original_command, "bt");
}

TEST(CommandParserTest, AliasFrame) {
    CommandParser parser;
    auto result = parser.parse("f 2");
    EXPECT_EQ(result.command, "frame");
    // 'f <n>' expands to 'frame select <n>'
    ASSERT_GE(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[1], "select");
    EXPECT_EQ(result.tokens[2], "2");
}

TEST(CommandParserTest, AliasWatchpoint) {
    CommandParser parser;
    auto result = parser.parse("w 0 0 4");
    EXPECT_EQ(result.command, "watchpoint");
}

TEST(CommandParserTest, AliasWatchpointList) {
    CommandParser parser;
    auto result = parser.parse("wl");
    EXPECT_EQ(result.command, "watchpoint");
    ASSERT_GE(result.tokens.size(), 2);
    EXPECT_EQ(result.tokens[1], "list");
}

TEST(CommandParserTest, AliasWatchpointDelete) {
    CommandParser parser;
    auto result = parser.parse("wd 1");
    EXPECT_EQ(result.command, "watchpoint");
    ASSERT_GE(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[1], "delete");
}

TEST(CommandParserTest, AliasList) {
    CommandParser parser;
    auto result = parser.parse("l");
    EXPECT_EQ(result.command, "source");
    EXPECT_EQ(result.original_command, "l");
}

TEST(CommandParserTest, AliasHelp) {
    CommandParser parser;
    auto result = parser.parse("h");
    EXPECT_EQ(result.command, "help");
    EXPECT_EQ(result.original_command, "h");
}

// ============================================================================
// Full Command (No Alias) Tests
// ============================================================================

TEST(CommandParserTest, FullCommandRun) {
    CommandParser parser;
    auto result = parser.parse("run");
    EXPECT_EQ(result.command, "run");
    EXPECT_EQ(result.original_command, "run");  // No alias used
}

TEST(CommandParserTest, FullCommandBreakpointSet) {
    CommandParser parser;
    auto result = parser.parse("breakpoint set --address 0x10");
    EXPECT_EQ(result.command, "breakpoint");
    ASSERT_GE(result.tokens.size(), 4);
    EXPECT_EQ(result.tokens[1], "set");
    EXPECT_EQ(result.tokens[2], "--address");
    EXPECT_EQ(result.tokens[3], "0x10");
}

// ============================================================================
// Option Parsing Tests
// ============================================================================

TEST(CommandParserTest, ParseLongOptions) {
    CommandParser parser;
    auto result = parser.parse("disassemble --count 5");
    EXPECT_EQ(result.command, "disassemble");
    ASSERT_GE(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[1], "--count");
    EXPECT_EQ(result.tokens[2], "5");
}

TEST(CommandParserTest, ParseShortOptions) {
    CommandParser parser;
    auto result = parser.parse("disassemble -c 10");
    ASSERT_GE(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[1], "-c");
    EXPECT_EQ(result.tokens[2], "10");
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(CommandParserTest, UnterminatedQuote) {
    CommandParser parser;
    auto result = parser.parse(R"(breakpoint condition 1 "unterminated)");
    // Should handle gracefully - include remainder as one token
    ASSERT_GE(result.tokens.size(), 4);
}

TEST(CommandParserTest, EmptyQuotedString) {
    CommandParser parser;
    auto result = parser.parse(R"(echo "")");
    ASSERT_GE(result.tokens.size(), 2);
    EXPECT_EQ(result.tokens[1], "");
}

TEST(CommandParserTest, MixedQuotes) {
    CommandParser parser;
    auto result = parser.parse(R"(cmd "arg with spaces" 'another arg')");
    ASSERT_EQ(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[0], "cmd");
    EXPECT_EQ(result.tokens[1], "arg with spaces");
    EXPECT_EQ(result.tokens[2], "another arg");
}

TEST(CommandParserTest, HexAddressParsing) {
    CommandParser parser;
    auto result = parser.parse("b 0x1A2B");
    // 'b <addr>' expands to 'breakpoint set <addr>'
    ASSERT_GE(result.tokens.size(), 3);
    EXPECT_EQ(result.tokens[1], "set");
    EXPECT_EQ(result.tokens[2], "0x1A2B");
}

TEST(CommandParserTest, NegativeNumber) {
    CommandParser parser;
    auto result = parser.parse("breakpoint condition 1 -5");
    ASSERT_GE(result.tokens.size(), 4);
    EXPECT_EQ(result.tokens[3], "-5");
}

// ============================================================================
// Custom Alias Registration Tests
// ============================================================================

TEST(CommandParserTest, RegisterCustomAlias) {
    CommandParser parser;
    parser.register_alias("br", "breakpoint");
    auto result = parser.parse("br set 0x20");
    EXPECT_EQ(result.command, "breakpoint");
    EXPECT_EQ(result.original_command, "br");
}

TEST(CommandParserTest, OverrideBuiltinAlias) {
    CommandParser parser;
    parser.register_alias("r", "register");  // Override 'r' from 'run'
    auto result = parser.parse("r");
    EXPECT_EQ(result.command, "register");
}

TEST(CommandParserTest, GetRegisteredAliases) {
    CommandParser parser;
    auto aliases = parser.get_aliases();
    // Should contain default aliases
    EXPECT_TRUE(aliases.contains("r"));
    EXPECT_TRUE(aliases.contains("c"));
    EXPECT_TRUE(aliases.contains("s"));
    EXPECT_TRUE(aliases.contains("q"));
}

}  // namespace
}  // namespace dotvm::debugger
