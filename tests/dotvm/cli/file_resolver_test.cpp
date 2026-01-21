/// @file file_resolver_test.cpp
/// @brief DSL-003 FileResolver class unit tests

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "dotvm/cli/file_resolver.hpp"

using namespace dotvm::cli;

// ============================================================================
// Test Fixture
// ============================================================================

class FileResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test directory
        test_dir_ = std::filesystem::temp_directory_path() / "dotvm_cli_test";
        std::filesystem::create_directories(test_dir_);

        // Create test files
        create_file(test_dir_ / "main.dsl", "dot Main:\n    state:\n        x: 0\n");
        create_file(test_dir_ / "lib.dsl", "dot Lib:\n    state:\n        y: 1\n");

        std::filesystem::create_directories(test_dir_ / "subdir");
        create_file(test_dir_ / "subdir" / "nested.dsl", "dot Nested:\n    state:\n        z: 2\n");
    }

    void TearDown() override { std::filesystem::remove_all(test_dir_); }

    void create_file(const std::filesystem::path& path, const std::string& content) {
        std::ofstream file(path);
        file << content;
    }

    std::filesystem::path test_dir_;
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(FileResolverTest, DefaultConstruction) {
    FileResolver resolver;
    EXPECT_EQ(resolver.base_dir(), std::filesystem::current_path());
}

TEST_F(FileResolverTest, ConstructionWithBaseDir) {
    FileResolver resolver(test_dir_);
    EXPECT_EQ(resolver.base_dir(), test_dir_);
}

// ============================================================================
// Read File Tests
// ============================================================================

TEST_F(FileResolverTest, ReadExistingFile) {
    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(test_dir_ / "main.dsl");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->find("dot Main:") != std::string::npos);
    EXPECT_TRUE(result->find("x: 0") != std::string::npos);
}

TEST_F(FileResolverTest, ReadNonExistentFile) {
    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(test_dir_ / "nonexistent.dsl");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ExitCode::FileNotFound);
}

TEST_F(FileResolverTest, ReadDirectory) {
    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(test_dir_);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ExitCode::IoError);
}

// ============================================================================
// Import Resolution Tests
// ============================================================================

TEST_F(FileResolverTest, ResolveRelativeImport) {
    FileResolver resolver(test_dir_);
    auto result = resolver.resolve_import("lib.dsl", test_dir_ / "main.dsl");

    ASSERT_TRUE(result.has_value());
    // The resolved path should point to lib.dsl
    EXPECT_TRUE(std::filesystem::equivalent(*result, test_dir_ / "lib.dsl"));
}

TEST_F(FileResolverTest, ResolveNestedImport) {
    FileResolver resolver(test_dir_);
    auto result = resolver.resolve_import("subdir/nested.dsl", test_dir_ / "main.dsl");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(*result, test_dir_ / "subdir" / "nested.dsl"));
}

TEST_F(FileResolverTest, ResolveParentImport) {
    FileResolver resolver(test_dir_);
    auto result = resolver.resolve_import("../main.dsl", test_dir_ / "subdir" / "nested.dsl");

    // This should point back to main.dsl
    // Note: parent of test_dir_/subdir is test_dir_, so ../main.dsl from subdir is
    // test_dir_/main.dsl But the import is relative to nested.dsl in subdir, so we need
    // test_dir_/main.dsl Actually: from test_dir_/subdir/nested.dsl, "../main.dsl" is
    // test_dir_/main.dsl Let's check this logic
    ASSERT_TRUE(result.has_value());
    // main.dsl should be in test_dir_, not test_dir_/subdir
    // The parent of test_dir_/subdir is test_dir_, so ../main.dsl is test_dir_/main.dsl
    EXPECT_TRUE(std::filesystem::equivalent(*result, test_dir_ / "main.dsl"));
}

TEST_F(FileResolverTest, ResolveNonExistentImport) {
    FileResolver resolver(test_dir_);
    auto result = resolver.resolve_import("nonexistent.dsl", test_dir_ / "main.dsl");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ExitCode::FileNotFound);
}

// ============================================================================
// Import Tracking Tests
// ============================================================================

TEST_F(FileResolverTest, ImportTrackingInitiallyEmpty) {
    FileResolver resolver(test_dir_);
    EXPECT_FALSE(resolver.is_imported(test_dir_ / "main.dsl"));
}

TEST_F(FileResolverTest, MarkImported) {
    FileResolver resolver(test_dir_);
    auto path = test_dir_ / "main.dsl";

    EXPECT_FALSE(resolver.is_imported(path));
    resolver.mark_imported(path);
    EXPECT_TRUE(resolver.is_imported(path));
}

TEST_F(FileResolverTest, ClearImports) {
    FileResolver resolver(test_dir_);
    auto path = test_dir_ / "main.dsl";

    resolver.mark_imported(path);
    EXPECT_TRUE(resolver.is_imported(path));

    resolver.clear_imports();
    EXPECT_FALSE(resolver.is_imported(path));
}

TEST_F(FileResolverTest, CircularImportDetection) {
    FileResolver resolver(test_dir_);

    // Simulate processing main.dsl
    resolver.mark_imported(test_dir_ / "main.dsl");

    // Now if we encounter an import back to main.dsl, we should detect it
    EXPECT_TRUE(resolver.is_imported(test_dir_ / "main.dsl"));
}

// ============================================================================
// Default Output Path Tests
// ============================================================================

TEST_F(FileResolverTest, DefaultOutputPath) {
    auto output = FileResolver::default_output_path("test.dsl");
    EXPECT_EQ(output.extension(), ".dot");
    EXPECT_EQ(output.stem(), "test");
}

TEST_F(FileResolverTest, DefaultOutputPathWithPath) {
    auto output = FileResolver::default_output_path("/path/to/test.dsl");
    EXPECT_EQ(output.extension(), ".dot");
    EXPECT_EQ(output.filename(), "test.dot");
}

TEST_F(FileResolverTest, DefaultOutputPathNoExtension) {
    auto output = FileResolver::default_output_path("test");
    EXPECT_EQ(output.extension(), ".dot");
}
