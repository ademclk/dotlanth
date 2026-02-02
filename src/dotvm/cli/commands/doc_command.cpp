/// @file doc_command.cpp
/// @brief CLI-005 Documentation generator command implementation

#include "dotvm/cli/commands/doc_command.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "dotvm/cli/doc/doc_extractor.hpp"
#include "dotvm/cli/doc/doc_generator.hpp"
#include "dotvm/cli/file_resolver.hpp"
#include "dotvm/core/dsl/parser.hpp"

namespace dotvm::cli::commands {

namespace fs = std::filesystem;

ExitCode execute_doc(const DocOptions& opts, const GlobalOptions& global, Terminal& term) {
    // Read input file
    FileResolver resolver;
    auto read_result = resolver.read_file(opts.input_file);
    if (!read_result) {
        term.error("error: ");
        term.print("Cannot read file: " + opts.input_file + "\n");
        return ExitCode::FileNotFound;
    }

    const std::string& source = read_result.value();
    fs::path input_path(opts.input_file);
    std::string module_name = input_path.filename().string();

    if (global.verbose) {
        term.info("info: ");
        term.print("Parsing " + opts.input_file + "...\n");
    }

    // Parse DSL source
    auto parse_result = core::dsl::DslParser::parse(source);
    if (!parse_result.is_ok()) {
        term.error("error: ");
        term.print("Failed to parse DSL file\n");

        // Print parse errors - capture error list to avoid dangling reference
        const auto& error_list = parse_result.error();
        for (const auto& err : error_list.errors()) {
            term.print_error(opts.input_file, source, err.span, "error", err.message);
        }
        return ExitCode::ParseError;
    }

    if (global.verbose) {
        term.info("info: ");
        term.print("Extracting documentation...\n");
    }

    // Extract documentation
    auto doc_result = doc::DocExtractor::extract(source, parse_result.value(), module_name);

    if (global.verbose) {
        term.info("info: ");
        term.print("Found " + std::to_string(doc_result.entities.size()) + " entities\n");
    }

    // Create generator
    auto generator = doc::DocGenerator::create(opts.format);
    if (!generator) {
        term.error("error: ");
        term.print("Unknown format\n");
        return ExitCode::IoError;
    }

    // Determine output destination
    std::ostream* out = &std::cout;
    std::ofstream file_out;

    if (!opts.output_file.empty()) {
        file_out.open(opts.output_file);
        if (!file_out) {
            term.error("error: ");
            term.print("Cannot open output file: " + opts.output_file + "\n");
            return ExitCode::IoError;
        }
        out = &file_out;
    }

    if (global.verbose) {
        term.info("info: ");
        if (opts.output_file.empty()) {
            term.print("Writing to stdout...\n");
        } else {
            term.print("Writing to " + opts.output_file + "...\n");
        }
    }

    // Generate documentation
    generator->generate(doc_result, *out);

    // Check for stream errors
    if (!out->good()) {
        term.error("error: ");
        term.print("Failed to write documentation output\n");
        return ExitCode::IoError;
    }

    if (!opts.output_file.empty()) {
        term.success("success: ");
        term.print("Documentation written to " + opts.output_file + "\n");
    }

    // Print statistics
    if (global.verbose) {
        std::size_t documented = 0;
        for (const auto& entity : doc_result.entities) {
            if (entity.doc.has_value())
                documented++;
        }

        term.info("info: ");
        term.print("Statistics: " + std::to_string(documented) + "/" +
                   std::to_string(doc_result.entities.size()) + " entities documented\n");
    }

    return ExitCode::Success;
}

}  // namespace dotvm::cli::commands
