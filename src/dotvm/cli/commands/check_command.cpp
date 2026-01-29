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

/// @brief Result of processing includes for check
struct CheckIncludeResult {
    bool success = true;
    ExitCode code = ExitCode::Success;
    std::size_t include_count = 0;
};

/// @brief Process includes recursively for validation
CheckIncludeResult check_includes(FileResolver& resolver, const std::filesystem::path& file_path,
                                  std::string_view source, Terminal& term, bool verbose, bool debug,
                                  std::uint32_t include_line = 0) {
    CheckIncludeResult result;

    // Check for circular include
    if (resolver.would_create_cycle(file_path)) {
        // Report circular include error with chain
        std::string chain_info = FileError::format_include_chain(resolver.include_stack());
        if (!chain_info.empty()) {
            term.print(chain_info);
            term.newline();
        }
        term.error("error: ");
        term.print("circular include detected: " + file_path.string());
        term.newline();
        result.success = false;
        result.code = ExitCode::CircularInclude;
        return result;
    }

    // Push this file onto the include stack
    resolver.push_include_stack(file_path, include_line);

    // Parse to find includes
    auto parse_result = core::dsl::DslParser::parse(source);
    if (!parse_result.is_ok()) {
        // Parse error - don't continue checking includes
        resolver.pop_include_stack();
        return result;  // Let the main check report the error
    }

    const auto& module = parse_result.value();

    // If no includes, we're done
    if (module.includes.empty()) {
        resolver.pop_include_stack();
        return result;
    }

    // Process each include
    for (const auto& include : module.includes) {
        auto resolve_result = resolver.resolve_include(include.path, file_path);
        if (!resolve_result.has_value()) {
            const auto& err = resolve_result.error();
            // Print include chain
            std::string chain_info = FileError::format_include_chain(resolver.include_stack());
            if (!chain_info.empty()) {
                term.print(chain_info);
                term.newline();
            }
            term.error("error: ");
            term.print(err.message);
            term.newline();
            result.success = false;
            result.code = err.code;
            resolver.pop_include_stack();
            return result;
        }

        const auto& resolved_path = *resolve_result;

        // Skip if already included
        if (resolver.is_included(resolved_path)) {
            if (debug) {
                term.info("[debug] ");
                term.print("Skipping already included: " + resolved_path.string());
                term.newline();
            }
            continue;
        }

        // Mark as included
        resolver.mark_included(resolved_path);
        result.include_count++;

        if (verbose) {
            term.info("Checking include: ");
            term.print(resolved_path.string());
            term.newline();
        }

        // Read the included file
        auto file_result = resolver.read_file(resolved_path);
        if (!file_result.has_value()) {
            const auto& err = file_result.error();
            std::string chain_info = FileError::format_include_chain(resolver.include_stack());
            if (!chain_info.empty()) {
                term.print(chain_info);
                term.newline();
            }
            term.error("error: ");
            term.print(err.message);
            term.newline();
            result.success = false;
            result.code = err.code;
            resolver.pop_include_stack();
            return result;
        }

        // Parse the included file for validation
        auto include_parse = core::dsl::DslParser::parse(*file_result);
        if (!include_parse.is_ok()) {
            const auto& error_list = include_parse.error();

            // Print include chain
            std::string chain_info = FileError::format_include_chain(resolver.include_stack());
            if (!chain_info.empty()) {
                term.print(chain_info);
                term.newline();
            }

            // Print all parse errors
            for (const auto& err : error_list) {
                print_dsl_error(term, resolved_path.string(), *file_result, err);
            }

            result.success = false;
            result.code = ExitCode::ParseError;
            resolver.pop_include_stack();
            return result;
        }

        // Recursively check includes in the included file
        auto include_result = check_includes(resolver, resolved_path, *file_result, term, verbose,
                                             debug, include.span.start.line);
        if (!include_result.success) {
            resolver.pop_include_stack();
            return include_result;
        }
        result.include_count += include_result.include_count;
    }

    resolver.pop_include_stack();
    return result;
}

}  // namespace

ExitCode execute_check(const CheckOptions& opts, const GlobalOptions& global, Terminal& term) {
    auto start_time = std::chrono::steady_clock::now();

    // SEC-010: Strict mode for promoting warnings to errors
    // Currently the parser only produces errors, so strict mode has no effect yet.
    // When warnings are implemented (DSL-003), they will be promoted to errors
    // when global.strict is true. The infrastructure is in place - warnings just
    // need to be added to the parser.
    if (global.strict && global.debug) {
        term.info("[debug] ");
        term.print("Strict mode enabled (warnings will be promoted to errors when implemented)");
        term.newline();
    }

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

    // Add include search paths
    for (const auto& path : opts.include_paths) {
        resolver.add_search_path(path);
        if (global.debug) {
            term.info("[debug] ");
            term.print("Added include path: " + path);
            term.newline();
        }
    }

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

    // Process includes for validation
    resolver.mark_included(input_path);  // Mark main file as included
    auto include_result =
        check_includes(resolver, input_path, source, term, global.verbose, global.debug);
    if (!include_result.success) {
        return include_result.code;
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
        std::size_t include_count = module.includes.size();

        term.success("Syntax OK: ");
        term.print(opts.input_file);
        term.print(" (" + std::to_string(dot_count) + " dots, " + std::to_string(link_count) +
                   " links");
        if (include_count > 0 || include_result.include_count > 0) {
            term.print(", " + std::to_string(include_count) + " includes");
            if (include_result.include_count > 0) {
                term.print(" [" + std::to_string(include_result.include_count) + " files]");
            }
        }
        term.print(")");
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
