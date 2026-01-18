#pragma once

/// @file decision_tree.hpp
/// @brief SEC-009 Decision tree for O(log n) policy evaluation
///
/// Implements a hybrid decision tree structure with:
/// - Fixed array of 256 entries for opcode-based dispatch
/// - Sparse prefix trie for key-based rules
/// - Priority-sorted rule sets at leaf nodes

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dotvm/core/policy/policy_decision.hpp"
#include "dotvm/core/policy/policy_rule.hpp"

namespace dotvm::core::policy {

// Forward declarations
struct EvaluationContext;

// ============================================================================
// Compiled Rule
// ============================================================================

/// @brief A rule optimized for evaluation
///
/// Contains pre-compiled condition data for fast matching.
struct CompiledRule {
    /// Original rule ID
    std::uint64_t id{0};

    /// Priority (higher = evaluated first)
    std::int32_t priority{0};

    /// The original conditions
    std::vector<Condition> conditions;

    /// Action to take when matched
    RuleAction action;

    /// Comparison for priority sorting (higher priority first)
    [[nodiscard]] bool operator<(const CompiledRule& other) const noexcept {
        return priority > other.priority;
    }
};

// ============================================================================
// Prefix Trie Node
// ============================================================================

/// @brief Sparse prefix trie node for key-based lookups
///
/// Uses unordered_map for children to minimize memory for sparse tries.
class PrefixTrieNode {
public:
    /// Child nodes indexed by character
    std::unordered_map<char, std::unique_ptr<PrefixTrieNode>> children;

    /// Rules that match at this prefix (sorted by priority)
    std::vector<CompiledRule> rules;

    /// @brief Insert a rule at this node
    void insert_rule(CompiledRule rule) {
        rules.push_back(std::move(rule));
        std::sort(rules.begin(), rules.end());
    }

    /// @brief Get or create child node
    [[nodiscard]] PrefixTrieNode& get_or_create_child(char c) {
        auto& child = children[c];
        if (!child) {
            child = std::make_unique<PrefixTrieNode>();
        }
        return *child;
    }

    /// @brief Get child node (nullptr if not found)
    [[nodiscard]] const PrefixTrieNode* get_child(char c) const {
        auto it = children.find(c);
        if (it == children.end()) return nullptr;
        return it->second.get();
    }
};

// ============================================================================
// Opcode Node
// ============================================================================

/// @brief Node for opcode-specific rules
///
/// Contains a prefix trie for key-based rules and fallback rules.
struct OpcodeNode {
    /// Prefix trie root for key-based lookups
    std::unique_ptr<PrefixTrieNode> prefix_trie;

    /// Rules without key prefix condition (sorted by priority)
    std::vector<CompiledRule> fallback_rules;

    /// Default action for this opcode (overrides global default)
    std::optional<Decision> default_action;

    OpcodeNode() : prefix_trie{std::make_unique<PrefixTrieNode>()} {}
};

// ============================================================================
// Decision Tree
// ============================================================================

/// @brief Compiled decision tree for fast policy evaluation
///
/// Structure:
/// - OpcodeNode[256]: Fixed array indexed by opcode value
/// - Each OpcodeNode contains a PrefixTrieNode root
/// - Each PrefixTrieNode contains child nodes and rules
///
/// Lookup complexity: O(k) where k = key length, independent of rule count
class DecisionTree {
public:
    DecisionTree() = default;

    // Non-copyable but movable
    DecisionTree(const DecisionTree&) = delete;
    DecisionTree& operator=(const DecisionTree&) = delete;
    DecisionTree(DecisionTree&&) noexcept = default;
    DecisionTree& operator=(DecisionTree&&) noexcept = default;

    /// @brief Compile rules into the decision tree
    ///
    /// @param rules Rules to compile
    /// @param default_action Default action when no rule matches
    void compile(const std::vector<Rule>& rules, Decision default_action);

    /// @brief Evaluate a context against the tree
    ///
    /// @param ctx Evaluation context
    /// @param opcode Operation opcode
    /// @param state_key State key (for state operations)
    /// @return Policy decision
    [[nodiscard]] PolicyDecision evaluate(const EvaluationContext& ctx, std::uint8_t opcode,
                                          std::string_view state_key = "") const;

    /// @brief Get the default action
    [[nodiscard]] Decision default_action() const noexcept { return default_action_; }

    /// @brief Get total number of compiled rules
    [[nodiscard]] std::size_t rule_count() const noexcept { return rule_count_; }

private:
    /// @brief Opcode dispatch table
    std::array<OpcodeNode, 256> opcode_nodes_;

    /// @brief Rules without opcode condition
    std::vector<CompiledRule> global_rules_;

    /// @brief Default action when no rule matches
    Decision default_action_{Decision::Allow};

    /// @brief Total number of rules
    std::size_t rule_count_{0};

    /// @brief Check if all conditions in a rule match
    [[nodiscard]] bool matches_all_conditions(const CompiledRule& rule,
                                              const EvaluationContext& ctx,
                                              std::uint8_t opcode,
                                              std::string_view state_key) const;

    /// @brief Check a single condition
    [[nodiscard]] bool matches_condition(const Condition& condition,
                                         const EvaluationContext& ctx,
                                         std::uint8_t opcode,
                                         std::string_view state_key) const;

    /// @brief Find matching rules in a trie path
    [[nodiscard]] const CompiledRule* find_best_match_in_trie(
        const PrefixTrieNode* node, std::string_view key, const EvaluationContext& ctx,
        std::uint8_t opcode) const;

    /// @brief Find best matching rule in a rule vector
    [[nodiscard]] const CompiledRule* find_best_match_in_rules(
        const std::vector<CompiledRule>& rules, const EvaluationContext& ctx, std::uint8_t opcode,
        std::string_view state_key) const;

    /// @brief Create decision from rule action
    [[nodiscard]] PolicyDecision make_decision(const CompiledRule& rule) const;
};

}  // namespace dotvm::core::policy
