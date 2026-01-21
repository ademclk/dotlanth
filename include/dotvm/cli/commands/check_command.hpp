#pragma once

/// @file check_command.hpp
/// @brief DSL-003 Check command handler
///
/// Implements the 'dotdsl check <file.dsl>' command.
/// Validates DSL syntax without generating bytecode.

#include <string_view>

#include "dotvm/cli/cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Execute the check command
/// @param opts Command-specific options
/// @param global Global options
/// @param term Terminal for output
/// @return Exit code
[[nodiscard]] ExitCode execute_check(const CheckOptions& opts, const GlobalOptions& global,
                                     Terminal& term);

}  // namespace dotvm::cli::commands
