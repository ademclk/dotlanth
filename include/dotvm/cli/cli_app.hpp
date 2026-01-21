#pragma once

/// @file cli_app.hpp
/// @brief DSL-003 CLI application using CLI11
///
/// Main entry point for the dotdsl command-line compiler tool.
/// Sets up CLI11 with subcommands: compile, check, format, watch

#include <memory>
#include <string>
#include <vector>

#include "dotvm/cli/terminal.hpp"

// Forward declare CLI11 types to avoid header pollution
namespace CLI {
class App;
}

namespace dotvm::cli {

/// @brief Global options shared by all subcommands
struct GlobalOptions {
    bool verbose = false;    ///< Enable verbose output
    bool debug = false;      ///< Enable debug output
    bool strict = false;     ///< Enable strict mode (warnings become errors)
    bool output_ir = false;  ///< Output IR for debugging
    bool no_color = false;   ///< Disable colored output
};

/// @brief Options for the compile command
struct CompileOptions {
    std::string input_file;                  ///< Input .dsl file path
    std::string output_file;                 ///< Output bytecode file path (default: input.dot)
    std::vector<std::string> include_paths;  ///< Include search paths (-I)
};

/// @brief Options for the check command
struct CheckOptions {
    std::string input_file;                  ///< Input .dsl file path
    std::vector<std::string> include_paths;  ///< Include search paths (-I)
};

/// @brief Options for the format command
struct FormatOptions {
    std::string input_file;  ///< Input .dsl file path
    bool in_place = false;   ///< Modify file in place
};

/// @brief Options for the watch command
struct WatchOptions {
    std::string directory;  ///< Directory to watch for changes
};

/// @brief Main CLI application class
///
/// Provides the command-line interface for the dotdsl compiler tool.
/// Uses CLI11 for argument parsing with subcommands.
class CliApp {
public:
    /// @brief Construct the CLI application
    CliApp();

    /// @brief Destructor
    ~CliApp();

    // Non-copyable
    CliApp(const CliApp&) = delete;
    CliApp& operator=(const CliApp&) = delete;

    // Movable
    CliApp(CliApp&&) noexcept;
    CliApp& operator=(CliApp&&) noexcept;

    /// @brief Parse command-line arguments
    /// @param argc Argument count
    /// @param argv Argument values
    /// @return Exit code
    [[nodiscard]] ExitCode parse(int argc, const char* const* argv);

    /// @brief Run the parsed command
    /// @return Exit code
    [[nodiscard]] ExitCode run();

    /// @brief Get the global options
    [[nodiscard]] const GlobalOptions& global_options() const noexcept { return global_opts_; }

    /// @brief Get the compile options
    [[nodiscard]] const CompileOptions& compile_options() const noexcept { return compile_opts_; }

    /// @brief Get the check options
    [[nodiscard]] const CheckOptions& check_options() const noexcept { return check_opts_; }

    /// @brief Get the format options
    [[nodiscard]] const FormatOptions& format_options() const noexcept { return format_opts_; }

    /// @brief Get the watch options
    [[nodiscard]] const WatchOptions& watch_options() const noexcept { return watch_opts_; }

    /// @brief Get the currently selected subcommand name
    [[nodiscard]] std::string current_subcommand() const;

    /// @brief Get the underlying CLI11 app (for testing)
    [[nodiscard]] CLI::App& app() noexcept { return *app_; }

    /// @brief Check if help or version was requested (parse handled it)
    [[nodiscard]] bool help_requested() const noexcept { return help_requested_; }

private:
    void setup_global_options();
    void setup_compile_command();
    void setup_check_command();
    void setup_format_command();
    void setup_watch_command();

    std::unique_ptr<CLI::App> app_;

    GlobalOptions global_opts_;
    CompileOptions compile_opts_;
    CheckOptions check_opts_;
    FormatOptions format_opts_;
    WatchOptions watch_opts_;

    // Subcommand pointers (owned by app_)
    CLI::App* compile_cmd_ = nullptr;
    CLI::App* check_cmd_ = nullptr;
    CLI::App* format_cmd_ = nullptr;
    CLI::App* watch_cmd_ = nullptr;

    // Flag to track if help/version was handled during parse
    bool help_requested_ = false;
};

}  // namespace dotvm::cli
