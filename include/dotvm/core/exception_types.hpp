#pragma once

/// @file exception_types.hpp
/// @brief Exception type definitions for DotVM exception handling (EXEC-011)
///
/// This header defines the ErrorCode enumeration for categorizing exceptions
/// and the Exception struct for representing runtime exceptions.

#include <cstdint>
#include <string>
#include <string_view>

namespace dotvm::core {

// ============================================================================
// Error Code Enumeration
// ============================================================================

/// Exception type codes for structured exception handling
///
/// These codes identify the type of exception that occurred. Built-in codes
/// cover common runtime errors; user-defined exceptions start at Custom.
///
/// @note Values below 0x1000 are reserved for built-in exception types.
enum class ErrorCode : std::uint32_t {
    /// No exception (default state)
    None = 0,

    /// Division by zero attempted
    DivByZero = 1,

    /// Array/memory index out of bounds
    OutOfBounds = 2,

    /// Call stack or exception stack overflow
    StackOverflow = 3,

    /// Invalid memory handle (freed, wrong generation)
    InvalidHandle = 4,

    /// Type mismatch in operation
    TypeMismatch = 5,

    /// Null pointer dereference
    NullPointer = 6,

    /// Assertion failed
    AssertionFailed = 7,

    /// Invalid argument to operation
    InvalidArgument = 8,

    /// Operation not supported
    NotSupported = 9,

    /// I/O error
    IoError = 10,

    /// User-defined exceptions start here (0x1000 = 4096)
    Custom = 0x1000,
};

// ============================================================================
// Catch Type Masks
// ============================================================================

/// Bitmask constants for catch_types field in exception frames
///
/// These masks allow handlers to selectively catch specific exception types.
/// Multiple masks can be ORed together to catch multiple types.
namespace catch_mask {

/// Catch all exception types
inline constexpr std::uint8_t ALL = 0xFF;

/// Catch DivByZero exceptions
inline constexpr std::uint8_t DIVZERO = 0x01;

/// Catch OutOfBounds exceptions
inline constexpr std::uint8_t BOUNDS = 0x02;

/// Catch StackOverflow exceptions
inline constexpr std::uint8_t STACK = 0x04;

/// Catch InvalidHandle exceptions
inline constexpr std::uint8_t HANDLE = 0x08;

/// Catch TypeMismatch exceptions
inline constexpr std::uint8_t TYPE = 0x10;

/// Catch NullPointer exceptions
inline constexpr std::uint8_t NULLPTR = 0x20;

/// Catch user-defined (Custom) exceptions
inline constexpr std::uint8_t CUSTOM = 0x80;

}  // namespace catch_mask

// ============================================================================
// Error Code Utilities
// ============================================================================

/// Convert ErrorCode to human-readable string
///
/// @param code The error code to convert
/// @return String representation of the error code
[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::None:            return "None";
        case ErrorCode::DivByZero:       return "DivByZero";
        case ErrorCode::OutOfBounds:     return "OutOfBounds";
        case ErrorCode::StackOverflow:   return "StackOverflow";
        case ErrorCode::InvalidHandle:   return "InvalidHandle";
        case ErrorCode::TypeMismatch:    return "TypeMismatch";
        case ErrorCode::NullPointer:     return "NullPointer";
        case ErrorCode::AssertionFailed: return "AssertionFailed";
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::NotSupported:    return "NotSupported";
        case ErrorCode::IoError:         return "IoError";
        default:
            if (static_cast<std::uint32_t>(code) >= static_cast<std::uint32_t>(ErrorCode::Custom)) {
                return "Custom";
            }
            return "Unknown";
    }
}

/// Convert ErrorCode to catch mask for handler matching
///
/// Maps an ErrorCode to its corresponding catch_mask bit. User-defined
/// exceptions (>= Custom) map to CUSTOM mask.
///
/// @param code The error code to convert
/// @return Catch mask bit for this error code
[[nodiscard]] constexpr std::uint8_t error_to_catch_mask(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::None:          return 0;
        case ErrorCode::DivByZero:     return catch_mask::DIVZERO;
        case ErrorCode::OutOfBounds:   return catch_mask::BOUNDS;
        case ErrorCode::StackOverflow: return catch_mask::STACK;
        case ErrorCode::InvalidHandle: return catch_mask::HANDLE;
        case ErrorCode::TypeMismatch:  return catch_mask::TYPE;
        case ErrorCode::NullPointer:   return catch_mask::NULLPTR;
        default:
            // User-defined exceptions and others map to CUSTOM
            return catch_mask::CUSTOM;
    }
}

