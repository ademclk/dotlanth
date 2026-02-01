/// @file package_registry_test.cpp
/// @brief Unit tests for PRD-007 package registry

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

#include "dotvm/pkg/package_registry.hpp"

namespace dotvm::pkg {
namespace {

class PackageRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "dotpkg_registry_test";
        std::filesystem::create_directories(test_dir_);
        registry_path_ = test_dir_ / "dotpkg_registry.json";
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    RegistryEntry make_entry(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) {
        RegistryEntry entry;
        entry.version = Version{major, minor, patch, {}};
        entry.installed_at = 1234567890;
        // Fill checksum with a pattern
        for (std::size_t i = 0; i < entry.checksum.size(); ++i) {
            entry.checksum[i] = static_cast<std::uint8_t>(i);
        }
        return entry;
    }

    std::filesystem::path test_dir_;
    std::filesystem::path registry_path_;
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(PackageRegistryTest, ConstructEmpty) {
    PackageRegistry registry(registry_path_);
    EXPECT_EQ(registry.count(), 0);
    EXPECT_FALSE(registry.exists());
}

// ============================================================================
// Package Management Tests
// ============================================================================

TEST_F(PackageRegistryTest, AddPackage) {
    PackageRegistry registry(registry_path_);

    registry.add("mylib", make_entry(1, 2, 3));

    EXPECT_EQ(registry.count(), 1);
    EXPECT_TRUE(registry.has("mylib"));
}

TEST_F(PackageRegistryTest, AddMultiplePackages) {
    PackageRegistry registry(registry_path_);

    registry.add("lib-a", make_entry(1, 0, 0));
    registry.add("lib-b", make_entry(2, 0, 0));
    registry.add("lib-c", make_entry(3, 0, 0));

    EXPECT_EQ(registry.count(), 3);
    EXPECT_TRUE(registry.has("lib-a"));
    EXPECT_TRUE(registry.has("lib-b"));
    EXPECT_TRUE(registry.has("lib-c"));
}

TEST_F(PackageRegistryTest, UpdatePackage) {
    PackageRegistry registry(registry_path_);

    registry.add("mylib", make_entry(1, 0, 0));
    registry.add("mylib", make_entry(2, 0, 0));

    EXPECT_EQ(registry.count(), 1);

    const auto* entry = registry.get("mylib");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->version.major, 2);
}

TEST_F(PackageRegistryTest, GetPackage) {
    PackageRegistry registry(registry_path_);

    registry.add("testlib", make_entry(1, 2, 3));

    const auto* entry = registry.get("testlib");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->version.major, 1);
    EXPECT_EQ(entry->version.minor, 2);
    EXPECT_EQ(entry->version.patch, 3);
}

TEST_F(PackageRegistryTest, GetNotFound) {
    PackageRegistry registry(registry_path_);

    const auto* entry = registry.get("nonexistent");
    EXPECT_EQ(entry, nullptr);
}

TEST_F(PackageRegistryTest, RemovePackage) {
    PackageRegistry registry(registry_path_);

    registry.add("to-remove", make_entry(1, 0, 0));
    ASSERT_TRUE(registry.has("to-remove"));

    EXPECT_TRUE(registry.remove("to-remove"));
    EXPECT_FALSE(registry.has("to-remove"));
}

TEST_F(PackageRegistryTest, RemoveNotFound) {
    PackageRegistry registry(registry_path_);
    EXPECT_FALSE(registry.remove("nonexistent"));
}

TEST_F(PackageRegistryTest, Clear) {
    PackageRegistry registry(registry_path_);

    registry.add("lib1", make_entry(1, 0, 0));
    registry.add("lib2", make_entry(2, 0, 0));
    ASSERT_EQ(registry.count(), 2);

    registry.clear();
    EXPECT_EQ(registry.count(), 0);
}

// ============================================================================
// Query Tests
// ============================================================================

TEST_F(PackageRegistryTest, PackageNames) {
    PackageRegistry registry(registry_path_);

    registry.add("alpha", make_entry(1, 0, 0));
    registry.add("beta", make_entry(1, 0, 0));
    registry.add("gamma", make_entry(1, 0, 0));

    auto names = registry.package_names();
    EXPECT_EQ(names.size(), 3);

    // Sort for deterministic comparison
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names[0], "alpha");
    EXPECT_EQ(names[1], "beta");
    EXPECT_EQ(names[2], "gamma");
}

TEST_F(PackageRegistryTest, DependentsOf) {
    PackageRegistry registry(registry_path_);

    auto entry_a = make_entry(1, 0, 0);
    entry_a.dependencies = {"shared"};
    registry.add("lib-a", entry_a);

    auto entry_b = make_entry(1, 0, 0);
    entry_b.dependencies = {"shared"};
    registry.add("lib-b", entry_b);

    auto entry_c = make_entry(1, 0, 0);
    entry_c.dependencies = {};
    registry.add("lib-c", entry_c);

    registry.add("shared", make_entry(1, 0, 0));

    auto dependents = registry.dependents_of("shared");
    EXPECT_EQ(dependents.size(), 2);

    std::sort(dependents.begin(), dependents.end());
    EXPECT_EQ(dependents[0], "lib-a");
    EXPECT_EQ(dependents[1], "lib-b");
}

// ============================================================================
// Persistence Tests
// ============================================================================

TEST_F(PackageRegistryTest, SaveAndLoad) {
    // Save
    {
        PackageRegistry registry(registry_path_);

        auto entry = make_entry(1, 2, 3);
        entry.dependencies = {"dep1", "dep2"};
        entry.install_path = "packages/testlib";
        registry.add("testlib", entry);

        auto result = registry.save();
        ASSERT_TRUE(result.is_ok());
    }

    EXPECT_TRUE(std::filesystem::exists(registry_path_));

    // Load
    {
        PackageRegistry registry(registry_path_);
        auto result = registry.load();
        ASSERT_TRUE(result.is_ok());

        EXPECT_EQ(registry.count(), 1);
        EXPECT_TRUE(registry.has("testlib"));

        const auto* entry = registry.get("testlib");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->version.major, 1);
        EXPECT_EQ(entry->version.minor, 2);
        EXPECT_EQ(entry->version.patch, 3);
        EXPECT_EQ(entry->dependencies.size(), 2);
        EXPECT_EQ(entry->dependencies[0], "dep1");
        EXPECT_EQ(entry->dependencies[1], "dep2");
    }
}

TEST_F(PackageRegistryTest, LoadNonexistent) {
    PackageRegistry registry(registry_path_);

    // Loading a non-existent file should succeed (empty registry)
    auto result = registry.load();
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(registry.count(), 0);
}

TEST_F(PackageRegistryTest, ExistsAfterSave) {
    PackageRegistry registry(registry_path_);
    EXPECT_FALSE(registry.exists());

    registry.add("lib", make_entry(1, 0, 0));
    (void)registry.save();

    EXPECT_TRUE(registry.exists());
}

}  // namespace
}  // namespace dotvm::pkg
