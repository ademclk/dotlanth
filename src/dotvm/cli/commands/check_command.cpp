/// @file check_command.cpp
/// @brief DSL-003 Check command implementation

#include "dotvm/cli/commands/check_command.hpp"

#include <chrono>
#include <filesystem>

#include "dotvm/cli/file_resolver.hpp"
#include "dotvm/core/dsl/parser.hpp"

namespace dotvm::cli::commands {

namespace {

/// @brief Print a DSL error with colored source snippet
void print_dsl_error(Terminal& term, std::string_view filename, std::string_view source,
                     const core::dsl::DslErrorInfo& err) {
    term.print_error(filename, source, err.span, "error", err.message);
}

}  // namespace

ExitCode execute_check(const CheckOptions& opts, const GlobalOptions& global, Terminal& term) {
    auto start_time = std::chrono::steady_clock::now();

    // TODO(DSL-003): Use global.strict when parser produces warnings.
    // Currently the parser only produces errors, so strict mode has no effect.
    // When warnings are implemented, they should be promoted to errors when
    // global.strict is true.
    (void)global.strict;

    // Verbose: announce what we're doing
    if (global.verbose) {
        term.info("Checking: ");
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
    std::string_view filename = opts.input_file;

    // Debug: show source size
    if (global.debug) {
        term.info("[debug] ");
        term.print("Source size: " + std::to_string(source.size()) + " bytes");
        term.newline();
    }

    // Parse the source (validation only, no compilation)
    auto parse_result = core::dsl::DslParser::parse(source);

    if (!parse_result.is_ok()) {
        const auto& error_list = parse_result.error();

        // Print all parse errors
        for (const auto& err : error_list) {
            print_dsl_error(term, filename, source, err);
        }

        // Show error count summary
        if (error_list.size() > 1) {
            term.error("error: ");
            term.print("found " + std::to_string(error_list.size()) + " errors");
            term.newline();
        }

        return ExitCode::ParseError;
    }

    // Calculate elapsed time
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // Success message
    if (global.verbose) {
        const auto& module = parse_result.value();
        std::size_t dot_count = module.dots.size();
        std::size_t link_count = module.links.size();

        term.success("Syntax OK: ");
        term.print(opts.input_file);
        term.print(" (" + std::to_string(dot_count) + " dots, " + std::to_string(link_count) +
                   " links)");
        term.newline();
    }

    // Debug: show timing
    if (global.debug) {
        term.info("[debug] ");
        term.print("Parse time: " + std::to_string(elapsed_ms) + "ms");
        term.newline();
    }

    return ExitCode::Success;
}

}  // namespace dotvm::cli::commands
