#pragma once

/// @file vm_info_command.hpp
/// @brief TOOL-005 VM info command declaration
///
/// Displays detailed information about a bytecode file.

#include "dotvm/cli/terminal.hpp"
#include "dotvm/cli/vm_cli_app.hpp"

namespace dotvm::cli::commands {

/// @brief Display bytecode file information
///
/// Loads and displays detailed information about a .dot bytecode file:
/// - File path
/// - Magic bytes ("DOTM")
/// - Version number
/// - Architecture (32-bit / 64-bit)
/// - Flags (DEBUG, OPTIMIZED)
/// - Entry point (hex address + instruction index)
/// - Constant pool (entry count, byte size)
/// - Code section (instruction count, byte size)
///
/// @param opts Info command options (input file)
/// @param global Global options (verbose, quiet)
/// @param term Terminal for colored output
/// @return VmExitCode::Success if info displayed successfully,
///         VmExitCode::ValidationError if file cannot be loaded
[[nodiscard]] VmExitCode execute_info(const VmInfoOptions& opts, const VmGlobalOptions& global,
                                      Terminal& term);

}  // namespace dotvm::cli::commands
