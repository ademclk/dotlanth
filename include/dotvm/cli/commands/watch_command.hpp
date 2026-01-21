#pragma once

/// @file watch_command.hpp
/// @brief DSL-003 Watch command handler
///
/// Implements the 'dotdsl watch <dir>' command.
/// Watches a directory for changes and recompiles on file modification.

#include <string_view>

#include "dotvm/cli/cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Execute the watch command
/// @param opts Command-specific options
/// @param global Global options
/// @param term Terminal for output
/// @return Exit code
[[nodiscard]] ExitCode execute_watch(const WatchOptions& opts, const GlobalOptions& global,
                                     Terminal& term);

}  // namespace dotvm::cli::commands
