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

        // Create include search path directories
        std::filesystem::create_directories(test_dir_ / "stdlib");
        create_file(test_dir_ / "stdlib" / "common.dsl", "dot Common:\n    state:\n        c: 0\n");

        std::filesystem::create_directories(test_dir_ / "include");
        create_file(test_dir_ / "include" / "utils.dsl", "dot Utils:\n    state:\n        u: 0\n");
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
// Include Resolution Tests
// ============================================================================

TEST_F(FileResolverTest, ResolveExplicitRelativeInclude) {
    FileResolver resolver(test_dir_);
    auto result = resolver.resolve_include("./lib.dsl", test_dir_ / "main.dsl");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(*result, test_dir_ / "lib.dsl"));
}

TEST_F(FileResolverTest, ResolveParentRelativeInclude) {
    FileResolver resolver(test_dir_);
    auto result = resolver.resolve_include("../main.dsl", test_dir_ / "subdir" / "nested.dsl");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(*result, test_dir_ / "main.dsl"));
}

TEST_F(FileResolverTest, ResolveIncludeFromSearchPath) {
    FileResolver resolver(test_dir_);
    resolver.add_search_path(test_dir_ / "stdlib");

    auto result = resolver.resolve_include("common.dsl", test_dir_ / "main.dsl");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(*result, test_dir_ / "stdlib" / "common.dsl"));
}

TEST_F(FileResolverTest, ResolveIncludeMultipleSearchPaths) {
    FileResolver resolver(test_dir_);
    resolver.add_search_path(test_dir_ / "include");
    resolver.add_search_path(test_dir_ / "stdlib");

    // Should find in first search path
    auto result1 = resolver.resolve_include("utils.dsl", test_dir_ / "main.dsl");
    ASSERT_TRUE(result1.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(*result1, test_dir_ / "include" / "utils.dsl"));

    // Should find in second search path
    auto result2 = resolver.resolve_include("common.dsl", test_dir_ / "main.dsl");
    ASSERT_TRUE(result2.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(*result2, test_dir_ / "stdlib" / "common.dsl"));
}

TEST_F(FileResolverTest, ResolveIncludeFallbackToRelative) {
    FileResolver resolver(test_dir_);
    // No search paths added

    // Should fall back to relative resolution
    auto result = resolver.resolve_include("lib.dsl", test_dir_ / "main.dsl");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::filesystem::equivalent(*result, test_dir_ / "lib.dsl"));
}

TEST_F(FileResolverTest, ResolveIncludeNotFound) {
    FileResolver resolver(test_dir_);
    resolver.add_search_path(test_dir_ / "stdlib");

    auto result = resolver.resolve_include("nonexistent.dsl", test_dir_ / "main.dsl");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ExitCode::FileNotFound);
}

// ============================================================================
// Search Path Tests
// ============================================================================

TEST_F(FileResolverTest, AddSearchPath) {
    FileResolver resolver(test_dir_);
    EXPECT_TRUE(resolver.search_paths().empty());

    resolver.add_search_path(test_dir_ / "stdlib");
    EXPECT_EQ(resolver.search_paths().size(), 1);

    resolver.add_search_path(test_dir_ / "include");
    EXPECT_EQ(resolver.search_paths().size(), 2);
}

TEST_F(FileResolverTest, AddDuplicateSearchPath) {
    FileResolver resolver(test_dir_);
    resolver.add_search_path(test_dir_ / "stdlib");
    resolver.add_search_path(test_dir_ / "stdlib");  // Duplicate

    EXPECT_EQ(resolver.search_paths().size(), 1);
}

TEST_F(FileResolverTest, AddNonexistentSearchPath) {
    FileResolver resolver(test_dir_);
    resolver.add_search_path(test_dir_ / "nonexistent");

    // Should not add nonexistent directory
    EXPECT_TRUE(resolver.search_paths().empty());
}

// ============================================================================
// Include Tracking Tests
// ============================================================================

TEST_F(FileResolverTest, IncludeTrackingInitiallyEmpty) {
    FileResolver resolver(test_dir_);
    EXPECT_FALSE(resolver.is_included(test_dir_ / "main.dsl"));
}

TEST_F(FileResolverTest, MarkIncluded) {
    FileResolver resolver(test_dir_);
    auto path = test_dir_ / "main.dsl";

    EXPECT_FALSE(resolver.is_included(path));
    resolver.mark_included(path);
    EXPECT_TRUE(resolver.is_included(path));
}

TEST_F(FileResolverTest, ClearIncludes) {
    FileResolver resolver(test_dir_);
    auto path = test_dir_ / "main.dsl";

    resolver.mark_included(path);
    EXPECT_TRUE(resolver.is_included(path));

    resolver.clear_includes();
    EXPECT_FALSE(resolver.is_included(path));
}

