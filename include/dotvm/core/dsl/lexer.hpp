#pragma once

/// @file lexer.hpp
/// @brief DSL-001 Indentation-aware lexer for the DotLanth DSL
///
/// Hand-written lexer optimized for performance:
/// - Zero-copy lexemes via string_view into source
/// - Indentation stack for INDENT/DEDENT token emission
/// - String interpolation support with token splitting
/// - Character classification lookup tables
/// - Keyword lookup via switch on (length << 8) | first_char

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "dotvm/core/dsl/dsl_error.hpp"
#include "dotvm/core/dsl/source_location.hpp"
#include "dotvm/core/dsl/token.hpp"

namespace dotvm::core::dsl {

/// @brief Indentation-aware lexer for the DotLanth DSL
///
/// Tokenizes DSL source with automatic INDENT/DEDENT emission based on
/// indentation levels. Supports string interpolation via ${...} syntax.
///
/// @note The lexer operates in two modes:
/// - Normal mode: tokenizes regular DSL syntax
/// - String mode: handles interpolated strings with ${...}
class Lexer {
public:
    /// Maximum indentation depth
    static constexpr std::size_t MAX_INDENT_DEPTH = 64;

    /// @brief Construct a lexer for the given source
    /// @param source Source code to tokenize (must remain valid during lexing)
    explicit Lexer(std::string_view source) noexcept;

    /// @brief Get the next token
    /// @return The next token from the source
    [[nodiscard]] Token next_token();

    /// @brief Peek at the next token without consuming it
    /// @return The next token (will be returned again on next next_token call)
    [[nodiscard]] const Token& peek();

    /// @brief Check if lexer has reached end of file
    [[nodiscard]] bool at_end() const noexcept;

    /// @brief Get the current source location
    [[nodiscard]] SourceLocation location() const noexcept;

    /// @brief Get accumulated errors
    [[nodiscard]] const DslErrorList& errors() const noexcept { return errors_; }

    /// @brief Check if there are any errors
    [[nodiscard]] bool has_errors() const noexcept { return errors_.has_errors(); }

    /// @brief Get the source being lexed
    [[nodiscard]] std::string_view source() const noexcept { return source_; }

private:
    // ===== Character Classification =====

    /// Check if character is alphabetic or underscore
    [[nodiscard]] static bool is_alpha(char c) noexcept;

    /// Check if character is digit
    [[nodiscard]] static bool is_digit(char c) noexcept;

    /// Check if character is alphanumeric or underscore
    [[nodiscard]] static bool is_alnum(char c) noexcept;

    /// Check if character is whitespace (excluding newline)
    [[nodiscard]] static bool is_whitespace(char c) noexcept;

    // ===== Basic Scanning =====

    /// Peek at current character
    [[nodiscard]] char peek_char() const noexcept;

    /// Peek at character n positions ahead
    [[nodiscard]] char peek_char(std::size_t n) const noexcept;

    /// Advance to next character
    char advance() noexcept;

    /// Check if current character matches expected
    bool match(char expected) noexcept;

    /// Skip to end of line (for comments)
    void skip_comment() noexcept;

    // ===== Token Scanning =====

    /// Scan an identifier or keyword
    [[nodiscard]] Token scan_identifier();

    /// Scan a number (integer or float)
    [[nodiscard]] Token scan_number();

    /// Scan a string literal (handles interpolation)
    [[nodiscard]] Token scan_string();

    /// Continue scanning string content after interpolation
    [[nodiscard]] Token continue_string();

    /// Lookup keyword from identifier text
    [[nodiscard]] TokenType lookup_keyword(std::string_view text) const noexcept;

    // ===== Indentation Handling =====

    /// Process indentation at start of line
    void process_indentation();

    /// Get current indentation level
    [[nodiscard]] std::size_t current_indent() const noexcept;

    /// Emit pending INDENT/DEDENT tokens
    [[nodiscard]] Token emit_indent_token();

    // ===== Error Handling =====

    /// Record an error
    void report_error(DslError code, std::string_view msg = "");

    /// Create an error token at current position
    [[nodiscard]] Token error_token(std::string_view msg = "");

    // ===== State =====

    /// Source code being lexed
    std::string_view source_;

    /// Current position in source
    std::size_t pos_{0};

    /// Current line (1-based)
    std::uint32_t line_{1};

    /// Current column (1-based)
    std::uint32_t column_{1};

    /// Token start position
    std::size_t token_start_{0};

    /// Token start location
    SourceLocation token_start_loc_;

    /// Indentation stack (stores column numbers)
    std::vector<std::size_t> indent_stack_;

    /// Pending DEDENT count (for emitting multiple DEDENTs)
    std::size_t pending_dedents_{0};

    /// Need to emit INDENT on next token
    bool pending_indent_{false};

    /// At start of line (for indentation processing)
    bool at_line_start_{true};

    /// Inside string interpolation
    bool in_interpolation_{false};

    /// Interpolation nesting depth (for nested ${})
    std::size_t interpolation_depth_{0};

    /// Accumulated errors
    DslErrorList errors_;

    /// Peeked token (for lookahead)
    Token peeked_token_;

    /// Whether we have a peeked token
    bool has_peeked_{false};
};

}  // namespace dotvm::core::dsl
