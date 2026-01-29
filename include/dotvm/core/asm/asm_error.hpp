#pragma once

/// @file asm_error.hpp
/// @brief TOOL-004 Error types for the assembly lexer and parser
///
/// Defines error codes for lexing and parsing assembly source.
/// Supports multi-error collection for comprehensive error reporting.

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/core/dsl/source_location.hpp"

namespace dotvm::core::asm_ {

// Reuse SourceLocation and SourceSpan from DSL
using dsl::SourceLocation;
using dsl::SourceSpan;

/// @brief Error codes for assembly operations
///
/// Error codes are grouped by category:
/// - Lexer errors: 1-29
/// - Parser errors: 30-59
/// - Semantic errors: 60-79
enum class AsmError : std::uint8_t {
    /// Operation succeeded
    Success = 0,

    // ===== Lexer Errors (1-29) =====

    /// Unexpected character in input
    UnexpectedCharacter = 1,

    /// Invalid register name (expected R0-R255)
    InvalidRegister = 2,

    /// Invalid immediate value
    InvalidImmediate = 3,

    /// Unterminated string literal
    UnterminatedString = 4,

    /// Invalid escape sequence in string
    InvalidEscapeSequence = 5,

    /// Invalid hexadecimal number
    InvalidHexNumber = 6,

    /// Invalid binary number
    InvalidBinaryNumber = 7,

    /// Invalid label format
    InvalidLabel = 8,

    /// Invalid directive name
    InvalidDirective = 9,

    /// Unexpected end of file
    UnexpectedEof = 10,

    /// Number out of range
    NumberOutOfRange = 11,

    // ===== Parser Errors (30-59) =====

    /// Unexpected token
    UnexpectedToken = 30,

    /// Expected opcode
    ExpectedOpcode = 31,

    /// Expected register operand
    ExpectedRegister = 32,

    /// Expected immediate operand
    ExpectedImmediate = 33,

    /// Expected label
    ExpectedLabel = 34,

    /// Expected comma between operands
    ExpectedComma = 35,

    /// Expected newline
    ExpectedNewline = 36,

    /// Expected colon after label
    ExpectedColon = 37,

    /// Expected right bracket
    ExpectedRBracket = 38,

    /// Invalid operand count for instruction
    InvalidOperandCount = 39,

    /// Invalid operand type for instruction
    InvalidOperandType = 40,

    /// Unknown opcode mnemonic
    UnknownOpcode = 41,

    /// Expected directive argument
    ExpectedDirectiveArg = 42,

    /// Expected string literal
    ExpectedString = 43,

    // ===== Semantic Errors (60-79) =====

    /// Duplicate label definition
    DuplicateLabel = 60,

    /// Undefined label reference
    UndefinedLabel = 61,

    /// Circular include detected
    CircularInclude = 62,

    /// Include depth exceeded
    IncludeDepthExceeded = 63,

    /// File not found
    FileNotFound = 64,

    /// Invalid section name
    InvalidSection = 65,
};

/// @brief Convert AsmError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(AsmError error) noexcept {
    switch (error) {
        case AsmError::Success:
            return "Success";
        // Lexer errors
        case AsmError::UnexpectedCharacter:
            return "UnexpectedCharacter";
        case AsmError::InvalidRegister:
            return "InvalidRegister";
        case AsmError::InvalidImmediate:
            return "InvalidImmediate";
        case AsmError::UnterminatedString:
            return "UnterminatedString";
        case AsmError::InvalidEscapeSequence:
            return "InvalidEscapeSequence";
        case AsmError::InvalidHexNumber:
            return "InvalidHexNumber";
        case AsmError::InvalidBinaryNumber:
            return "InvalidBinaryNumber";
        case AsmError::InvalidLabel:
            return "InvalidLabel";
        case AsmError::InvalidDirective:
            return "InvalidDirective";
        case AsmError::UnexpectedEof:
            return "UnexpectedEof";
        case AsmError::NumberOutOfRange:
            return "NumberOutOfRange";
        // Parser errors
        case AsmError::UnexpectedToken:
            return "UnexpectedToken";
        case AsmError::ExpectedOpcode:
            return "ExpectedOpcode";
        case AsmError::ExpectedRegister:
            return "ExpectedRegister";
        case AsmError::ExpectedImmediate:
            return "ExpectedImmediate";
        case AsmError::ExpectedLabel:
            return "ExpectedLabel";
        case AsmError::ExpectedComma:
            return "ExpectedComma";
        case AsmError::ExpectedNewline:
            return "ExpectedNewline";
        case AsmError::ExpectedColon:
            return "ExpectedColon";
        case AsmError::ExpectedRBracket:
            return "ExpectedRBracket";
        case AsmError::InvalidOperandCount:
            return "InvalidOperandCount";
        case AsmError::InvalidOperandType:
            return "InvalidOperandType";
        case AsmError::UnknownOpcode:
            return "UnknownOpcode";
        case AsmError::ExpectedDirectiveArg:
            return "ExpectedDirectiveArg";
        case AsmError::ExpectedString:
            return "ExpectedString";
        // Semantic errors
        case AsmError::DuplicateLabel:
            return "DuplicateLabel";
        case AsmError::UndefinedLabel:
            return "UndefinedLabel";
        case AsmError::CircularInclude:
            return "CircularInclude";
        case AsmError::IncludeDepthExceeded:
            return "IncludeDepthExceeded";
        case AsmError::FileNotFound:
            return "FileNotFound";
        case AsmError::InvalidSection:
            return "InvalidSection";
    }
    return "Unknown";
}

