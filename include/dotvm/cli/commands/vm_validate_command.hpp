#pragma once

/// @file vm_validate_command.hpp
/// @brief TOOL-005 VM validate command declaration
///
/// Validates bytecode file structure and format without execution.

#include "dotvm/cli/terminal.hpp"
#include "dotvm/cli/vm_cli_app.hpp"

namespace dotvm::cli::commands {

/// @brief Validate bytecode file
///
/// Performs comprehensive validation of a .dot bytecode file:
/// - Magic bytes verification ("DOTM")
/// - Version compatibility check
/// - Architecture validation
/// - Header field bounds checking
/// - Constant pool integrity (if present)
/// - Code section alignment
///
/// @param opts Validate command options (input file)
/// @param global Global options (verbose, quiet)
/// @param term Terminal for colored output
/// @return VmExitCode::Success if bytecode is valid,
///         VmExitCode::ValidationError if validation fails
[[nodiscard]] VmExitCode execute_validate(const VmValidateOptions& opts,
                                          const VmGlobalOptions& global, Terminal& term);

}  // namespace dotvm::cli::commands
