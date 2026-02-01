#pragma once

/// @file pkg_list_command.hpp
/// @brief PRD-007 Package list command declaration
///
/// Lists installed packages with optional dependency tree view.

#include "dotvm/cli/pkg_cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief List installed packages
///
/// Displays installed packages:
/// - Default: Simple list with name and version
/// - --tree: Dependency tree view
/// - --outdated: Only packages that can be updated
///
/// @param opts List command options (tree, outdated)
/// @param global Global options (verbose, quiet, config_dir)
/// @param term Terminal for colored output
/// @return PkgExitCode::Success on success,
///         PkgExitCode::IoError on read failure
[[nodiscard]] PkgExitCode execute_list(const PkgListOptions& opts, const PkgGlobalOptions& global,
                                       Terminal& term);

}  // namespace dotvm::cli::commands
