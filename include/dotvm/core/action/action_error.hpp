#pragma once

/// @file action_error.hpp
/// @brief DEP-005 Action system error codes
///
/// Error codes for Action operations, covering action management,
/// parameter validation, execution, and misc operational failures.

#include <cstdint>
#include <format>
#include <string_view>

namespace dotvm::core::action {

// ============================================================================
// ActionError Enum
// ============================================================================

/// @brief Error codes for action operations
///
/// Error codes are grouped by category in the 208-223 range:
/// - 208-211: Action management errors
/// - 212-215: Parameter errors
/// - 216-219: Execution errors
/// - 220-223: Miscellaneous errors
enum class ActionError : std::uint8_t {
    // Action management errors (208-211)
    ActionNotFound = 208,       ///< Action does not exist in registry
    ActionAlreadyExists = 209,  ///< Action name already registered
    InvalidActionName = 210,    ///< Action name is empty or invalid
    MaxActionsExceeded = 211,   ///< Maximum number of actions exceeded

    // Parameter errors (212-215)
    InvalidParameter = 212,          ///< Parameter value invalid for type
    RequiredParameterMissing = 213,  ///< Required parameter not provided
    UnknownParameter = 214,          ///< Parameter not defined in action
    ParameterTypeMismatch = 215,     ///< Parameter type does not match definition

    // Execution errors (216-219)
    PermissionDenied = 216,  ///< Caller lacks required permissions
    ExecutionFailed = 217,   ///< Bytecode execution failed
    HandlerNotFound = 218,   ///< Handler bytecode offset invalid
    ExecutionTimeout = 219,  ///< Action execution timed out

    // Misc errors (220-223)
    RateLimitExceeded = 220,   ///< Action rate limit exceeded
    AuditLoggingFailed = 221,  ///< Failed to log audit event
    ContextInvalid = 222,      ///< Action context is invalid
    Reserved = 223,            ///< Reserved for future use
};

/// @brief Convert ActionError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(ActionError error) noexcept {
    switch (error) {
        case ActionError::ActionNotFound:
            return "ActionNotFound";
        case ActionError::ActionAlreadyExists:
            return "ActionAlreadyExists";
        case ActionError::InvalidActionName:
            return "InvalidActionName";
        case ActionError::MaxActionsExceeded:
            return "MaxActionsExceeded";
        case ActionError::InvalidParameter:
            return "InvalidParameter";
        case ActionError::RequiredParameterMissing:
            return "RequiredParameterMissing";
        case ActionError::UnknownParameter:
            return "UnknownParameter";
        case ActionError::ParameterTypeMismatch:
            return "ParameterTypeMismatch";
        case ActionError::PermissionDenied:
            return "PermissionDenied";
        case ActionError::ExecutionFailed:
            return "ExecutionFailed";
        case ActionError::HandlerNotFound:
            return "HandlerNotFound";
        case ActionError::ExecutionTimeout:
            return "ExecutionTimeout";
        case ActionError::RateLimitExceeded:
            return "RateLimitExceeded";
        case ActionError::AuditLoggingFailed:
            return "AuditLoggingFailed";
        case ActionError::ContextInvalid:
            return "ContextInvalid";
        case ActionError::Reserved:
            return "Reserved";
    }
    return "Unknown";
}

/// @brief Check if an action error is recoverable
///
/// @param error The error to check
/// @return true if the error is recoverable
[[nodiscard]] constexpr bool is_recoverable(ActionError error) noexcept {
    return error == ActionError::ExecutionTimeout || error == ActionError::RateLimitExceeded;
}

/// @brief Check if an action error is related to action management
///
/// @param error The error to check
/// @return true if the error is an action-management-related error
[[nodiscard]] constexpr bool is_action_management_error(ActionError error) noexcept {
    const auto code = static_cast<std::uint8_t>(error);
    return code >= 208 && code <= 211;
}

/// @brief Check if an action error is related to parameters
///
/// @param error The error to check
/// @return true if the error is a parameter-related error
[[nodiscard]] constexpr bool is_parameter_error(ActionError error) noexcept {
    const auto code = static_cast<std::uint8_t>(error);
    return code >= 212 && code <= 215;
}

/// @brief Check if an action error is related to execution
///
/// @param error The error to check
/// @return true if the error is an execution-related error
[[nodiscard]] constexpr bool is_execution_error(ActionError error) noexcept {
    const auto code = static_cast<std::uint8_t>(error);
    return code >= 216 && code <= 219;
}

}  // namespace dotvm::core::action

// ============================================================================
// std::formatter specialization for ActionError
// ============================================================================

template <>
struct std::formatter<dotvm::core::action::ActionError> : std::formatter<std::string_view> {
    auto format(dotvm::core::action::ActionError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
