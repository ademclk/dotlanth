#pragma once

/// @file token.hpp
/// @brief DSL-001 Token types for the DSL lexer
///
/// Defines token types and the Token struct used by the lexer.
/// Tokens carry zero-copy lexemes via string_view into the source.

#include <cstdint>
#include <string_view>

#include "dotvm/core/dsl/source_location.hpp"

namespace dotvm::core::dsl {

/// @brief Token types for the DSL lexer
///
/// Organized by category for clarity:
/// - Structure tokens: control parsing flow
/// - Literals: values in the source
/// - Keywords: reserved words
/// - Punctuation: delimiters and grouping
/// - Operators: mathematical and comparison
enum class TokenType : std::uint8_t {
    // ===== Structure =====

    /// End of file
    Eof = 0,

    /// Lexer error (check error list)
    Error = 1,

    /// Newline (significant for statement termination)
    Newline = 2,

    /// Indentation increase
    Indent = 3,

    /// Indentation decrease
    Dedent = 4,

    // ===== Literals =====

    /// Identifier (variable/function names)
    Identifier = 10,

    /// Simple string (no interpolation)
    String = 11,

    /// String start (before first interpolation)
    StringStart = 12,

    /// String middle (between interpolations)
    StringMiddle = 13,

    /// String end (after last interpolation)
    StringEnd = 14,

    /// Integer literal
    Integer = 15,

    /// Floating-point literal
    Float = 16,

    // ===== Keywords =====

    /// 'dot' keyword (agent definition)
    KwDot = 20,

    /// 'when' keyword (trigger)
    KwWhen = 21,

    /// 'do' keyword (action block)
    KwDo = 22,

    /// 'state' keyword (state block)
    KwState = 23,

    /// 'link' keyword (agent linking)
    KwLink = 24,

    /// 'import' keyword
    KwImport = 25,

    /// 'true' literal
    KwTrue = 26,

    /// 'false' literal
    KwFalse = 27,

    /// 'and' logical operator
    KwAnd = 28,

    /// 'or' logical operator
    KwOr = 29,

    /// 'not' logical operator
    KwNot = 30,

    // ===== Punctuation =====

    /// ':' colon
    Colon = 40,

    /// ',' comma
    Comma = 41,

    /// '.' dot (member access)
    Dot = 42,

    /// '->' arrow (link operator)
    Arrow = 43,

    /// '(' left parenthesis
    LParen = 44,

    /// ')' right parenthesis
    RParen = 45,

    // ===== Operators =====

    /// '+' plus
    Plus = 50,

    /// '-' minus
    Minus = 51,

    /// '*' multiply
    Star = 52,

    /// '/' divide
    Slash = 53,

    /// '%' modulo
    Percent = 54,

    /// '=' assignment
    Equals = 55,

    /// '+=' add-assign
    PlusEquals = 56,

    /// '-=' subtract-assign
    MinusEquals = 57,

    /// '*=' multiply-assign
    StarEquals = 58,

    /// '/=' divide-assign
    SlashEquals = 59,

    /// '==' equality
    EqualEqual = 60,

    /// '!=' not equal
    NotEqual = 61,

    /// '<' less than
    Less = 62,

    /// '<=' less or equal
    LessEqual = 63,

    /// '>' greater than
    Greater = 64,

