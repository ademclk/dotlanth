#pragma once

/// @file info_cli_app.hpp
/// @brief TOOL-009 Bytecode Inspector CLI application using CLI11
///
/// Provides the command-line interface for the dotinfo bytecode inspector tool.

#include <cstdint>
#include <memory>
#include <string>

#include "dotvm/cli/info_formatter.hpp"
#include "dotvm/cli/terminal.hpp"

// Forward declare CLI11 types to avoid header pollution
namespace CLI {
class App;
}

namespace dotvm::cli {

/// @brief Exit codes for the inspector
enum class InfoExitCode : int {
    Success = 0,          ///< Inspection completed successfully
    ValidationError = 1,  ///< Bytecode validation failure (info still output)
    IOError = 2           ///< File I/O error
};

/// @brief Options for the inspector
struct InfoOptions {
    std::string input_file;                            ///< Input .dot bytecode file
    std::string output_file;                           ///< Output file (empty = stdout)
    InfoOutputFormat format = InfoOutputFormat::Table; ///< Output format
    bool detailed = false;                             ///< Show detailed breakdown
    bool quiet = false;                                ///< Suppress non-essential output
    bool no_color = false;                             ///< Disable ANSI color codes
    bool force_color = false;                          ///< Force colors
};

/// @brief Main inspector CLI application class
///
/// Provides the command-line interface for the dotinfo bytecode inspector.
/// Uses CLI11 for argument parsing.
class InfoCliApp {
public:
    /// @brief Construct the inspector CLI application
    InfoCliApp();

    /// @brief Destructor
    ~InfoCliApp();

    // Non-copyable
    InfoCliApp(const InfoCliApp&) = delete;
    InfoCliApp& operator=(const InfoCliApp&) = delete;

    // Movable
    InfoCliApp(InfoCliApp&&) noexcept;
    InfoCliApp& operator=(InfoCliApp&&) noexcept;

    /// @brief Parse command-line arguments
    /// @param argc Argument count
    /// @param argv Argument values
    /// @return Exit code
    [[nodiscard]] InfoExitCode parse(int argc, const char* const* argv);

    /// @brief Run the inspector
    /// @return Exit code
    [[nodiscard]] InfoExitCode run();

    /// @brief Get the options
    [[nodiscard]] const InfoOptions& options() const noexcept { return opts_; }

    /// @brief Get the underlying CLI11 app (for testing)
    [[nodiscard]] CLI::App& app() noexcept { return *app_; }

    /// @brief Check if help or version was requested (parse handled it)
    [[nodiscard]] bool help_requested() const noexcept { return help_requested_; }

private:
    void setup_options();

    std::unique_ptr<CLI::App> app_;
    InfoOptions opts_;
    std::string format_str_ = "table";  ///< CLI11 bound string for format option

    // Flag to track if help/version was handled during parse
    bool help_requested_ = false;
};

/// @brief Create a Terminal with color mode resolution based on options
///
/// Priority: --no-color > --color > auto-detect TTY
/// @param out Output stream
/// @param opts Inspector options
/// @return Configured Terminal
[[nodiscard]] inline Terminal make_terminal(std::ostream& out, const InfoOptions& opts) {
    if (opts.no_color) {
        return Terminal(out, true);  // force_no_color = true
    }
    if (opts.force_color) {
        return Terminal(out, true, true);  // colors_enabled = true, explicit_tag = true
    }
    return Terminal(out, false);  // auto-detect TTY
}

}  // namespace dotvm::cli
