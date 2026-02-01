#pragma once

/// @file pkg_install_command.hpp
/// @brief PRD-007 Package install command declaration
///
/// Installs packages from local filesystem paths.

#include "dotvm/cli/pkg_cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Install a package from local path
///
/// Installs a package from a local directory or archive:
/// - Reads package manifest (dotpkg.json) from source
/// - Resolves dependencies
/// - Copies package to cache
/// - Updates registry and lock file
///
/// @param opts Install command options (package path, dry_run)
/// @param global Global options (verbose, quiet, config_dir)
/// @param term Terminal for colored output
/// @return PkgExitCode::Success on successful installation,
///         PkgExitCode::PackageNotFound if source doesn't exist,
///         PkgExitCode::DependencyError on resolution failure,
///         PkgExitCode::CacheError on cache operation failure
[[nodiscard]] PkgExitCode execute_install(const PkgInstallOptions& opts,
                                          const PkgGlobalOptions& global, Terminal& term);

}  // namespace dotvm::cli::commands
