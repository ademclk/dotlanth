/// @file decision_tree.cpp
/// @brief SEC-009 Decision tree implementation

#include "dotvm/core/policy/decision_tree.hpp"

#include <algorithm>

#include "dotvm/core/policy/evaluation_context.hpp"

namespace dotvm::core::policy {

// ============================================================================
// Compilation
// ============================================================================

void DecisionTree::compile(const std::vector<Rule>& rules, Decision default_action) {
    default_action_ = default_action;
    rule_count_ = rules.size();

    // Reset state
    for (auto& node : opcode_nodes_) {
        node = OpcodeNode{};
    }
    global_rules_.clear();

    // Process each rule
    for (const auto& rule : rules) {
        CompiledRule compiled;
        compiled.id = rule.id;
        compiled.priority = rule.priority;
        compiled.conditions = rule.conditions;
        compiled.action = rule.action;

        // Find opcode and key_prefix conditions
        std::optional<std::uint8_t> opcode;
        std::optional<std::string> key_prefix;

        for (const auto& condition : rule.conditions) {
            if (auto* oc = std::get_if<OpcodeCondition>(&condition)) {
                opcode = oc->opcode;
            } else if (auto* kp = std::get_if<KeyPrefixCondition>(&condition)) {
                key_prefix = kp->prefix;
            }
        }

        if (opcode.has_value()) {
            auto& node = opcode_nodes_[*opcode];

            if (key_prefix.has_value() && !key_prefix->empty()) {
                // Insert into prefix trie
                PrefixTrieNode* current = node.prefix_trie.get();
                for (char c : *key_prefix) {
                    current = &current->get_or_create_child(c);
                }
                current->insert_rule(std::move(compiled));
            } else {
                // No key prefix - add to fallback
                node.fallback_rules.push_back(std::move(compiled));
                std::sort(node.fallback_rules.begin(), node.fallback_rules.end());
            }
        } else {
            // No opcode condition - global rule
            global_rules_.push_back(std::move(compiled));
            std::sort(global_rules_.begin(), global_rules_.end());
        }
    }
}

// ============================================================================
// Evaluation
// ============================================================================

PolicyDecision DecisionTree::evaluate(const EvaluationContext& ctx, std::uint8_t opcode,
                                      std::string_view state_key) const {
    const CompiledRule* best_match = nullptr;
    std::int32_t best_priority = std::numeric_limits<std::int32_t>::min();

    // Check opcode-specific rules
    const auto& node = opcode_nodes_[opcode];

    // Check prefix trie if we have a state key
    if (!state_key.empty() && node.prefix_trie) {
        const CompiledRule* trie_match =
            find_best_match_in_trie(node.prefix_trie.get(), state_key, ctx, opcode);
        if (trie_match && trie_match->priority > best_priority) {
            best_match = trie_match;
            best_priority = trie_match->priority;
        }
    }

    // Check fallback rules for this opcode
    const CompiledRule* fallback_match =
        find_best_match_in_rules(node.fallback_rules, ctx, opcode, state_key);
    if (fallback_match && fallback_match->priority > best_priority) {
        best_match = fallback_match;
        best_priority = fallback_match->priority;
    }

    // Check global rules (no opcode condition)
    const CompiledRule* global_match =
        find_best_match_in_rules(global_rules_, ctx, opcode, state_key);
    if (global_match && global_match->priority > best_priority) {
        best_match = global_match;
    }

    // Return matched rule decision or default
    if (best_match) {
        return make_decision(*best_match);
    }

    // Check opcode-specific default
    if (node.default_action.has_value()) {
        PolicyDecision d;
        d.decision = *node.default_action;
        return d;
    }

    // Global default
    PolicyDecision d;
    d.decision = default_action_;
    return d;
}

// ============================================================================
// Condition Matching
// ============================================================================

bool DecisionTree::matches_all_conditions(const CompiledRule& rule, const EvaluationContext& ctx,
                                          std::uint8_t opcode, std::string_view state_key) const {
    for (const auto& condition : rule.conditions) {
        if (!matches_condition(condition, ctx, opcode, state_key)) {
            return false;
        }
    }
    return true;
}

bool DecisionTree::matches_condition(const Condition& condition, const EvaluationContext& ctx,
                                     std::uint8_t opcode, std::string_view state_key) const {
    return std::visit(
        [&](const auto& cond) -> bool {
            using T = std::decay_t<decltype(cond)>;

            if constexpr (std::is_same_v<T, OpcodeCondition>) {
                return cond.opcode == opcode;
            } else if constexpr (std::is_same_v<T, KeyPrefixCondition>) {
                return state_key.starts_with(cond.prefix);
            } else if constexpr (std::is_same_v<T, DotIdCondition>) {
                return ctx.dot_id == cond.dot_id;
            } else if constexpr (std::is_same_v<T, CapabilityCondition>) {
                return ctx.has_capability(cond.capability_name);
            } else if constexpr (std::is_same_v<T, MemoryRegionCondition>) {
                return ctx.memory_address >= cond.start && ctx.memory_address < cond.end;
            } else if constexpr (std::is_same_v<T, TimeWindowCondition>) {
                std::uint16_t current = ctx.current_time_minutes();
                if (cond.after_minutes <= cond.before_minutes) {
                    // Normal range (e.g., 09:00 - 17:00)
                    return current >= cond.after_minutes && current <= cond.before_minutes;
                } else {
                    // Overnight range (e.g., 22:00 - 06:00)
                    return current >= cond.after_minutes || current <= cond.before_minutes;
                }
            } else if constexpr (std::is_same_v<T, CallerChainCondition>) {
                if (cond.min_depth > 0 && ctx.call_depth < cond.min_depth) {
                    return false;
                }
                if (cond.max_depth > 0 && ctx.call_depth > cond.max_depth) {
                    return false;
                }
                if (cond.root_dot != 0 && ctx.root_caller_dot != cond.root_dot) {
                    return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, ResourceUsageCondition>) {
                if (cond.memory_above_percent > 0 &&
                    ctx.memory_percent() < cond.memory_above_percent) {
                    return false;
                }
                if (cond.instructions_above > 0 &&
                    ctx.instructions_executed < cond.instructions_above) {
                    return false;
                }
                return true;
            } else {
                return false;  // Unknown condition type
            }
        },
        condition);
}

// ============================================================================
// Trie Traversal
// ============================================================================

const CompiledRule* DecisionTree::find_best_match_in_trie(const PrefixTrieNode* node,
                                                          std::string_view key,
                                                          const EvaluationContext& ctx,
                                                          std::uint8_t opcode) const {
    if (!node) return nullptr;

    const CompiledRule* best_match = nullptr;
    std::int32_t best_priority = std::numeric_limits<std::int32_t>::min();

    // Check rules at this node (empty prefix matches all keys)
    for (const auto& rule : node->rules) {
        if (rule.priority > best_priority && matches_all_conditions(rule, ctx, opcode, key)) {
            best_match = &rule;
            best_priority = rule.priority;
        }
    }

    // Traverse down the trie following the key
    // Use >= to prefer longer (more specific) prefixes when priorities are equal
    const PrefixTrieNode* current = node;
    for (char c : key) {
        current = current->get_child(c);
        if (!current) break;

        // Check rules at each node along the path
        for (const auto& rule : current->rules) {
            if (rule.priority >= best_priority && matches_all_conditions(rule, ctx, opcode, key)) {
                best_match = &rule;
                best_priority = rule.priority;
            }
        }
    }

    return best_match;
}

const CompiledRule* DecisionTree::find_best_match_in_rules(const std::vector<CompiledRule>& rules,
                                                           const EvaluationContext& ctx,
                                                           std::uint8_t opcode,
                                                           std::string_view state_key) const {
    // Rules are sorted by priority (highest first), so first match wins
    for (const auto& rule : rules) {
        if (matches_all_conditions(rule, ctx, opcode, state_key)) {
            return &rule;
        }
    }
    return nullptr;
}

// ============================================================================
// Decision Creation
// ============================================================================

PolicyDecision DecisionTree::make_decision(const CompiledRule& rule) const {
    PolicyDecision d;
    d.decision = rule.action.decision;
    d.matched_rule_id = rule.id;

    switch (rule.action.decision) {
        case Decision::Allow:
            break;
        case Decision::Audit:
            d.audit_reason = rule.action.reason;
            break;
        case Decision::RequireCapability:
            d.required_capability = rule.action.capability;
            break;
        case Decision::Deny:
            d.audit_reason = rule.action.reason;
            break;
    }

    return d;
}

}  // namespace dotvm::core::policy