/// @brief Extended error information with location and context
struct AsmErrorInfo {
    /// Error code
    AsmError error{AsmError::Success};

    /// Location in source where error occurred
    SourceSpan span;

    /// Additional context message
    std::string message;

    /// @brief Check if this represents success
    [[nodiscard]] bool is_ok() const noexcept { return error == AsmError::Success; }

    /// @brief Check if this represents an error
    [[nodiscard]] bool is_err() const noexcept { return error != AsmError::Success; }

    /// @brief Create a success result
    [[nodiscard]] static AsmErrorInfo ok() noexcept { return AsmErrorInfo{}; }

    /// @brief Create an error result
    [[nodiscard]] static AsmErrorInfo err(AsmError code, SourceSpan span,
                                          std::string_view msg = "") noexcept {
        return AsmErrorInfo{code, span, std::string{msg}};
    }

    /// @brief Create an error result from a single location
    [[nodiscard]] static AsmErrorInfo err(AsmError code, SourceLocation loc,
                                          std::string_view msg = "") noexcept {
        return AsmErrorInfo{code, SourceSpan::at(loc), std::string{msg}};
    }
};

/// @brief Collection of assembly errors with capacity limit
///
/// Collects multiple errors during parsing for comprehensive error reporting.
/// Has a maximum capacity to prevent unbounded memory growth on malformed input.
class AsmErrorList {
public:
    /// Maximum number of errors to collect
    static constexpr std::size_t MAX_ERRORS = 32;

    /// @brief Add an error to the list
    /// @return true if error was added, false if at capacity
    bool add(AsmErrorInfo error) {
        if (errors_.size() >= MAX_ERRORS) {
            return false;
        }
        errors_.push_back(std::move(error));
        return true;
    }

    /// @brief Add an error by components
    bool add(AsmError code, SourceSpan span, std::string_view msg = "") {
        return add(AsmErrorInfo::err(code, span, msg));
    }

    /// @brief Add an error by components (single location)
    bool add(AsmError code, SourceLocation loc, std::string_view msg = "") {
        return add(AsmErrorInfo::err(code, loc, msg));
    }

    /// @brief Check if there are any errors
    [[nodiscard]] bool has_errors() const noexcept { return !errors_.empty(); }

    /// @brief Check if error list is empty (no errors)
    [[nodiscard]] bool empty() const noexcept { return errors_.empty(); }

    /// @brief Get number of errors
    [[nodiscard]] std::size_t size() const noexcept { return errors_.size(); }

    /// @brief Check if at capacity
    [[nodiscard]] bool at_capacity() const noexcept { return errors_.size() >= MAX_ERRORS; }

    /// @brief Get all errors
    [[nodiscard]] const std::vector<AsmErrorInfo>& errors() const noexcept { return errors_; }

    /// @brief Access error by index
    [[nodiscard]] const AsmErrorInfo& operator[](std::size_t index) const { return errors_[index]; }

    /// @brief Clear all errors
    void clear() noexcept { errors_.clear(); }

    /// @brief Iterator support
    [[nodiscard]] auto begin() const noexcept { return errors_.begin(); }
    [[nodiscard]] auto end() const noexcept { return errors_.end(); }

private:
    std::vector<AsmErrorInfo> errors_;
};

}  // namespace dotvm::core::asm_

// ============================================================================
// std::formatter specialization for AsmError
// ============================================================================

template <>
struct std::formatter<dotvm::core::asm_::AsmError> : std::formatter<std::string_view> {
    auto format(dotvm::core::asm_::AsmError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
