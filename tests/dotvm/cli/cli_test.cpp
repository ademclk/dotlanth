/// @file cli_test.cpp
/// @brief DSL-003 CLI application unit tests

#include <gtest/gtest.h>

#include "dotvm/cli/cli_app.hpp"

using namespace dotvm::cli;

// ============================================================================
// CliApp Basic Tests
// ============================================================================

TEST(CliAppTest, Construction) {
    CliApp app;
    // Should not throw
    SUCCEED();
}

TEST(CliAppTest, NoSubcommandShowsHelp) {
    CliApp app;
    const char* argv[] = {"dotdsl"};
    ExitCode code = app.parse(1, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_TRUE(app.current_subcommand().empty());
}

TEST(CliAppTest, HelpFlag) {
    CliApp app;
    const char* argv[] = {"dotdsl", "--help"};
    ExitCode code = app.parse(2, argv);
    EXPECT_EQ(code, ExitCode::Success);
}

TEST(CliAppTest, VersionFlag) {
    CliApp app;
    const char* argv[] = {"dotdsl", "--version"};
    ExitCode code = app.parse(2, argv);
    EXPECT_EQ(code, ExitCode::Success);
}

// ============================================================================
// Global Options Tests
// ============================================================================

TEST(CliAppTest, VerboseFlag) {
    CliApp app;
    const char* argv[] = {"dotdsl", "--verbose"};
    ExitCode code = app.parse(2, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_TRUE(app.global_options().verbose);
}

TEST(CliAppTest, DebugFlag) {
    CliApp app;
    const char* argv[] = {"dotdsl", "--debug"};
    ExitCode code = app.parse(2, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_TRUE(app.global_options().debug);
}

TEST(CliAppTest, StrictFlag) {
    CliApp app;
    const char* argv[] = {"dotdsl", "--strict"};
    ExitCode code = app.parse(2, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_TRUE(app.global_options().strict);
}

TEST(CliAppTest, OutputIrFlag) {
    CliApp app;
    const char* argv[] = {"dotdsl", "--output-ir"};
    ExitCode code = app.parse(2, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_TRUE(app.global_options().output_ir);
}

TEST(CliAppTest, NoColorFlag) {
    CliApp app;
    const char* argv[] = {"dotdsl", "--no-color"};
    ExitCode code = app.parse(2, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_TRUE(app.global_options().no_color);
}

TEST(CliAppTest, MultipleGlobalFlags) {
    CliApp app;
    const char* argv[] = {"dotdsl", "--verbose", "--debug", "--no-color"};
    ExitCode code = app.parse(4, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_TRUE(app.global_options().verbose);
    EXPECT_TRUE(app.global_options().debug);
    EXPECT_TRUE(app.global_options().no_color);
    EXPECT_FALSE(app.global_options().strict);
}

// ============================================================================
// Compile Subcommand Tests
// ============================================================================

TEST(CliAppTest, CompileSubcommandRequiresFile) {
    CliApp app;
    const char* argv[] = {"dotdsl", "compile"};
    ExitCode code = app.parse(2, argv);
    // Should fail - missing required file argument
    EXPECT_EQ(code, ExitCode::ParseError);
}

TEST(CliAppTest, CompileSubcommandWithNonExistentFile) {
    CliApp app;
    const char* argv[] = {"dotdsl", "compile", "nonexistent.dsl"};
    ExitCode code = app.parse(3, argv);
    // CLI11 validates file existence
    EXPECT_EQ(code, ExitCode::ParseError);
}

// ============================================================================
// Check Subcommand Tests
// ============================================================================

TEST(CliAppTest, CheckSubcommandRequiresFile) {
    CliApp app;
    const char* argv[] = {"dotdsl", "check"};
    ExitCode code = app.parse(2, argv);
    // Should fail - missing required file argument
    EXPECT_EQ(code, ExitCode::ParseError);
}

// ============================================================================
// Format Subcommand Tests
// ============================================================================

TEST(CliAppTest, FormatSubcommandRequiresFile) {
    CliApp app;
    const char* argv[] = {"dotdsl", "format"};
    ExitCode code = app.parse(2, argv);
    // Should fail - missing required file argument
    EXPECT_EQ(code, ExitCode::ParseError);
}

// ============================================================================
// Watch Subcommand Tests
// ============================================================================

TEST(CliAppTest, WatchSubcommandRequiresDirectory) {
    CliApp app;
    const char* argv[] = {"dotdsl", "watch"};
    ExitCode code = app.parse(2, argv);
    // Should fail - missing required directory argument
    EXPECT_EQ(code, ExitCode::ParseError);
}

TEST(CliAppTest, WatchSubcommandWithNonExistentDirectory) {
    CliApp app;
    const char* argv[] = {"dotdsl", "watch", "/nonexistent/path"};
    ExitCode code = app.parse(3, argv);
    // CLI11 validates directory existence
    EXPECT_EQ(code, ExitCode::ParseError);
}

// ============================================================================
// Invalid Arguments Tests
// ============================================================================

TEST(CliAppTest, UnknownFlag) {
    CliApp app;
    const char* argv[] = {"dotdsl", "--unknown-flag"};
    ExitCode code = app.parse(2, argv);
    EXPECT_EQ(code, ExitCode::ParseError);
}

TEST(CliAppTest, UnknownSubcommand) {
    CliApp app;
    const char* argv[] = {"dotdsl", "unknown"};
    ExitCode code = app.parse(2, argv);
    EXPECT_EQ(code, ExitCode::ParseError);
}
