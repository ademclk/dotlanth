#pragma once

/// @file policy_decision.hpp
/// @brief SEC-009 Policy decision types for the policy enforcement engine
///
/// Defines the Decision enum and PolicyDecision struct representing
/// the outcome of policy evaluation.

#include <cstdint>
#include <optional>
#include <string>

namespace dotvm::core::policy {

/// @brief Policy decision outcome
///
/// Represents the action to take based on policy evaluation.
/// Ordered by restrictiveness: Allow < Audit < RequireCapability < Deny
enum class Decision : std::uint8_t {
    /// Operation is permitted
    Allow = 0,

    /// Operation is permitted but should be logged
    Audit = 1,

    /// Operation requires a specific capability to proceed
    RequireCapability = 2,

    /// Operation is blocked
    Deny = 3,
};

/// @brief Convert Decision to human-readable string
[[nodiscard]] constexpr const char* to_string(Decision decision) noexcept {
    switch (decision) {
        case Decision::Allow:
            return "Allow";
        case Decision::Audit:
            return "Audit";
        case Decision::RequireCapability:
            return "RequireCapability";
        case Decision::Deny:
            return "Deny";
    }
    return "Unknown";
}

/// @brief Complete policy decision with metadata
///
/// Contains the decision outcome along with additional information
/// for logging, debugging, and capability enforcement.
struct PolicyDecision {
    /// The decision outcome
    Decision decision{Decision::Allow};

    /// Required capability name (only for RequireCapability decision)
    std::string required_capability;

    /// Reason for audit logging (only for Audit or Deny decisions)
    std::string audit_reason;

    /// ID of the rule that matched (0 if default action)
    std::uint64_t matched_rule_id{0};

    // ========== Factory Methods ==========

    /// Create an Allow decision
    [[nodiscard]] static PolicyDecision allow() noexcept {
        PolicyDecision d;
        d.decision = Decision::Allow;
        return d;
    }

    /// Create an Audit decision
    [[nodiscard]] static PolicyDecision audit(std::string reason = "",
                                               std::uint64_t rule_id = 0) noexcept {
        PolicyDecision d;
        d.decision = Decision::Audit;
        d.audit_reason = std::move(reason);
        d.matched_rule_id = rule_id;
        return d;
    }

    /// Create a RequireCapability decision
    [[nodiscard]] static PolicyDecision require_capability(std::string capability,
                                                            std::uint64_t rule_id = 0) noexcept {
        PolicyDecision d;
        d.decision = Decision::RequireCapability;
        d.required_capability = std::move(capability);
        d.matched_rule_id = rule_id;
        return d;
    }

    /// Create a Deny decision
    [[nodiscard]] static PolicyDecision deny(std::string reason = "",
                                              std::uint64_t rule_id = 0) noexcept {
        PolicyDecision d;
        d.decision = Decision::Deny;
        d.audit_reason = std::move(reason);
        d.matched_rule_id = rule_id;
        return d;
    }

    // ========== Query Methods ==========

    /// Check if the decision allows the operation
    [[nodiscard]] bool is_allowed() const noexcept {
        return decision == Decision::Allow || decision == Decision::Audit;
    }

    /// Check if the decision denies the operation
    [[nodiscard]] bool is_denied() const noexcept { return decision == Decision::Deny; }

    /// Check if the decision requires a capability check
    [[nodiscard]] bool requires_capability() const noexcept {
        return decision == Decision::RequireCapability;
    }

    /// Check if the decision should be audited
    [[nodiscard]] bool should_audit() const noexcept {
        return decision == Decision::Audit || decision == Decision::Deny;
    }
};

}  // namespace dotvm::core::policy
