/// @file vm_bench_command.cpp
/// @brief Benchmark command implementation (CLI-005 Benchmark Runner)

#include "dotvm/cli/commands/vm_bench_command.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "dotvm/cli/bench/benchmark_statistics.hpp"
#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/vm_context.hpp"
#include "dotvm/exec/execution_engine.hpp"
#include "dotvm/exec/profiling.hpp"

namespace dotvm::cli::commands {

namespace {

/// @brief Read entire file into a buffer
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string>
read_file_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected("Failed to open file");
    }

    auto size = file.tellg();
    if (size < 0) {
        return std::unexpected("Failed to get file size");
    }

    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return std::unexpected("Failed to read file");
    }

    return buffer;
}

/// @brief Format time duration for human output
[[nodiscard]] std::string format_time_human(double ns) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (ns >= 1'000'000'000.0) {
        oss << (ns / 1'000'000'000.0) << " s";
    } else if (ns >= 1'000'000.0) {
        oss << (ns / 1'000'000.0) << " ms";
    } else if (ns >= 1'000.0) {
        oss << (ns / 1'000.0) << " us";
    } else {
        oss << ns << " ns";
    }
    return oss.str();
}

/// @brief Format throughput for human output
[[nodiscard]] std::string format_throughput(double ips) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (ips >= 1'000'000'000.0) {
        oss << (ips / 1'000'000'000.0) << " G";
    } else if (ips >= 1'000'000.0) {
        oss << (ips / 1'000'000.0) << " M";
    } else if (ips >= 1'000.0) {
        oss << (ips / 1'000.0) << " K";
    } else {
        oss << ips;
    }
    return oss.str();
}

/// @brief Output statistics in console format
void output_console(const bench::BenchmarkStatistics& stats, const VmBenchOptions& opts,
                    Terminal& term) {
    term.print("Benchmark Results: " + opts.input_file);
    term.newline();
    term.print("─────────────────────────────────────────────");
    term.newline();

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);

    oss << "Samples:        " << stats.sample_count << " runs";
    term.print(oss.str());
    term.newline();
    oss.str("");

    oss << "Mean:           " << format_time_human(stats.mean_ns);
    term.print(oss.str());
    term.newline();
    oss.str("");

    oss << "Median:         " << format_time_human(stats.median_ns);
    term.print(oss.str());
    term.newline();
    oss.str("");

    oss << "Std Dev:        " << format_time_human(stats.stddev_ns) << " ("
        << (stats.coefficient_of_variation() * 100.0) << "% CV)";
    term.print(oss.str());
    term.newline();
    oss.str("");

    oss << "Min:            " << format_time_human(stats.min_ns);
    term.print(oss.str());
    term.newline();
    oss.str("");

    oss << "Max:            " << format_time_human(stats.max_ns);
    term.print(oss.str());
    term.newline();

    term.print("─────────────────────────────────────────────");
    term.newline();

    oss.str("");
    oss << "Percentiles:    p50=" << format_time_human(stats.p50_ns)
        << "  p90=" << format_time_human(stats.p90_ns);
    term.print(oss.str());
    term.newline();
    oss.str("");

    oss << "                p95=" << format_time_human(stats.p95_ns)
        << "  p99=" << format_time_human(stats.p99_ns);
    term.print(oss.str());
    term.newline();

    if (stats.instructions_per_run > 0) {
        term.print("─────────────────────────────────────────────");
        term.newline();
        oss.str("");
        oss << "Instructions:   " << stats.instructions_per_run << " per run";
        term.print(oss.str());
        term.newline();
        oss.str("");

        oss << "Throughput:     " << format_throughput(stats.instructions_per_second()) << " IPS";
        term.print(oss.str());
        term.newline();

        if (stats.total_cycles > 0) {
            oss.str("");
            oss << "CPI:            " << std::setprecision(3) << stats.cycles_per_instruction();
            term.print(oss.str());
            term.newline();
        }
    }
}

/// @brief Output statistics in JSON format
void output_json(const bench::BenchmarkStatistics& stats, const VmBenchOptions& opts,
                 std::ostream& out) {
    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"file\": \"" << opts.input_file << "\",\n";
    out << "  \"warmup_iterations\": " << opts.warmup_iterations << ",\n";
    out << "  \"measurement_runs\": " << opts.measurement_runs << ",\n";
    out << "  \"sample_count\": " << stats.sample_count << ",\n";
    out << "  \"timing_ns\": {\n";
    out << "    \"mean\": " << stats.mean_ns << ",\n";
    out << "    \"median\": " << stats.median_ns << ",\n";
    out << "    \"stddev\": " << stats.stddev_ns << ",\n";
    out << "    \"min\": " << stats.min_ns << ",\n";
    out << "    \"max\": " << stats.max_ns << ",\n";
    out << "    \"p50\": " << stats.p50_ns << ",\n";
    out << "    \"p90\": " << stats.p90_ns << ",\n";
    out << "    \"p95\": " << stats.p95_ns << ",\n";
    out << "    \"p99\": " << stats.p99_ns << "\n";
    out << "  },\n";
    out << "  \"instructions_per_run\": " << stats.instructions_per_run << ",\n";
    out << "  \"instructions_per_second\": " << stats.instructions_per_second() << ",\n";
    out << "  \"total_cycles\": " << stats.total_cycles << ",\n";
    out << "  \"cycles_per_instruction\": " << stats.cycles_per_instruction() << "\n";
    out << "}\n";
}

/// @brief Output statistics in CSV format
void output_csv(const bench::BenchmarkStatistics& stats, const VmBenchOptions& opts,
                std::ostream& out) {
    out << std::fixed << std::setprecision(6);
    // Header
    out << "file,samples,mean_ns,median_ns,stddev_ns,min_ns,max_ns,p50_ns,p90_ns,p95_ns,p99_ns,"
           "instructions,ips,cpi\n";
    // Data row
    out << opts.input_file << "," << stats.sample_count << "," << stats.mean_ns << ","
        << stats.median_ns << "," << stats.stddev_ns << "," << stats.min_ns << "," << stats.max_ns
        << "," << stats.p50_ns << "," << stats.p90_ns << "," << stats.p95_ns << "," << stats.p99_ns
        << "," << stats.instructions_per_run << "," << stats.instructions_per_second() << ","
        << stats.cycles_per_instruction() << "\n";
}

}  // namespace

