#pragma once

/// @file dis_command.hpp
/// @brief TOOL-008 Disassembler command declaration
///
/// Executes the disassembly of a bytecode file.

#include "dotvm/cli/dis_cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Execute bytecode disassembly
///
/// Loads and disassembles a .dot bytecode file, outputting human-readable
/// assembly or JSON format based on options.
///
/// @param opts Disassembler options
/// @param term Terminal for colored output
/// @return DisExitCode::Success on success, error code otherwise
[[nodiscard]] DisExitCode execute_disassemble(const DisOptions& opts, Terminal& term);

}  // namespace dotvm::cli::commands
