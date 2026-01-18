#pragma once

/// @file policy_error.hpp
/// @brief SEC-009 Policy error types for the policy enforcement engine
///
/// Defines error types for policy parsing, validation, and evaluation.

#include <cstdint>
#include <string>
#include <string_view>

namespace dotvm::core::policy {

/// @brief Error codes for policy operations
enum class PolicyError : std::uint8_t {
    /// Operation succeeded
    Success = 0,

    // ===== Parsing Errors (1-19) =====

    /// JSON syntax error
    JsonSyntaxError = 1,

    /// Unexpected end of input
    UnexpectedEof = 2,

    /// Invalid character in input
    InvalidCharacter = 3,

    /// Invalid escape sequence in string
    InvalidEscapeSequence = 4,

    /// Number overflow or invalid format
    InvalidNumber = 5,

    /// Invalid UTF-8 encoding
    InvalidUtf8 = 6,

    /// Nesting depth exceeded
    NestingTooDeep = 7,

    // ===== Schema Errors (20-39) =====

    /// Missing required field
    MissingField = 20,

    /// Invalid field type
    InvalidFieldType = 21,

    /// Invalid rule ID (must be positive)
    InvalidRuleId = 22,

    /// Invalid priority value
    InvalidPriority = 23,

    /// Unknown opcode name
    UnknownOpcode = 24,

    /// Invalid condition format
    InvalidCondition = 25,

    /// Invalid action format
    InvalidAction = 26,

    /// Invalid time window format
    InvalidTimeWindow = 27,

    /// Invalid memory region format
    InvalidMemoryRegion = 28,

    /// Duplicate rule ID
    DuplicateRuleId = 29,

    // ===== Evaluation Errors (40-59) =====

    /// No policy loaded
    NoPolicyLoaded = 40,

    /// Policy file not found
    FileNotFound = 41,

    /// Policy file read error
    FileReadError = 42,

    /// Internal evaluation error
    EvaluationError = 43,
};

/// @brief Convert PolicyError to human-readable string
[[nodiscard]] constexpr const char* to_string(PolicyError error) noexcept {
    switch (error) {
        case PolicyError::Success:
            return "Success";
        case PolicyError::JsonSyntaxError:
            return "JsonSyntaxError";
        case PolicyError::UnexpectedEof:
            return "UnexpectedEof";
        case PolicyError::InvalidCharacter:
            return "InvalidCharacter";
        case PolicyError::InvalidEscapeSequence:
            return "InvalidEscapeSequence";
        case PolicyError::InvalidNumber:
            return "InvalidNumber";
        case PolicyError::InvalidUtf8:
            return "InvalidUtf8";
        case PolicyError::NestingTooDeep:
            return "NestingTooDeep";
        case PolicyError::MissingField:
            return "MissingField";
        case PolicyError::InvalidFieldType:
            return "InvalidFieldType";
        case PolicyError::InvalidRuleId:
            return "InvalidRuleId";
        case PolicyError::InvalidPriority:
            return "InvalidPriority";
        case PolicyError::UnknownOpcode:
            return "UnknownOpcode";
        case PolicyError::InvalidCondition:
            return "InvalidCondition";
        case PolicyError::InvalidAction:
            return "InvalidAction";
        case PolicyError::InvalidTimeWindow:
            return "InvalidTimeWindow";
        case PolicyError::InvalidMemoryRegion:
            return "InvalidMemoryRegion";
        case PolicyError::DuplicateRuleId:
            return "DuplicateRuleId";
        case PolicyError::NoPolicyLoaded:
            return "NoPolicyLoaded";
        case PolicyError::FileNotFound:
            return "FileNotFound";
        case PolicyError::FileReadError:
            return "FileReadError";
        case PolicyError::EvaluationError:
            return "EvaluationError";
    }
    return "Unknown";
}

/// @brief Extended error information with location details
struct PolicyErrorInfo {
    /// Error code
    PolicyError error{PolicyError::Success};

    /// Line number (1-based, 0 if unknown)
    std::uint32_t line{0};

    /// Column number (1-based, 0 if unknown)
    std::uint32_t column{0};

    /// Additional context message
    std::string message;

    /// Check if this represents success
    [[nodiscard]] bool is_ok() const noexcept { return error == PolicyError::Success; }

    /// Check if this represents an error
    [[nodiscard]] bool is_err() const noexcept { return error != PolicyError::Success; }

    /// Create a success result
    [[nodiscard]] static PolicyErrorInfo ok() noexcept { return PolicyErrorInfo{}; }

    /// Create an error result
    [[nodiscard]] static PolicyErrorInfo err(PolicyError code, std::string_view msg = "",
                                             std::uint32_t line = 0,
                                             std::uint32_t col = 0) noexcept {
        return PolicyErrorInfo{code, line, col, std::string{msg}};
    }
};

}  // namespace dotvm::core::policy
