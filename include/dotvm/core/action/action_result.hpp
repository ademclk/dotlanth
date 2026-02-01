#pragma once

/// @file action_result.hpp
/// @brief DEP-005 Action execution result container
///
/// Captures the outcome of an action handler invocation, including
/// return value, output parameters, timing, and error state.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "dotvm/core/action/action_error.hpp"
#include "dotvm/core/value.hpp"

namespace dotvm::core::action {

// ============================================================================
// ActionResult
// ============================================================================

/// @brief Result of executing an action
struct ActionResult {
    using OutputMap = std::unordered_map<std::string, Value>;

    Value return_value{};
    OutputMap outputs{};
    std::chrono::nanoseconds duration{0};
    std::uint64_t instructions_executed{0};
    bool success{false};
    std::optional<ActionError> error{std::nullopt};

    /// @brief Create a success result
    [[nodiscard]] static ActionResult ok(Value value) noexcept {
        ActionResult result{};
        result.return_value = value;
        result.success = true;
        result.error = std::nullopt;
        return result;
    }

    /// @brief Create a failure result
    [[nodiscard]] static ActionResult failed(ActionError err) noexcept {
        ActionResult result{};
        result.success = false;
        result.error = err;
        return result;
    }
};

}  // namespace dotvm::core::action
