#pragma once

/// @file bench_cli_app.hpp
/// @brief TOOL-010 Benchmark CLI application using CLI11
///
/// Provides the command-line interface for the dotvm_bench benchmark suite tool.

#include <cstdint>
#include <memory>
#include <string>

#include "dotvm/cli/terminal.hpp"

// Forward declare CLI11 types to avoid header pollution
namespace CLI {
class App;
}

namespace dotvm::cli {

/// @brief Exit codes for the benchmark suite
enum class BenchExitCode : int {
    Success = 0,          ///< Benchmarks completed successfully
    ValidationError = 1,  ///< Command-line argument validation failure
    IOError = 2,          ///< File I/O error
    TargetMissed = 3      ///< Performance targets not met (--strict mode)
};

/// @brief Output format selection
enum class BenchOutputFormat {
    Console,  ///< Human-readable console output with optional colors
    Json,     ///< Structured JSON output
    Csv       ///< CSV format for spreadsheet import
};

/// @brief Options for the benchmark suite
struct BenchOptions {
    std::string filter;     ///< Regex filter for benchmark names
    int repetitions = 3;    ///< Number of repetitions per benchmark
    double min_time = 0.5;  ///< Minimum time in seconds per benchmark
    BenchOutputFormat format = BenchOutputFormat::Console;  ///< Output format
    std::string output_file;                                ///< Output file (empty = stdout)
    bool list_only = false;                                 ///< Just list available benchmarks
    bool strict = false;                                    ///< Exit code 3 if targets missed
    bool no_color = false;                                  ///< Disable ANSI color codes
    bool force_color = false;                               ///< Force colors (for piping)

    /// @brief Target file path for performance thresholds (SEC-010)
    ///
    /// When specified, loads target times from a JSON file and compares
    /// benchmark results against them. In strict mode, exits with code 3
    /// if any targets are missed.
    std::string target_file;
};

/// @brief Main benchmark CLI application class
///
/// Provides the command-line interface for the dotvm_bench benchmark suite.
/// Uses CLI11 for argument parsing.
class BenchCliApp {
public:
    /// @brief Construct the benchmark CLI application
    BenchCliApp();

    /// @brief Destructor
    ~BenchCliApp();

    // Non-copyable
    BenchCliApp(const BenchCliApp&) = delete;
    BenchCliApp& operator=(const BenchCliApp&) = delete;

    // Movable
    BenchCliApp(BenchCliApp&&) noexcept;
    BenchCliApp& operator=(BenchCliApp&&) noexcept;

    /// @brief Parse command-line arguments
    /// @param argc Argument count
    /// @param argv Argument values
    /// @return Exit code
    [[nodiscard]] BenchExitCode parse(int argc, const char* const* argv);

    /// @brief Run the benchmarks
    /// @return Exit code
    [[nodiscard]] BenchExitCode run();

    /// @brief Get the options
    [[nodiscard]] const BenchOptions& options() const noexcept { return opts_; }

    /// @brief Get the underlying CLI11 app (for testing)
    [[nodiscard]] CLI::App& app() noexcept { return *app_; }

    /// @brief Check if help or version was requested (parse handled it)
    [[nodiscard]] bool help_requested() const noexcept { return help_requested_; }

private:
    void setup_options();

    std::unique_ptr<CLI::App> app_;
    BenchOptions opts_;
    std::string format_str_ = "console";  ///< CLI11 bound string for format option

    // Flag to track if help/version was handled during parse
    bool help_requested_ = false;
};

/// @brief Create a Terminal with color mode resolution based on options
///
/// Priority: --no-color > --color > auto-detect TTY
/// @param out Output stream
/// @param opts Benchmark options
/// @return Configured Terminal
[[nodiscard]] inline Terminal make_terminal(std::ostream& out, const BenchOptions& opts) {
    if (opts.no_color) {
        return Terminal(out, true);  // force_no_color = true
    }
    if (opts.force_color) {
        return Terminal(out, true, true);  // colors_enabled = true, explicit_tag = true
    }
    return Terminal(out, false);  // auto-detect TTY
}

}  // namespace dotvm::cli