VmExitCode execute_bench(const VmBenchOptions& opts, const VmGlobalOptions& global,
                         Terminal& term) {
    // Read bytecode file
    auto buffer_result = read_file_bytes(opts.input_file);
    if (!buffer_result.has_value()) {
        term.error("error: ");
        term.print(buffer_result.error());
        term.print(": ");
        term.print(opts.input_file);
        term.newline();
        return VmExitCode::ValidationError;
    }
    const auto& buffer = buffer_result.value();

    // Read and validate header
    if (buffer.size() < core::bytecode::HEADER_SIZE) {
        term.error("error: ");
        term.print(core::to_string(core::BytecodeError::FileTooSmall));
        term.newline();
        return VmExitCode::ValidationError;
    }

    auto header_result = core::read_header(buffer);
    if (!header_result.has_value()) {
        term.error("error: ");
        term.print(core::to_string(header_result.error()));
        term.newline();
        return VmExitCode::ValidationError;
    }

    auto header = header_result.value();

    auto validation_error = core::validate_header(header, buffer.size());
    if (validation_error != core::BytecodeError::Success) {
        term.error("error: ");
        term.print(core::to_string(validation_error));
        term.newline();
        return VmExitCode::ValidationError;
    }

    // Override architecture if specified
    if (global.arch_override != 0) {
        header.arch =
            (global.arch_override == 32) ? core::Architecture::Arch32 : core::Architecture::Arch64;
    }

    // Load constant pool
    std::vector<core::Value> const_pool;
    if (header.const_pool_size > 0) {
        std::span<const std::uint8_t> pool_data(buffer.data() + header.const_pool_offset,
                                                static_cast<std::size_t>(header.const_pool_size));
        auto pool_result = core::load_constant_pool(pool_data);
        if (!pool_result.has_value()) {
            term.error("error: ");
            term.print("constant pool: ");
            term.print(core::to_string(pool_result.error()));
            term.newline();
            return VmExitCode::ValidationError;
        }
        const_pool = std::move(pool_result.value());
    }

    // Get code section
    const auto* code_ptr =
        reinterpret_cast<const std::uint32_t*>(buffer.data() + header.code_offset);
    std::size_t code_size =
        static_cast<std::size_t>(header.code_size) / core::bytecode::INSTRUCTION_ALIGNMENT;
    std::size_t entry_point = header.entry_point / core::bytecode::INSTRUCTION_ALIGNMENT;

    // Create base configuration
    core::VmConfig config = core::VmConfig::for_arch(header.arch);
    config.strict_overflow = header.is_debug();

    // Apply instruction limit if specified
    if (opts.instruction_limit > 0) {
        config.resource_limits.max_instructions = opts.instruction_limit;
    }

    if (!global.quiet) {
        term.info("Benchmarking: " + opts.input_file);
        term.newline();
        std::ostringstream oss;
        oss << "Warm-up: " << opts.warmup_iterations << " iterations, "
            << "Runs: " << opts.measurement_runs;
        term.print(oss.str());
        term.newline();
    }

    // Warm-up runs
    for (std::size_t i = 0; i < opts.warmup_iterations; ++i) {
        core::VmContext vm_ctx(config);
        exec::ExecutionEngine engine(vm_ctx);
        (void)engine.execute(code_ptr, code_size, entry_point, std::span{const_pool});
    }

    // Measurement runs
    std::vector<double> samples;
    samples.reserve(opts.measurement_runs);
    std::uint64_t total_instructions = 0;
    std::uint64_t total_cycles = 0;

    exec::HighResTimer timer;
    for (std::size_t i = 0; i < opts.measurement_runs; ++i) {
        core::VmContext vm_ctx(config);
        exec::ExecutionEngine engine(vm_ctx);

        std::uint64_t start_cycles = exec::rdtscp();
        timer.start();
        auto result = engine.execute(code_ptr, code_size, entry_point, std::span{const_pool});
        auto elapsed_ns = timer.stop();
        std::uint64_t end_cycles = exec::rdtscp();

        samples.push_back(static_cast<double>(elapsed_ns));
        total_instructions += engine.instructions_executed();
        total_cycles += (end_cycles - start_cycles);

        // Check for runtime errors on first run only
        if (result != exec::ExecResult::Success && i == 0) {
            term.warning("Warning: Execution did not succeed: ");
            term.print(exec::to_string(result));
            term.newline();
        }
    }

    // Compute statistics
    auto stats = bench::BenchmarkStatistics::compute(samples);
    stats.instructions_per_run = total_instructions / opts.measurement_runs;
    stats.total_cycles = total_cycles / opts.measurement_runs;

    // Output results
    if (!opts.output_file.empty()) {
        std::ofstream out_file(opts.output_file);
        if (!out_file) {
            term.error("error: Could not open output file: " + opts.output_file);
            term.newline();
            return VmExitCode::RuntimeError;
        }
        switch (opts.format) {
            case VmBenchOutputFormat::Json:
                output_json(stats, opts, out_file);
                break;
            case VmBenchOutputFormat::Csv:
                output_csv(stats, opts, out_file);
                break;
            case VmBenchOutputFormat::Console:
            default:
                // For file output in console format, use a simple text format
                out_file << "Benchmark Results: " << opts.input_file << "\n";
                out_file << "Samples: " << stats.sample_count << " runs\n";
                out_file << "Mean: " << format_time_human(stats.mean_ns) << "\n";
                out_file << "Median: " << format_time_human(stats.median_ns) << "\n";
                out_file << "Std Dev: " << format_time_human(stats.stddev_ns) << "\n";
                out_file << "Min: " << format_time_human(stats.min_ns) << "\n";
                out_file << "Max: " << format_time_human(stats.max_ns) << "\n";
                break;
        }
        if (!global.quiet) {
            term.success("Results written to: " + opts.output_file);
            term.newline();
        }
    } else {
        // Output to terminal
        switch (opts.format) {
            case VmBenchOutputFormat::Json:
                output_json(stats, opts, std::cout);
                break;
            case VmBenchOutputFormat::Csv:
                output_csv(stats, opts, std::cout);
                break;
            case VmBenchOutputFormat::Console:
            default:
                output_console(stats, opts, term);
                break;
        }
    }

    return VmExitCode::Success;
}

}  // namespace dotvm::cli::commands
