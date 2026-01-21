/// @file compile_command.cpp
/// @brief DSL-003 Compile command implementation

#include "dotvm/cli/commands/compile_command.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "dotvm/cli/file_resolver.hpp"
#include "dotvm/core/dsl/compiler/dsl_compiler.hpp"
#include "dotvm/core/dsl/ir/printer.hpp"

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

}  // namespace

ExitCode execute_compile(const CompileOptions& opts, const GlobalOptions& global, Terminal& term) {
    auto start_time = std::chrono::steady_clock::now();

    // TODO(DSL-003): Use global.strict when compiler produces warnings.
    // Currently the compiler only produces errors, so strict mode has no effect.
    // When warnings are implemented, they should be promoted to errors when
    // global.strict is true.
    (void)global.strict;

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

    // Configure compiler options
    core::dsl::compiler::CompileOptions compile_opts;
    compile_opts.dump_ir = global.debug;  // Dump IR in debug mode

    // Compile the source
    core::dsl::compiler::DslCompiler compiler(compile_opts);
    auto compile_result = compiler.compile_source(source);

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
