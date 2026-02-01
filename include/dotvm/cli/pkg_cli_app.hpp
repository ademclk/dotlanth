#pragma once

/// @file pkg_cli_app.hpp
/// @brief PRD-007 Package Manager CLI application using CLI11
///
/// Provides the command-line interface for the dotpkg package manager.
/// Commands: install, uninstall, list, update

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "dotvm/cli/terminal.hpp"

// Forward declare CLI11 types to avoid header pollution
namespace CLI {
class App;
}

namespace dotvm::cli {

/// @brief Exit codes for the package manager
///
/// Following the plan specification for PRD-007:
/// - 0: Success
/// - 1: PackageNotFound
/// - 2: DependencyError
/// - 3: CacheError
/// - 4: LockFileError
/// - 5: IoError
enum class PkgExitCode : int {
    Success = 0,          ///< Operation completed successfully
    PackageNotFound = 1,  ///< Specified package was not found
    DependencyError = 2,  ///< Dependency resolution failed
    CacheError = 3,       ///< Package cache operation failed
    LockFileError = 4,    ///< Lock file read/write error
    IoError = 5           ///< General I/O error
};

/// @brief Global options shared by all package manager subcommands
struct PkgGlobalOptions {
    bool verbose = false;                                ///< Enable verbose output
    bool quiet = false;                                  ///< Suppress non-essential output
    bool no_color = false;                               ///< Disable ANSI color codes
    std::filesystem::path config_dir;                    ///< Package cache directory (~/.dotpkg)
    std::filesystem::path project_dir;                   ///< Project directory (cwd by default)
};

/// @brief Options for the install command
struct PkgInstallOptions {
    std::string package_path;  ///< Local path to package directory or archive
    bool dry_run = false;      ///< Show what would be installed without installing
};

/// @brief Options for the uninstall command
struct PkgUninstallOptions {
    std::string package_name;  ///< Name of the package to uninstall
    bool force = false;        ///< Force uninstall even if other packages depend on it
};

/// @brief Options for the list command
struct PkgListOptions {
    bool tree = false;      ///< Show dependency tree
    bool outdated = false;  ///< Show only outdated packages
};

/// @brief Options for the update command
struct PkgUpdateOptions {
    std::optional<std::string> package_name;  ///< Package to update (empty = update all)
    bool dry_run = false;                     ///< Show what would be updated without updating
};

/// @brief Main package manager CLI application class
///
/// Provides the command-line interface for the dotpkg package manager.
/// Uses CLI11 for argument parsing with subcommands: install, uninstall, list, update.
class PkgCliApp {
public:
    /// @brief Construct the package manager CLI application
    PkgCliApp();

    /// @brief Destructor
    ~PkgCliApp();

    // Non-copyable
    PkgCliApp(const PkgCliApp&) = delete;
    PkgCliApp& operator=(const PkgCliApp&) = delete;

    // Movable
    PkgCliApp(PkgCliApp&&) noexcept;
    PkgCliApp& operator=(PkgCliApp&&) noexcept;

    /// @brief Parse command-line arguments
    /// @param argc Argument count
    /// @param argv Argument values
    /// @return Exit code
    [[nodiscard]] PkgExitCode parse(int argc, const char* const* argv);

    /// @brief Run the parsed command
    /// @return Exit code
    [[nodiscard]] PkgExitCode run();

    /// @brief Get the global options
    [[nodiscard]] const PkgGlobalOptions& global_options() const noexcept { return global_opts_; }

    /// @brief Get the install options
    [[nodiscard]] const PkgInstallOptions& install_options() const noexcept {
        return install_opts_;
    }

    /// @brief Get the uninstall options
    [[nodiscard]] const PkgUninstallOptions& uninstall_options() const noexcept {
        return uninstall_opts_;
    }

    /// @brief Get the list options
    [[nodiscard]] const PkgListOptions& list_options() const noexcept { return list_opts_; }

    /// @brief Get the update options
    [[nodiscard]] const PkgUpdateOptions& update_options() const noexcept { return update_opts_; }

    /// @brief Get the currently selected subcommand name
    [[nodiscard]] std::string current_subcommand() const;

    /// @brief Get the underlying CLI11 app (for testing)
    [[nodiscard]] CLI::App& app() noexcept { return *app_; }

    /// @brief Check if help or version was requested (parse handled it)
    [[nodiscard]] bool help_requested() const noexcept { return help_requested_; }

private:
    void setup_global_options();
    void setup_install_command();
    void setup_uninstall_command();
    void setup_list_command();
    void setup_update_command();

    std::unique_ptr<CLI::App> app_;

    PkgGlobalOptions global_opts_;
    PkgInstallOptions install_opts_;
    PkgUninstallOptions uninstall_opts_;
    PkgListOptions list_opts_;
    PkgUpdateOptions update_opts_;

    // Subcommand pointers (owned by app_)
    CLI::App* install_cmd_ = nullptr;
    CLI::App* uninstall_cmd_ = nullptr;
    CLI::App* list_cmd_ = nullptr;
    CLI::App* update_cmd_ = nullptr;

    // Flag to track if help/version was handled during parse
    bool help_requested_ = false;
};

/// @brief Create a Terminal with color mode resolution based on global options
///
/// Priority: --no-color > auto-detect TTY
/// @param out Output stream
/// @param opts Global options
/// @return Configured Terminal
[[nodiscard]] inline Terminal make_terminal(std::ostream& out, const PkgGlobalOptions& opts) {
    if (opts.no_color) {
        return Terminal(out, true);  // force_no_color = true
    }
    return Terminal(out, false);  // auto-detect TTY
}

}  // namespace dotvm::cli
