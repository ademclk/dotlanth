/// @file sandbox_test.cpp
/// @brief Unit tests for FilesystemSandbox

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "dotvm/core/io/filesystem_sandbox.hpp"

namespace dotvm::test {
namespace {

namespace fs = std::filesystem;
using namespace dotvm::core::io;

// ============================================================================
// Test Fixture
// ============================================================================

class FilesystemSandboxTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test directory
        test_dir_ = fs::temp_directory_path() / "dotvm_sandbox_test";
        fs::create_directories(test_dir_);

        // Create subdirectories
        fs::create_directories(test_dir_ / "allowed");
        fs::create_directories(test_dir_ / "readonly");
        fs::create_directories(test_dir_ / "forbidden");

        // Create test files
        std::ofstream(test_dir_ / "allowed" / "test.txt") << "allowed content";
        std::ofstream(test_dir_ / "readonly" / "readonly.txt") << "readonly content";
        std::ofstream(test_dir_ / "forbidden" / "secret.txt") << "secret content";
    }

    void TearDown() override { fs::remove_all(test_dir_); }

    fs::path test_dir_;
};

// ============================================================================
// Path Validation Tests
// ============================================================================

TEST_F(FilesystemSandboxTest, AllowDirectory) {
    FilesystemSandbox sandbox;
    auto err = sandbox.allow_directory((test_dir_ / "allowed").string());
    EXPECT_EQ(err, IoError::Success);
    EXPECT_TRUE(sandbox.has_allowlist());
}

TEST_F(FilesystemSandboxTest, AllowReadOnly) {
    FilesystemSandbox sandbox;
    auto err = sandbox.allow_read_only((test_dir_ / "readonly").string());
    EXPECT_EQ(err, IoError::Success);
}

TEST_F(FilesystemSandboxTest, AllowRelativePathFails) {
    FilesystemSandbox sandbox;
    auto err = sandbox.allow_directory("relative/path");
    EXPECT_EQ(err, IoError::InvalidPath);
}

TEST_F(FilesystemSandboxTest, EmptySandboxDeniesAll) {
    FilesystemSandbox sandbox;
    auto result = sandbox.validate_path((test_dir_ / "allowed" / "test.txt").string());
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IoError::SandboxDisabled);
}

TEST_F(FilesystemSandboxTest, AllowedPathValidates) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto result = sandbox.validate_path((test_dir_ / "allowed" / "test.txt").string());
    EXPECT_TRUE(result.has_value());
}

TEST_F(FilesystemSandboxTest, ForbiddenPathDenied) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto result = sandbox.validate_path((test_dir_ / "forbidden" / "secret.txt").string());
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IoError::PathDenied);
}

TEST_F(FilesystemSandboxTest, ReadOnlyPathDeniesWrite) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_read_only((test_dir_ / "readonly").string()), IoError::Success);

    // Read should work
    auto read_result =
        sandbox.validate_path((test_dir_ / "readonly" / "readonly.txt").string(), false);
    EXPECT_TRUE(read_result.has_value());

    // Write should fail
    auto write_result =
        sandbox.validate_path((test_dir_ / "readonly" / "readonly.txt").string(), true);
    EXPECT_FALSE(write_result.has_value());
    EXPECT_EQ(write_result.error(), IoError::PathDenied);
}

TEST_F(FilesystemSandboxTest, IsAllowed) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    EXPECT_TRUE(sandbox.is_allowed((test_dir_ / "allowed" / "test.txt").string()));
    EXPECT_FALSE(sandbox.is_allowed((test_dir_ / "forbidden" / "secret.txt").string()));
}

// ============================================================================
// File Operations Tests
// ============================================================================

TEST_F(FilesystemSandboxTest, ReadFile) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto result = sandbox.read_file((test_dir_ / "allowed" / "test.txt").string());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "allowed content");
}

TEST_F(FilesystemSandboxTest, ReadFileForbidden) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto result = sandbox.read_file((test_dir_ / "forbidden" / "secret.txt").string());
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IoError::PathDenied);
}

TEST_F(FilesystemSandboxTest, ReadFileNotFound) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto result = sandbox.read_file((test_dir_ / "allowed" / "nonexistent.txt").string());
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IoError::NotFound);
}

TEST_F(FilesystemSandboxTest, WriteFile) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto path = (test_dir_ / "allowed" / "new_file.txt").string();
    auto err = sandbox.write_file(path, "new content");
    EXPECT_EQ(err, IoError::Success);

    auto result = sandbox.read_file(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "new content");
}

TEST_F(FilesystemSandboxTest, WriteFileReadOnly) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_read_only((test_dir_ / "readonly").string()), IoError::Success);

    auto err = sandbox.write_file((test_dir_ / "readonly" / "new.txt").string(), "content");
    EXPECT_EQ(err, IoError::PathDenied);
}

TEST_F(FilesystemSandboxTest, AppendFile) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto path = (test_dir_ / "allowed" / "test.txt").string();
    auto err = sandbox.append_file(path, " appended");
    EXPECT_EQ(err, IoError::Success);

    auto result = sandbox.read_file(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "allowed content appended");
}

