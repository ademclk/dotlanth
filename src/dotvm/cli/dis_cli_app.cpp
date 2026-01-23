/// @file dis_cli_app.cpp
/// @brief TOOL-008 Disassembler CLI application implementation

#include "dotvm/cli/dis_cli_app.hpp"

#include <CLI/CLI.hpp>
#include <iostream>

#include "dotvm/cli/commands/dis_command.hpp"

namespace dotvm::cli {

namespace {

/// @brief Application description for help text
constexpr const char* kAppDescription = R"(DotVM Bytecode Disassembler

A command-line tool for disassembling DotVM bytecode files back into
human-readable assembly format with control flow analysis.)";

/// @brief Examples epilog for help text
constexpr const char* kExamplesEpilog = R"(
EXAMPLES:
    Disassemble a bytecode file:
        dotdis program.dot

    Show raw instruction bytes:
        dotdis --show-bytes program.dot

    Use labels for branch targets:
        dotdis --show-labels program.dot

    Output as JSON:
        dotdis --format json program.dot

    Write to file:
        dotdis -o output.asm program.dot

    Full featured output:
        dotdis --show-bytes --show-labels --annotate program.dot

For more information, visit: https://github.com/dotlanth/dotvm)";

}  // namespace

DisCliApp::DisCliApp() : app_(std::make_unique<CLI::App>(kAppDescription, "dotdis")) {
    app_->set_version_flag("-v,--version", "0.1.0");
    app_->footer(kExamplesEpilog);

    // Configure formatter for better help output
    auto fmt = std::make_shared<CLI::Formatter>();
    fmt->column_width(40);
    app_->formatter(fmt);

    setup_options();
}

DisCliApp::~DisCliApp() = default;

DisCliApp::DisCliApp(DisCliApp&&) noexcept = default;
DisCliApp& DisCliApp::operator=(DisCliApp&&) noexcept = default;

void DisCliApp::setup_options() {
    // Input file (positional argument)
    app_->add_option("file", opts_.input_file, "Input .dot bytecode file to disassemble")
        ->check(CLI::ExistingFile)
        ->type_name("FILE");

    // Output options
    app_->add_option("-o,--output", opts_.output_file, "Write output to file")->type_name("FILE");

    // Format selection - use a string member and convert after parsing
    app_->add_option("--format", format_str_, "Output format: text or json (default: text)")
        ->check(CLI::IsMember({"text", "json"}))
        ->default_str("text")
        ->type_name("{text,json}");

    // Display options
    app_->add_flag("--show-bytes", opts_.show_bytes, "Show raw instruction bytes");
    app_->add_flag("--show-labels", opts_.show_labels, "Mark branch targets with labels");
    app_->add_flag("--annotate", opts_.annotate, "Add comments for patterns (entry, loops)");

    // General options
    app_->add_flag("--verbose", opts_.verbose, "Enable verbose output (show file info)");
    app_->add_flag("-q,--quiet", opts_.quiet, "Suppress non-essential output");
    app_->add_flag("--no-color", opts_.no_color, "Disable ANSI color codes in output");
    app_->add_flag("--color", opts_.force_color, "Force colors (useful for piping to less -R)");
}

DisExitCode DisCliApp::parse(int argc, const char* const* argv) {
    help_requested_ = false;
    format_str_ = "text";  // Reset to default

    try {
        app_->parse(argc, argv);

        // Convert format string to enum after successful parse
        if (format_str_ == "json") {
            opts_.format = DisOutputFormat::Json;
        } else {
            opts_.format = DisOutputFormat::Text;
        }

        return DisExitCode::Success;
    } catch (const CLI::ParseError& e) {
        int result = app_->exit(e);
        if (result == 0) {
            // Help or version was requested
            help_requested_ = true;
            return DisExitCode::Success;
        }
        return DisExitCode::ValidationError;
    }
}

DisExitCode DisCliApp::run() {
    // If no input file, show help
    if (opts_.input_file.empty()) {
        std::cout << app_->help() << std::endl;
        return DisExitCode::Success;
    }

    // Create terminal for output with color settings
    Terminal term = make_terminal(std::cerr, opts_);

    // Execute the disassembly
    return commands::execute_disassemble(opts_, term);
}

}  // namespace dotvm::cli
