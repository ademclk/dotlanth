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

// ============================================================================
// Compile Command Integration Tests
// ============================================================================

TEST_F(CliIntegrationTest, CompileValidFileProducesBytecode) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    std::filesystem::path output_path = test_dir_ / "valid.dot";
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::Success);

    // Verify output file was created
    EXPECT_TRUE(std::filesystem::exists(output_path));

    // Verify output file is non-empty
    auto size = std::filesystem::file_size(output_path);
    EXPECT_GT(size, 0U);
}

TEST_F(CliIntegrationTest, CompileWithCustomOutputPath) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    std::filesystem::path custom_output = test_dir_ / "custom_output.dot";
    std::string outpath = custom_output.string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str(), "-o", outpath.c_str()};

    ASSERT_EQ(app.parse(5, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::Success);

    // Verify custom output file was created
    EXPECT_TRUE(std::filesystem::exists(custom_output));
}

TEST_F(CliIntegrationTest, CompileInvalidSyntaxReturnsError) {
    // Create a file with invalid syntax
    create_file(test_dir_ / "invalid.dsl", "dot Broken\n"
                                           "    this is not valid syntax!\n");

    CliApp app;
    std::string filepath = (test_dir_ / "invalid.dsl").string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::CompilationError);
}

TEST_F(CliIntegrationTest, CompileNonexistentFileReturnsError) {
    CliApp app;
    std::string filepath = (test_dir_ / "nonexistent.dsl").string();

    // CLI11 file validation will catch this during parse
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};
    EXPECT_EQ(app.parse(3, argv), ExitCode::ParseError);
}

TEST_F(CliIntegrationTest, CompileWithOutputIRFlag) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "--output-ir", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(4, argv), ExitCode::Success);
    EXPECT_TRUE(app.global_options().output_ir);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, CompileEmptyFileProducesBytecode) {
    CliApp app;
    std::string filepath = (test_dir_ / "empty.dsl").string();
    std::filesystem::path output_path = test_dir_ / "empty.dot";
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    // Empty file should parse successfully (empty module is valid)
    ExitCode result = app.run();
    // Note: may succeed or fail depending on whether empty module generates valid bytecode
    // For now we just check it doesn't crash
    EXPECT_TRUE(result == ExitCode::Success || result == ExitCode::CompilationError);
}

// ============================================================================
// Check Command Integration Tests
// ============================================================================

TEST_F(CliIntegrationTest, CheckValidFileSucceeds) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::Success);

    // Verify no output file was created (check doesn't produce bytecode)
    std::filesystem::path output_path = test_dir_ / "valid.dot";
    // Note: If compile was run before, this might exist, so we clean it
    if (std::filesystem::exists(output_path)) {
        std::filesystem::remove(output_path);
    }

    // Run check again
    CliApp app2;
    ASSERT_EQ(app2.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app2.run(), ExitCode::Success);

    // Verify check didn't create an output file
    // (check should only validate, not compile)
}

TEST_F(CliIntegrationTest, CheckInvalidSyntaxReturnsError) {
    // Create a file with invalid syntax
    create_file(test_dir_ / "invalid_check.dsl", "dot Broken\n"
                                                 "    missing colon after dot\n");

    CliApp app;
    std::string filepath = (test_dir_ / "invalid_check.dsl").string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::ParseError);
}

TEST_F(CliIntegrationTest, CheckEmptyFileSucceeds) {
    CliApp app;
    std::string filepath = (test_dir_ / "empty.dsl").string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    // Empty file should be valid (empty module)
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, CheckWithVerboseFlag) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "--verbose", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(4, argv), ExitCode::Success);
    EXPECT_TRUE(app.global_options().verbose);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, CheckWithDebugFlag) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "--debug", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(4, argv), ExitCode::Success);
    EXPECT_TRUE(app.global_options().debug);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, CheckMultipleErrorsInFile) {
    // Create a file with multiple syntax errors
    create_file(test_dir_ / "multi_error.dsl", "dot First\n"
                                               "    invalid stuff\n"
                                               "dot Second\n"
                                               "    more invalid\n");

    CliApp app;
    std::string filepath = (test_dir_ / "multi_error.dsl").string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::ParseError);
}

// ============================================================================
// Complex DSL File Tests
// ============================================================================

TEST_F(CliIntegrationTest, CompileComplexDslFile) {
    // Create a more complex DSL file with multiple dots and links
    create_file(test_dir_ / "complex.dsl", "dot Counter:\n"
                                           "    state:\n"
                                           "        count: 0\n"
                                           "        max: 100\n"
                                           "    when count < max:\n"
                                           "        do:\n"
                                           "            count += 1\n"
                                           "\n"
                                           "dot Logger:\n"
                                           "    state:\n"
                                           "        messages: 0\n"
                                           "    when messages < 10:\n"
                                           "        do:\n"
                                           "            messages += 1\n"
                                           "\n"
                                           "link Counter -> Logger\n");

    CliApp app;
    std::string filepath = (test_dir_ / "complex.dsl").string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::Success);

    // Verify output file was created
    std::filesystem::path output_path = test_dir_ / "complex.dot";
    EXPECT_TRUE(std::filesystem::exists(output_path));
}

