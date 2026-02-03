#pragma once

/// @file vm_cli_app.hpp
/// @brief TOOL-005 VM CLI Executor application using CLI11
///
/// Provides the command-line interface for the dotvm bytecode executor tool.
/// Commands: run, validate, info

#include <cstdint>
#include <memory>
#include <string>

#include "dotvm/cli/terminal.hpp"

// Forward declare CLI11 types to avoid header pollution
namespace CLI {
class App;
}

namespace dotvm::cli {

/// @brief Exit codes specific to the VM executor
///
/// These are separate from the DSL compiler exit codes to match
/// the TOOL-005 specification.
enum class VmExitCode : int {
    Success = 0,         ///< Execution completed successfully
    RuntimeError = 1,    ///< Runtime error during bytecode execution
    ValidationError = 2  ///< Bytecode validation failure
};

/// @brief Global options shared by all VM subcommands
struct VmGlobalOptions {
    bool verbose = false;            ///< Enable verbose output
    bool quiet = false;              ///< Suppress non-essential output
    std::string output_file;         ///< Write output to file (empty = stdout)
    bool no_color = false;           ///< Disable ANSI color codes
    bool force_color = false;        ///< Force colors (for piping to less -R)
    std::uint8_t arch_override = 0;  ///< Architecture override: 0=use header, 32 or 64=override
};

/// @brief Options for the run command
struct VmRunOptions {
    std::string input_file;               ///< Input .dot bytecode file
    bool debug = false;                   ///< Enable debug mode with full trace
    std::uint64_t instruction_limit = 0;  ///< Instruction limit (0 = unlimited)
};

/// @brief Options for the validate command
struct VmValidateOptions {
    std::string input_file;  ///< Input .dot bytecode file to validate
};

/// @brief Options for the info command
struct VmInfoOptions {
    std::string input_file;  ///< Input .dot bytecode file to inspect
};

/// @brief Options for the debug command
struct VmDebugOptions {
    std::string input_file;  ///< Input .dot bytecode file to debug
    bool no_color = false;   ///< Disable ANSI colors in debugger
};

/// @brief Output format for benchmark results
enum class VmBenchOutputFormat {
    Console,  ///< Human-readable console output with optional colors
    Json,     ///< Structured JSON output
    Csv       ///< CSV format for spreadsheet import
};

/// @brief Options for the bench command (bytecode benchmarking)
struct VmBenchOptions {
    std::string input_file;                 ///< Input .dot bytecode file to benchmark
    std::size_t warmup_iterations = 10;     ///< Warm-up iterations before measurement
    std::size_t measurement_runs = 100;     ///< Number of measurement runs
    std::uint64_t instruction_limit = 0;    ///< Max instructions per run (0 = unlimited)

    // Baseline comparison
    std::string baseline_file;              ///< Compare against this baseline JSON file
    bool save_baseline = false;             ///< Save results as new baseline
    std::string save_baseline_path;         ///< Path to save baseline (if save_baseline)
    double regression_threshold = 5.0;      ///< Regression threshold percentage (5% default)

    // Flamegraph generation
    bool generate_flamegraph = false;       ///< Generate flamegraph data
    std::string flamegraph_output;          ///< Output path for folded stacks
    std::size_t sample_rate_instructions = 1000;  ///< Sample every N instructions

    // Output formatting
    VmBenchOutputFormat format = VmBenchOutputFormat::Console;  ///< Output format
    std::string output_file;                ///< Write results to file (empty = stdout)
};

/// @brief Main VM CLI application class
///
/// Provides the command-line interface for the dotvm bytecode executor tool.
/// Uses CLI11 for argument parsing with subcommands: run, validate, info.
class VmCliApp {
public:
    /// @brief Construct the VM CLI application
    VmCliApp();

    /// @brief Destructor
    ~VmCliApp();

    // Non-copyable
    VmCliApp(const VmCliApp&) = delete;
    VmCliApp& operator=(const VmCliApp&) = delete;

    // Movable
    VmCliApp(VmCliApp&&) noexcept;
    VmCliApp& operator=(VmCliApp&&) noexcept;

    /// @brief Parse command-line arguments
    /// @param argc Argument count
    /// @param argv Argument values
    /// @return Exit code
    [[nodiscard]] VmExitCode parse(int argc, const char* const* argv);

    /// @brief Run the parsed command
    /// @return Exit code
    [[nodiscard]] VmExitCode run();

    /// @brief Get the global options
    [[nodiscard]] const VmGlobalOptions& global_options() const noexcept { return global_opts_; }

    /// @brief Get the run options
    [[nodiscard]] const VmRunOptions& run_options() const noexcept { return run_opts_; }

    /// @brief Get the validate options
    [[nodiscard]] const VmValidateOptions& validate_options() const noexcept {
        return validate_opts_;
    }

    /// @brief Get the info options
    [[nodiscard]] const VmInfoOptions& info_options() const noexcept { return info_opts_; }

    /// @brief Get the debug options
    [[nodiscard]] const VmDebugOptions& debug_options() const noexcept { return debug_opts_; }

    /// @brief Get the bench options
    [[nodiscard]] const VmBenchOptions& bench_options() const noexcept { return bench_opts_; }

    /// @brief Get the currently selected subcommand name
    [[nodiscard]] std::string current_subcommand() const;

    /// @brief Get the underlying CLI11 app (for testing)
    [[nodiscard]] CLI::App& app() noexcept { return *app_; }

    /// @brief Check if help or version was requested (parse handled it)
    [[nodiscard]] bool help_requested() const noexcept { return help_requested_; }

private:
    void setup_global_options();
    void setup_run_command();
    void setup_validate_command();
    void setup_info_command();
    void setup_debug_command();
    void setup_bench_command();

    std::unique_ptr<CLI::App> app_;

    VmGlobalOptions global_opts_;
    VmRunOptions run_opts_;
    VmValidateOptions validate_opts_;
    VmInfoOptions info_opts_;
    VmDebugOptions debug_opts_;
    VmBenchOptions bench_opts_;

    // Subcommand pointers (owned by app_)
    CLI::App* run_cmd_ = nullptr;
    CLI::App* validate_cmd_ = nullptr;
    CLI::App* info_cmd_ = nullptr;
    CLI::App* debug_cmd_ = nullptr;
    CLI::App* bench_cmd_ = nullptr;

    // CLI11 bound strings for enum parsing
    std::string bench_format_str_ = "console";

    // Flag to track if help/version was handled during parse
    bool help_requested_ = false;
};

/// @brief Create a Terminal with color mode resolution based on global options
///
/// Priority: --no-color > --color > auto-detect TTY
/// @param out Output stream
/// @param opts Global options
/// @return Configured Terminal
[[nodiscard]] inline Terminal make_terminal(std::ostream& out, const VmGlobalOptions& opts) {
    if (opts.no_color) {
        return Terminal(out, true);  // force_no_color = true
    }
    if (opts.force_color) {
        return Terminal(out, true, true);  // colors_enabled = true, explicit_tag = true
    }
    return Terminal(out, false);  // auto-detect TTY
}

}  // namespace dotvm::cli
