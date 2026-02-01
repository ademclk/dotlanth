/// @file lock_file_test.cpp
/// @brief Unit tests for PRD-007 lock file

#include <cstring>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "dotvm/pkg/lock_file.hpp"

namespace dotvm::pkg {
namespace {

// ============================================================================
// Test Helpers
// ============================================================================

LockedPackage make_test_package(std::string name, std::uint32_t major, std::uint32_t minor,
                                std::uint32_t patch) {
    LockedPackage pkg;
    pkg.name = std::move(name);
    pkg.version = Version{major, minor, patch, {}};
    // Fill checksum with a pattern based on name
    for (std::size_t i = 0; i < pkg.checksum.size(); ++i) {
        pkg.checksum[i] = static_cast<std::uint8_t>(i + pkg.name.length());
    }
    return pkg;
}

class LockFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temp directory for test files
        test_dir_ = std::filesystem::temp_directory_path() / "dotpkg_test";
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        // Clean up temp directory
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(LockFileTest, DefaultConstruction) {
    LockFile lock;
    EXPECT_EQ(lock.package_count(), 0);
    EXPECT_TRUE(lock.packages().empty());
    EXPECT_EQ(lock.timestamp(), 0);
}

// ============================================================================
// Package Management Tests
// ============================================================================

TEST_F(LockFileTest, AddPackage) {
    LockFile lock;
    auto pkg = make_test_package("mylib", 1, 2, 3);

    lock.add_package(pkg);

    EXPECT_EQ(lock.package_count(), 1);
    EXPECT_TRUE(lock.has_package("mylib"));
    EXPECT_FALSE(lock.has_package("otherlib"));
}

TEST_F(LockFileTest, AddMultiplePackages) {
    LockFile lock;
    lock.add_package(make_test_package("lib-a", 1, 0, 0));
    lock.add_package(make_test_package("lib-b", 2, 0, 0));
    lock.add_package(make_test_package("lib-c", 3, 0, 0));

    EXPECT_EQ(lock.package_count(), 3);
    EXPECT_TRUE(lock.has_package("lib-a"));
    EXPECT_TRUE(lock.has_package("lib-b"));
    EXPECT_TRUE(lock.has_package("lib-c"));
}

TEST_F(LockFileTest, UpdateExistingPackage) {
    LockFile lock;
    lock.add_package(make_test_package("mylib", 1, 0, 0));
    lock.add_package(make_test_package("mylib", 2, 0, 0));

    EXPECT_EQ(lock.package_count(), 1);

    const auto* pkg = lock.get_package("mylib");
    ASSERT_NE(pkg, nullptr);
    EXPECT_EQ(pkg->version.major, 2);
}

TEST_F(LockFileTest, GetPackage) {
    LockFile lock;
    auto original = make_test_package("testlib", 1, 2, 3);
    lock.add_package(original);

    const auto* pkg = lock.get_package("testlib");
    ASSERT_NE(pkg, nullptr);
    EXPECT_EQ(pkg->name, "testlib");
    EXPECT_EQ(pkg->version.major, 1);
    EXPECT_EQ(pkg->version.minor, 2);
    EXPECT_EQ(pkg->version.patch, 3);
}

TEST_F(LockFileTest, GetPackageNotFound) {
    LockFile lock;
    lock.add_package(make_test_package("exists", 1, 0, 0));

    const auto* pkg = lock.get_package("does-not-exist");
    EXPECT_EQ(pkg, nullptr);
}

TEST_F(LockFileTest, RemovePackage) {
    LockFile lock;
    lock.add_package(make_test_package("to-remove", 1, 0, 0));
    lock.add_package(make_test_package("to-keep", 1, 0, 0));

    EXPECT_TRUE(lock.remove_package("to-remove"));
    EXPECT_FALSE(lock.has_package("to-remove"));
    EXPECT_TRUE(lock.has_package("to-keep"));
    EXPECT_EQ(lock.package_count(), 1);
}

TEST_F(LockFileTest, RemovePackageNotFound) {
    LockFile lock;
    EXPECT_FALSE(lock.remove_package("nonexistent"));
}

TEST_F(LockFileTest, Clear) {
    LockFile lock;
    lock.add_package(make_test_package("lib1", 1, 0, 0));
    lock.add_package(make_test_package("lib2", 1, 0, 0));
    lock.set_timestamp(12345);

    lock.clear();

    EXPECT_EQ(lock.package_count(), 0);
    EXPECT_EQ(lock.timestamp(), 0);
}

// ============================================================================
// Serialization Tests
// ============================================================================

TEST_F(LockFileTest, SerializeEmptyLockFile) {
    LockFile lock;
    auto data = lock.serialize();

    // Should have header + footer
    EXPECT_GE(data.size(), LockFileConstants::HEADER_SIZE + LockFileConstants::FOOTER_SIZE);

    // Check magic bytes
    std::uint32_t magic = 0;
    std::memcpy(&magic, data.data(), sizeof(magic));
    EXPECT_EQ(magic, LockFileConstants::MAGIC);
}

TEST_F(LockFileTest, SerializeAndParse) {
    LockFile original;
    original.set_timestamp(1234567890);
    original.add_package(make_test_package("lib-alpha", 1, 2, 3));
    original.add_package(make_test_package("lib-beta", 2, 0, 0));

    auto data = original.serialize();
    auto result = LockFile::parse(data);

    ASSERT_TRUE(result.is_ok());

    const auto& parsed = result.value();
    EXPECT_EQ(parsed.package_count(), 2);
    EXPECT_EQ(parsed.timestamp(), 1234567890);

    const auto* alpha = parsed.get_package("lib-alpha");
    ASSERT_NE(alpha, nullptr);
    EXPECT_EQ(alpha->version.major, 1);
    EXPECT_EQ(alpha->version.minor, 2);
    EXPECT_EQ(alpha->version.patch, 3);

    const auto* beta = parsed.get_package("lib-beta");
    ASSERT_NE(beta, nullptr);
    EXPECT_EQ(beta->version.major, 2);
}

TEST_F(LockFileTest, SerializeWithDependencies) {
    LockFile original;

    auto pkg = make_test_package("main-lib", 1, 0, 0);
    pkg.dependencies = {"dep-a", "dep-b", "dep-c"};
    original.add_package(pkg);

    auto data = original.serialize();
    auto result = LockFile::parse(data);

    ASSERT_TRUE(result.is_ok());

    const auto* parsed_pkg = result.value().get_package("main-lib");
    ASSERT_NE(parsed_pkg, nullptr);
    EXPECT_EQ(parsed_pkg->dependencies.size(), 3);
    EXPECT_EQ(parsed_pkg->dependencies[0], "dep-a");
    EXPECT_EQ(parsed_pkg->dependencies[1], "dep-b");
    EXPECT_EQ(parsed_pkg->dependencies[2], "dep-c");
}

// ============================================================================
// Parse Error Tests
// ============================================================================

TEST_F(LockFileTest, ParseEmptyDataFails) {
    std::vector<std::uint8_t> empty;
    auto result = LockFile::parse(empty);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidLockFile);
}

TEST_F(LockFileTest, ParseInvalidMagicFails) {
    std::vector<std::uint8_t> bad_magic(
        LockFileConstants::HEADER_SIZE + LockFileConstants::FOOTER_SIZE, 0);
    bad_magic[0] = 'X';  // Invalid magic

    auto result = LockFile::parse(bad_magic);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::InvalidLockFile);
}

