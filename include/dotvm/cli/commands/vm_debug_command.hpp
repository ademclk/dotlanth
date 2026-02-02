#pragma once

/// @file vm_debug_command.hpp
/// @brief TOOL-011 VM debug command declaration
///
/// Interactive debugger command for DotVM bytecode.

#include "dotvm/cli/terminal.hpp"
#include "dotvm/cli/vm_cli_app.hpp"

namespace dotvm::cli::commands {

/// @brief Execute the interactive debugger
///
/// Starts an LLDB-style REPL debugger for the specified bytecode file.
/// Provides breakpoints, stepping, register/memory inspection, and disassembly.
///
/// @param opts Debug command options (input file)
/// @param global Global options (verbose, quiet, arch override)
/// @param term Terminal for colored output
/// @return VmExitCode::Success on clean exit,
///         VmExitCode::RuntimeError on error,
///         VmExitCode::ValidationError on bytecode loading failure
[[nodiscard]] VmExitCode execute_debug(const VmDebugOptions& opts, const VmGlobalOptions& global,
                                       Terminal& term);

}  // namespace dotvm::cli::commands
