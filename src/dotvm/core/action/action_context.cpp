#include "dotvm/core/action/action_context.hpp"

#include <utility>

namespace dotvm::core::action {

bool ActionContext::is_timed_out(TimePoint now) const noexcept {
    if (timeout_ms == 0 || start_time == TimePoint{}) {
        return false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
    return elapsed.count() >= timeout_ms;
}

ActionContextBuilder& ActionContextBuilder::with_caller(DotId caller_id) noexcept {
    ctx_.caller_id = caller_id;
    return *this;
}

ActionContextBuilder& ActionContextBuilder::with_action_name(std::string action_name) noexcept {
    ctx_.action_name = std::move(action_name);
    return *this;
}

ActionContextBuilder& ActionContextBuilder::with_parameter(std::string name, Value value) noexcept {
    ctx_.parameters[std::move(name)] = value;
    return *this;
}

ActionContextBuilder&
ActionContextBuilder::with_parameters(ActionContext::ParamMap parameters) noexcept {
    ctx_.parameters = std::move(parameters);
    return *this;
}

ActionContextBuilder& ActionContextBuilder::with_vm_context(VmContext* context) noexcept {
    ctx_.vm_context = context;
    return *this;
}

ActionContextBuilder&
ActionContextBuilder::with_security_context(security::SecurityContext* context) noexcept {
    ctx_.security_context = context;
    return *this;
}

ActionContextBuilder&
ActionContextBuilder::with_audit_logger(security::AuditLogger* logger) noexcept {
    ctx_.audit_logger = logger;
    return *this;
}

ActionContextBuilder& ActionContextBuilder::with_timeout_ms(std::uint32_t timeout_ms) noexcept {
    ctx_.timeout_ms = timeout_ms;
    return *this;
}

ActionContext ActionContextBuilder::build() && noexcept {
    ctx_.start_time = ActionContext::Clock::now();
    return std::move(ctx_);
}

ActionContext ActionContextBuilder::build() const& noexcept {
    ActionContext context = ctx_;
    context.start_time = ActionContext::Clock::now();
    return context;
}

}  // namespace dotvm::core::action
