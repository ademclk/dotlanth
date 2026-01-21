/// @file cli_app.cpp
/// @brief DSL-003 CLI application implementation

#include "dotvm/cli/cli_app.hpp"

#include <CLI/CLI.hpp>
#include <iostream>

namespace dotvm::cli {

CliApp::CliApp() : app_(std::make_unique<CLI::App>("DotLanth DSL Compiler", "dotdsl")) {
    app_->set_version_flag("-v,--version", "0.1.0");
    app_->require_subcommand(0, 1);  // 0 to 1 subcommands allowed

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
    app_->add_flag("--verbose", global_opts_.verbose, "Enable verbose output");
    app_->add_flag("--debug", global_opts_.debug, "Enable debug output");
    app_->add_flag("--strict", global_opts_.strict, "Enable strict mode (warnings become errors)");
    app_->add_flag("--output-ir", global_opts_.output_ir, "Output IR for debugging");
    app_->add_flag("--no-color", global_opts_.no_color, "Disable colored output");
}

void CliApp::setup_compile_command() {
    compile_cmd_ = app_->add_subcommand("compile", "Compile DSL source to bytecode");

    compile_cmd_->add_option("file", compile_opts_.input_file, "Input .dsl file")
        ->required()
        ->check(CLI::ExistingFile);

    compile_cmd_->add_option("-o,--output", compile_opts_.output_file, "Output bytecode file")
        ->default_str("");
}

void CliApp::setup_check_command() {
    check_cmd_ = app_->add_subcommand("check", "Validate DSL syntax without compiling");

    check_cmd_->add_option("file", check_opts_.input_file, "Input .dsl file")
        ->required()
        ->check(CLI::ExistingFile);
}

void CliApp::setup_format_command() {
    format_cmd_ = app_->add_subcommand("format", "Auto-format DSL source code");

    format_cmd_->add_option("file", format_opts_.input_file, "Input .dsl file")
        ->required()
        ->check(CLI::ExistingFile);

    format_cmd_->add_flag("-i,--in-place", format_opts_.in_place, "Modify file in place");
}

void CliApp::setup_watch_command() {
    watch_cmd_ = app_->add_subcommand("watch", "Watch directory for changes and recompile");

    watch_cmd_->add_option("dir", watch_opts_.directory, "Directory to watch")
        ->required()
        ->check(CLI::ExistingDirectory);
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

    // Commands will be implemented in Phase 2
    // For now, just return success
    if (cmd == "compile") {
        if (global_opts_.verbose) {
            std::cerr << "Compiling: " << compile_opts_.input_file << std::endl;
        }
        // TODO: Implement in Phase 2
        return ExitCode::Success;
    }

    if (cmd == "check") {
        if (global_opts_.verbose) {
            std::cerr << "Checking: " << check_opts_.input_file << std::endl;
        }
        // TODO: Implement in Phase 2
        return ExitCode::Success;
    }

    if (cmd == "format") {
        if (global_opts_.verbose) {
            std::cerr << "Formatting: " << format_opts_.input_file << std::endl;
        }
        // TODO: Implement in Phase 3
        return ExitCode::Success;
    }

    if (cmd == "watch") {
        if (global_opts_.verbose) {
            std::cerr << "Watching: " << watch_opts_.directory << std::endl;
        }
        // TODO: Implement in Phase 4
        return ExitCode::Success;
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
