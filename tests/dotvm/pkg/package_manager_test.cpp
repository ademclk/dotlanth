/// @file package_manager_test.cpp
/// @brief Unit tests for PRD-007 package manager

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "dotvm/pkg/package_manager.hpp"

namespace dotvm::pkg {
namespace {

class PackageManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "dotpkg_manager_test";
        std::filesystem::create_directories(test_dir_);

        // Set up config
        config_.project_dir = test_dir_;
        config_.cache_config.root_dir = test_dir_ / ".dotpkg";
    }

    void TearDown() override { std::filesystem::remove_all(test_dir_); }

    // Create a test package directory with manifest
    std::filesystem::path create_test_package(std::string_view name, std::string_view version) {
        auto pkg_dir = test_dir_ / "packages" / name;
        std::filesystem::create_directories(pkg_dir);

        // Create manifest
        std::ofstream manifest(pkg_dir / "dotpkg.json");
        manifest << "{\n";
        manifest << "  \"name\": \"" << name << "\",\n";
        manifest << "  \"version\": \"" << version << "\"\n";
        manifest << "}\n";

        // Create a source file
        std::ofstream src(pkg_dir / "lib.cpp");
        src << "// Package: " << name << "\n";

        return pkg_dir;
    }

    // Create project manifest
    void create_project_manifest(std::string_view name, std::string_view version) {
        std::ofstream manifest(test_dir_ / "dotpkg.json");
        manifest << "{\n";
        manifest << "  \"name\": \"" << name << "\",\n";
        manifest << "  \"version\": \"" << version << "\"\n";
        manifest << "}\n";
    }

    std::filesystem::path test_dir_;
    PackageManagerConfig config_;
};

// ============================================================================
// Initialization Tests
// ============================================================================

TEST_F(PackageManagerTest, InitializeCreatesDirectories) {
    PackageManager pm(config_);
    auto result = pm.initialize();

    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(pm.is_initialized());
    EXPECT_TRUE(std::filesystem::exists(config_.cache_config.root_dir / "cache" / "packages"));
}

TEST_F(PackageManagerTest, InitializeTwiceSucceeds) {
    PackageManager pm(config_);
    ASSERT_TRUE(pm.initialize().is_ok());
    ASSERT_TRUE(pm.initialize().is_ok());
}

// ============================================================================
// Installation Tests
// ============================================================================

TEST_F(PackageManagerTest, InstallFromSource) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto pkg_dir = create_test_package("mylib", "1.0.0");
    PackageSource source{pkg_dir, false};

    auto result = pm.install_from_source(source);
    ASSERT_TRUE(result.is_ok());

    EXPECT_TRUE(pm.is_installed("mylib"));
}

TEST_F(PackageManagerTest, InstallCreatesLockFile) {
    config_.use_lock_file = true;
    PackageManager pm(config_);
    (void)pm.initialize();

    auto pkg_dir = create_test_package("locked", "2.0.0");
    PackageSource source{pkg_dir, false};

    (void)pm.install_from_source(source);

    EXPECT_TRUE(std::filesystem::exists(test_dir_ / "dotpkg.lock"));
}

TEST_F(PackageManagerTest, InstallCreatesRegistry) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto pkg_dir = create_test_package("registered", "3.0.0");
    PackageSource source{pkg_dir, false};

    (void)pm.install_from_source(source);

    EXPECT_TRUE(std::filesystem::exists(test_dir_ / "dotpkg_registry.json"));
}

// ============================================================================
// Uninstallation Tests
// ============================================================================

TEST_F(PackageManagerTest, UninstallRemovesPackage) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto pkg_dir = create_test_package("toremove", "1.0.0");
    PackageSource source{pkg_dir, false};

    (void)pm.install_from_source(source);
    ASSERT_TRUE(pm.is_installed("toremove"));

    auto result = pm.uninstall("toremove");
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(pm.is_installed("toremove"));
}

TEST_F(PackageManagerTest, UninstallNonexistentFails) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto result = pm.uninstall("nonexistent");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::PackageNotFound);
}

// ============================================================================
// Query Tests
// ============================================================================

TEST_F(PackageManagerTest, ListInstalledEmpty) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto installed = pm.list_installed();
    EXPECT_TRUE(installed.empty());
}

TEST_F(PackageManagerTest, ListInstalledReturnsPackages) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto pkg1 = create_test_package("list-a", "1.0.0");
    auto pkg2 = create_test_package("list-b", "2.0.0");

    (void)pm.install_from_source(PackageSource{pkg1, false});
    (void)pm.install_from_source(PackageSource{pkg2, false});

    auto installed = pm.list_installed();
    EXPECT_EQ(installed.size(), 2);
}

TEST_F(PackageManagerTest, GetInstalled) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto pkg_dir = create_test_package("getme", "4.5.6");
    (void)pm.install_from_source(PackageSource{pkg_dir, false});

    auto pkg = pm.get_installed("getme");
    ASSERT_TRUE(pkg.has_value());
    EXPECT_EQ(pkg->name, "getme");
    EXPECT_EQ(pkg->version.major, 4);
    EXPECT_EQ(pkg->version.minor, 5);
    EXPECT_EQ(pkg->version.patch, 6);
}

TEST_F(PackageManagerTest, GetInstalledNotFound) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto pkg = pm.get_installed("notinstalled");
    EXPECT_FALSE(pkg.has_value());
}

// ============================================================================
// Manifest Tests
// ============================================================================

TEST_F(PackageManagerTest, LoadManifest) {
    create_project_manifest("myproject", "0.1.0");

    PackageManager pm(config_);
    (void)pm.initialize();

    ASSERT_NE(pm.manifest(), nullptr);
    EXPECT_EQ(pm.manifest()->name, "myproject");
    EXPECT_EQ(pm.manifest()->version.to_string(), "0.1.0");
}

TEST_F(PackageManagerTest, NoManifest) {
    PackageManager pm(config_);
    (void)pm.initialize();

    EXPECT_EQ(pm.manifest(), nullptr);
}

// ============================================================================
// Dependency Tree Tests
// ============================================================================

TEST_F(PackageManagerTest, DependencyTreeEmpty) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto tree = pm.dependency_tree();
    EXPECT_TRUE(tree.empty());
}

TEST_F(PackageManagerTest, DependencyTreeSinglePackage) {
    PackageManager pm(config_);
    (void)pm.initialize();

    auto pkg_dir = create_test_package("standalone", "1.0.0");
    (void)pm.install_from_source(PackageSource{pkg_dir, false});

    auto tree = pm.dependency_tree();
    ASSERT_EQ(tree.size(), 1);
    EXPECT_EQ(tree[0].name, "standalone");
    EXPECT_TRUE(tree[0].dependencies.empty());
}

}  // namespace
}  // namespace dotvm::pkg
