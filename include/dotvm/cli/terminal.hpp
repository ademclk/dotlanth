#pragma once

/// @file terminal.hpp
/// @brief DSL-003 Terminal output utilities with ANSI color support
///
/// Provides terminal output formatting with:
/// - TTY detection and automatic color enable/disable
/// - Color methods: error (red), warning (yellow), info (cyan), success (green)
/// - Source code snippet printing with line numbers and error markers

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include "dotvm/core/dsl/source_location.hpp"

namespace dotvm::cli {

/// @brief Exit codes for the CLI
enum class ExitCode : int {
    Success = 0,
    ParseError = 1,
    CompilationError = 2,
    FileNotFound = 3,
    IoError = 4,
    CircularInclude = 5,
};

/// @brief ANSI color codes for terminal output
struct AnsiColor {
    static constexpr const char* Reset = "\033[0m";
    static constexpr const char* Bold = "\033[1m";
    static constexpr const char* Dim = "\033[2m";

    // Foreground colors
    static constexpr const char* Red = "\033[31m";
    static constexpr const char* Green = "\033[32m";
    static constexpr const char* Yellow = "\033[33m";
    static constexpr const char* Blue = "\033[34m";
    static constexpr const char* Magenta = "\033[35m";
    static constexpr const char* Cyan = "\033[36m";
    static constexpr const char* White = "\033[37m";

    // Bright foreground colors
    static constexpr const char* BrightRed = "\033[91m";
    static constexpr const char* BrightGreen = "\033[92m";
    static constexpr const char* BrightYellow = "\033[93m";
    static constexpr const char* BrightCyan = "\033[96m";
};

/// @brief Terminal output utilities with ANSI color support
///
/// Automatically detects TTY status and enables/disables colors accordingly.
/// The --no-color flag can override to disable colors even on TTY.
class Terminal {
public:
    /// @brief Construct terminal with auto-detection
    /// @param out Output stream to use (default: std::cerr)
    /// @param force_no_color Force disable colors (for --no-color flag)
    explicit Terminal(std::ostream& out, bool force_no_color = false);

    /// @brief Construct terminal with explicit color setting
    /// @param out Output stream to use
    /// @param colors_enabled Whether to use ANSI colors
    Terminal(std::ostream& out, bool colors_enabled, bool /*explicit_tag*/);

    /// @brief Check if colors are enabled
    [[nodiscard]] bool colors_enabled() const noexcept { return colors_enabled_; }

    /// @brief Enable or disable colors
    void set_colors_enabled(bool enabled) noexcept { colors_enabled_ = enabled; }

    // ===== Color Output Methods =====

    /// @brief Print error message (red)
    /// @param msg Message to print (without newline)
    void error(std::string_view msg);

    /// @brief Print warning message (yellow)
    /// @param msg Message to print (without newline)
    void warning(std::string_view msg);

    /// @brief Print info message (cyan)
    /// @param msg Message to print (without newline)
    void info(std::string_view msg);

    /// @brief Print success message (green)
    /// @param msg Message to print (without newline)
    void success(std::string_view msg);

    /// @brief Print plain message (no color)
    /// @param msg Message to print (without newline)
    void print(std::string_view msg);

    /// @brief Print a newline
    void newline();

    // ===== Formatted Error Output =====

    /// @brief Print an error label with location
    /// @param filename Source filename
    /// @param loc Source location
    /// @param label Error type label (e.g., "error", "warning")
    void print_error_header(std::string_view filename, const core::dsl::SourceLocation& loc,
                            std::string_view label = "error");

    /// @brief Print a source code snippet with error marker
    ///
    /// Shows the source line with line number and a caret (^) pointing
    /// to the error column, optionally with a message below.
    ///
    /// @param source Full source code
    /// @param span Source span to highlight
    /// @param message Optional message to show below the marker
    void print_source_snippet(std::string_view source, const core::dsl::SourceSpan& span,
                              std::string_view message = "");

    /// @brief Print a complete error with header and snippet
    /// @param filename Source filename
    /// @param source Full source code
    /// @param span Error location span
    /// @param label Error type label
    /// @param message Error message
    void print_error(std::string_view filename, std::string_view source,
                     const core::dsl::SourceSpan& span, std::string_view label,
                     std::string_view message);

    // ===== Utility Methods =====

    /// @brief Get the output stream
    [[nodiscard]] std::ostream& stream() noexcept { return out_; }

    /// @brief Apply a color code (if colors enabled)
    void apply_color(const char* color);

    /// @brief Reset color (if colors enabled)
    void reset_color();

    /// @brief Check if the output stream is a TTY
    [[nodiscard]] static bool is_tty(std::ostream& stream);

private:
    /// @brief Extract a single line from source by line number (1-based)
    [[nodiscard]] static std::string_view get_line(std::string_view source, std::uint32_t line);

    /// @brief Format a line number with padding
    [[nodiscard]] std::string format_line_number(std::uint32_t line, std::uint32_t max_line);

    std::ostream& out_;
    bool colors_enabled_;
};

}  // namespace dotvm::cli
