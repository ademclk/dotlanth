#pragma once

/// @file format_command.hpp
/// @brief DSL-003 Format command handler
///
/// Implements the 'dotdsl format <file.dsl> [--in-place]' command.
/// Auto-formats DSL source code.

#include <string_view>

#include "dotvm/cli/cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Execute the format command
/// @param opts Command-specific options
/// @param global Global options
/// @param term Terminal for output
/// @return Exit code
[[nodiscard]] ExitCode execute_format(const FormatOptions& opts, const GlobalOptions& global,
                                      Terminal& term);

}  // namespace dotvm::cli::commands
