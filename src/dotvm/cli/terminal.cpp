/// @file terminal.cpp
/// @brief DSL-003 Terminal output utilities implementation

#include "dotvm/cli/terminal.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
    #include <io.h>
    #define isatty _isatty
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

namespace dotvm::cli {

Terminal::Terminal(std::ostream& out, bool force_no_color)
    : out_(out), colors_enabled_(!force_no_color && is_tty(out)) {}

Terminal::Terminal(std::ostream& out, bool colors_enabled, bool /*explicit_tag*/)
    : out_(out), colors_enabled_(colors_enabled) {}

bool Terminal::is_tty(std::ostream& stream) {
    // Check common streams
    if (&stream == &std::cout) {
        return isatty(fileno(stdout)) != 0;
    }
    if (&stream == &std::cerr || &stream == &std::clog) {
        return isatty(fileno(stderr)) != 0;
    }
    // For other streams, assume not a TTY
    return false;
}

void Terminal::apply_color(const char* color) {
    if (colors_enabled_) {
        out_ << color;
    }
}

void Terminal::reset_color() {
    if (colors_enabled_) {
        out_ << AnsiColor::Reset;
    }
}

void Terminal::error(std::string_view msg) {
    apply_color(AnsiColor::BrightRed);
    out_ << msg;
    reset_color();
}

void Terminal::warning(std::string_view msg) {
    apply_color(AnsiColor::BrightYellow);
    out_ << msg;
    reset_color();
}

void Terminal::info(std::string_view msg) {
    apply_color(AnsiColor::BrightCyan);
    out_ << msg;
    reset_color();
}

void Terminal::success(std::string_view msg) {
    apply_color(AnsiColor::BrightGreen);
    out_ << msg;
    reset_color();
}

void Terminal::print(std::string_view msg) {
    out_ << msg;
}

void Terminal::newline() {
    out_ << '\n';
}

void Terminal::print_error_header(std::string_view filename, const core::dsl::SourceLocation& loc,
                                  std::string_view label) {
    // Format: filename:line:column: error: message
    apply_color(AnsiColor::Bold);
    out_ << filename << ':' << loc.line << ':' << loc.column << ": ";
    reset_color();

    if (label == "error") {
        apply_color(AnsiColor::BrightRed);
        apply_color(AnsiColor::Bold);
        out_ << "error: ";
    } else if (label == "warning") {
        apply_color(AnsiColor::BrightYellow);
        apply_color(AnsiColor::Bold);
        out_ << "warning: ";
    } else if (label == "note") {
        apply_color(AnsiColor::BrightCyan);
        apply_color(AnsiColor::Bold);
        out_ << "note: ";
    } else {
        apply_color(AnsiColor::Bold);
        out_ << label << ": ";
    }
    reset_color();
}

std::string_view Terminal::get_line(std::string_view source, std::uint32_t line) {
    if (line == 0 || source.empty()) {
        return {};
    }

    std::uint32_t current_line = 1;
    std::size_t line_start = 0;

    for (std::size_t i = 0; i < source.size(); ++i) {
        if (current_line == line) {
            // Found the start of our target line
            line_start = i;
            // Find the end of this line
            std::size_t line_end = source.find('\n', line_start);
            if (line_end == std::string_view::npos) {
                line_end = source.size();
            }
            return source.substr(line_start, line_end - line_start);
        }

        if (source[i] == '\n') {
            ++current_line;
        }
    }

    // Line not found (past end of file)
    return {};
}

std::string Terminal::format_line_number(std::uint32_t line, std::uint32_t max_line) {
    // Determine width needed for line numbers
    std::uint32_t width = 1;
    std::uint32_t temp = max_line;
    while (temp >= 10) {
        ++width;
        temp /= 10;
    }

    std::ostringstream oss;
    oss.width(static_cast<std::streamsize>(width));
    oss << line;
    return oss.str();
}

void Terminal::print_source_snippet(std::string_view source, const core::dsl::SourceSpan& span,
                                    std::string_view message) {
    std::uint32_t line_num = span.start.line;
    std::string_view line_text = get_line(source, line_num);

    if (line_text.empty()) {
        return;
    }

    // Calculate line number width (for alignment)
    std::string line_str = format_line_number(line_num, line_num + 1);
    std::size_t gutter_width = line_str.size();

    // Print the source line
    apply_color(AnsiColor::Blue);
    out_ << line_str << " | ";
    reset_color();
    out_ << line_text << '\n';

    // Print the error marker line
    // Gutter spaces + " | " + column offset
    std::string gutter_spaces(gutter_width, ' ');
    apply_color(AnsiColor::Blue);
    out_ << gutter_spaces << " | ";
    reset_color();

    // Calculate column position (1-based column to 0-based offset)
    std::uint32_t col = span.start.column;
    if (col > 0) {
        --col;  // Convert to 0-based
    }

    // Account for tabs in the line before the error
    std::size_t visual_col = 0;
    for (std::size_t i = 0; i < col && i < line_text.size(); ++i) {
        if (line_text[i] == '\t') {
            visual_col += 4;  // Assume tab width of 4
        } else {
            ++visual_col;
        }
    }

    // Print spaces to reach the error column
    std::string spaces(visual_col, ' ');
    out_ << spaces;

    // Calculate error span length (minimum 1)
    std::size_t span_length = 1;
    if (span.start.line == span.end.line && span.end.column > span.start.column) {
        span_length = span.end.column - span.start.column;
    }

    // Print the caret(s)
    apply_color(AnsiColor::BrightGreen);
    out_ << '^';
    for (std::size_t i = 1; i < span_length; ++i) {
        out_ << '~';
    }
    reset_color();

    // Print message if provided
    if (!message.empty()) {
        out_ << ' ';
        apply_color(AnsiColor::BrightGreen);
        out_ << message;
        reset_color();
    }

    out_ << '\n';
}

void Terminal::print_error(std::string_view filename, std::string_view source,
                           const core::dsl::SourceSpan& span, std::string_view label,
                           std::string_view message) {
    print_error_header(filename, span.start, label);
    out_ << message << '\n';
    print_source_snippet(source, span, {});
}

}  // namespace dotvm::cli
