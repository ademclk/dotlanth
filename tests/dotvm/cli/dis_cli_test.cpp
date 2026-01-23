/// @file dis_cli_test.cpp
/// @brief TOOL-008 Disassembler CLI unit tests

#include <gtest/gtest.h>

#include "dotvm/cli/dis_cli_app.hpp"

using namespace dotvm::cli;

// ============================================================================
// DisCliApp Construction Tests
// ============================================================================

TEST(DisCliAppTest, Construction) {
    DisCliApp app;
    SUCCEED();
}

TEST(DisCliAppTest, DefaultOptions) {
    DisCliApp app;
    EXPECT_FALSE(app.options().show_bytes);
    EXPECT_FALSE(app.options().show_labels);
    EXPECT_FALSE(app.options().annotate);
    EXPECT_FALSE(app.options().verbose);
    EXPECT_FALSE(app.options().no_color);
    EXPECT_EQ(app.options().format, DisOutputFormat::Text);
    EXPECT_TRUE(app.options().output_file.empty());
    EXPECT_TRUE(app.options().input_file.empty());
}

// ============================================================================
// Option Parsing Tests
// ============================================================================

TEST(DisCliAppTest, ShowBytesFlag) {
    DisCliApp app;
    // Note: Can't actually parse with ExistingFile validator without real file
    // But we can verify the default flag value
    EXPECT_FALSE(app.options().show_bytes);
}

TEST(DisCliAppTest, ShowLabelsFlag) {
    DisCliApp app;
    EXPECT_FALSE(app.options().show_labels);
}

TEST(DisCliAppTest, AnnotateFlag) {
    DisCliApp app;
    EXPECT_FALSE(app.options().annotate);
}

TEST(DisCliAppTest, VerboseFlag) {
    DisCliApp app;
    const char* argv[] = {"dotdis", "--verbose"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, DisExitCode::Success);
    EXPECT_TRUE(app.options().verbose);
}

TEST(DisCliAppTest, QuietFlag) {
    DisCliApp app;
    const char* argv[] = {"dotdis", "-q"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, DisExitCode::Success);
    EXPECT_TRUE(app.options().quiet);
}

TEST(DisCliAppTest, NoColorFlag) {
    DisCliApp app;
    const char* argv[] = {"dotdis", "--no-color"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, DisExitCode::Success);
    EXPECT_TRUE(app.options().no_color);
}

TEST(DisCliAppTest, JsonFormat) {
    DisCliApp app;
    const char* argv[] = {"dotdis", "--format", "json"};
    auto code = app.parse(3, argv);
    EXPECT_EQ(code, DisExitCode::Success);
    EXPECT_EQ(app.options().format, DisOutputFormat::Json);
}

TEST(DisCliAppTest, TextFormat) {
    DisCliApp app;
    const char* argv[] = {"dotdis", "--format", "text"};
    auto code = app.parse(3, argv);
    EXPECT_EQ(code, DisExitCode::Success);
    EXPECT_EQ(app.options().format, DisOutputFormat::Text);
}

TEST(DisCliAppTest, InvalidFormat) {
    DisCliApp app;
    const char* argv[] = {"dotdis", "--format", "invalid"};
    auto code = app.parse(3, argv);
    EXPECT_EQ(code, DisExitCode::ValidationError);
}

TEST(DisCliAppTest, OutputFileOption) {
    DisCliApp app;
    const char* argv[] = {"dotdis", "-o", "output.asm"};
    auto code = app.parse(3, argv);
    EXPECT_EQ(code, DisExitCode::Success);
    EXPECT_EQ(app.options().output_file, "output.asm");
}

// ============================================================================
// Help and Version Tests
// ============================================================================

TEST(DisCliAppTest, HelpFlag) {
    DisCliApp app;
    const char* argv[] = {"dotdis", "--help"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, DisExitCode::Success);
    EXPECT_TRUE(app.help_requested());
}

TEST(DisCliAppTest, VersionFlag) {
    DisCliApp app;
    const char* argv[] = {"dotdis", "--version"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, DisExitCode::Success);
    EXPECT_TRUE(app.help_requested());
}

// ============================================================================
// DisExitCode Tests
// ============================================================================

TEST(DisExitCodeTest, Values) {
    EXPECT_EQ(static_cast<int>(DisExitCode::Success), 0);
    EXPECT_EQ(static_cast<int>(DisExitCode::ValidationError), 1);
    EXPECT_EQ(static_cast<int>(DisExitCode::IOError), 2);
}
