#pragma once

/// @file dsl_error.hpp
/// @brief DSL-001 Error types for the DSL parser
///
/// Defines error codes for lexing, parsing, and structural validation.
/// Supports multi-error collection for comprehensive error reporting.

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/core/dsl/source_location.hpp"

namespace dotvm::core::dsl {

/// @brief Error codes for DSL operations
///
/// Error codes are grouped by category:
/// - Lexer errors: 1-19
/// - Parser errors: 20-39
/// - Structural errors: 40-49
enum class DslError : std::uint8_t {
    /// Operation succeeded
    Success = 0,

    // ===== Lexer Errors (1-19) =====

    /// Unexpected character in input
    UnexpectedCharacter = 1,

    /// Unterminated string literal
    UnterminatedString = 2,

    /// Invalid escape sequence in string
    InvalidEscapeSequence = 3,

    /// Invalid number format
    InvalidNumber = 4,

    /// Unterminated interpolation in string
    UnterminatedInterpolation = 5,

    /// Invalid interpolation syntax
    InvalidInterpolation = 6,

    /// Inconsistent indentation (mixed tabs/spaces)
    InconsistentIndentation = 7,

    /// Invalid indentation level
    InvalidIndentation = 8,

    /// Unexpected end of file
    UnexpectedEof = 9,

    /// Invalid identifier
    InvalidIdentifier = 10,

    // ===== Parser Errors (20-39) =====

    /// Unexpected token
    UnexpectedToken = 20,

    /// Expected identifier
    ExpectedIdentifier = 21,

    /// Expected colon
    ExpectedColon = 22,

    /// Expected expression
    ExpectedExpression = 23,

    /// Expected keyword
    ExpectedKeyword = 24,

    /// Expected indent
    ExpectedIndent = 25,

    /// Expected dedent
    ExpectedDedent = 26,

    /// Expected string literal
    ExpectedString = 27,

    /// Expected newline
    ExpectedNewline = 28,

    /// Invalid assignment target
    InvalidAssignment = 29,

    /// Expected arrow (->)
    ExpectedArrow = 30,

    /// Invalid trigger condition
    InvalidTriggerCondition = 31,

    /// Invalid action
    InvalidAction = 32,

    // ===== Structural Errors (40-49) =====

    /// Duplicate state definition
    DuplicateState = 40,

    /// Duplicate variable name
    DuplicateVariable = 41,

    /// Undefined variable reference
    UndefinedVariable = 42,

    /// Invalid dot definition
    InvalidDotDef = 43,

    /// Maximum nesting depth exceeded
    NestingTooDeep = 44,

    /// Invalid import path
    InvalidImportPath = 45,

    /// Duplicate import
    DuplicateImport = 46,

    /// Circular import detected
    CircularImport = 47,
};

/// @brief Convert DslError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(DslError error) noexcept {
    switch (error) {
        case DslError::Success:
            return "Success";
        // Lexer errors
        case DslError::UnexpectedCharacter:
            return "UnexpectedCharacter";
        case DslError::UnterminatedString:
            return "UnterminatedString";
        case DslError::InvalidEscapeSequence:
            return "InvalidEscapeSequence";
        case DslError::InvalidNumber:
            return "InvalidNumber";
        case DslError::UnterminatedInterpolation:
            return "UnterminatedInterpolation";
        case DslError::InvalidInterpolation:
            return "InvalidInterpolation";
        case DslError::InconsistentIndentation:
            return "InconsistentIndentation";
        case DslError::InvalidIndentation:
            return "InvalidIndentation";
        case DslError::UnexpectedEof:
            return "UnexpectedEof";
        case DslError::InvalidIdentifier:
            return "InvalidIdentifier";
        // Parser errors
        case DslError::UnexpectedToken:
            return "UnexpectedToken";
        case DslError::ExpectedIdentifier:
            return "ExpectedIdentifier";
        case DslError::ExpectedColon:
            return "ExpectedColon";
        case DslError::ExpectedExpression:
            return "ExpectedExpression";
        case DslError::ExpectedKeyword:
            return "ExpectedKeyword";
        case DslError::ExpectedIndent:
            return "ExpectedIndent";
        case DslError::ExpectedDedent:
            return "ExpectedDedent";
        case DslError::ExpectedString:
            return "ExpectedString";
        case DslError::ExpectedNewline:
            return "ExpectedNewline";
        case DslError::InvalidAssignment:
            return "InvalidAssignment";
        case DslError::ExpectedArrow:
            return "ExpectedArrow";
        case DslError::InvalidTriggerCondition:
            return "InvalidTriggerCondition";
        case DslError::InvalidAction:
            return "InvalidAction";
        // Structural errors
        case DslError::DuplicateState:
            return "DuplicateState";
        case DslError::DuplicateVariable:
            return "DuplicateVariable";
        case DslError::UndefinedVariable:
            return "UndefinedVariable";
        case DslError::InvalidDotDef:
            return "InvalidDotDef";
        case DslError::NestingTooDeep:
            return "NestingTooDeep";
        case DslError::InvalidImportPath:
            return "InvalidImportPath";
        case DslError::DuplicateImport:
            return "DuplicateImport";
        case DslError::CircularImport:
            return "CircularImport";
    }
    return "Unknown";
}

