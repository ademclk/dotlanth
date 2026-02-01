/// @file pkg_cli_app.cpp
/// @brief PRD-007 Package Manager CLI application implementation

#include "dotvm/cli/pkg_cli_app.hpp"

#include <CLI/CLI.hpp>
#include <cstdlib>
#include <iostream>

#include "dotvm/cli/commands/pkg_install_command.hpp"
#include "dotvm/cli/commands/pkg_list_command.hpp"
#include "dotvm/cli/commands/pkg_uninstall_command.hpp"
#include "dotvm/cli/commands/pkg_update_command.hpp"

namespace dotvm::cli {

namespace {

/// @brief Application description for help text
constexpr const char* kAppDescription = R"(DotPkg Package Manager

A local package manager for DotVM projects.
Manages package dependencies with lock file support for reproducible installs.)";

/// @brief Examples epilog for help text
constexpr const char* kExamplesEpilog = R"(
EXAMPLES:
    Install a package from local path:
        dotpkg install ./packages/mylib

    Install with dry-run (show what would happen):
        dotpkg install ./packages/mylib --dry-run

    Uninstall a package:
        dotpkg uninstall mylib

    Force uninstall (ignore dependents):
        dotpkg uninstall mylib --force

    List installed packages:
        dotpkg list

    Show dependency tree:
        dotpkg list --tree

    Show outdated packages:
        dotpkg list --outdated

    Update all packages:
        dotpkg update

    Update a specific package:
        dotpkg update mylib

For more information, visit: https://github.com/dotlanth/dotvm)";

/// @brief Get the default config directory (~/.dotpkg)
std::filesystem::path default_config_dir() {
    const char* home = std::getenv("HOME");
    if (home != nullptr) {
        return std::filesystem::path(home) / ".dotpkg";
    }
    // Fallback to current directory
    return std::filesystem::current_path() / ".dotpkg";
}

}  // namespace

PkgCliApp::PkgCliApp() : app_(std::make_unique<CLI::App>(kAppDescription, "dotpkg")) {
    app_->set_version_flag("-v,--version", "0.1.0");
    app_->require_subcommand(0, 1);  // 0 to 1 subcommands allowed
    app_->footer(kExamplesEpilog);

    // Configure formatter for better help output
    auto fmt = std::make_shared<CLI::Formatter>();
    fmt->column_width(40);
    app_->formatter(fmt);

    // Set default directories
    global_opts_.config_dir = default_config_dir();
    global_opts_.project_dir = std::filesystem::current_path();

    setup_global_options();
    setup_install_command();
    setup_uninstall_command();
    setup_list_command();
    setup_update_command();
}

PkgCliApp::~PkgCliApp() = default;

PkgCliApp::PkgCliApp(PkgCliApp&&) noexcept = default;
PkgCliApp& PkgCliApp::operator=(PkgCliApp&&) noexcept = default;

void PkgCliApp::setup_global_options() {
    // Create option group for global flags
    auto* global_group = app_->add_option_group("Global Options");

    global_group->add_flag("--verbose", global_opts_.verbose,
                           "Enable verbose output (show detailed progress)");

    global_group->add_flag("-q,--quiet", global_opts_.quiet,
                           "Suppress non-essential output (only show results)");

    global_group->add_flag("--no-color", global_opts_.no_color,
                           "Disable ANSI color codes in output");

    global_group->add_option("-C,--config-dir", global_opts_.config_dir, "Package cache directory")
        ->type_name("DIR")
        ->default_val(default_config_dir().string());

    global_group->add_option("-P,--project-dir", global_opts_.project_dir, "Project directory")
        ->type_name("DIR")
        ->check(CLI::ExistingDirectory);
}

void PkgCliApp::setup_install_command() {
    install_cmd_ = app_->add_subcommand("install", "Install a package from local path");
    install_cmd_->description("Installs a package from a local directory or archive.\n"
                              "The package must contain a dotpkg.json manifest file.");

    install_cmd_->add_option("path", install_opts_.package_path, "Path to package directory")
        ->required()
        ->check(CLI::ExistingPath)
        ->type_name("PATH");

    install_cmd_->add_flag("-n,--dry-run", install_opts_.dry_run,
                           "Show what would be installed without installing");
}

void PkgCliApp::setup_uninstall_command() {
    uninstall_cmd_ = app_->add_subcommand("uninstall", "Uninstall a package");
    uninstall_cmd_->description("Removes an installed package.\n"
                                "Fails if other packages depend on it (use --force to override).");

    uninstall_cmd_->add_option("name", uninstall_opts_.package_name, "Package name to uninstall")
        ->required()
        ->type_name("NAME");

    uninstall_cmd_->add_flag("-f,--force", uninstall_opts_.force,
                             "Force uninstall even if other packages depend on it");
}

void PkgCliApp::setup_list_command() {
    list_cmd_ = app_->add_subcommand("list", "List installed packages");
    list_cmd_->description("Displays all installed packages.\n"
                           "Use --tree for dependency tree or --outdated for updateable packages.");

    list_cmd_->add_flag("-t,--tree", list_opts_.tree, "Show dependency tree");

    list_cmd_->add_flag("-o,--outdated", list_opts_.outdated, "Show only outdated packages");
}

void PkgCliApp::setup_update_command() {
    update_cmd_ = app_->add_subcommand("update", "Update packages");
    update_cmd_->description("Updates one or all installed packages.\n"
                             "Without arguments, updates all packages.");

    update_cmd_->add_option("name", update_opts_.package_name, "Specific package to update")
        ->type_name("NAME");

    update_cmd_->add_flag("-n,--dry-run", update_opts_.dry_run,
                          "Show what would be updated without updating");
}

PkgExitCode PkgCliApp::parse(int argc, const char* const* argv) {
    help_requested_ = false;
    try {
        app_->parse(argc, argv);
        return PkgExitCode::Success;
    } catch (const CLI::ParseError& e) {
        // Let CLI11 handle the error output
        int result = app_->exit(e);
        if (result == 0) {
            // Help or version was requested
            help_requested_ = true;
            return PkgExitCode::Success;
        }
        return PkgExitCode::IoError;  // Parse errors
    }
}

PkgExitCode PkgCliApp::run() {
    // Get the subcommand that was selected
    std::string cmd = current_subcommand();

    if (cmd.empty()) {
        // No subcommand selected, show help
        std::cout << app_->help() << std::endl;
        return PkgExitCode::Success;
    }

    // Create terminal for output with color settings
    Terminal term = make_terminal(std::cerr, global_opts_);

    if (cmd == "install") {
        return commands::execute_install(install_opts_, global_opts_, term);
    }

    if (cmd == "uninstall") {
        return commands::execute_uninstall(uninstall_opts_, global_opts_, term);
    }

    if (cmd == "list") {
        return commands::execute_list(list_opts_, global_opts_, term);
    }

    if (cmd == "update") {
        return commands::execute_update(update_opts_, global_opts_, term);
    }

    return PkgExitCode::Success;
}

std::string PkgCliApp::current_subcommand() const {
    for (auto* sub : app_->get_subcommands()) {
        if (sub->parsed()) {
            return sub->get_name();
        }
    }
    return "";
}

}  // namespace dotvm::cli