// ============================================================================
// Import Tracking Tests (backward compatibility)
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

// ============================================================================
// Circular Include Detection Tests
// ============================================================================

TEST_F(FileResolverTest, WouldCreateCycleInitially) {
    FileResolver resolver(test_dir_);

    // No include stack - should not create cycle
    EXPECT_FALSE(resolver.would_create_cycle(test_dir_ / "main.dsl"));
}

TEST_F(FileResolverTest, WouldCreateCycleWithStack) {
    FileResolver resolver(test_dir_);

    // Push a file onto the stack
    resolver.push_include_stack(test_dir_ / "main.dsl", 1);

    // Same file would create a cycle
    EXPECT_TRUE(resolver.would_create_cycle(test_dir_ / "main.dsl"));

    // Different file would not
    EXPECT_FALSE(resolver.would_create_cycle(test_dir_ / "lib.dsl"));
}

TEST_F(FileResolverTest, WouldCreateCycleDeepStack) {
    FileResolver resolver(test_dir_);

    // Simulate: main.dsl includes lib.dsl includes nested.dsl
    resolver.push_include_stack(test_dir_ / "main.dsl", 1);
    resolver.push_include_stack(test_dir_ / "lib.dsl", 2);
    resolver.push_include_stack(test_dir_ / "subdir" / "nested.dsl", 3);

    // Any file on the stack would create a cycle
    EXPECT_TRUE(resolver.would_create_cycle(test_dir_ / "main.dsl"));
    EXPECT_TRUE(resolver.would_create_cycle(test_dir_ / "lib.dsl"));
    EXPECT_TRUE(resolver.would_create_cycle(test_dir_ / "subdir" / "nested.dsl"));

    // New file would not
    EXPECT_FALSE(resolver.would_create_cycle(test_dir_ / "stdlib" / "common.dsl"));
}

TEST_F(FileResolverTest, IncludeStackPushPop) {
    FileResolver resolver(test_dir_);

    EXPECT_TRUE(resolver.include_stack().empty());

    resolver.push_include_stack(test_dir_ / "main.dsl", 1);
    EXPECT_EQ(resolver.include_stack().size(), 1);

    resolver.push_include_stack(test_dir_ / "lib.dsl", 5);
    EXPECT_EQ(resolver.include_stack().size(), 2);

    resolver.pop_include_stack();
    EXPECT_EQ(resolver.include_stack().size(), 1);

    resolver.pop_include_stack();
    EXPECT_TRUE(resolver.include_stack().empty());
}

// ============================================================================
// File Caching Tests
// ============================================================================

TEST_F(FileResolverTest, CacheFile) {
    FileResolver resolver(test_dir_);
    auto path = test_dir_ / "main.dsl";
    std::string contents = "cached content";

    EXPECT_EQ(resolver.get_cached(path), nullptr);

    resolver.cache_file(path, contents);

    auto cached = resolver.get_cached(path);
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(*cached, contents);
}