    /// '>=' greater or equal
    GreaterEqual = 65,
};

/// @brief Convert TokenType to human-readable string
[[nodiscard]] constexpr const char* to_string(TokenType type) noexcept {
    switch (type) {
        // Structure
        case TokenType::Eof:
            return "Eof";
        case TokenType::Error:
            return "Error";
        case TokenType::Newline:
            return "Newline";
        case TokenType::Indent:
            return "Indent";
        case TokenType::Dedent:
            return "Dedent";
        // Literals
        case TokenType::Identifier:
            return "Identifier";
        case TokenType::String:
            return "String";
        case TokenType::StringStart:
            return "StringStart";
        case TokenType::StringMiddle:
            return "StringMiddle";
        case TokenType::StringEnd:
            return "StringEnd";
        case TokenType::Integer:
            return "Integer";
        case TokenType::Float:
            return "Float";
        // Keywords
        case TokenType::KwDot:
            return "KwDot";
        case TokenType::KwWhen:
            return "KwWhen";
        case TokenType::KwDo:
            return "KwDo";
        case TokenType::KwState:
            return "KwState";
        case TokenType::KwLink:
            return "KwLink";
        case TokenType::KwImport:
            return "KwImport";
        case TokenType::KwTrue:
            return "KwTrue";
        case TokenType::KwFalse:
            return "KwFalse";
        case TokenType::KwAnd:
            return "KwAnd";
        case TokenType::KwOr:
            return "KwOr";
        case TokenType::KwNot:
            return "KwNot";
        // Punctuation
        case TokenType::Colon:
            return "Colon";
        case TokenType::Comma:
            return "Comma";
        case TokenType::Dot:
            return "Dot";
        case TokenType::Arrow:
            return "Arrow";
        case TokenType::LParen:
            return "LParen";
        case TokenType::RParen:
            return "RParen";
        // Operators
        case TokenType::Plus:
            return "Plus";
        case TokenType::Minus:
            return "Minus";
        case TokenType::Star:
            return "Star";
        case TokenType::Slash:
            return "Slash";
        case TokenType::Percent:
            return "Percent";
        case TokenType::Equals:
            return "Equals";
        case TokenType::PlusEquals:
            return "PlusEquals";
        case TokenType::MinusEquals:
            return "MinusEquals";
        case TokenType::StarEquals:
            return "StarEquals";
        case TokenType::SlashEquals:
            return "SlashEquals";
        case TokenType::EqualEqual:
            return "EqualEqual";
        case TokenType::NotEqual:
            return "NotEqual";
        case TokenType::Less:
            return "Less";
        case TokenType::LessEqual:
            return "LessEqual";
        case TokenType::Greater:
            return "Greater";
        case TokenType::GreaterEqual:
            return "GreaterEqual";
    }
    return "Unknown";
}

/// @brief Check if token type is a keyword
[[nodiscard]] constexpr bool is_keyword(TokenType type) noexcept {
    return static_cast<std::uint8_t>(type) >= 20 && static_cast<std::uint8_t>(type) <= 30;
}

/// @brief Check if token type is an operator
[[nodiscard]] constexpr bool is_operator(TokenType type) noexcept {
    return static_cast<std::uint8_t>(type) >= 50 && static_cast<std::uint8_t>(type) <= 65;
}

/// @brief Check if token type is a comparison operator
[[nodiscard]] constexpr bool is_comparison(TokenType type) noexcept {
    return type == TokenType::EqualEqual || type == TokenType::NotEqual ||
           type == TokenType::Less || type == TokenType::LessEqual || type == TokenType::Greater ||
           type == TokenType::GreaterEqual;
}

/// @brief Check if token type is an assignment operator
[[nodiscard]] constexpr bool is_assignment(TokenType type) noexcept {
    return type == TokenType::Equals || type == TokenType::PlusEquals ||
           type == TokenType::MinusEquals || type == TokenType::StarEquals ||
           type == TokenType::SlashEquals;
}

/// @brief A token from the lexer
///
/// Tokens are lightweight, carrying only a string_view into the source
/// buffer for zero-copy lexeme access.
struct Token {
    /// Token type
    TokenType type{TokenType::Eof};

    /// Location span in source
    SourceSpan span;

    /// Lexeme text (view into source buffer)
    std::string_view lexeme;

    /// @brief Create an EOF token
    [[nodiscard]] static Token eof(SourceLocation loc) noexcept {
        return Token{TokenType::Eof, SourceSpan::at(loc), ""};
    }

    /// @brief Create an error token
    [[nodiscard]] static Token error(SourceSpan span, std::string_view lexeme = "") noexcept {
        return Token{TokenType::Error, span, lexeme};
    }

    /// @brief Create a token with full details
    [[nodiscard]] static Token make(TokenType type, SourceSpan span,
                                    std::string_view lexeme) noexcept {
        return Token{type, span, lexeme};
    }

    /// @brief Check if this is an EOF token
    [[nodiscard]] bool is_eof() const noexcept { return type == TokenType::Eof; }

    /// @brief Check if this is an error token
    [[nodiscard]] bool is_error() const noexcept { return type == TokenType::Error; }

    /// @brief Check if this is a specific type
    [[nodiscard]] bool is(TokenType t) const noexcept { return type == t; }

    /// @brief Check if this is any of the given types
    template <typename... Types>
    [[nodiscard]] bool is_any(Types... types) const noexcept {
        return ((type == types) || ...);
    }
};

}  // namespace dotvm::core::dsl
