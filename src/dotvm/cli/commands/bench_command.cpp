/// @file bench_command.cpp
/// @brief TOOL-010 Benchmark command implementation

#include "dotvm/cli/commands/bench_command.hpp"

#include <benchmark/benchmark.h>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>

namespace dotvm::cli::commands {

namespace {

/// @brief Get current ISO 8601 timestamp
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};

#if defined(_WIN32)
    gmtime_s(&tm_now, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_now);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/// @brief Custom reporter that collects results into a BenchmarkReport
class ReportCollector : public benchmark::BenchmarkReporter {
public:
    explicit ReportCollector(BenchmarkReport& report) : report_(report) {}

    bool ReportContext(const Context& /*context*/) override { return true; }

    void ReportRuns(const std::vector<Run>& runs) override {
        for (const auto& run : runs) {
            // In Google Benchmark 1.8.x, skip_message being non-empty indicates skipped
            if (run.skip_message.empty()) {
                BenchmarkResult result;
                result.name = run.benchmark_name();
                result.cpu_time_ns = run.GetAdjustedCPUTime();
                result.real_time_ns = run.GetAdjustedRealTime();
                result.iterations = static_cast<std::uint64_t>(run.iterations);

                // Extract items_per_second if set
                auto it = run.counters.find("items_per_second");
                if (it != run.counters.end()) {
                    result.items_per_second = it->second;
                }

                // Extract bytes_per_second if set
                it = run.counters.find("bytes_per_second");
                if (it != run.counters.end()) {
                    result.bytes_per_second = it->second;
                }

                report_.results.push_back(result);
            }
        }
    }

    void Finalize() override {}

private:
    BenchmarkReport& report_;
};

/// @brief Known benchmark names (hard-coded since internal API is not public)
const std::vector<std::string> kKnownBenchmarks = {
    "BM_Fibonacci/10",
    "BM_Fibonacci/20",
    "BM_Fibonacci/35",
    "BM_Fibonacci/50",
    "BM_Fibonacci/100",
    "BM_Quicksort/100",
    "BM_Quicksort/1000",
    "BM_Quicksort/10000",
    "BM_Quicksort/100000",
    "BM_SHA256/64",
    "BM_SHA256/1024",
    "BM_SHA256/65536",
    "BM_SHA256/1048576",
    "BM_RegisterReadWrite/100",
    "BM_RegisterReadWrite/1000",
    "BM_RegisterReadWrite/10000",
    "BM_MemoryReadWrite/100",
    "BM_MemoryReadWrite/1000",
    "BM_MemoryReadWrite/10000",
    "BM_MemoryAllocate/64",
    "BM_MemoryAllocate/4096",
    "BM_MemoryAllocate/65536",
    "BM_MemoryAllocate/1048576",
    "BM_ValueCreation",
    "BM_ValueTypeCheck",
};

}  // namespace

std::vector<std::string> list_benchmarks() {
    return kKnownBenchmarks;
}

bool run_benchmarks(const BenchOptions& opts, BenchmarkReport& report) {
    // Set up report metadata
    report.version = "0.1.0";
    report.timestamp = get_timestamp();

    // Configure Google Benchmark
    int argc = 1;
    const char* argv_storage[] = {"dotvm_bench", nullptr, nullptr, nullptr, nullptr};
    const char** argv = argv_storage;

    std::string filter_arg;
    std::string reps_arg;
    std::string min_time_arg;

    if (!opts.filter.empty()) {
        filter_arg = "--benchmark_filter=" + opts.filter;
        argv_storage[argc++] = filter_arg.c_str();
    }

    if (opts.repetitions != 3) {
        reps_arg = "--benchmark_repetitions=" + std::to_string(opts.repetitions);
        argv_storage[argc++] = reps_arg.c_str();
    }

    if (opts.min_time != 0.5) {
        std::ostringstream oss;
        oss << "--benchmark_min_time=" << opts.min_time;
        min_time_arg = oss.str();
        argv_storage[argc++] = min_time_arg.c_str();
    }

    // Initialize Google Benchmark
    benchmark::Initialize(&argc, const_cast<char**>(argv));

    // Create our custom reporter to collect results
    ReportCollector collector(report);

    // Run benchmarks with our collector
    benchmark::RunSpecifiedBenchmarks(&collector);

    return true;
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

    // Run benchmarks and collect results
    BenchmarkReport report;
    if (!run_benchmarks(opts, report)) {
        term.error("Failed to run benchmarks\n");
        return BenchExitCode::ValidationError;
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
