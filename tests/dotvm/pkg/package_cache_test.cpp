/// @file package_cache_test.cpp
/// @brief Unit tests for PRD-007 package cache

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>

#include "dotvm/pkg/package_cache.hpp"

namespace dotvm::pkg {
namespace {

class PackageCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temp directory for test files
        test_dir_ = std::filesystem::temp_directory_path() / "dotpkg_cache_test";
        std::filesystem::create_directories(test_dir_);

        config_.root_dir = test_dir_;
    }

    void TearDown() override {
        // Clean up temp directory
        std::filesystem::remove_all(test_dir_);
    }

    // Create a test package directory
    std::filesystem::path create_test_package_dir(std::string_view name, std::string_view version) {
        auto pkg_dir = test_dir_ / "source" / name;
        std::filesystem::create_directories(pkg_dir);

        // Create a simple dotpkg.json
        std::ofstream manifest(pkg_dir / "dotpkg.json");
        manifest << R"({"name": ")" << name << R"(", "version": ")" << version << R"("})";

        // Create a dummy source file
        std::ofstream src(pkg_dir / "lib.cpp");
        src << "// Test library code\n";

        return pkg_dir;
    }

    std::filesystem::path test_dir_;
    CacheConfig config_;
};

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(PackageCacheTest, DefaultConfigHasHomeDir) {
    auto config = CacheConfig::defaults();
    EXPECT_FALSE(config.root_dir.empty());
}

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(PackageCacheTest, InitializeCreatesDirectory) {
    PackageCache cache(config_);
    auto result = cache.initialize();

    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(std::filesystem::exists(test_dir_ / "cache" / "packages"));
}

TEST_F(PackageCacheTest, InitializeTwiceSucceeds) {
    PackageCache cache(config_);
    ASSERT_TRUE(cache.initialize().is_ok());
    ASSERT_TRUE(cache.initialize().is_ok());
}

TEST_F(PackageCacheTest, IsValidAfterInit) {
    PackageCache cache(config_);
    EXPECT_FALSE(cache.is_valid());

    (void)cache.initialize();
    EXPECT_TRUE(cache.is_valid());
}

// ============================================================================
// Package Path Tests
// ============================================================================

TEST_F(PackageCacheTest, PackagePathFormat) {
    PackageCache cache(config_);
    auto path = cache.package_path("mylib", Version{1, 2, 3, {}});

    EXPECT_EQ(path, test_dir_ / "cache" / "packages" / "mylib" / "1.2.3");
}

// ============================================================================
// Add Package Tests
// ============================================================================

TEST_F(PackageCacheTest, AddFromDirectory) {
    PackageCache cache(config_);
    (void)cache.initialize();

    auto src_dir = create_test_package_dir("testlib", "1.0.0");

    PackageManifest manifest;
    manifest.name = "testlib";
    manifest.version = Version{1, 0, 0, {}};

    auto result = cache.add_from_directory(src_dir, manifest);
    ASSERT_TRUE(result.is_ok());

    const auto& cached = result.value();
    EXPECT_EQ(cached.name, "testlib");
    EXPECT_EQ(cached.version.major, 1);
    EXPECT_GT(cached.size, 0);
}

TEST_F(PackageCacheTest, HasPackageAfterAdd) {
    PackageCache cache(config_);
    (void)cache.initialize();

    auto src_dir = create_test_package_dir("mylib", "2.0.0");

    PackageManifest manifest;
    manifest.name = "mylib";
    manifest.version = Version{2, 0, 0, {}};

    (void)cache.add_from_directory(src_dir, manifest);

    EXPECT_TRUE(cache.has("mylib", Version{2, 0, 0, {}}));
    EXPECT_FALSE(cache.has("mylib", Version{1, 0, 0, {}}));
    EXPECT_FALSE(cache.has("otherlib", Version{2, 0, 0, {}}));
}

