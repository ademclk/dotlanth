/// @file bench_cli_app.cpp
/// @brief TOOL-010 Benchmark CLI application implementation

#include "dotvm/cli/bench_cli_app.hpp"

#include <CLI/CLI.hpp>
#include <iostream>

namespace dotvm::cli {

namespace {

/// @brief Application description for help text
constexpr const char* kAppDescription = R"(DotVM Benchmark Suite

A command-line tool for running performance benchmarks on DotVM components
including arithmetic operations, cryptographic functions, and VM state operations.)";

/// @brief Examples epilog for help text
constexpr const char* kExamplesEpilog = R"(
EXAMPLES:
    Run all benchmarks:
        dotvm_bench

    Run specific benchmarks:
        dotvm_bench --benchmark_filter=fib

    Run with more repetitions:
        dotvm_bench --benchmark_repetitions=10

    Output as JSON:
        dotvm_bench --format=json -o results.json

    List available benchmarks:
        dotvm_bench --list

    Strict mode (exit 3 if targets missed):
        dotvm_bench --strict

For more information, visit: https://github.com/dotlanth/dotvm)";

}  // namespace

BenchCliApp::BenchCliApp() : app_(std::make_unique<CLI::App>(kAppDescription, "dotvm_bench")) {
    app_->set_version_flag("-v,--version", "0.1.0");
    app_->footer(kExamplesEpilog);

    // Configure formatter for better help output
    auto fmt = std::make_shared<CLI::Formatter>();
    fmt->column_width(40);
    app_->formatter(fmt);

    setup_options();
}

BenchCliApp::~BenchCliApp() = default;

BenchCliApp::BenchCliApp(BenchCliApp&&) noexcept = default;
BenchCliApp& BenchCliApp::operator=(BenchCliApp&&) noexcept = default;

void BenchCliApp::setup_options() {
    // Benchmark filter option (Google Benchmark style)
    app_->add_option("--benchmark_filter", opts_.filter,
                     "Regex filter for benchmark names (e.g., 'fib|sort')")
        ->type_name("<regex>");

    // Benchmark repetitions
    app_->add_option("--benchmark_repetitions", opts_.repetitions,
                     "Number of repetitions per benchmark (default: 3)")
        ->check(CLI::PositiveNumber)
        ->type_name("<n>");

    // Minimum time per benchmark
    app_->add_option("--benchmark_min_time", opts_.min_time,
                     "Minimum time in seconds per benchmark (default: 0.5)")
        ->check(CLI::PositiveNumber)
        ->type_name("<sec>");

    // Format selection
    app_->add_option("--format", format_str_,
                     "Output format: console, json, or csv (default: console)")
        ->check(CLI::IsMember({"console", "json", "csv"}))
        ->default_str("console")
        ->type_name("{console,json,csv}");

    // Output file
    app_->add_option("-o,--output", opts_.output_file, "Write results to file")
        ->type_name("<file>");

    // List only flag
    app_->add_flag("--list", opts_.list_only, "List available benchmarks without running");

    // Strict mode flag
    app_->add_flag("--strict", opts_.strict, "Exit with code 3 if performance targets are missed");

    // Color options
    app_->add_flag("--no-color", opts_.no_color, "Disable ANSI color codes in output");
    app_->add_flag("--color", opts_.force_color, "Force colors (useful for piping to less -R)");

    // Help flag is automatically added by CLI11
}

BenchExitCode BenchCliApp::parse(int argc, const char* const* argv) {
    help_requested_ = false;
    format_str_ = "console";  // Reset to default

    try {
        app_->parse(argc, argv);

        // Convert format string to enum after successful parse
        if (format_str_ == "json") {
            opts_.format = BenchOutputFormat::Json;
        } else if (format_str_ == "csv") {
            opts_.format = BenchOutputFormat::Csv;
        } else {
            opts_.format = BenchOutputFormat::Console;
        }

        return BenchExitCode::Success;
    } catch (const CLI::ParseError& e) {
        int result = app_->exit(e);
        if (result == 0) {
            // Help or version was requested
            help_requested_ = true;
            return BenchExitCode::Success;
        }
        return BenchExitCode::ValidationError;
    }
}

BenchExitCode BenchCliApp::run() {
    // The actual benchmark execution is in bench_main.cpp which links
    // against the benchmark library. This method is a stub for testing.
    std::cerr << "Error: run() should not be called directly. Use bench_main.\n";
    return BenchExitCode::ValidationError;
}

}  // namespace dotvm::cli
