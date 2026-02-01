#pragma once

/// @file action_context.hpp
/// @brief DEP-005 Action execution context and builder
///
/// Defines ActionContext used to pass caller identity, parameters, and
/// execution dependencies into action handlers.

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "dotvm/core/security/audit_event.hpp"
#include "dotvm/core/value.hpp"

namespace dotvm::core {
class VmContext;
}  // namespace dotvm::core

namespace dotvm::core::security {
class SecurityContext;
class AuditLogger;
}  // namespace dotvm::core::security

namespace dotvm::core::action {

using DotId = std::uint64_t;

// ============================================================================
// ActionContext
// ============================================================================

/// @brief Execution context for a single action invocation
struct ActionContext {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using ParamMap = std::unordered_map<std::string, Value>;

    DotId caller_id{0};
    std::string action_name{};
    ParamMap parameters{};
    VmContext* vm_context{nullptr};
    security::SecurityContext* security_context{nullptr};
    security::AuditLogger* audit_logger{nullptr};
    std::uint32_t timeout_ms{0};  // 0 = no timeout
    TimePoint start_time{};

    [[nodiscard]] bool is_timed_out(TimePoint now = Clock::now()) const noexcept;
};

// ============================================================================
// ActionContextBuilder
// ============================================================================

class ActionContextBuilder {
public:
    ActionContextBuilder() noexcept = default;

    ActionContextBuilder& with_caller(DotId caller_id) noexcept;
    ActionContextBuilder& with_action_name(std::string action_name) noexcept;
    ActionContextBuilder& with_parameter(std::string name, Value value) noexcept;
    ActionContextBuilder& with_parameters(ActionContext::ParamMap parameters) noexcept;
    ActionContextBuilder& with_vm_context(VmContext* context) noexcept;
    ActionContextBuilder& with_security_context(security::SecurityContext* context) noexcept;
    ActionContextBuilder& with_audit_logger(security::AuditLogger* logger) noexcept;
    ActionContextBuilder& with_timeout_ms(std::uint32_t timeout_ms) noexcept;

    [[nodiscard]] ActionContext build() && noexcept;
    [[nodiscard]] ActionContext build() const& noexcept;

private:
    ActionContext ctx_;
};

}  // namespace dotvm::core::action