/// @brief Extended error information with location and context
struct DslErrorInfo {
    /// Error code
    DslError error{DslError::Success};

    /// Location in source where error occurred
    SourceSpan span;

    /// Additional context message
    std::string message;

    /// @brief Check if this represents success
    [[nodiscard]] bool is_ok() const noexcept { return error == DslError::Success; }

    /// @brief Check if this represents an error
    [[nodiscard]] bool is_err() const noexcept { return error != DslError::Success; }

    /// @brief Create a success result
    [[nodiscard]] static DslErrorInfo ok() noexcept { return DslErrorInfo{}; }

    /// @brief Create an error result
    [[nodiscard]] static DslErrorInfo err(DslError code, SourceSpan span,
                                          std::string_view msg = "") noexcept {
        return DslErrorInfo{code, span, std::string{msg}};
    }

    /// @brief Create an error result from a single location
    [[nodiscard]] static DslErrorInfo err(DslError code, SourceLocation loc,
                                          std::string_view msg = "") noexcept {
        return DslErrorInfo{code, SourceSpan::at(loc), std::string{msg}};
    }
};

/// @brief Collection of DSL errors with capacity limit
///
/// Collects multiple errors during parsing for comprehensive error reporting.
/// Has a maximum capacity to prevent unbounded memory growth on malformed input.
class DslErrorList {
public:
    /// Maximum number of errors to collect
    static constexpr std::size_t MAX_ERRORS = 32;

    /// @brief Add an error to the list
    /// @return true if error was added, false if at capacity
    bool add(DslErrorInfo error) {
        if (errors_.size() >= MAX_ERRORS) {
            return false;
        }
        errors_.push_back(std::move(error));
        return true;
    }

    /// @brief Add an error by components
    bool add(DslError code, SourceSpan span, std::string_view msg = "") {
        return add(DslErrorInfo::err(code, span, msg));
    }

    /// @brief Add an error by components (single location)
    bool add(DslError code, SourceLocation loc, std::string_view msg = "") {
        return add(DslErrorInfo::err(code, loc, msg));
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
    [[nodiscard]] const std::vector<DslErrorInfo>& errors() const noexcept { return errors_; }

    /// @brief Access error by index
    [[nodiscard]] const DslErrorInfo& operator[](std::size_t index) const { return errors_[index]; }

    /// @brief Clear all errors
    void clear() noexcept { errors_.clear(); }

    /// @brief Iterator support
    [[nodiscard]] auto begin() const noexcept { return errors_.begin(); }
    [[nodiscard]] auto end() const noexcept { return errors_.end(); }

private:
    std::vector<DslErrorInfo> errors_;
};

}  // namespace dotvm::core::dsl

// ============================================================================
// std::formatter specialization for DslError
// ============================================================================

template <>
struct std::formatter<dotvm::core::dsl::DslError> : std::formatter<std::string_view> {
    auto format(dotvm::core::dsl::DslError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
