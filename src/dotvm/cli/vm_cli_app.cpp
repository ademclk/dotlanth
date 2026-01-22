/// @file vm_cli_app.cpp
/// @brief TOOL-005 VM CLI application implementation

#include "dotvm/cli/vm_cli_app.hpp"

#include <CLI/CLI.hpp>
#include <iostream>

#include "dotvm/cli/commands/vm_info_command.hpp"
#include "dotvm/cli/commands/vm_run_command.hpp"
#include "dotvm/cli/commands/vm_validate_command.hpp"

namespace dotvm::cli {

namespace {

/// @brief Application description for help text
constexpr const char* kAppDescription = R"(DotVM Bytecode Executor

A command-line tool for executing, validating, and inspecting DotVM bytecode files.
Loads .dot bytecode files and runs them on the DotVM virtual machine.)";

/// @brief Examples epilog for help text
constexpr const char* kExamplesEpilog = R"(
EXAMPLES:
    Execute a bytecode file:
        dotvm run program.dot

    Execute with debug tracing:
        dotvm run program.dot --debug

    Execute with instruction limit (for testing):
        dotvm run program.dot --limit 10000

    Validate bytecode without execution:
        dotvm validate program.dot

    Display bytecode file information:
        dotvm info program.dot

    Execute with verbose output:
        dotvm -v run program.dot

    Override architecture (ignore header):
        dotvm --arch 64 run program.dot

    Force colored output (for piping to less -R):
        dotvm --color info program.dot | less -R

For more information, visit: https://github.com/dotlanth/dotvm)";

/// @brief Architecture validator for CLI11
class ArchValidator : public CLI::Validator {
public:
    ArchValidator() : Validator("ARCH") {
        func_ = [](const std::string& str) -> std::string {
            if (str == "32" || str == "64") {
                return {};  // Valid
            }
            return "Architecture must be 32 or 64";
        };
    }
};

}  // namespace

VmCliApp::VmCliApp() : app_(std::make_unique<CLI::App>(kAppDescription, "dotvm")) {
    app_->set_version_flag("-v,--version", "0.1.0");
    app_->require_subcommand(0, 1);  // 0 to 1 subcommands allowed
    app_->footer(kExamplesEpilog);

    // Configure formatter for better help output
    auto fmt = std::make_shared<CLI::Formatter>();
    fmt->column_width(40);
    app_->formatter(fmt);

    setup_global_options();
    setup_run_command();
    setup_validate_command();
    setup_info_command();
}

VmCliApp::~VmCliApp() = default;

VmCliApp::VmCliApp(VmCliApp&&) noexcept = default;
VmCliApp& VmCliApp::operator=(VmCliApp&&) noexcept = default;

void VmCliApp::setup_global_options() {
    // Create option group for global flags
    auto* global_group = app_->add_option_group("Global Options");

    global_group->add_flag("--verbose", global_opts_.verbose,
                           "Enable verbose output (show file info before execution)");

    global_group->add_flag("-q,--quiet", global_opts_.quiet,
                           "Suppress non-essential output (only show results)");

    global_group->add_option("-o,--output", global_opts_.output_file, "Write output to file")
        ->type_name("FILE");

    global_group->add_flag("--no-color", global_opts_.no_color,
                           "Disable ANSI color codes in output");

    global_group->add_flag("--color", global_opts_.force_color,
                           "Force colors (useful for piping to less -R)");

    // Architecture override with custom validator
    global_group
        ->add_option("--arch", global_opts_.arch_override,
                     "Override architecture (ignore header): 32 or 64")
        ->check(ArchValidator())
        ->type_name("{32,64}");
}

void VmCliApp::setup_run_command() {
    run_cmd_ = app_->add_subcommand("run", "Execute bytecode file");
    run_cmd_->description(
        "Loads and executes a .dot bytecode file.\n"
        "Displays execution result, R1 register value, instruction count, and timing.");

    run_cmd_->add_option("file", run_opts_.input_file, "Input .dot bytecode file to execute")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("FILE");

    run_cmd_->add_flag("-d,--debug", run_opts_.debug,
                       "Enable debug mode with full instruction trace");

    run_cmd_
        ->add_option("-l,--limit", run_opts_.instruction_limit,
                     "Maximum instructions to execute (0 = unlimited)")
        ->default_val(0)
        ->type_name("N");
}

void VmCliApp::setup_validate_command() {
    validate_cmd_ = app_->add_subcommand("validate", "Validate bytecode file");
    validate_cmd_->description(
        "Validates a .dot bytecode file without execution.\n"
        "Checks magic bytes, version, architecture, section bounds, and constant pool.");

    validate_cmd_->add_option("file", validate_opts_.input_file, "Input .dot bytecode file")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("FILE");
}

void VmCliApp::setup_info_command() {
    info_cmd_ = app_->add_subcommand("info", "Display bytecode file information");
    info_cmd_->description("Displays detailed information about a .dot bytecode file.\n"
                           "Shows header fields, section sizes, and flags.");

    info_cmd_->add_option("file", info_opts_.input_file, "Input .dot bytecode file")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("FILE");
}

VmExitCode VmCliApp::parse(int argc, const char* const* argv) {
    help_requested_ = false;
    try {
        app_->parse(argc, argv);
        return VmExitCode::Success;
    } catch (const CLI::ParseError& e) {
        // Let CLI11 handle the error output
        int result = app_->exit(e);
        if (result == 0) {
            // Help or version was requested
            help_requested_ = true;
            return VmExitCode::Success;
        }
        return VmExitCode::ValidationError;  // Parse errors are validation failures
    }
}

VmExitCode VmCliApp::run() {
    // Get the subcommand that was selected
    std::string cmd = current_subcommand();

    if (cmd.empty()) {
        // No subcommand selected, show help
        std::cout << app_->help() << std::endl;
        return VmExitCode::Success;
    }

    // Create terminal for output with color settings
    Terminal term = make_terminal(std::cerr, global_opts_);

    if (cmd == "run") {
        return commands::execute_run(run_opts_, global_opts_, term);
    }

    if (cmd == "validate") {
        return commands::execute_validate(validate_opts_, global_opts_, term);
    }

    if (cmd == "info") {
        return commands::execute_info(info_opts_, global_opts_, term);
    }

    return VmExitCode::Success;
}

std::string VmCliApp::current_subcommand() const {
    for (auto* sub : app_->get_subcommands()) {
        if (sub->parsed()) {
            return sub->get_name();
        }
    }
    return "";
}

}  // namespace dotvm::cli
