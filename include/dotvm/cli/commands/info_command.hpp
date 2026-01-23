#pragma once

/// @file info_command.hpp
/// @brief TOOL-009 Bytecode Inspector command interface

#include "dotvm/cli/info_cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Execute the bytecode inspection command
///
/// Reads the input file, inspects it, and outputs the result in the
/// requested format (table or JSON).
///
/// @param opts Command options
/// @param term Terminal for error output
/// @return Exit code indicating success or failure type
[[nodiscard]] InfoExitCode execute_info(const InfoOptions& opts, Terminal& term);

}  // namespace dotvm::cli::commands
