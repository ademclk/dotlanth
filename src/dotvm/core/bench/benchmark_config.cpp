/// @file benchmark_config.cpp
/// @brief Implementation of benchmark framework utilities
///
/// Part of CORE-008: Performance Benchmarks for the dotlanth VM

#include "dotvm/core/bench/benchmark_config.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace dotvm::core::bench {

// ============================================================================
// BenchmarkResult Implementation
// ============================================================================

void BenchmarkResult::print() const {
    // Calculate average ns/op
    double avg_ns = avg_ns_per_op();

    // Format the output
    std::cout << std::setw(35) << std::left << name << " | " << std::setw(12) << std::right
              << iterations << " iters" << " | " << std::setw(10) << std::right << std::fixed
              << std::setprecision(1) << avg_ns << " ns/op" << " | " << std::setw(12) << std::right
              << std::scientific << std::setprecision(2) << ops_per_second << " ops/s";

    if (throughput_mb_per_sec > 0) {
        std::cout << " | " << std::setw(10) << std::right << std::fixed << std::setprecision(2)
                  << throughput_mb_per_sec << " MB/s";
    }

    std::cout << std::endl;
}

// ============================================================================
// BenchmarkSuite Implementation
// ============================================================================

void BenchmarkSuite::print_all() const {
    print_header(name_);

    for (const auto& result : results_) {
        result.print();
    }

    print_separator('=');
    std::cout << std::endl;
}

void BenchmarkSuite::print_comparison(const BenchmarkResult& baseline,
                                      const BenchmarkResult& optimized) {
    double baseline_ns = baseline.avg_ns_per_op();
    double optimized_ns = optimized.avg_ns_per_op();

    double speedup = 1.0;
    if (optimized_ns > 0) {
        speedup = baseline_ns / optimized_ns;
    }

    std::cout << std::setw(35) << std::left << "Comparison" << " | " << baseline.name << " -> "
              << optimized.name << " | Speedup: " << std::fixed << std::setprecision(2) << speedup
              << "x" << std::endl;
}

// ============================================================================
// Utility Functions Implementation
// ============================================================================

void print_separator(char ch, std::size_t width) {
    std::cout << std::string(width, ch) << std::endl;
}

void print_header(const std::string& title) {
    print_separator('=');
    std::cout << "  " << title << std::endl;
    print_separator('=');
    std::cout << std::setw(35) << std::left << "Benchmark" << " | " << std::setw(12) << std::right
              << "Iterations" << " | " << std::setw(14) << std::right << "Time/Op" << " | "
              << std::setw(14) << std::right << "Ops/Sec" << " | Throughput" << std::endl;
    print_separator('-');
}

std::string format_time(std::chrono::nanoseconds ns) {
    std::ostringstream oss;

    auto count = ns.count();

    if (count < 1000) {
        oss << count << " ns";
    } else if (count < 1000000) {
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(count) / 1000.0) << " us";
    } else if (count < 1000000000) {
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(count) / 1000000.0)
            << " ms";
    } else {
        oss << std::fixed << std::setprecision(2) << (static_cast<double>(count) / 1000000000.0)
            << " s";
    }

    return oss.str();
}

std::string format_throughput(double mb_per_sec) {
    std::ostringstream oss;

    if (mb_per_sec < 1.0) {
        oss << std::fixed << std::setprecision(2) << (mb_per_sec * 1024.0) << " KB/s";
    } else if (mb_per_sec < 1024.0) {
        oss << std::fixed << std::setprecision(2) << mb_per_sec << " MB/s";
    } else {
        oss << std::fixed << std::setprecision(2) << (mb_per_sec / 1024.0) << " GB/s";
    }

    return oss.str();
}

}  // namespace dotvm::core::bench
