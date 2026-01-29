/// @file bench_command.cpp
/// @brief TOOL-010 Benchmark command implementation

#include "bench_command.hpp"

#include <fstream>
#include <iostream>
#include <unordered_map>

// Include the benchmark runner interface (no Google Benchmark dependency)
#include "dotvm/bench/bench_runner.hpp"

namespace dotvm::cli::commands {

namespace {

/// @brief Load target times from a JSON-like target file (SEC-010)
///
/// Expected format (simple key=value per line):
///   BenchmarkName=target_time_ns
///
/// Example:
///   BM_Fibonacci/10=1000
///   BM_Memory/1024=5000
///
/// @param path Path to the target file
/// @param targets Output map of benchmark name to target time in nanoseconds
/// @return true on success, false on failure
bool load_targets(const std::string& path, std::unordered_map<std::string, double>& targets) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Find the '=' separator
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;  // Skip malformed lines
        }

        std::string name = line.substr(0, pos);
        std::string value_str = line.substr(pos + 1);

        // Trim whitespace
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
            name.pop_back();
        }
        while (!value_str.empty() && std::isspace(static_cast<unsigned char>(value_str.front()))) {
            value_str.erase(0, 1);
        }

        // Parse the target time
        try {
            double target_ns = std::stod(value_str);
            targets[name] = target_ns;
        } catch (const std::exception&) {
            // Skip lines with invalid numbers
            continue;
        }
    }

    return true;
}

}  // namespace

std::vector<std::string> list_benchmarks() {
    return bench::get_benchmark_names();
}

BenchExitCode execute_bench(const BenchOptions& opts, Terminal& term) {
    // Handle --list option
    if (opts.list_only) {
        auto names = list_benchmarks();
        std::cout << "Available benchmarks:\n";
        for (const auto& name : names) {
            std::cout << "  " << name << "\n";
        }
        return BenchExitCode::Success;
    }

    // Convert CLI options to runner options
    bench::RunOptions run_opts;
    run_opts.filter = opts.filter;
    run_opts.repetitions = opts.repetitions;
    run_opts.min_time = opts.min_time;

    // Run benchmarks and collect results
    bench::Report bench_report;
    if (!bench::run_benchmarks(run_opts, bench_report)) {
        term.error("Failed to run benchmarks\n");
        return BenchExitCode::ValidationError;
    }

    // Load targets if specified (SEC-010)
    std::unordered_map<std::string, double> targets;
    if (!opts.target_file.empty()) {
        if (!load_targets(opts.target_file, targets)) {
            term.error("Failed to load target file: " + opts.target_file + "\n");
            return BenchExitCode::IOError;
        }
    }

    // Convert benchmark report to CLI report
    BenchmarkReport report;
    report.version = bench_report.version;
    report.timestamp = bench_report.timestamp;
    for (const auto& r : bench_report.results) {
        BenchmarkResult result;
        result.name = r.name;
        result.cpu_time_ns = r.cpu_time_ns;
        result.real_time_ns = r.real_time_ns;
        result.iterations = r.iterations;
        result.items_per_second = r.items_per_second;
        result.bytes_per_second = r.bytes_per_second;

        // Apply target if available (SEC-010)
        auto it = targets.find(r.name);
        if (it != targets.end()) {
            result.target_cpu_time_ns = it->second;
        }

        report.results.push_back(result);
    }

    // Determine output stream
    std::ostream* out_stream = &std::cout;
    std::ofstream file_stream;

    if (!opts.output_file.empty()) {
        file_stream.open(opts.output_file);
        if (!file_stream) {
            term.error("Failed to open output file: " + opts.output_file + "\n");
            return BenchExitCode::IOError;
        }
        out_stream = &file_stream;
    }

    // Format and output results
    bool use_color = !opts.no_color && opts.output_file.empty();
    if (opts.force_color) {
        use_color = true;
    }

    switch (opts.format) {
        case BenchOutputFormat::Console:
            format_bench_console(*out_stream, report, use_color);
            break;
        case BenchOutputFormat::Json:
            format_bench_json(*out_stream, report);
            break;
        case BenchOutputFormat::Csv:
            format_bench_csv(*out_stream, report);
            break;
    }

    // SEC-010: In strict mode, check for target misses
    if (opts.strict && !report.all_targets_met()) {
        auto missed = report.missed_targets();
        term.error("Benchmark targets missed:\n");
        for (const auto& name : missed) {
            term.error("  - ");
            term.print(name);
            term.newline();
        }
        return BenchExitCode::TargetMissed;
    }

    return BenchExitCode::Success;
}

}  // namespace dotvm::cli::commands
