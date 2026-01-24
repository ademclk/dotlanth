/// @file bench_main.cpp
/// @brief TOOL-010 Benchmark CLI entry point

#include <cstdlib>
#include <fstream>
#include <iostream>

#include "dotvm/cli/bench_cli_app.hpp"
#include "dotvm/cli/bench_formatter.hpp"
#include "dotvm/cli/commands/bench_command.hpp"

int main(int argc, char* argv[]) {
    dotvm::cli::BenchCliApp app;

    auto parse_result = app.parse(argc, argv);
    if (parse_result != dotvm::cli::BenchExitCode::Success) {
        return static_cast<int>(parse_result);
    }

    // If help/version was requested, parse() already handled output
    if (app.help_requested()) {
        return static_cast<int>(dotvm::cli::BenchExitCode::Success);
    }

    const auto& opts = app.options();

    // Create terminal for error output
    dotvm::cli::Terminal term = dotvm::cli::make_terminal(std::cerr, opts);

    // Execute the benchmarks
    auto run_result = dotvm::cli::commands::execute_bench(opts, term);
    return static_cast<int>(run_result);
}
