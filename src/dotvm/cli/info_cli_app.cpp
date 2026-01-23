/// @file info_cli_app.cpp
/// @brief TOOL-009 Bytecode Inspector CLI application implementation

#include "dotvm/cli/info_cli_app.hpp"

#include <CLI/CLI.hpp>
#include <iostream>

#include "dotvm/cli/commands/info_command.hpp"

namespace dotvm::cli {

namespace {

/// @brief Application description for help text
constexpr const char* kAppDescription = R"(DotVM Bytecode Inspector

A command-line tool for inspecting DotVM bytecode files, providing detailed
analysis including header information, constant pool statistics, code analysis
with opcode histograms, validation status, and execution cost estimates.)";

/// @brief Examples epilog for help text
constexpr const char* kExamplesEpilog = R"(
EXAMPLES:
    Inspect a bytecode file (summary):
        dotinfo program.dot

    Show detailed breakdown:
        dotinfo --detailed program.dot

    Output as JSON:
        dotinfo --format json program.dot

    Write to file:
        dotinfo -o output.txt program.dot

    Detailed JSON output:
        dotinfo --detailed --format json program.dot

For more information, visit: https://github.com/dotlanth/dotvm)";

}  // namespace

InfoCliApp::InfoCliApp() : app_(std::make_unique<CLI::App>(kAppDescription, "dotinfo")) {
    app_->set_version_flag("-v,--version", "0.1.0");
    app_->footer(kExamplesEpilog);

    // Configure formatter for better help output
    auto fmt = std::make_shared<CLI::Formatter>();
    fmt->column_width(40);
    app_->formatter(fmt);

    setup_options();
}

InfoCliApp::~InfoCliApp() = default;

InfoCliApp::InfoCliApp(InfoCliApp&&) noexcept = default;
InfoCliApp& InfoCliApp::operator=(InfoCliApp&&) noexcept = default;

void InfoCliApp::setup_options() {
    // Input file (positional argument)
    app_->add_option("file", opts_.input_file, "Input .dot bytecode file to inspect")
        ->check(CLI::ExistingFile)
        ->type_name("FILE");

    // Output options
    app_->add_option("-o,--output", opts_.output_file, "Write output to file")->type_name("FILE");

    // Format selection - use a string member and convert after parsing
    app_->add_option("--format", format_str_, "Output format: table or json (default: table)")
        ->check(CLI::IsMember({"table", "json"}))
        ->default_str("table")
        ->type_name("{table,json}");

    // Display options
    app_->add_flag("--detailed", opts_.detailed, "Show detailed breakdown (categories, checks)");

    // General options
    app_->add_flag("-q,--quiet", opts_.quiet, "Suppress non-essential output");
    app_->add_flag("--no-color", opts_.no_color, "Disable ANSI color codes in output");
    app_->add_flag("--color", opts_.force_color, "Force colors (useful for piping to less -R)");
}

InfoExitCode InfoCliApp::parse(int argc, const char* const* argv) {
    help_requested_ = false;
    format_str_ = "table";  // Reset to default

    try {
        app_->parse(argc, argv);

        // Convert format string to enum after successful parse
        if (format_str_ == "json") {
            opts_.format = InfoOutputFormat::Json;
        } else {
            opts_.format = InfoOutputFormat::Table;
        }

        return InfoExitCode::Success;
    } catch (const CLI::ParseError& e) {
        int result = app_->exit(e);
        if (result == 0) {
            // Help or version was requested
            help_requested_ = true;
            return InfoExitCode::Success;
        }
        return InfoExitCode::ValidationError;
    }
}

InfoExitCode InfoCliApp::run() {
    // If no input file, show help
    if (opts_.input_file.empty()) {
        std::cout << app_->help() << std::endl;
        return InfoExitCode::Success;
    }

    // Create terminal for output with color settings
    Terminal term = make_terminal(std::cerr, opts_);

    // Execute the inspection
    return commands::execute_info(opts_, term);
}

}  // namespace dotvm::cli
