#pragma once

/// @file doc_command.hpp
/// @brief CLI-005 Documentation generator command
///
/// Command handler for the `dotdsl doc` subcommand that generates
/// documentation from DSL source files.

#include <string>

#include "dotvm/cli/cli_app.hpp"
#include "dotvm/cli/doc/doc_generator.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief Options for the doc command
struct DocOptions {
    std::string input_file;   ///< Input .dsl source file
    std::string output_file;  ///< Output file (default: stdout or <input>.{html,md})
    doc::DocFormat format = doc::DocFormat::Markdown;  ///< Output format
};

/// @brief Execute the doc command
///
/// Generates documentation from a DSL source file in the specified format.
///
/// @param opts Command-specific options
/// @param global Global options (verbose, debug, etc.)
/// @param term Terminal for output
/// @return Exit code
[[nodiscard]] ExitCode execute_doc(const DocOptions& opts, const GlobalOptions& global,
                                   Terminal& term);

}  // namespace dotvm::cli::commands
