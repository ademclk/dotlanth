/// @file cli_app.cpp
/// @brief DSL-003 CLI application implementation

#include "dotvm/cli/cli_app.hpp"

#include <CLI/CLI.hpp>
#include <iostream>

#include "dotvm/cli/commands/check_command.hpp"
#include "dotvm/cli/commands/compile_command.hpp"
#include "dotvm/cli/commands/format_command.hpp"
#include "dotvm/cli/commands/watch_command.hpp"

namespace dotvm::cli {

namespace {

/// @brief Application description for help text
constexpr const char* kAppDescription = R"(DotLanth DSL Compiler

A compiler for the DotLanth domain-specific language (DSL).
Compiles .dsl source files into .dot bytecode for the DotVM runtime.)";

/// @brief Examples epilog for help text
constexpr const char* kExamplesEpilog = R"(
EXAMPLES:
    Compile a DSL file to bytecode:
        dotdsl compile hello.dsl

    Compile with custom output path:
        dotdsl compile hello.dsl -o output/hello.dot

    Compile with include search path:
        dotdsl compile main.dsl -I ./lib -I ./vendor

    Validate syntax without compilation:
        dotdsl check hello.dsl

    Format a DSL file (print to stdout):
        dotdsl format hello.dsl

    Format a DSL file in place:
        dotdsl format hello.dsl --in-place

    Watch a directory for changes and recompile:
        dotdsl watch ./src

    Compile with verbose output and IR dump:
        dotdsl --verbose --output-ir compile hello.dsl

For more information, visit: https://github.com/dotlanth/dotvm)";

}  // namespace

CliApp::CliApp() : app_(std::make_unique<CLI::App>(kAppDescription, "dotdsl")) {
    app_->set_version_flag("-v,--version", "0.1.0");
    app_->require_subcommand(0, 1);  // 0 to 1 subcommands allowed
    app_->footer(kExamplesEpilog);

    // Configure formatter for better help output
    auto fmt = std::make_shared<CLI::Formatter>();
    fmt->column_width(40);
    app_->formatter(fmt);

    setup_global_options();
    setup_compile_command();
    setup_check_command();
    setup_format_command();
    setup_watch_command();
}

CliApp::~CliApp() = default;

CliApp::CliApp(CliApp&&) noexcept = default;
CliApp& CliApp::operator=(CliApp&&) noexcept = default;

void CliApp::setup_global_options() {
    // Create option group for global flags
    auto* global_group = app_->add_option_group("Global Options");

    global_group->add_flag("--verbose", global_opts_.verbose,
                           "Enable verbose output (show progress and details)");
    global_group->add_flag("--debug", global_opts_.debug,
                           "Enable debug output (show internal state and timing)");
    global_group->add_flag("--strict", global_opts_.strict,
                           "Enable strict mode (treat warnings as errors)");
    global_group->add_flag("--output-ir", global_opts_.output_ir,
                           "Print intermediate representation (IR) to stderr");
    global_group->add_flag("--no-color", global_opts_.no_color,
                           "Disable ANSI color codes in output");
}

void CliApp::setup_compile_command() {
    compile_cmd_ = app_->add_subcommand("compile", "Compile DSL source file to bytecode");
    compile_cmd_->description(
        "Compiles a .dsl source file into .dot bytecode.\n"
        "The output file defaults to the input filename with .dot extension.");

    compile_cmd_->add_option("file", compile_opts_.input_file, "Input .dsl source file to compile")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("FILE");

    compile_cmd_
        ->add_option("-o,--output", compile_opts_.output_file,
                     "Output bytecode file path (default: <input>.dot)")
        ->type_name("FILE");

    compile_cmd_
        ->add_option("-I,--include", compile_opts_.include_paths,
                     "Add directory to include search path (can be specified multiple times)")
        ->check(CLI::ExistingDirectory)
        ->type_name("DIR")
        ->take_all();
}

void CliApp::setup_check_command() {
    check_cmd_ = app_->add_subcommand("check", "Validate DSL syntax without compiling");
    check_cmd_->description("Parses and validates a .dsl source file without producing bytecode.\n"
                            "Useful for syntax checking and catching errors quickly.");

    check_cmd_->add_option("file", check_opts_.input_file, "Input .dsl source file to validate")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("FILE");

    check_cmd_
        ->add_option("-I,--include", check_opts_.include_paths,
                     "Add directory to include search path (can be specified multiple times)")
        ->check(CLI::ExistingDirectory)
        ->type_name("DIR")
        ->take_all();
}

void CliApp::setup_format_command() {
    format_cmd_ = app_->add_subcommand("format", "Auto-format DSL source code");
    format_cmd_->description(
        "Formats a .dsl source file according to the canonical style.\n"
        "By default, prints formatted output to stdout. Use --in-place to modify the file.");

    format_cmd_->add_option("file", format_opts_.input_file, "Input .dsl source file to format")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("FILE");

    format_cmd_->add_flag("-i,--in-place", format_opts_.in_place,
                          "Modify the file in place instead of printing to stdout");
}

void CliApp::setup_watch_command() {
    watch_cmd_ =
        app_->add_subcommand("watch", "Watch for file changes and recompile automatically");
    watch_cmd_->description(
        "Watches a directory or file for changes to .dsl files.\n"
        "Automatically recompiles when changes are detected. Press Ctrl+C to stop.");

    watch_cmd_
        ->add_option("path", watch_opts_.directory, "Directory or .dsl file to watch for changes")
        ->required()
        ->check(CLI::ExistingPath)
        ->type_name("PATH");
}

ExitCode CliApp::parse(int argc, const char* const* argv) {
    help_requested_ = false;
    try {
        app_->parse(argc, argv);
        return ExitCode::Success;
    } catch (const CLI::ParseError& e) {
        // Let CLI11 handle the error output
        int result = app_->exit(e);
        if (result == 0) {
            // Help or version was requested
            help_requested_ = true;
            return ExitCode::Success;
        }
        return ExitCode::ParseError;
    }
}

ExitCode CliApp::run() {
    // Get the subcommand that was selected
    std::string cmd = current_subcommand();

    if (cmd.empty()) {
        // No subcommand selected, show help
        std::cout << app_->help() << std::endl;
        return ExitCode::Success;
    }

    // Create terminal for output with color settings
    Terminal term(std::cerr, global_opts_.no_color);

    if (cmd == "compile") {
        return commands::execute_compile(compile_opts_, global_opts_, term);
    }

    if (cmd == "check") {
        return commands::execute_check(check_opts_, global_opts_, term);
    }

    if (cmd == "format") {
        return commands::execute_format(format_opts_, global_opts_, term);
    }

    if (cmd == "watch") {
        return commands::execute_watch(watch_opts_, global_opts_, term);
    }

    return ExitCode::Success;
}

std::string CliApp::current_subcommand() const {
    for (auto* sub : app_->get_subcommands()) {
        if (sub->parsed()) {
            return sub->get_name();
        }
    }
    return "";
}

}  // namespace dotvm::cli
