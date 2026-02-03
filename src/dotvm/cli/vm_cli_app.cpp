/// @file vm_cli_app.cpp
/// @brief TOOL-005 VM CLI application implementation

#include "dotvm/cli/vm_cli_app.hpp"

#include <CLI/CLI.hpp>
#include <iostream>

#include "dotvm/cli/commands/vm_bench_command.hpp"
#include "dotvm/cli/commands/vm_debug_command.hpp"
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

    Benchmark bytecode execution:
        dotvm bench program.dot --warmup 50 --runs 200

    Benchmark with baseline comparison:
        dotvm bench program.dot --compare baseline.json --threshold 10.0

    Save benchmark results as baseline:
        dotvm bench program.dot --save-baseline baseline.json

    Generate flamegraph data:
        dotvm bench program.dot --flamegraph --flamegraph-out perf.folded

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
    setup_debug_command();
    setup_bench_command();
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

void VmCliApp::setup_debug_command() {
    debug_cmd_ = app_->add_subcommand("debug", "Interactive debugger (REPL)");
    debug_cmd_->description("Launches an interactive debugger for a .dot bytecode file.\n"
                            "Provides LLDB-style commands: breakpoints, stepping, inspection.");

    debug_cmd_->add_option("file", debug_opts_.input_file, "Input .dot bytecode file to debug")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("FILE");

    debug_cmd_->add_flag("--no-color", debug_opts_.no_color,
                         "Disable ANSI colors in debugger output");
}

void VmCliApp::setup_bench_command() {
    bench_cmd_ = app_->add_subcommand("bench", "Benchmark bytecode execution");
    bench_cmd_->description(
        "Benchmarks a .dot bytecode file execution.\n"
        "Provides timing statistics, baseline comparison, and optional flamegraph generation.");

    // Input file (positional argument)
    bench_cmd_->add_option("file", bench_opts_.input_file, "Input .dot bytecode file to benchmark")
        ->required()
        ->check(CLI::ExistingFile)
        ->type_name("FILE");

    // Benchmark configuration
    bench_cmd_->add_option("-w,--warmup", bench_opts_.warmup_iterations, "Warm-up iterations")
        ->default_val(10)
        ->type_name("N");

    bench_cmd_->add_option("-n,--runs", bench_opts_.measurement_runs, "Number of measurement runs")
        ->default_val(100)
        ->type_name("N");

    bench_cmd_
        ->add_option("-l,--limit", bench_opts_.instruction_limit,
                     "Max instructions per run (0 = unlimited)")
        ->default_val(0)
        ->type_name("N");

    // Baseline comparison
    bench_cmd_->add_option("--compare", bench_opts_.baseline_file, "Compare against baseline JSON")
        ->check(CLI::ExistingFile)
        ->type_name("FILE");

    bench_cmd_
        ->add_option("--save-baseline", bench_opts_.save_baseline_path, "Save results as baseline")
        ->type_name("FILE");

    bench_cmd_
        ->add_option("--threshold", bench_opts_.regression_threshold,
                     "Regression threshold percentage")
        ->default_val(5.0)
        ->check(CLI::Range(0.0, 100.0))
        ->type_name("PCT");

    // Flamegraph generation
    bench_cmd_->add_flag("--flamegraph", bench_opts_.generate_flamegraph,
                         "Generate flamegraph folded stacks");

    bench_cmd_
        ->add_option("--flamegraph-out", bench_opts_.flamegraph_output,
                     "Output path for flamegraph (default: <input>.folded)")
        ->type_name("FILE");

    bench_cmd_
        ->add_option("--sample-rate", bench_opts_.sample_rate_instructions,
                     "Sample every N instructions for flamegraph")
        ->default_val(1000)
        ->type_name("N");

    // Output formatting
    bench_cmd_->add_option("--format", bench_format_str_, "Output format: console, json, csv")
        ->default_str("console")
        ->check(CLI::IsMember({"console", "json", "csv"}))
        ->type_name("FMT");

    bench_cmd_->add_option("-o,--output", bench_opts_.output_file, "Write results to file")
        ->type_name("FILE");

    // Post-parse callback to convert format string to enum
    bench_cmd_->callback([this]() {
        if (bench_format_str_ == "json") {
            bench_opts_.format = VmBenchOutputFormat::Json;
        } else if (bench_format_str_ == "csv") {
            bench_opts_.format = VmBenchOutputFormat::Csv;
        } else {
            bench_opts_.format = VmBenchOutputFormat::Console;
        }
        // Mark save_baseline true if path was provided
        bench_opts_.save_baseline = !bench_opts_.save_baseline_path.empty();
    });
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

    if (cmd == "debug") {
        return commands::execute_debug(debug_opts_, global_opts_, term);
    }

    if (cmd == "bench") {
        return commands::execute_bench(bench_opts_, global_opts_, term);
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
