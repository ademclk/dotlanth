/// @file bench_formatter.cpp
/// @brief TOOL-010 Benchmark output formatter implementation

#include "dotvm/cli/bench_formatter.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace dotvm::cli {

namespace {

// Column widths for console output
constexpr int kNameWidth = 40;
constexpr int kTimeWidth = 15;
constexpr int kCpuWidth = 15;
constexpr int kIterWidth = 15;
constexpr int kThroughputWidth = 15;

/// @brief Print a horizontal separator line
void print_separator(std::ostream& out, int width) {
    for (int i = 0; i < width; ++i) {
        out << '-';
    }
    out << '\n';
}

/// @brief Escape a string for JSON output
std::string json_escape(const std::string& s) {
    std::ostringstream out;
    for (char c : s) {
        switch (c) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << c;
        }
    }
    return out.str();
}

}  // namespace

std::string format_time_human(double time_ns) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);

    if (time_ns < 1000.0) {
        // Nanoseconds
        out << time_ns << " ns";
    } else if (time_ns < 1000000.0) {
        // Microseconds
        out << (time_ns / 1000.0) << " us";
    } else if (time_ns < 1000000000.0) {
        // Milliseconds
        out << (time_ns / 1000000.0) << " ms";
    } else {
        // Seconds
        out << (time_ns / 1000000000.0) << " s";
    }

    return out.str();
}

std::string format_throughput(double items_per_sec) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);

    if (items_per_sec < 1000.0) {
        out << items_per_sec << "/s";
    } else if (items_per_sec < 1000000.0) {
        out << (items_per_sec / 1000.0) << "K/s";
    } else if (items_per_sec < 1000000000.0) {
        out << (items_per_sec / 1000000.0) << "M/s";
    } else {
        out << (items_per_sec / 1000000000.0) << "G/s";
    }

    return out.str();
}

std::string format_bytes_per_second(double bytes_per_sec) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);

    if (bytes_per_sec < 1024.0) {
        out << bytes_per_sec << " B/s";
    } else if (bytes_per_sec < 1024.0 * 1024.0) {
        out << (bytes_per_sec / 1024.0) << " KB/s";
    } else if (bytes_per_sec < 1024.0 * 1024.0 * 1024.0) {
        out << (bytes_per_sec / (1024.0 * 1024.0)) << " MB/s";
    } else {
        out << (bytes_per_sec / (1024.0 * 1024.0 * 1024.0)) << " GB/s";
    }

    return out.str();
}

void format_bench_console(std::ostream& out, const BenchmarkReport& report, bool use_color) {
    // Total width for separator
    const int total_width = kNameWidth + kTimeWidth + kCpuWidth + kIterWidth + kThroughputWidth;

    // Header
    out << "DotVM Benchmark Suite v" << report.version << '\n';
    print_separator(out, total_width);

    // Column headers
    out << std::left << std::setw(kNameWidth) << "Benchmark" << std::right << std::setw(kTimeWidth)
        << "Time" << std::setw(kCpuWidth) << "CPU" << std::setw(kIterWidth) << "Iterations"
        << std::setw(kThroughputWidth) << "Items/s" << '\n';
    print_separator(out, total_width);

    // Results
    for (const auto& result : report.results) {
        out << std::left << std::setw(kNameWidth) << result.name << std::right
            << std::setw(kTimeWidth) << format_time_human(result.real_time_ns)
            << std::setw(kCpuWidth) << format_time_human(result.cpu_time_ns)
            << std::setw(kIterWidth) << result.iterations;

        if (result.items_per_second > 0.0) {
            out << std::setw(kThroughputWidth) << format_throughput(result.items_per_second);
        } else if (result.bytes_per_second > 0.0) {
            out << std::setw(kThroughputWidth) << format_bytes_per_second(result.bytes_per_second);
        }

        out << '\n';
    }

    // Footer separator
    print_separator(out, total_width);

    // Unused parameter - reserved for future color support
    (void)use_color;
}

void format_bench_json(std::ostream& out, const BenchmarkReport& report) {
    out << "{\n";
    out << "  \"version\": \"" << json_escape(report.version) << "\",\n";
    out << "  \"timestamp\": \"" << json_escape(report.timestamp) << "\",\n";
    out << "  \"benchmarks\": [\n";

    for (std::size_t i = 0; i < report.results.size(); ++i) {
        const auto& result = report.results[i];
        out << "    {\n";
        out << "      \"name\": \"" << json_escape(result.name) << "\",\n";
        out << "      \"cpu_time_ns\": " << std::fixed << std::setprecision(2) << result.cpu_time_ns
            << ",\n";
        out << "      \"real_time_ns\": " << std::fixed << std::setprecision(2)
            << result.real_time_ns << ",\n";
        out << "      \"iterations\": " << result.iterations;

        if (result.items_per_second > 0.0) {
            out << ",\n      \"items_per_second\": " << std::fixed << std::setprecision(2)
                << result.items_per_second;
        }

        if (result.bytes_per_second > 0.0) {
            out << ",\n      \"bytes_per_second\": " << std::fixed << std::setprecision(2)
                << result.bytes_per_second;
        }

        out << "\n    }";
        if (i + 1 < report.results.size()) {
            out << ",";
        }
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
}

void format_bench_csv(std::ostream& out, const BenchmarkReport& report) {
    // Header row
    out << "name,cpu_time_ns,real_time_ns,iterations,items_per_second,bytes_per_second\n";

    // Data rows
    for (const auto& result : report.results) {
        out << result.name << "," << std::fixed << std::setprecision(2) << result.cpu_time_ns << ","
            << std::fixed << std::setprecision(2) << result.real_time_ns << "," << result.iterations
            << "," << std::fixed << std::setprecision(2) << result.items_per_second << ","
            << std::fixed << std::setprecision(2) << result.bytes_per_second << "\n";
    }
}

}  // namespace dotvm::cli
