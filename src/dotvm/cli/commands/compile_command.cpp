/// @file compile_command.cpp
/// @brief DSL-003 Compile command implementation

#include "dotvm/cli/commands/compile_command.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "dotvm/cli/file_resolver.hpp"
#include "dotvm/core/capabilities/capability.hpp"
#include "dotvm/core/dsl/compiler/dsl_compiler.hpp"
#include "dotvm/core/dsl/ir/printer.hpp"
#include "dotvm/core/dsl/parser.hpp"

namespace dotvm::cli::commands {

namespace {

/// @brief Print a compile error with colored source snippet
void print_compile_error(Terminal& term, std::string_view filename, std::string_view source,
                         const core::dsl::compiler::CompileError& err) {
    std::string label = "error";
    std::string stage_prefix;

    switch (err.stage) {
        case core::dsl::compiler::CompileError::Stage::Parse:
            stage_prefix = "parse error: ";
            break;
        case core::dsl::compiler::CompileError::Stage::IRBuild:
            stage_prefix = "ir error: ";
            break;
        case core::dsl::compiler::CompileError::Stage::Optimize:
            stage_prefix = "optimization error: ";
            break;
        case core::dsl::compiler::CompileError::Stage::Lower:
            stage_prefix = "lowering error: ";
            break;
        case core::dsl::compiler::CompileError::Stage::Codegen:
            stage_prefix = "codegen error: ";
            break;
    }

    std::string message = stage_prefix + err.message;
    term.print_error(filename, source, err.span, label, message);
}

/// @brief Write bytecode to a file
bool write_bytecode(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytecode,
                    Terminal& term) {
    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file) {
        term.error("error: ");
        term.print("failed to open output file: " + path.string());
        term.newline();
        return false;
    }

    file.write(reinterpret_cast<const char*>(bytecode.data()),
               static_cast<std::streamsize>(bytecode.size()));

    if (!file.good()) {
        term.error("error: ");
        term.print("failed to write bytecode to: " + path.string());
        term.newline();
        return false;
    }

    return true;
}

/// @brief Result of processing includes
struct IncludeResult {
    bool success = true;
    ExitCode code = ExitCode::Success;
    std::string merged_source;
};

/// @brief Process includes recursively and merge sources
IncludeResult process_includes(FileResolver& resolver, const std::filesystem::path& file_path,
                               std::string_view source, Terminal& term, bool verbose, bool debug,
                               std::uint32_t include_line = 0) {
    IncludeResult result;

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
        // Parse error - let the compiler handle detailed error reporting
        result.merged_source = std::string(source);
        resolver.pop_include_stack();
        return result;
    }

    const auto& module = parse_result.value();

    // If no includes, return the source as-is
    if (module.includes.empty()) {
        result.merged_source = std::string(source);
        resolver.pop_include_stack();
        return result;
    }

    // Process each include
    for (const auto& include : module.includes) {
        // Check if already included (prevents re-inclusion)
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

        if (verbose) {
            term.info("Including: ");
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

        // Process includes in the included file recursively
        auto include_result = process_includes(resolver, resolved_path, *file_result, term, verbose,
                                               debug, include.span.start.line);
        if (!include_result.success) {
            resolver.pop_include_stack();
            return include_result;
        }

        // Add the processed included content (the actual merging happens at compile time
        // since includes are directive-based, not textual inclusion)
        // For now, we cache the processed file for later use
        resolver.cache_file(resolved_path, include_result.merged_source);
    }

    // For now, return original source - the compiler will need to be updated
    // to handle module merging. This phase focuses on the resolution infrastructure.
    result.merged_source = std::string(source);
    resolver.pop_include_stack();
    return result;
}

}  // namespace

ExitCode execute_compile(const CompileOptions& opts, const GlobalOptions& global, Terminal& term) {
    auto start_time = std::chrono::steady_clock::now();

    // SEC-010: Strict mode for promoting warnings to errors
    // Currently the compiler only produces errors, so strict mode has no effect yet.
    // When warnings are implemented (DSL-003), they will be promoted to errors
    // when global.strict is true. The infrastructure is in place - warnings just
    // need to be added to the compiler.
    if (global.strict && global.debug) {
        term.info("[debug] ");
        term.print("Strict mode enabled (warnings will be promoted to errors when implemented)");
        term.newline();
    }

    // Verbose: announce what we're doing
    if (global.verbose) {
        term.info("Compiling: ");
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

    // Process includes
    resolver.mark_included(input_path);  // Mark main file as included
    auto include_result =
        process_includes(resolver, input_path, source, term, global.verbose, global.debug);
    if (!include_result.success) {
        return include_result.code;
    }

    // Configure compiler options
    core::dsl::compiler::CompileOptions compile_opts;
    compile_opts.dump_ir = global.debug;              // Dump IR in debug mode
    compile_opts.granted_caps = opts.capabilities();  // DSL-004: Pass granted capabilities

    // Verbose: show granted capabilities
    if (global.verbose && compile_opts.granted_caps != core::capabilities::Permission::None) {
        term.info("Capabilities: ");
        term.print(core::capabilities::to_string(compile_opts.granted_caps));
        term.newline();
    }

    // Compile the source
    core::dsl::compiler::DslCompiler compiler(compile_opts);
    auto compile_result = compiler.compile_source(include_result.merged_source);

    if (!compile_result.has_value()) {
        print_compile_error(term, filename, source, compile_result.error());
        return ExitCode::CompilationError;
    }

    const auto& result = *compile_result;

    // Output IR if requested
    if (global.output_ir) {
        term.info("; IR output:");
        term.newline();
        std::string ir_str = core::dsl::ir::print_to_string(result.ir);
        term.print(ir_str);
    }

    // Determine output path
    std::filesystem::path output_path;
    if (opts.output_file.empty()) {
        output_path = FileResolver::default_output_path(input_path);
    } else {
        output_path = opts.output_file;
    }

    // Write bytecode to file
    if (!write_bytecode(output_path, result.bytecode, term)) {
        return ExitCode::IoError;
    }

    // Calculate elapsed time
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // Success message
    if (global.verbose) {
        term.success("Compiled successfully: ");
        term.print(output_path.string());
        term.print(" (" + std::to_string(result.bytecode.size()) + " bytes)");
        term.newline();
    }

    // Debug: show timing
    if (global.debug) {
        term.info("[debug] ");
        term.print("Compilation time: " + std::to_string(elapsed_ms) + "ms");
        term.newline();
    }

    return ExitCode::Success;
}

}  // namespace dotvm::cli::commands