TEST_F(FilesystemSandboxTest, FileExists) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto exists = sandbox.file_exists((test_dir_ / "allowed" / "test.txt").string());
    ASSERT_TRUE(exists.has_value());
    EXPECT_TRUE(*exists);

    auto not_exists = sandbox.file_exists((test_dir_ / "allowed" / "nope.txt").string());
    ASSERT_TRUE(not_exists.has_value());
    EXPECT_FALSE(*not_exists);
}

TEST_F(FilesystemSandboxTest, FileExistsForbidden) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    // Forbidden paths return false (file doesn't exist from sandbox perspective)
    auto result = sandbox.file_exists((test_dir_ / "forbidden" / "secret.txt").string());
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(*result);
}

TEST_F(FilesystemSandboxTest, DeleteFile) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    // Create a file to delete
    auto path = (test_dir_ / "allowed" / "to_delete.txt").string();
    ASSERT_EQ(sandbox.write_file(path, "delete me"), IoError::Success);

    auto err = sandbox.delete_file(path);
    EXPECT_EQ(err, IoError::Success);

    auto exists = sandbox.file_exists(path);
    ASSERT_TRUE(exists.has_value());
    EXPECT_FALSE(*exists);
}

TEST_F(FilesystemSandboxTest, DeleteFileNotFound) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto err = sandbox.delete_file((test_dir_ / "allowed" / "nonexistent.txt").string());
    EXPECT_EQ(err, IoError::NotFound);
}

// ============================================================================
// Directory Operations Tests
// ============================================================================

TEST_F(FilesystemSandboxTest, CreateDirectory) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto path = (test_dir_ / "allowed" / "new_dir").string();
    auto err = sandbox.create_directory(path);
    EXPECT_EQ(err, IoError::Success);

    EXPECT_TRUE(fs::is_directory(path));
}

TEST_F(FilesystemSandboxTest, CreateDirectoryRecursive) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto path = (test_dir_ / "allowed" / "a" / "b" / "c").string();
    auto err = sandbox.create_directory(path, true);
    EXPECT_EQ(err, IoError::Success);

    EXPECT_TRUE(fs::is_directory(path));
}

TEST_F(FilesystemSandboxTest, ListDirectory) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    // Add more files
    std::ofstream(test_dir_ / "allowed" / "file1.txt") << "1";
    std::ofstream(test_dir_ / "allowed" / "file2.txt") << "2";

    auto result = sandbox.list_directory((test_dir_ / "allowed").string());
    ASSERT_TRUE(result.has_value());

    // Should have original test.txt plus file1.txt and file2.txt
    EXPECT_GE(result->size(), 3u);
}

TEST_F(FilesystemSandboxTest, ListDirectoryNotFound) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto result = sandbox.list_directory((test_dir_ / "allowed" / "nonexistent").string());
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IoError::NotFound);
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(FilesystemSandboxTest, MaxFileSize) {
    FilesystemSandbox sandbox(FilesystemSandbox::Config{.max_file_size = 100});
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    std::string large_content(200, 'x');
    auto err = sandbox.write_file((test_dir_ / "allowed" / "large.txt").string(), large_content);
    EXPECT_EQ(err, IoError::TooLarge);
}

TEST_F(FilesystemSandboxTest, HiddenFilesBlocked) {
    FilesystemSandbox sandbox(FilesystemSandbox::Config{.allow_hidden = false});
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    // Create a hidden file
    std::ofstream(test_dir_ / "allowed" / ".hidden") << "secret";

    auto result = sandbox.read_file((test_dir_ / "allowed" / ".hidden").string());
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IoError::PathDenied);
}

TEST_F(FilesystemSandboxTest, HiddenFilesAllowed) {
    FilesystemSandbox sandbox(FilesystemSandbox::Config{.allow_hidden = true});
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    // Create a hidden file
    std::ofstream(test_dir_ / "allowed" / ".hidden") << "visible";

    auto result = sandbox.read_file((test_dir_ / "allowed" / ".hidden").string());
    EXPECT_TRUE(result.has_value());
}

// ============================================================================
// Security Tests
// ============================================================================

TEST_F(FilesystemSandboxTest, PathTraversalBlocked) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    // Try to escape using ../
    auto result =
        sandbox.read_file((test_dir_ / "allowed" / ".." / "forbidden" / "secret.txt").string());
    EXPECT_FALSE(result.has_value());
}

TEST_F(FilesystemSandboxTest, RelativePathBlocked) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);

    auto result = sandbox.read_file("./test.txt");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), IoError::InvalidPath);
}

TEST_F(FilesystemSandboxTest, ClearAllowlist) {
    FilesystemSandbox sandbox;
    ASSERT_EQ(sandbox.allow_directory((test_dir_ / "allowed").string()), IoError::Success);
    EXPECT_TRUE(sandbox.has_allowlist());

    sandbox.clear_allowlist();
    EXPECT_FALSE(sandbox.has_allowlist());

    // Should now deny everything
    auto result = sandbox.read_file((test_dir_ / "allowed" / "test.txt").string());
    EXPECT_FALSE(result.has_value());
}

}  // namespace
}  // namespace dotvm::test