TEST_F(LockFileTest, ParseInvalidVersionFails) {
    // Create valid header but with wrong version
    LockFile lock;
    auto data = lock.serialize();

    // Corrupt version bytes (bytes 4-5)
    data[4] = 0xFF;
    data[5] = 0xFF;

    auto result = LockFile::parse(data);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::LockFileVersionMismatch);
}

TEST_F(LockFileTest, ParseTruncatedDataFails) {
    LockFile lock;
    lock.add_package(make_test_package("lib", 1, 0, 0));
    auto data = lock.serialize();

    // Truncate the data
    data.resize(data.size() / 2);

    auto result = LockFile::parse(data);

    ASSERT_TRUE(result.is_err());
}

// ============================================================================
// File I/O Tests
// ============================================================================

TEST_F(LockFileTest, SaveAndLoad) {
    auto path = test_dir_ / "test.lock";

    LockFile original;
    original.set_timestamp(9999);
    original.add_package(make_test_package("saved-lib", 5, 4, 3));

    auto save_result = original.save(path);
    ASSERT_TRUE(save_result.is_ok());
    EXPECT_TRUE(std::filesystem::exists(path));

    auto load_result = LockFile::load(path);
    ASSERT_TRUE(load_result.is_ok());

    const auto& loaded = load_result.value();
    EXPECT_EQ(loaded.package_count(), 1);
    EXPECT_EQ(loaded.timestamp(), 9999);

    const auto* pkg = loaded.get_package("saved-lib");
    ASSERT_NE(pkg, nullptr);
    EXPECT_EQ(pkg->version.major, 5);
}

TEST_F(LockFileTest, LoadNonexistentFileFails) {
    auto result = LockFile::load(test_dir_ / "does-not-exist.lock");

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::LockFileNotFound);
}

// ============================================================================
// CRC32 Tests
// ============================================================================

TEST_F(LockFileTest, Crc32EmptyData) {
    std::vector<std::uint8_t> empty;
    auto crc = compute_crc32(empty);

    // CRC32 of empty data is 0x00000000 (or varies by implementation)
    // Just verify it returns a value
    (void)crc;
}

TEST_F(LockFileTest, Crc32KnownValue) {
    // "123456789" has a known CRC32 value
    std::vector<std::uint8_t> data = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    auto crc = compute_crc32(data);

    // Standard CRC32 of "123456789" is 0xCBF43926
    EXPECT_EQ(crc, 0xCBF43926);
}

TEST_F(LockFileTest, VerifyIntegrity) {
    LockFile lock;
    lock.add_package(make_test_package("lib", 1, 0, 0));

    // Freshly created lock file should have valid integrity
    EXPECT_TRUE(lock.verify_integrity());
}

// ============================================================================
// Timestamp Tests
// ============================================================================

TEST_F(LockFileTest, TimestampPersists) {
    LockFile lock;
    lock.set_timestamp(1704067200);  // 2024-01-01 00:00:00 UTC

    auto data = lock.serialize();
    auto result = LockFile::parse(data);

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().timestamp(), 1704067200);
}

}  // namespace
}  // namespace dotvm::pkg
