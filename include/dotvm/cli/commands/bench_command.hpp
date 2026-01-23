#pragma once

/// @file bench_command.hpp
/// @brief TOOL-010 Benchmark command declarations
///
/// Executes the benchmark suite with configurable options.

#include <string>
#include <vector>

#include "dotvm/cli/bench_cli_app.hpp"
#include "dotvm/cli/bench_formatter.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief List available benchmark names
///
/// Returns a list of all registered benchmark names that can be used
/// with the --benchmark_filter option.
///
/// @return Vector of benchmark names
[[nodiscard]] std::vector<std::string> list_benchmarks();

/// @brief Execute the benchmark suite
///
/// Runs the benchmarks according to the specified options and outputs
/// results in the requested format.
///
/// @param opts Benchmark options
/// @param term Terminal for colored output (used for errors/warnings)
/// @return BenchExitCode::Success on success, error code otherwise
[[nodiscard]] BenchExitCode execute_bench(const BenchOptions& opts, Terminal& term);

/// @brief Run benchmarks and collect results
///
/// Internal function that executes benchmarks and populates a report.
///
/// @param opts Benchmark options
/// @param report Output report to populate
/// @return true on success, false on error
[[nodiscard]] bool run_benchmarks(const BenchOptions& opts, BenchmarkReport& report);

}  // namespace dotvm::cli::commands
