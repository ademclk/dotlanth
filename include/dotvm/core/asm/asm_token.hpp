#pragma once

/// @file asm_token.hpp
/// @brief TOOL-004 Token types for the assembly lexer
///
/// Defines token types and the AsmToken struct used by the assembly lexer.
/// Tokens carry zero-copy lexemes via string_view into the source.

#include <cstdint>
#include <string_view>

#include "dotvm/core/dsl/source_location.hpp"

namespace dotvm::core::asm_ {

// Reuse SourceLocation and SourceSpan from DSL
using dsl::SourceLocation;
using dsl::SourceSpan;

/// @brief Token types for the assembly lexer
///
/// Organized by category for clarity:
/// - Structure tokens (0-9): control parsing flow
/// - Labels/Identifiers (10-19): names and references
/// - Operands (20-29): instruction operands
/// - Directives (30-39): assembler directives
/// - Punctuation (40-49): delimiters and operators
enum class AsmTokenType : std::uint8_t {
    // ===== Structure (0-9) =====

    /// End of file
    Eof = 0,

    /// Lexer error (check error list)
    Error = 1,

    /// Newline (significant for statement termination)
    Newline = 2,

    // ===== Labels/Identifiers (10-19) =====

    /// Label definition (identifier followed by colon, e.g., "main:")
    /// The lexeme includes the trailing colon
    Label = 10,

    /// Identifier (label references, symbol names)
    Identifier = 11,

    // ===== Operands (20-29) =====

    /// Opcode mnemonic (ADD, SUB, LOAD, etc.)
    /// Validated against known opcodes during lexing
    Opcode = 20,

    /// Register (R0-R255)
    Register = 21,

    /// Immediate value (#123, #0xFF, #-45, #0b1010)
    Immediate = 22,

    /// String literal ("hello")
    String = 23,

    // ===== Directives (30-39) =====

    /// .section directive
    DirSection = 30,

    /// .global directive
    DirGlobal = 31,

    /// .data directive
    DirData = 32,

    /// .text directive
    DirText = 33,

    /// .byte directive
    DirByte = 34,

    /// .word directive
    DirWord = 35,

    /// .include directive
    DirInclude = 36,

    /// .dword directive (64-bit)
    DirDword = 37,

    /// .align directive
    DirAlign = 38,

    // ===== Punctuation (40-49) =====

    /// ',' comma (operand separator)
    Comma = 40,

    /// ':' colon (label terminator)
    Colon = 41,

    /// '[' left bracket (memory operand start)
    LBracket = 42,

    /// ']' right bracket (memory operand end)
    RBracket = 43,

    /// '+' plus (memory offset)
    Plus = 44,

    /// '-' minus (memory offset, negative immediate)
    Minus = 45,
};

/// @brief Convert AsmTokenType to human-readable string
[[nodiscard]] constexpr const char* to_string(AsmTokenType type) noexcept {
    switch (type) {
        // Structure
        case AsmTokenType::Eof:
            return "Eof";
        case AsmTokenType::Error:
            return "Error";
        case AsmTokenType::Newline:
            return "Newline";
        // Labels/Identifiers
        case AsmTokenType::Label:
            return "Label";
        case AsmTokenType::Identifier:
            return "Identifier";
        // Operands
        case AsmTokenType::Opcode:
            return "Opcode";
        case AsmTokenType::Register:
            return "Register";
        case AsmTokenType::Immediate:
            return "Immediate";
        case AsmTokenType::String:
            return "String";
        // Directives
        case AsmTokenType::DirSection:
            return "DirSection";
        case AsmTokenType::DirGlobal:
            return "DirGlobal";
        case AsmTokenType::DirData:
            return "DirData";
        case AsmTokenType::DirText:
            return "DirText";
        case AsmTokenType::DirByte:
            return "DirByte";
        case AsmTokenType::DirWord:
            return "DirWord";
        case AsmTokenType::DirInclude:
            return "DirInclude";
        case AsmTokenType::DirDword:
            return "DirDword";
        case AsmTokenType::DirAlign:
            return "DirAlign";
        // Punctuation
        case AsmTokenType::Comma:
            return "Comma";
        case AsmTokenType::Colon:
            return "Colon";
        case AsmTokenType::LBracket:
            return "LBracket";
        case AsmTokenType::RBracket:
            return "RBracket";
        case AsmTokenType::Plus:
            return "Plus";
        case AsmTokenType::Minus:
            return "Minus";
    }
    return "Unknown";
}

/// @brief Check if token type is a directive
[[nodiscard]] constexpr bool is_directive(AsmTokenType type) noexcept {
    auto val = static_cast<std::uint8_t>(type);
    return val >= 30 && val <= 38;
}

/// @brief Check if token type is punctuation
[[nodiscard]] constexpr bool is_punctuation(AsmTokenType type) noexcept {
    auto val = static_cast<std::uint8_t>(type);
    return val >= 40 && val <= 45;
}

/// @brief Check if token type can start an operand
[[nodiscard]] constexpr bool can_start_operand(AsmTokenType type) noexcept {
    return type == AsmTokenType::Register || type == AsmTokenType::Immediate ||
           type == AsmTokenType::Identifier || type == AsmTokenType::LBracket ||
           type == AsmTokenType::String;
}

/// @brief A token from the assembly lexer
///
/// Tokens are lightweight, carrying only a string_view into the source
/// buffer for zero-copy lexeme access.
struct AsmToken {
    /// Token type
    AsmTokenType type{AsmTokenType::Eof};

    /// Location span in source
    SourceSpan span;

    /// Lexeme text (view into source buffer)
    std::string_view lexeme;

    /// For Register tokens: the register number (0-255)
    /// For Immediate tokens: not used (parse from lexeme)
    /// For Opcode tokens: the opcode value (if validated)
    std::uint8_t value{0};

    /// @brief Create an EOF token
    [[nodiscard]] static AsmToken eof(SourceLocation loc) noexcept {
        return AsmToken{AsmTokenType::Eof, SourceSpan::at(loc), "", 0};
    }

    /// @brief Create an error token
    [[nodiscard]] static AsmToken error(SourceSpan span, std::string_view lexeme = "") noexcept {
        return AsmToken{AsmTokenType::Error, span, lexeme, 0};
    }

    /// @brief Create a token with full details
    [[nodiscard]] static AsmToken make(AsmTokenType type, SourceSpan span,
                                       std::string_view lexeme) noexcept {
        return AsmToken{type, span, lexeme, 0};
    }

    /// @brief Create a token with full details and value
    [[nodiscard]] static AsmToken make(AsmTokenType type, SourceSpan span, std::string_view lexeme,
                                       std::uint8_t val) noexcept {
        return AsmToken{type, span, lexeme, val};
    }

    /// @brief Check if this is an EOF token
    [[nodiscard]] bool is_eof() const noexcept { return type == AsmTokenType::Eof; }

    /// @brief Check if this is an error token
    [[nodiscard]] bool is_error() const noexcept { return type == AsmTokenType::Error; }

    /// @brief Check if this is a specific type
    [[nodiscard]] bool is(AsmTokenType t) const noexcept { return type == t; }

    /// @brief Check if this is any of the given types
    template <typename... Types>
    [[nodiscard]] bool is_any(Types... types) const noexcept {
        return ((type == types) || ...);
    }
};

}  // namespace dotvm::core::asm_
