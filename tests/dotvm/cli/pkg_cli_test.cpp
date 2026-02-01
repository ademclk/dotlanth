/// @file pkg_cli_test.cpp
/// @brief Unit tests for PRD-007 package manager CLI

#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include "dotvm/cli/pkg_cli_app.hpp"

namespace dotvm::cli {
namespace {

class PkgCliAppTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "dotpkg_cli_test";
        std::filesystem::create_directories(test_dir_);
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

        return pkg_dir;
    }

    std::filesystem::path test_dir_;
};

// ============================================================================
// Parse Tests
// ============================================================================

TEST_F(PkgCliAppTest, ParseHelp) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "--help"};
    auto result = app.parse(2, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.help_requested());
}

TEST_F(PkgCliAppTest, ParseVersion) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "--version"};
    auto result = app.parse(2, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.help_requested());
}

TEST_F(PkgCliAppTest, ParseNoSubcommand) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg"};
    auto result = app.parse(1, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_FALSE(app.help_requested());
    EXPECT_TRUE(app.current_subcommand().empty());
}

// ============================================================================
// Install Command Parse Tests
// ============================================================================

TEST_F(PkgCliAppTest, ParseInstallCommand) {
    auto pkg_dir = create_test_package("testpkg", "1.0.0");

    PkgCliApp app;
    const char* argv[] = {"dotpkg", "install", pkg_dir.c_str()};
    auto result = app.parse(3, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_EQ(app.current_subcommand(), "install");
    EXPECT_EQ(app.install_options().package_path, pkg_dir.string());
    EXPECT_FALSE(app.install_options().dry_run);
}

TEST_F(PkgCliAppTest, ParseInstallDryRun) {
    auto pkg_dir = create_test_package("testpkg", "1.0.0");

    PkgCliApp app;
    const char* argv[] = {"dotpkg", "install", pkg_dir.c_str(), "--dry-run"};
    auto result = app.parse(4, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.install_options().dry_run);
}

TEST_F(PkgCliAppTest, ParseInstallMissingPath) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "install"};
    auto result = app.parse(2, argv);

    // Should fail due to missing required argument
    EXPECT_NE(result, PkgExitCode::Success);
}

// ============================================================================
// Uninstall Command Parse Tests
// ============================================================================

TEST_F(PkgCliAppTest, ParseUninstallCommand) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "uninstall", "mypackage"};
    auto result = app.parse(3, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_EQ(app.current_subcommand(), "uninstall");
    EXPECT_EQ(app.uninstall_options().package_name, "mypackage");
    EXPECT_FALSE(app.uninstall_options().force);
}

TEST_F(PkgCliAppTest, ParseUninstallForce) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "uninstall", "mypackage", "--force"};
    auto result = app.parse(4, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.uninstall_options().force);
}

TEST_F(PkgCliAppTest, ParseUninstallMissingName) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "uninstall"};
    auto result = app.parse(2, argv);

    // Should fail due to missing required argument
    EXPECT_NE(result, PkgExitCode::Success);
}

// ============================================================================
// List Command Parse Tests
// ============================================================================

TEST_F(PkgCliAppTest, ParseListCommand) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "list"};
    auto result = app.parse(2, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_EQ(app.current_subcommand(), "list");
    EXPECT_FALSE(app.list_options().tree);
    EXPECT_FALSE(app.list_options().outdated);
}

TEST_F(PkgCliAppTest, ParseListTree) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "list", "--tree"};
    auto result = app.parse(3, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.list_options().tree);
}

TEST_F(PkgCliAppTest, ParseListOutdated) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "list", "--outdated"};
    auto result = app.parse(3, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.list_options().outdated);
}

// ============================================================================
// Update Command Parse Tests
// ============================================================================

TEST_F(PkgCliAppTest, ParseUpdateAll) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "update"};
    auto result = app.parse(2, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_EQ(app.current_subcommand(), "update");
    EXPECT_FALSE(app.update_options().package_name.has_value());
    EXPECT_FALSE(app.update_options().dry_run);
}

TEST_F(PkgCliAppTest, ParseUpdateSpecific) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "update", "mypackage"};
    auto result = app.parse(3, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    ASSERT_TRUE(app.update_options().package_name.has_value());
    EXPECT_EQ(app.update_options().package_name.value(), "mypackage");
}

TEST_F(PkgCliAppTest, ParseUpdateDryRun) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "update", "--dry-run"};
    auto result = app.parse(3, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.update_options().dry_run);
}

// ============================================================================
// Global Options Parse Tests
// ============================================================================

TEST_F(PkgCliAppTest, ParseGlobalVerbose) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "--verbose", "list"};
    auto result = app.parse(3, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.global_options().verbose);
}

TEST_F(PkgCliAppTest, ParseGlobalQuiet) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "-q", "list"};
    auto result = app.parse(3, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.global_options().quiet);
}

TEST_F(PkgCliAppTest, ParseGlobalNoColor) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "--no-color", "list"};
    auto result = app.parse(3, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_TRUE(app.global_options().no_color);
}

TEST_F(PkgCliAppTest, ParseGlobalConfigDir) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "-C", test_dir_.c_str(), "list"};
    auto result = app.parse(4, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_EQ(app.global_options().config_dir, test_dir_);
}

TEST_F(PkgCliAppTest, ParseGlobalProjectDir) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "-P", test_dir_.c_str(), "list"};
    auto result = app.parse(4, argv);

    EXPECT_EQ(result, PkgExitCode::Success);
    EXPECT_EQ(app.global_options().project_dir, test_dir_);
}

// ============================================================================
// Run Tests (Integration)
// ============================================================================

TEST_F(PkgCliAppTest, RunNoSubcommandShowsHelp) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg"};
    (void)app.parse(1, argv);

    // Running without subcommand should show help and return success
    auto result = app.run();
    EXPECT_EQ(result, PkgExitCode::Success);
}

TEST_F(PkgCliAppTest, RunListEmptyProject) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg", "-P", test_dir_.c_str(), "-C", test_dir_.c_str(), "-q", "list"};
    (void)app.parse(7, argv);

    auto result = app.run();
    EXPECT_EQ(result, PkgExitCode::Success);
}

TEST_F(PkgCliAppTest, RunUninstallNotInstalled) {
    PkgCliApp app;
    const char* argv[] = {"dotpkg",          "-P", test_dir_.c_str(), "-C",
                          test_dir_.c_str(), "-q", "uninstall",       "nonexistent"};
    (void)app.parse(8, argv);

    auto result = app.run();
    EXPECT_EQ(result, PkgExitCode::PackageNotFound);
}

TEST_F(PkgCliAppTest, RunInstallDryRun) {
    auto pkg_dir = create_test_package("dryrun", "1.0.0");

    PkgCliApp app;
    const char* argv[] = {"dotpkg", "-P",      test_dir_.c_str(), "-C",       test_dir_.c_str(),
                          "-q",     "install", pkg_dir.c_str(),   "--dry-run"};
    (void)app.parse(9, argv);

    auto result = app.run();
    EXPECT_EQ(result, PkgExitCode::Success);
}

}  // namespace
}  // namespace dotvm::cli
