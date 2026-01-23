#pragma once

/// @file vm_run_command.hpp
/// @brief TOOL-005 VM run command declaration
///
/// Executes bytecode with optional debug tracing and instruction limits.

#include "dotvm/cli/terminal.hpp"
#include "dotvm/cli/vm_cli_app.hpp"

namespace dotvm::cli::commands {

/// @brief Execute bytecode file
///
/// Loads and executes a .dot bytecode file with the following capabilities:
/// - Optional debug mode with full instruction tracing
/// - Instruction limit for preventing infinite loops
/// - Architecture override from global options
/// - Timing and instruction count statistics
///
/// @param opts Run command options (input file, debug flag, limit)
/// @param global Global options (verbose, quiet, arch override)
/// @param term Terminal for colored output
/// @return VmExitCode::Success on successful execution,
///         VmExitCode::RuntimeError on execution failure,
///         VmExitCode::ValidationError on bytecode loading failure
[[nodiscard]] VmExitCode execute_run(const VmRunOptions& opts, const VmGlobalOptions& global,
                                     Terminal& term);

}  // namespace dotvm::cli::commands
