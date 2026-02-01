#pragma once

/// @file pkg_uninstall_command.hpp
/// @brief PRD-007 Package uninstall command declaration
///
/// Removes installed packages from the project.

#include "dotvm/cli/pkg_cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Uninstall a package
///
/// Removes an installed package:
/// - Checks for dependent packages (fails unless --force)
/// - Removes from cache
/// - Updates registry and lock file
///
/// @param opts Uninstall command options (package name, force)
/// @param global Global options (verbose, quiet, config_dir)
/// @param term Terminal for colored output
/// @return PkgExitCode::Success on successful uninstallation,
///         PkgExitCode::PackageNotFound if package isn't installed,
///         PkgExitCode::DependencyError if other packages depend on it
[[nodiscard]] PkgExitCode execute_uninstall(const PkgUninstallOptions& opts,
                                            const PkgGlobalOptions& global, Terminal& term);

}  // namespace dotvm::cli::commands
