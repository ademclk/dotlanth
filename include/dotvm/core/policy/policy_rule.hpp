#pragma once

/// @file policy_rule.hpp
/// @brief SEC-009 Policy rule types for the policy enforcement engine
///
/// Defines the condition types and Rule struct for policy definitions.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "dotvm/core/policy/policy_decision.hpp"

namespace dotvm::core::policy {

// ============================================================================
// Condition Types
// ============================================================================

/// @brief Match a specific opcode by name or value
struct OpcodeCondition {
    /// Opcode value (0-255)
    std::uint8_t opcode{0};

    /// Human-readable opcode name (for debugging)
    std::string opcode_name;
};

/// @brief Match state keys by prefix
struct KeyPrefixCondition {
    /// Key prefix to match (e.g., "/admin")
    std::string prefix;
};

/// @brief Match a specific Dot ID
struct DotIdCondition {
    /// Dot ID to match
    std::uint64_t dot_id{0};
};

/// @brief Check if context has a named capability
struct CapabilityCondition {
    /// Capability name to check
    std::string capability_name;
};

/// @brief Match memory address within a region
struct MemoryRegionCondition {
    /// Start address (inclusive)
    std::uint64_t start{0};

    /// End address (exclusive)
    std::uint64_t end{0};
};

/// @brief Match operations within a time window (UTC)
struct TimeWindowCondition {
    /// Minutes after midnight (0-1439)
    std::uint16_t after_minutes{0};

    /// Minutes after midnight (0-1439)
    std::uint16_t before_minutes{1439};
};

/// @brief Match call chain depth and root caller
struct CallerChainCondition {
    /// Minimum call depth (0 = no minimum)
    std::uint32_t min_depth{0};

    /// Maximum call depth (0 = no maximum)
    std::uint32_t max_depth{0};

    /// Root caller Dot ID (0 = any)
    std::uint64_t root_dot{0};
};

/// @brief Match resource usage thresholds
struct ResourceUsageCondition {
    /// Memory usage percentage threshold (0 = no check)
    std::uint8_t memory_above_percent{0};

    /// Instruction count threshold (0 = no check)
    std::uint64_t instructions_above{0};
};

/// @brief Variant type for all condition types
using Condition = std::variant<OpcodeCondition, KeyPrefixCondition, DotIdCondition,
                               CapabilityCondition, MemoryRegionCondition, TimeWindowCondition,
                               CallerChainCondition, ResourceUsageCondition>;

// ============================================================================
// Action Types
// ============================================================================

/// @brief Action to take when a rule matches
struct RuleAction {
    /// Decision type
    Decision decision{Decision::Allow};

    /// Required capability (for RequireCapability action)
    std::string capability;

    /// Audit reason (for Audit/Deny actions)
    std::string reason;
};

// ============================================================================
// Rule Definition
// ============================================================================

/// @brief A policy rule definition
///
/// Rules are evaluated in priority order (higher priority first).
/// All conditions must match for the rule to trigger.
struct Rule {
    /// Unique rule identifier
    std::uint64_t id{0};

    /// Priority (higher = evaluated first)
    std::int32_t priority{0};

    /// Conditions that must all match
    std::vector<Condition> conditions;

    /// Action to take when all conditions match
    RuleAction action;

    /// Equality comparison by ID
    [[nodiscard]] bool operator==(const Rule& other) const noexcept { return id == other.id; }

    /// Comparison by priority (for sorting, higher priority first)
    [[nodiscard]] bool operator<(const Rule& other) const noexcept {
        return priority > other.priority;  // Higher priority sorts first
    }
};

// ============================================================================
// Policy Definition
// ============================================================================

/// @brief A complete policy definition
struct Policy {
    /// Rules in the policy
    std::vector<Rule> rules;

    /// Default action when no rule matches
    Decision default_action{Decision::Allow};

    /// Policy name (optional, for debugging)
    std::string name;

    /// Policy version (optional)
    std::string version;
};

}  // namespace dotvm::core::policy
