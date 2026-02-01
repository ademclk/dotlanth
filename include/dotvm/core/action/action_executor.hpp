#pragma once

/// @file action_executor.hpp
/// @brief DEP-005 Action execution orchestrator
///
/// Coordinates registry lookup, parameter validation, permission checks,
/// rate limiting, handler invocation, and audit logging.

#include <optional>
#include <string>

#include "dotvm/core/action/action_context.hpp"
#include "dotvm/core/action/action_def.hpp"
#include "dotvm/core/action/action_error.hpp"
#include "dotvm/core/action/action_registry.hpp"
#include "dotvm/core/action/action_result.hpp"
#include "dotvm/core/action/rate_limiter.hpp"
#include "dotvm/core/result.hpp"
#include "dotvm/core/security/security_context.hpp"

namespace dotvm::core::action {

class ActionExecutor {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ActionError>;

    ActionExecutor(ActionRegistry& registry, ActionRateLimiter& rate_limiter) noexcept;

    [[nodiscard]] Result<ActionResult> execute(const std::string& action_name,
                                               ActionContext& context) noexcept;

private:
    ActionRegistry& registry_;
    ActionRateLimiter& rate_limiter_;

    [[nodiscard]] Result<void> validate_parameters(const ActionDef& action,
                                                   ActionContext::ParamMap& params) noexcept;
    [[nodiscard]] Result<void> check_permissions(const ActionDef& action,
                                                 security::SecurityContext* security) noexcept;
    [[nodiscard]] Result<void> check_rate_limit(const ActionDef& action) noexcept;
    void log_audit_event(security::AuditEventType type, const ActionContext& context,
                         std::optional<ActionError> error = std::nullopt,
                         security::Permission permission = security::Permission::None) noexcept;
    [[nodiscard]] static security::AuditEventType audit_event_for_error(ActionError error) noexcept;
};

}  // namespace dotvm::core::action
