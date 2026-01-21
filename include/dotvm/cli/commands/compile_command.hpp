#pragma once

/// @file compile_command.hpp
/// @brief DSL-003 Compile command handler
///
/// Implements the 'dotdsl compile <file.dsl> [-o output.dot]' command.
/// Compiles DSL source to bytecode.

#include <string_view>

#include "dotvm/cli/cli_app.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Execute the compile command
/// @param opts Command-specific options
/// @param global Global options
/// @param term Terminal for output
/// @return Exit code
[[nodiscard]] ExitCode execute_compile(const CompileOptions& opts, const GlobalOptions& global,
                                       Terminal& term);

}  // namespace dotvm::cli::commands
