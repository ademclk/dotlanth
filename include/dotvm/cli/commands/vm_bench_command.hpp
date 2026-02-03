#pragma once

/// @file vm_bench_command.hpp
/// @brief Benchmark command for the dotvm CLI (CLI-005 Benchmark Runner)
///
/// Provides execution benchmarking with warm-up runs, statistical analysis,
/// baseline comparison, and optional flamegraph generation.

#include "dotvm/cli/terminal.hpp"
#include "dotvm/cli/vm_cli_app.hpp"

namespace dotvm::cli::commands {

/// @brief Execute the bench command
///
/// Runs bytecode multiple times, collecting timing statistics.
/// Optionally compares against a baseline and generates flamegraph data.
///
/// @param opts Benchmark options from CLI parsing
/// @param global Global options (verbose, quiet, colors, etc.)
/// @param term Terminal for colored output
/// @return Exit code: Success, RuntimeError, or ValidationError
[[nodiscard]] VmExitCode execute_bench(const VmBenchOptions& opts, const VmGlobalOptions& global,
                                       Terminal& term);

}  // namespace dotvm::cli::commands
