#pragma once

/// @file dis_cli_app.hpp
/// @brief TOOL-008 Disassembler CLI application using CLI11
///
/// Provides the command-line interface for the dotdis bytecode disassembler tool.

#include <cstdint>
#include <memory>
#include <string>

#include "dotvm/cli/terminal.hpp"

// Forward declare CLI11 types to avoid header pollution
namespace CLI {
class App;
}

namespace dotvm::cli {

/// @brief Exit codes for the disassembler
enum class DisExitCode : int {
    Success = 0,          ///< Disassembly completed successfully
    ValidationError = 1,  ///< Bytecode validation failure
    IOError = 2           ///< File I/O error
};

/// @brief Output format selection
enum class DisOutputFormat {
    Text,  ///< Human-readable assembly text
    Json   ///< Structured JSON output
};

/// @brief Options for the disassembler
struct DisOptions {
    std::string input_file;                          ///< Input .dot bytecode file
    std::string output_file;                         ///< Output file (empty = stdout)
    DisOutputFormat format = DisOutputFormat::Text;  ///< Output format
    bool show_bytes = false;                         ///< Show raw instruction bytes
    bool show_labels = false;                        ///< Mark branch targets with labels
    bool annotate = false;                           ///< Add comments for patterns
    bool verbose = false;                            ///< Enable verbose output
    bool quiet = false;                              ///< Suppress non-essential output
    bool no_color = false;                           ///< Disable ANSI color codes
    bool force_color = false;                        ///< Force colors (for piping to less -R)
};

/// @brief Main disassembler CLI application class
///
/// Provides the command-line interface for the dotdis bytecode disassembler.
/// Uses CLI11 for argument parsing.
class DisCliApp {
public:
    /// @brief Construct the disassembler CLI application
    DisCliApp();

    /// @brief Destructor
    ~DisCliApp();

    // Non-copyable
    DisCliApp(const DisCliApp&) = delete;
    DisCliApp& operator=(const DisCliApp&) = delete;

    // Movable
    DisCliApp(DisCliApp&&) noexcept;
    DisCliApp& operator=(DisCliApp&&) noexcept;

    /// @brief Parse command-line arguments
    /// @param argc Argument count
    /// @param argv Argument values
    /// @return Exit code
    [[nodiscard]] DisExitCode parse(int argc, const char* const* argv);

    /// @brief Run the disassembler
    /// @return Exit code
    [[nodiscard]] DisExitCode run();

    /// @brief Get the options
    [[nodiscard]] const DisOptions& options() const noexcept { return opts_; }

    /// @brief Get the underlying CLI11 app (for testing)
    [[nodiscard]] CLI::App& app() noexcept { return *app_; }

    /// @brief Check if help or version was requested (parse handled it)
    [[nodiscard]] bool help_requested() const noexcept { return help_requested_; }

private:
    void setup_options();

    std::unique_ptr<CLI::App> app_;
    DisOptions opts_;
    std::string format_str_ = "text";  ///< CLI11 bound string for format option

    // Flag to track if help/version was handled during parse
    bool help_requested_ = false;
};

/// @brief Create a Terminal with color mode resolution based on options
///
/// Priority: --no-color > --color > auto-detect TTY
/// @param out Output stream
/// @param opts Disassembler options
/// @return Configured Terminal
[[nodiscard]] inline Terminal make_terminal(std::ostream& out, const DisOptions& opts) {
    if (opts.no_color) {
        return Terminal(out, true);  // force_no_color = true
    }
    if (opts.force_color) {
        return Terminal(out, true, true);  // colors_enabled = true, explicit_tag = true
    }
    return Terminal(out, false);  // auto-detect TTY
}

}  // namespace dotvm::cli
