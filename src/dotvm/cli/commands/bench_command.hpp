/// @file bench_command.hpp
/// @brief TOOL-010 Benchmark command interface

#pragma once

#include <string>
#include <vector>

#include "dotvm/cli/bench_cli_app.hpp"
#include "dotvm/cli/bench_formatter.hpp"
#include "dotvm/cli/terminal.hpp"

namespace dotvm::cli::commands {

/// @brief List available benchmarks
/// @return Vector of benchmark names
std::vector<std::string> list_benchmarks();

/// @brief Execute benchmarks with given options
/// @param opts Benchmark options
/// @param term Terminal for output
/// @return Exit code
BenchExitCode execute_bench(const BenchOptions& opts, Terminal& term);

}  // namespace dotvm::cli::commands