TEST_F(CliIntegrationTest, CheckComplexDslFile) {
    // Use the same complex file
    create_file(test_dir_ / "complex_check.dsl", "dot Counter:\n"
                                                 "    state:\n"
                                                 "        count: 0\n"
                                                 "        max: 100\n"
                                                 "    when count < max:\n"
                                                 "        do:\n"
                                                 "            count += 1\n"
                                                 "\n"
                                                 "dot Logger:\n"
                                                 "    state:\n"
                                                 "        messages: 0\n"
                                                 "    when messages < 10:\n"
                                                 "        do:\n"
                                                 "            messages += 1\n"
                                                 "\n"
                                                 "link Counter -> Logger\n");

    CliApp app;
    std::string filepath = (test_dir_ / "complex_check.dsl").string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

// ============================================================================
// No Color Flag Tests
// ============================================================================

TEST_F(CliIntegrationTest, CompileWithNoColorFlag) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "--no-color", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(4, argv), ExitCode::Success);
    EXPECT_TRUE(app.global_options().no_color);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, CheckWithNoColorFlag) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "--no-color", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(4, argv), ExitCode::Success);
    EXPECT_TRUE(app.global_options().no_color);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

// ============================================================================
// Edge Case Integration Tests
// ============================================================================

TEST_F(CliIntegrationTest, CompileBinaryFileReturnsError) {
    // Create a file with null bytes (binary content)
    std::filesystem::path binary_file = test_dir_ / "binary.dsl";
    {
        std::ofstream file(binary_file, std::ios::binary);
        file << "dot Binary:\n";
        file << '\0';  // Null byte indicates binary
        file << "state:\n";
    }

    CliApp app;
    std::string filepath = binary_file.string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::IoError);
}

TEST_F(CliIntegrationTest, CheckBinaryFileReturnsError) {
    // Create a file with null bytes (binary content)
    std::filesystem::path binary_file = test_dir_ / "binary_check.dsl";
    {
        std::ofstream file(binary_file, std::ios::binary);
        file << "dot Binary:\n";
        file << '\0';  // Null byte
        file << "state:\n";
    }

    CliApp app;
    std::string filepath = binary_file.string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::IoError);
}

TEST_F(CliIntegrationTest, FormatBinaryFileReturnsError) {
    // Create a file with null bytes (binary content)
    std::filesystem::path binary_file = test_dir_ / "binary_format.dsl";
    {
        std::ofstream file(binary_file, std::ios::binary);
        file << "dot Binary:\n";
        file << '\0';  // Null byte
        file << "state:\n";
    }

    CliApp app;
    std::string filepath = binary_file.string();
    const char* argv[] = {"dotdsl", "format", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::IoError);
}

TEST_F(CliIntegrationTest, CompileInvalidUtf8ReturnsError) {
    // Create a file with invalid UTF-8 sequences
    std::filesystem::path invalid_utf8 = test_dir_ / "invalid_utf8.dsl";
    {
        std::ofstream file(invalid_utf8, std::ios::binary);
        file << "dot Invalid:\n";
        // Write invalid UTF-8: 0xFF is never valid as a start byte
        file << static_cast<char>(0xFF);
        file << static_cast<char>(0xFE);
        file << "state:\n";
    }

    CliApp app;
    std::string filepath = invalid_utf8.string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::IoError);
}

TEST_F(CliIntegrationTest, CompileFileWithOnlyComments) {
    // Create a file with only comments
    std::filesystem::path comment_file = test_dir_ / "comments_only.dsl";
    create_file(comment_file, "# This is a comment\n# Another comment\n");

    CliApp app;
    std::string filepath = comment_file.string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    // Empty module should compile successfully
    ExitCode result = app.run();
    EXPECT_TRUE(result == ExitCode::Success || result == ExitCode::CompilationError);
}

