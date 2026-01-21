#pragma once

/// @file watch_command.hpp
/// @brief DSL-003 Watch command handler
///
/// Implements the 'dotdsl watch <dir|file>' command.
/// Watches a directory or file for changes and recompiles on file modification.
/// Features:
/// - Clear screen on recompile
/// - Timestamp display
/// - Color-coded success/failure output
/// - Debouncing for rapid file saves

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

/// @brief Clear the terminal screen
/// @param term Terminal for output
void clear_screen(Terminal& term);

/// @brief Get current time as formatted string [HH:MM:SS]
/// @return Formatted timestamp string
[[nodiscard]] std::string get_timestamp();

}  // namespace dotvm::cli::commands
