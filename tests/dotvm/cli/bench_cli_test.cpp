/// @file bench_cli_test.cpp
/// @brief TOOL-010 Benchmark CLI unit tests

#include <gtest/gtest.h>

#include "dotvm/cli/bench_cli_app.hpp"

using namespace dotvm::cli;

// ============================================================================
// BenchCliApp Construction Tests
// ============================================================================

TEST(BenchCliAppTest, Construction) {
    BenchCliApp app;
    SUCCEED();
}

TEST(BenchCliAppTest, DefaultOptions) {
    BenchCliApp app;
    EXPECT_TRUE(app.options().filter.empty());
    EXPECT_EQ(app.options().repetitions, 3);
    EXPECT_DOUBLE_EQ(app.options().min_time, 0.5);
    EXPECT_EQ(app.options().format, BenchOutputFormat::Console);
    EXPECT_TRUE(app.options().output_file.empty());
    EXPECT_FALSE(app.options().list_only);
    EXPECT_FALSE(app.options().strict);
    EXPECT_FALSE(app.options().no_color);
    EXPECT_FALSE(app.options().force_color);
}

// ============================================================================
// Option Parsing Tests
// ============================================================================

TEST(BenchCliAppTest, FilterOption) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--benchmark_filter=fib"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_EQ(app.options().filter, "fib");
}

TEST(BenchCliAppTest, FilterOptionWithRegex) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--benchmark_filter=fib|sort"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_EQ(app.options().filter, "fib|sort");
}

TEST(BenchCliAppTest, RepetitionsOption) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--benchmark_repetitions=5"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_EQ(app.options().repetitions, 5);
}

TEST(BenchCliAppTest, MinTimeOption) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--benchmark_min_time=1.5"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_DOUBLE_EQ(app.options().min_time, 1.5);
}

TEST(BenchCliAppTest, FormatConsoleOption) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--format=console"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_EQ(app.options().format, BenchOutputFormat::Console);
}

TEST(BenchCliAppTest, FormatJsonOption) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--format=json"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_EQ(app.options().format, BenchOutputFormat::Json);
}

TEST(BenchCliAppTest, FormatCsvOption) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--format=csv"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_EQ(app.options().format, BenchOutputFormat::Csv);
}

TEST(BenchCliAppTest, InvalidFormat) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--format=invalid"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::ValidationError);
}

TEST(BenchCliAppTest, OutputFileOption) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "-o", "results.json"};
    auto code = app.parse(3, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_EQ(app.options().output_file, "results.json");
}

TEST(BenchCliAppTest, OutputFileOptionLong) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--output=results.csv"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_EQ(app.options().output_file, "results.csv");
}

TEST(BenchCliAppTest, ListFlag) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--list"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_TRUE(app.options().list_only);
}

TEST(BenchCliAppTest, StrictFlag) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--strict"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_TRUE(app.options().strict);
}

TEST(BenchCliAppTest, NoColorFlag) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--no-color"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_TRUE(app.options().no_color);
}

TEST(BenchCliAppTest, ColorFlag) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--color"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_TRUE(app.options().force_color);
}

TEST(BenchCliAppTest, CombinedOptions) {
    BenchCliApp app;
    const char* argv[] = {
        "dotvm_bench", "--benchmark_filter=sha", "--format=json", "-o", "out.json", "--strict"};
    auto code = app.parse(6, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_EQ(app.options().filter, "sha");
    EXPECT_EQ(app.options().format, BenchOutputFormat::Json);
    EXPECT_EQ(app.options().output_file, "out.json");
    EXPECT_TRUE(app.options().strict);
}

// ============================================================================
// Help and Version Tests
// ============================================================================

TEST(BenchCliAppTest, HelpFlag) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--help"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_TRUE(app.help_requested());
}

TEST(BenchCliAppTest, HelpFlagShort) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "-h"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_TRUE(app.help_requested());
}

TEST(BenchCliAppTest, VersionFlag) {
    BenchCliApp app;
    const char* argv[] = {"dotvm_bench", "--version"};
    auto code = app.parse(2, argv);
    EXPECT_EQ(code, BenchExitCode::Success);
    EXPECT_TRUE(app.help_requested());
}

// ============================================================================
// BenchExitCode Tests
// ============================================================================

TEST(BenchExitCodeTest, Values) {
    EXPECT_EQ(static_cast<int>(BenchExitCode::Success), 0);
    EXPECT_EQ(static_cast<int>(BenchExitCode::ValidationError), 1);
    EXPECT_EQ(static_cast<int>(BenchExitCode::IOError), 2);
    EXPECT_EQ(static_cast<int>(BenchExitCode::TargetMissed), 3);
}

// ============================================================================
// BenchOutputFormat Tests
// ============================================================================

TEST(BenchOutputFormatTest, AllFormatsExist) {
    // Verify all three format options exist
    BenchOutputFormat console = BenchOutputFormat::Console;
    BenchOutputFormat json = BenchOutputFormat::Json;
    BenchOutputFormat csv = BenchOutputFormat::Csv;

    EXPECT_NE(static_cast<int>(console), static_cast<int>(json));
    EXPECT_NE(static_cast<int>(json), static_cast<int>(csv));
    EXPECT_NE(static_cast<int>(console), static_cast<int>(csv));
}
