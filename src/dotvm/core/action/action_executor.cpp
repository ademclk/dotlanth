#include "dotvm/core/action/action_executor.hpp"

#include <chrono>
#include <utility>
#include <vector>

#include "dotvm/core/schema/validator.hpp"

namespace dotvm::core::action {

ActionExecutor::ActionExecutor(ActionRegistry& registry, ActionRateLimiter& rate_limiter) noexcept
    : registry_(registry), rate_limiter_(rate_limiter) {}

ActionExecutor::Result<ActionResult> ActionExecutor::execute(const std::string& action_name,
                                                             ActionContext& context) noexcept {
    context.action_name = action_name;
    if (context.start_time == ActionContext::TimePoint{}) {
        context.start_time = ActionContext::Clock::now();
    }

    // 1. Lookup action
    auto action_result = registry_.get_action(action_name);
    if (action_result.is_err()) {
        log_audit_event(audit_event_for_error(action_result.error()), context,
                        action_result.error());
        return action_result.error();
    }
    const auto& action = *action_result.value();

    // 2. Validate parameters
    if (auto param_result = validate_parameters(action, context.parameters);
        param_result.is_err()) {
        log_audit_event(audit_event_for_error(param_result.error()), context, param_result.error());
        return param_result.error();
    }

    // 3. Check permissions
    if (auto perm_result = check_permissions(action, context.security_context);
        perm_result.is_err()) {
        log_audit_event(audit_event_for_error(perm_result.error()), context, perm_result.error(),
                        action.required_permissions());
        return perm_result.error();
    }

    // 4. Check rate limit
    if (auto rate_result = check_rate_limit(action); rate_result.is_err()) {
        log_audit_event(security::AuditEventType::SecurityViolation, context, rate_result.error());
        return rate_result.error();
    }

    // 5. Invoke handler (placeholder - bytecode execution is future work)
    const auto handler_start = ActionContext::Clock::now();
    ActionResult result = ActionResult::ok(Value::nil());
    const auto handler_end = ActionContext::Clock::now();

    result.duration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(handler_end - handler_start);
    result.instructions_executed = 0;

    // 6. Log success
    log_audit_event(security::AuditEventType::DotCompleted, context);
    return result;
}

ActionExecutor::Result<void>
ActionExecutor::validate_parameters(const ActionDef& action,
                                    ActionContext::ParamMap& params) noexcept {
    // Check for unknown parameters
    for (const auto& [name, _] : params) {
        if (!action.has_parameter(name)) {
            return ActionError::UnknownParameter;
        }
    }

    // Check each defined parameter
    for (const auto& param_name : action.parameter_names()) {
        const ParamDef* def = action.get_parameter(param_name);
        if (def == nullptr)
            continue;

        auto it = params.find(def->name);
        if (it == params.end()) {
            // Parameter not provided
            if (def->required && !def->default_value.has_value()) {
                return ActionError::RequiredParameterMissing;
            }
            // Apply default if available
            if (def->default_value.has_value()) {
                params[def->name] = *def->default_value;
            }
            continue;
        }

        // Parameter provided - validate
        const Value& value = it->second;
        if (def->required && value.is_nil()) {
            return ActionError::RequiredParameterMissing;
        }
        if (!schema::is_compatible(value, def->type)) {
            return ActionError::ParameterTypeMismatch;
        }
        // Run validators
        if (!def->validators.empty()) {
            if (schema::validate_value(value, def->validators).is_err()) {
                return ActionError::InvalidParameter;
            }
        }
    }
    return {};
}

ActionExecutor::Result<void>
ActionExecutor::check_permissions(const ActionDef& action,
                                  security::SecurityContext* security) noexcept {
    const auto required = action.required_permissions();
    if (required == security::Permission::None) {
        return {};
    }
    if (security == nullptr) {
        return ActionError::ContextInvalid;
    }
    if (!security->can(required)) {
        return ActionError::PermissionDenied;
    }
    return {};
}

ActionExecutor::Result<void> ActionExecutor::check_rate_limit(const ActionDef& action) noexcept {
    if (!rate_limiter_.try_acquire(action.name(), action.max_calls_per_minute())) {
        return ActionError::RateLimitExceeded;
    }
    return {};
}

void ActionExecutor::log_audit_event(security::AuditEventType type, const ActionContext& context,
                                     std::optional<ActionError> error,
                                     security::Permission permission) noexcept {
    auto* logger = context.audit_logger;
    if (logger == nullptr || !logger->is_enabled()) {
        return;
    }
    const std::uint64_t value = error.has_value() ? static_cast<std::uint64_t>(*error) : 0;
    logger->log(security::AuditEvent::now(type, permission, value, context.action_name));
}

security::AuditEventType ActionExecutor::audit_event_for_error(ActionError error) noexcept {
    switch (error) {
        case ActionError::PermissionDenied:
            return security::AuditEventType::PermissionDenied;
        case ActionError::RateLimitExceeded:
            return security::AuditEventType::SecurityViolation;
        case ActionError::ContextInvalid:
            return security::AuditEventType::DotFailed;  // Context issues are general failures
        default:
            return security::AuditEventType::DotFailed;
    }
}

}  // namespace dotvm::core::action