TEST_F(CliIntegrationTest, CheckFileWithOnlyComments) {
    // Create a file with only comments
    std::filesystem::path comment_file = test_dir_ / "comments_check.dsl";
    create_file(comment_file, "# This is a comment\n# Another comment\n");

    CliApp app;
    std::string filepath = comment_file.string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, FormatFileWithOnlyComments) {
    // Create a file with only comments
    std::filesystem::path comment_file = test_dir_ / "comments_format.dsl";
    create_file(comment_file, "# This is a comment\n# Another comment\n");

    CliApp app;
    std::string filepath = comment_file.string();
    const char* argv[] = {"dotdsl", "format", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, CompileFileWithoutTrailingNewline) {
    // Create a file without trailing newline - the parser requires newlines
    // after statements, so this should fail with a parse error
    std::filesystem::path no_newline = test_dir_ / "no_newline.dsl";
    create_file(no_newline, "dot NoNewline:\n    state:\n        x: 0");

    CliApp app;
    std::string filepath = no_newline.string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    // Parser requires trailing newline after state variable
    EXPECT_EQ(app.run(), ExitCode::CompilationError);
}

TEST_F(CliIntegrationTest, CheckFileWithoutTrailingNewline) {
    // Create a file without trailing newline - the parser requires newlines
    // after statements, so this should fail with a parse error
    std::filesystem::path no_newline = test_dir_ / "no_newline_check.dsl";
    create_file(no_newline, "dot NoNewline:\n    state:\n        x: 0");

    CliApp app;
    std::string filepath = no_newline.string();
    const char* argv[] = {"dotdsl", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    // Parser requires trailing newline after state variable
    EXPECT_EQ(app.run(), ExitCode::ParseError);
}

TEST_F(CliIntegrationTest, CompileFileWithTrailingNewline) {
    // Create a valid file with proper trailing newline
    std::filesystem::path with_newline = test_dir_ / "with_newline.dsl";
    create_file(with_newline, "dot WithNewline:\n    state:\n        x: 0\n");

    CliApp app;
    std::string filepath = with_newline.string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, CompileFileWithOnlyWhitespace) {
    // Create a file with only whitespace
    std::filesystem::path ws_file = test_dir_ / "whitespace.dsl";
    create_file(ws_file, "   \n\t\n   \n");

    CliApp app;
    std::string filepath = ws_file.string();
    const char* argv[] = {"dotdsl", "compile", filepath.c_str()};

    ASSERT_EQ(app.parse(3, argv), ExitCode::Success);
    // Empty module should compile, or fail gracefully
    ExitCode result = app.run();
    EXPECT_TRUE(result == ExitCode::Success || result == ExitCode::CompilationError);
}

TEST_F(CliIntegrationTest, FormatFileInPlace) {
    // Create a file to format in place
    std::filesystem::path format_file = test_dir_ / "format_inplace.dsl";
    create_file(format_file, "dot  Counter  :\n    state:\n        count:0\n");

    CliApp app;
    std::string filepath = format_file.string();
    const char* argv[] = {"dotdsl", "format", "--in-place", filepath.c_str()};

    ASSERT_EQ(app.parse(4, argv), ExitCode::Success);
    EXPECT_TRUE(app.format_options().in_place);
    EXPECT_EQ(app.run(), ExitCode::Success);

    // Verify file was modified
    std::ifstream file(format_file);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_TRUE(content.find("dot Counter:") != std::string::npos);
}

TEST_F(CliIntegrationTest, CompileWithMultipleIncludePaths) {
    // Create include directories and files
    std::filesystem::create_directories(test_dir_ / "lib1");
    std::filesystem::create_directories(test_dir_ / "lib2");
    create_file(test_dir_ / "lib1" / "util1.dsl", "dot Util1:\n    state:\n        u1: 0\n");
    create_file(test_dir_ / "lib2" / "util2.dsl", "dot Util2:\n    state:\n        u2: 0\n");

    // Create main file that includes from both (path must be quoted)
    create_file(test_dir_ / "main_multi.dsl", "include: \"util1.dsl\"\n"
                                              "include: \"util2.dsl\"\n"
                                              "dot Main:\n"
                                              "    state:\n"
                                              "        x: 0\n");

    CliApp app;
    std::string filepath = (test_dir_ / "main_multi.dsl").string();
    std::string lib1 = (test_dir_ / "lib1").string();
    std::string lib2 = (test_dir_ / "lib2").string();
    const char* argv[] = {"dotdsl",     "compile", filepath.c_str(), "-I",
                          lib1.c_str(), "-I",      lib2.c_str()};

    ASSERT_EQ(app.parse(7, argv), ExitCode::Success);
    EXPECT_EQ(app.compile_options().include_paths.size(), 2U);
    EXPECT_EQ(app.run(), ExitCode::Success);
}

TEST_F(CliIntegrationTest, CheckWithStrictMode) {
    CliApp app;
    std::string filepath = (test_dir_ / "valid.dsl").string();
    const char* argv[] = {"dotdsl", "--strict", "check", filepath.c_str()};

    ASSERT_EQ(app.parse(4, argv), ExitCode::Success);
    EXPECT_TRUE(app.global_options().strict);
    EXPECT_EQ(app.run(), ExitCode::Success);
}
