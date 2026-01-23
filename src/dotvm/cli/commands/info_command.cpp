/// @file info_command.cpp
/// @brief TOOL-009 Bytecode Inspector command implementation

#include "dotvm/cli/commands/info_command.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "dotvm/cli/info_formatter.hpp"
#include "dotvm/core/inspector.hpp"

namespace dotvm::cli::commands {

namespace {

/// @brief Write string to file or stdout
[[nodiscard]] bool write_output(const std::string& content, const std::string& output_file) {
    if (output_file.empty()) {
        std::cout << content;
        return true;
    }

    std::ofstream file(output_file);
    if (!file) {
        return false;
    }
    file << content;
    return file.good();
}

}  // namespace

InfoExitCode execute_info(const InfoOptions& opts, Terminal& term) {
    // Inspect the file
    auto result = core::BytecodeInspector::inspect_file(opts.input_file);
    if (!result.has_value()) {
        term.error("error: ");
        term.print(result.error());
        term.newline();
        return InfoExitCode::IOError;
    }

    const auto& inspection = result.value();

    // Format output
    std::string output;
    if (opts.format == InfoOutputFormat::Json) {
        output = format_info_json(inspection, opts.detailed);
    } else {
        output = format_info_table(inspection, opts.detailed);
    }

    // Write output
    if (!write_output(output, opts.output_file)) {
        term.error("error: Failed to write output file: ");
        term.print(opts.output_file);
        term.newline();
        return InfoExitCode::IOError;
    }

    // Return validation error if bytecode is invalid (but info was still output)
    if (!inspection.is_valid()) {
        return InfoExitCode::ValidationError;
    }

    return InfoExitCode::Success;
}

}  // namespace dotvm::cli::commands
