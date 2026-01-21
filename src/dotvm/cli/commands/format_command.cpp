/// @file format_command.cpp
/// @brief DSL-003 Format command implementation

#include "dotvm/cli/commands/format_command.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "dotvm/cli/file_resolver.hpp"
#include "dotvm/cli/formatter.hpp"
#include "dotvm/core/dsl/parser.hpp"

namespace dotvm::cli::commands {

namespace {

/// @brief Write content to a file
/// @param path File path
/// @param content Content to write
/// @return true on success, false on error
bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    return file.good();
}

}  // namespace

ExitCode execute_format(const FormatOptions& opts, const GlobalOptions& global, Terminal& term) {
    // Verbose: announce what we're doing
    if (global.verbose) {
        term.info("Formatting: ");
        term.print(opts.input_file);
        term.newline();
    }

    // Read the input file
    std::filesystem::path input_path(opts.input_file);
    FileResolver resolver(input_path.parent_path().empty() ? std::filesystem::current_path()
                                                           : input_path.parent_path());

    auto file_result = resolver.read_file(input_path);
    if (!file_result.has_value()) {
        const auto& err = file_result.error();
        term.error("error: ");
        term.print(err.message);
        term.newline();
        return err.code;
    }

    const std::string& source = *file_result;

    // Debug: show source size
    if (global.debug) {
        term.info("[debug] ");
        term.print("Source size: " + std::to_string(source.size()) + " bytes");
        term.newline();
    }

    // Parse the source
    auto parse_result = core::dsl::DslParser::parse(source);

    if (!parse_result.is_ok()) {
        const auto& error_list = parse_result.error();

        // Print all parse errors
        for (const auto& err : error_list) {
            term.print_error(opts.input_file, source, err.span, "error", err.message);
        }

        // Show error count summary
        if (error_list.size() > 1) {
            term.error("error: ");
            term.print("found " + std::to_string(error_list.size()) + " errors");
            term.newline();
        }

        term.error("error: ");
        term.print("cannot format file with syntax errors");
        term.newline();

        return ExitCode::ParseError;
    }

    // Format the AST back to source
    AstFormatter formatter;
    std::string formatted = formatter.format(parse_result.value());

    // Debug: show formatted size
    if (global.debug) {
        term.info("[debug] ");
        term.print("Formatted size: " + std::to_string(formatted.size()) + " bytes");
        term.newline();
    }

    // Output the result
    if (opts.in_place) {
        // Write back to the same file
        if (!write_file(input_path, formatted)) {
            term.error("error: ");
            term.print("failed to write to " + input_path.string());
            term.newline();
            return ExitCode::IoError;
        }

        if (global.verbose) {
            term.success("Formatted: ");
            term.print(opts.input_file);
            term.newline();
        }
    } else {
        // Print to stdout
        std::cout << formatted;
    }

    return ExitCode::Success;
}

}  // namespace dotvm::cli::commands
