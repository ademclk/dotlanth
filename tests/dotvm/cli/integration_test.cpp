/// @file integration_test.cpp
/// @brief DSL-003 CLI integration tests

#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include "dotvm/cli/cli_app.hpp"
#include "dotvm/cli/file_resolver.hpp"
#include "dotvm/cli/terminal.hpp"

using namespace dotvm::cli;

// ============================================================================
// Integration Test Fixture
// ============================================================================

class CliIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test directory
        test_dir_ = std::filesystem::temp_directory_path() / "dotvm_cli_integration_test";
        std::filesystem::create_directories(test_dir_);

        // Create valid test files
        create_file(test_dir_ / "valid.dsl", "dot Counter:\n"
                                             "    state:\n"
                                             "        count: 0\n"
                                             "    when count < 10:\n"
                                             "        do:\n"
                                             "            count += 1\n");

        create_file(test_dir_ / "empty.dsl", "");
    }

    void TearDown() override { std::filesystem::remove_all(test_dir_); }

    void create_file(const std::filesystem::path& path, const std::string& content) {
        std::ofstream file(path);
        file << content;
    }

    std::filesystem::path test_dir_;
};

// ============================================================================
// Terminal + FileResolver Integration
// ============================================================================

TEST_F(CliIntegrationTest, TerminalReportFileError) {
    std::ostringstream oss;
    Terminal term(oss, true);

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(test_dir_ / "nonexistent.dsl");

    ASSERT_FALSE(result.has_value());

    // Use terminal to report error
    term.error("error: ");
    term.print(result.error().message);
    term.newline();

    std::string output = oss.str();
    EXPECT_TRUE(output.find("error:") != std::string::npos);
    EXPECT_TRUE(output.find("File not found") != std::string::npos);
}

// ============================================================================
// CLI App with Valid File Path
// ============================================================================

TEST_F(CliIntegrationTest, CompileWithValidPath) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ExitCode code = app.parse(3, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_EQ(app.current_subcommand(), "compile");
    EXPECT_EQ(app.compile_options().input_file, filepath);
}

TEST_F(CliIntegrationTest, CheckWithValidPath) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ExitCode code = app.parse(3, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_EQ(app.current_subcommand(), "check");
}

TEST_F(CliIntegrationTest, FormatWithValidPath) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "format", filepath.c_str()};

    ExitCode code = app.parse(3, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_EQ(app.current_subcommand(), "format");
}

TEST_F(CliIntegrationTest, WatchWithValidDirectory) {
    CliApp app;
    std::string dirpath = test_dir_.string();
    const char* argv[] = {"dotdsl", "watch", dirpath.c_str()};

    ExitCode code = app.parse(3, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_EQ(app.current_subcommand(), "watch");
    EXPECT_EQ(app.watch_options().directory, dirpath);
}

// ============================================================================
// Global Options with Subcommands
// ============================================================================

TEST_F(CliIntegrationTest, GlobalOptionsWithCompile) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "--verbose", "--debug", "compile", filepath.c_str()};

    ExitCode code = app.parse(5, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_TRUE(app.global_options().verbose);
    EXPECT_TRUE(app.global_options().debug);
    EXPECT_EQ(app.current_subcommand(), "compile");
}

TEST_F(CliIntegrationTest, CompileWithOutputOption) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    std::string outpath = (test_dir_ / "output.dot").string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str(), "-o", outpath.c_str()};

    ExitCode code = app.parse(5, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_EQ(app.compile_options().output_file, outpath);
}

TEST_F(CliIntegrationTest, FormatWithInPlaceOption) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "format", filepath.c_str(), "--in-place"};

    ExitCode code = app.parse(4, argv);
    EXPECT_EQ(code, ExitCode::Success);
    EXPECT_TRUE(app.format_options().in_place);
}

// ============================================================================
// Run Method Tests (Skeleton Verification)
// ============================================================================

TEST_F(CliIntegrationTest, RunCompileCommand) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    // Run should succeed (skeleton implementation)
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, RunCheckCommand) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, RunNoSubcommand) {
    CliApp app;
    const char* argv[] = {"dotdsl"};

    ASSERT_EQ(app.parse(1, argv), ExitCode::Success);
    // No subcommand should show help and return success
    EXPECT_EQ(app.run(), ExitCode::Success);
}
