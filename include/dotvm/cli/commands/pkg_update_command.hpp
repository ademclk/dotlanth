#pragma once

/// @file pkg_update_command.hpp
/// @brief PRD-007 Package update command declaration
///
/// Updates installed packages to newer versions.

#include "dotvm/cli/pkg_cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Update packages
///
/// Updates one or all installed packages:
/// - If package_name is provided, updates that package
/// - Otherwise, updates all packages
/// - --dry-run shows what would be updated without doing it
///
/// @param opts Update command options (package_name, dry_run)
/// @param global Global options (verbose, quiet, config_dir)
/// @param term Terminal for colored output
/// @return PkgExitCode::Success on successful update,
///         PkgExitCode::PackageNotFound if specified package isn't installed,
///         PkgExitCode::DependencyError if update causes conflicts
[[nodiscard]] PkgExitCode execute_update(const PkgUpdateOptions& opts,
                                         const PkgGlobalOptions& global, Terminal& term);

}  // namespace dotvm::cli::commands
