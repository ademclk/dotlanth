/// @file bench_runner.hpp
/// @brief TOOL-010 Benchmark runner interface
///
/// This header provides a clean interface for running benchmarks
/// without exposing Google Benchmark internals to the CLI layer.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dotvm::bench {

/// @brief Options for configuring benchmark execution
struct RunOptions {
    std::string filter;
    int repetitions = 3;
    double min_time = 0.5;
};

/// @brief Single benchmark result
struct Result {
    std::string name;
    double cpu_time_ns = 0.0;
    double real_time_ns = 0.0;
    std::uint64_t iterations = 0;
    double items_per_second = 0.0;
    double bytes_per_second = 0.0;
};

/// @brief Collection of benchmark results
struct Report {
    std::string version;
    std::string timestamp;
    std::vector<Result> results;
};

/// @brief Get list of available benchmark names
std::vector<std::string> get_benchmark_names();

/// @brief Run benchmarks and collect results
/// @param opts Run options (filter, repetitions, min_time)
/// @param report Output report to populate with results
/// @return true if benchmarks ran successfully
bool run_benchmarks(const RunOptions& opts, Report& report);

}  // namespace dotvm::bench