/// Check if a catch mask matches an error code
///
/// @param mask The catch mask from an exception frame
/// @param code The error code being thrown
/// @return true if the handler should catch this exception
[[nodiscard]] constexpr bool catch_mask_matches(std::uint8_t mask, ErrorCode code) noexcept {
    if (mask == catch_mask::ALL) {
        return true;
    }
    return (mask & error_to_catch_mask(code)) != 0;
}

/// Check if error code represents an active exception
[[nodiscard]] constexpr bool is_exception_active(ErrorCode code) noexcept {
    return code != ErrorCode::None;
}

/// Check if error code is a built-in type (not custom)
[[nodiscard]] constexpr bool is_builtin_error(ErrorCode code) noexcept {
    return static_cast<std::uint32_t>(code) < static_cast<std::uint32_t>(ErrorCode::Custom);
}

// ============================================================================
// Exception Structure
// ============================================================================

/// Runtime exception containing type, payload, and context
///
/// This structure represents an active exception during execution. It stores
/// the exception type, an optional numeric payload, optional message string,
/// and the program counter where the exception was thrown.
///
/// Memory layout:
/// - type_id:   4 bytes (ErrorCode enum)
/// - padding:   4 bytes
/// - payload:   8 bytes (numeric value or Value bits)
/// - throw_pc:  8 bytes (instruction index)
/// - message:   24+ bytes (std::string, SSO)
struct Exception {
    /// Exception type code
    ErrorCode type_id{ErrorCode::None};

    /// Numeric payload (can hold Value bits or custom data)
    ///
    /// Usage depends on exception type:
    /// - DivByZero: divisor value
    /// - OutOfBounds: attempted index
    /// - Custom: user-defined payload
    std::uint64_t payload{0};

    /// Program counter where exception was thrown
    ///
    /// This is the instruction index (not byte offset) of the THROW
    /// instruction that raised this exception.
    std::size_t throw_pc{0};

    /// Optional human-readable message
    ///
    /// May be empty for performance-critical exceptions. Populated
    /// for debugging or user-facing error reporting.
    std::string message;

    // ========================================================================
    // State Queries
    // ========================================================================

    /// Check if an exception is currently active
    ///
    /// @return true if type_id is not None
    [[nodiscard]] bool is_active() const noexcept {
        return type_id != ErrorCode::None;
    }

    /// Check if this is a user-defined exception
    ///
    /// @return true if type_id >= Custom
    [[nodiscard]] bool is_custom() const noexcept {
        return !is_builtin_error(type_id);
    }

    // ========================================================================
    // Modification
    // ========================================================================

    /// Clear the exception state
    ///
    /// Resets all fields to their default values.
    void clear() noexcept {
        type_id = ErrorCode::None;
        payload = 0;
        throw_pc = 0;
        message.clear();
    }

    // ========================================================================
    // Factory Methods
    // ========================================================================

    /// Create an exception with type only
    ///
    /// @param type The exception type
    /// @param pc Program counter where thrown (default 0)
    /// @return Exception instance
    [[nodiscard]] static Exception make(ErrorCode type, std::size_t pc = 0) noexcept {
        Exception exc;
        exc.type_id = type;
        exc.throw_pc = pc;
        return exc;
    }

    /// Create an exception with type and payload
    ///
    /// @param type The exception type
    /// @param payload_value Numeric payload
    /// @param pc Program counter where thrown (default 0)
    /// @return Exception instance
    [[nodiscard]] static Exception make(ErrorCode type, std::uint64_t payload_value,
                                        std::size_t pc = 0) noexcept {
        Exception exc;
        exc.type_id = type;
        exc.payload = payload_value;
        exc.throw_pc = pc;
        return exc;
    }

    /// Create an exception with type, payload, and message
    ///
    /// @param type The exception type
    /// @param payload_value Numeric payload
    /// @param msg Error message
    /// @param pc Program counter where thrown (default 0)
    /// @return Exception instance
    [[nodiscard]] static Exception make(ErrorCode type, std::uint64_t payload_value,
                                        std::string msg, std::size_t pc = 0) {
        Exception exc;
        exc.type_id = type;
        exc.payload = payload_value;
        exc.message = std::move(msg);
        exc.throw_pc = pc;
        return exc;
    }

    // ========================================================================
    // Comparison
    // ========================================================================

    /// Equality comparison
    [[nodiscard]] bool operator==(const Exception& other) const noexcept {
        return type_id == other.type_id &&
               payload == other.payload &&
               throw_pc == other.throw_pc &&
               message == other.message;
    }
};

}  // namespace dotvm::core