TEST_F(FileResolverTest, ClearCache) {
    FileResolver resolver(test_dir_);
    auto path = test_dir_ / "main.dsl";

    resolver.cache_file(path, "content");
    EXPECT_NE(resolver.get_cached(path), nullptr);

    resolver.clear_cache();
    EXPECT_EQ(resolver.get_cached(path), nullptr);
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

// ============================================================================
// FileError Formatting Tests
// ============================================================================

TEST_F(FileResolverTest, FormatIncludeChainEmpty) {
    std::vector<std::pair<std::filesystem::path, std::uint32_t>> chain;
    auto result = FileError::format_include_chain(chain);
    EXPECT_TRUE(result.empty());
}

TEST_F(FileResolverTest, FormatIncludeChainSingle) {
    std::vector<std::pair<std::filesystem::path, std::uint32_t>> chain;
    chain.emplace_back("main.dsl", 5);

    auto result = FileError::format_include_chain(chain);
    EXPECT_TRUE(result.find("main.dsl") != std::string::npos);
    EXPECT_TRUE(result.find("5") != std::string::npos);
}

TEST_F(FileResolverTest, FormatIncludeChainMultiple) {
    std::vector<std::pair<std::filesystem::path, std::uint32_t>> chain;
    chain.emplace_back("main.dsl", 5);
    chain.emplace_back("lib.dsl", 10);
    chain.emplace_back("nested.dsl", 15);

    auto result = FileError::format_include_chain(chain);
    EXPECT_TRUE(result.find("main.dsl") != std::string::npos);
    EXPECT_TRUE(result.find("lib.dsl") != std::string::npos);
    EXPECT_TRUE(result.find("nested.dsl") != std::string::npos);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(FileResolverTest, ReadEmptyFile) {
    // Create an empty file
    std::filesystem::path empty_file = test_dir_ / "empty.dsl";
    create_file(empty_file, "");

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(empty_file);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(FileResolverTest, ReadFileWithOnlyWhitespace) {
    // Create a file with only whitespace
    std::filesystem::path ws_file = test_dir_ / "whitespace.dsl";
    create_file(ws_file, "   \n\t\n   \n");

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(ws_file);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "   \n\t\n   \n");
}

TEST_F(FileResolverTest, ReadFileWithOnlyComments) {
    // Create a file with only comments
    std::filesystem::path comment_file = test_dir_ / "comments.dsl";
    create_file(comment_file, "# This is a comment\n# Another comment\n");

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(comment_file);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->find("# This is a comment") != std::string::npos);
}

TEST_F(FileResolverTest, ReadFileWithoutTrailingNewline) {
    // Create a file without trailing newline
    std::filesystem::path no_newline = test_dir_ / "no_newline.dsl";
    create_file(no_newline, "dot NoNewline:\n    state:\n        x: 0");

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(no_newline);

    ASSERT_TRUE(result.has_value());
    // Verify file content is preserved exactly
    EXPECT_EQ(result->back(), '0');  // No trailing newline
}

TEST_F(FileResolverTest, ReadFileWithVeryLongLine) {
    // Create a file with a very long line (10000 characters)
    std::filesystem::path long_line = test_dir_ / "long_line.dsl";
    std::string long_content = "dot LongLine:\n    state:\n        x: \"";
    long_content += std::string(10000, 'a');
    long_content += "\"\n";
    create_file(long_line, long_content);

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(long_line);

    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->size(), 10000U);
}

TEST_F(FileResolverTest, ReadBinaryFileDetected) {
    // Create a file with null bytes (binary content)
    std::filesystem::path binary_file = test_dir_ / "binary.dsl";
    {
        std::ofstream file(binary_file, std::ios::binary);
        file << "dot Binary:\n";
        file << '\0';  // Null byte indicates binary
        file << "state:\n";
    }

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(binary_file);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ExitCode::IoError);
    EXPECT_TRUE(result.error().message.find("Binary file") != std::string::npos);
}

TEST_F(FileResolverTest, ReadInvalidUtf8Detected) {
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

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(invalid_utf8);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ExitCode::IoError);
    EXPECT_TRUE(result.error().message.find("UTF-8") != std::string::npos);
}

TEST_F(FileResolverTest, ReadValidUtf8WithUnicode) {
    // Create a file with valid UTF-8 unicode characters
    std::filesystem::path unicode_file = test_dir_ / "unicode.dsl";
    // Using various valid UTF-8 sequences
    create_file(unicode_file, "# Comment with unicode: Hello World!\n"
                              "# Greek: \xCE\xB1\xCE\xB2\xCE\xB3\n"  // alpha beta gamma
                              "# Japanese: \xE3\x81\x82\n"           // hiragana 'a'
                              "dot Unicode:\n"
                              "    state:\n"
                              "        x: 0\n");

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(unicode_file);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->find("Unicode") != std::string::npos);
}

TEST_F(FileResolverTest, ReadFileWithBOM) {
    // Create a file with UTF-8 BOM (Byte Order Mark)
    std::filesystem::path bom_file = test_dir_ / "bom.dsl";
    {
        std::ofstream file(bom_file, std::ios::binary);
        // UTF-8 BOM
        file << static_cast<char>(0xEF);
        file << static_cast<char>(0xBB);
        file << static_cast<char>(0xBF);
        file << "dot WithBOM:\n    state:\n        x: 0\n";
    }

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(bom_file);

    // UTF-8 BOM is valid UTF-8, so this should succeed
    ASSERT_TRUE(result.has_value());
}

TEST_F(FileResolverTest, ReadFileTruncatedUtf8) {
    // Create a file with truncated UTF-8 sequence at the end
    std::filesystem::path truncated_file = test_dir_ / "truncated.dsl";
    {
        std::ofstream file(truncated_file, std::ios::binary);
        file << "dot Truncated:\n    state:\n        x: 0\n";
        // Write start of 3-byte sequence without completing it
        file << static_cast<char>(0xE3);
    }

    FileResolver resolver(test_dir_);
    auto result = resolver.read_file(truncated_file);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ExitCode::IoError);
    EXPECT_TRUE(result.error().message.find("UTF-8") != std::string::npos);
}
