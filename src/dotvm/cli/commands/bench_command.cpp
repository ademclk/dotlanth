/// @file bench_command.cpp
/// @brief TOOL-010 Benchmark command implementation

#include "bench_command.hpp"

#include <fstream>
#include <iostream>

// Include the benchmark runner interface (no Google Benchmark dependency)
#include "dotvm/bench/bench_runner.hpp"

namespace dotvm::cli::commands {

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

    // In strict mode, check for target misses (placeholder for future implementation)
    if (opts.strict) {
        // TODO: Implement target checking
        // For now, always succeed
    }

    return BenchExitCode::Success;
}

}  // namespace dotvm::cli::commands