TEST_F(PackageCacheTest, GetPackageAfterAdd) {
    PackageCache cache(config_);
    (void)cache.initialize();

    auto src_dir = create_test_package_dir("gettest", "3.1.4");

    PackageManifest manifest;
    manifest.name = "gettest";
    manifest.version = Version{3, 1, 4, {}};

    (void)cache.add_from_directory(src_dir, manifest);

    auto pkg = cache.get("gettest", Version{3, 1, 4, {}});
    ASSERT_TRUE(pkg.has_value());
    EXPECT_EQ(pkg->name, "gettest");
    EXPECT_EQ(pkg->version.major, 3);
    EXPECT_EQ(pkg->version.minor, 1);
    EXPECT_EQ(pkg->version.patch, 4);
}

// ============================================================================
// Remove Package Tests
// ============================================================================

TEST_F(PackageCacheTest, RemovePackage) {
    PackageCache cache(config_);
    (void)cache.initialize();

    auto src_dir = create_test_package_dir("toremove", "1.0.0");

    PackageManifest manifest;
    manifest.name = "toremove";
    manifest.version = Version{1, 0, 0, {}};

    (void)cache.add_from_directory(src_dir, manifest);
    ASSERT_TRUE(cache.has("toremove", Version{1, 0, 0, {}}));

    auto result = cache.remove("toremove", Version{1, 0, 0, {}});
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value());
    EXPECT_FALSE(cache.has("toremove", Version{1, 0, 0, {}}));
}

TEST_F(PackageCacheTest, RemoveNonexistentPackage) {
    PackageCache cache(config_);
    (void)cache.initialize();

    auto result = cache.remove("nonexistent", Version{1, 0, 0, {}});
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(result.value());
}

// ============================================================================
// List Tests
// ============================================================================

TEST_F(PackageCacheTest, ListVersions) {
    PackageCache cache(config_);
    (void)cache.initialize();

    // Add multiple versions
    std::array<std::uint32_t, 3> version_nums = {1, 2, 3};
    for (std::uint32_t v : version_nums) {
        auto src_dir = create_test_package_dir("multiversion", std::to_string(v) + ".0.0");
        PackageManifest manifest;
        manifest.name = "multiversion";
        manifest.version = Version{v, 0, 0, {}};
        (void)cache.add_from_directory(src_dir, manifest);
    }

    auto versions = cache.list_versions("multiversion");
    EXPECT_EQ(versions.size(), 3);
}

TEST_F(PackageCacheTest, ListVersionsEmptyForUnknown) {
    PackageCache cache(config_);
    (void)cache.initialize();

    auto versions = cache.list_versions("unknown");
    EXPECT_TRUE(versions.empty());
}

TEST_F(PackageCacheTest, PackageCount) {
    PackageCache cache(config_);
    (void)cache.initialize();

    EXPECT_EQ(cache.package_count(), 0);

    auto src1 = create_test_package_dir("pkg1", "1.0.0");
    PackageManifest m1;
    m1.name = "pkg1";
    m1.version = Version{1, 0, 0, {}};
    (void)cache.add_from_directory(src1, m1);

    auto src2 = create_test_package_dir("pkg2", "1.0.0");
    PackageManifest m2;
    m2.name = "pkg2";
    m2.version = Version{1, 0, 0, {}};
    (void)cache.add_from_directory(src2, m2);

    EXPECT_EQ(cache.package_count(), 2);
}

// ============================================================================
// Clear Tests
// ============================================================================

TEST_F(PackageCacheTest, Clear) {
    PackageCache cache(config_);
    (void)cache.initialize();

    auto src_dir = create_test_package_dir("toclear", "1.0.0");
    PackageManifest manifest;
    manifest.name = "toclear";
    manifest.version = Version{1, 0, 0, {}};
    (void)cache.add_from_directory(src_dir, manifest);

    ASSERT_EQ(cache.package_count(), 1);

    auto result = cache.clear();
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(cache.package_count(), 0);
}

}  // namespace
}  // namespace dotvm::pkg
