#pragma once

/// @file policy_engine.hpp
/// @brief SEC-009 Policy enforcement engine
///
/// Provides the main policy engine interface with:
/// - JSON policy loading and parsing
/// - Hot-reload via explicit reload_policy() API
/// - Thread-safe evaluation using RCU with atomic shared_ptr

#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "dotvm/core/policy/decision_tree.hpp"
#include "dotvm/core/policy/evaluation_context.hpp"
#include "dotvm/core/policy/json_parser.hpp"
#include "dotvm/core/policy/policy_error.hpp"
#include "dotvm/core/policy/policy_rule.hpp"
#include "dotvm/core/result.hpp"

namespace dotvm::core::policy {

/// @brief Policy enforcement engine
///
/// Thread Safety:
/// - evaluate() is lock-free and can be called concurrently from multiple threads
/// - load_policy(), load_policy_file(), and reload_policy() are serialized internally
/// - Hot-reload is atomic: readers see either old or new policy, never a torn state
///
/// @par Usage Example
/// @code
/// PolicyEngine engine;
///
/// // Load policy from JSON
/// auto result = engine.load_policy(R"({
///     "rules": [
///         {
///             "id": 1,
///             "priority": 100,
///             "if": { "opcode": "STATE_PUT", "key_prefix": "/admin" },
///             "then": { "action": "RequireCapability", "capability": "Admin" }
///         }
///     ],
///     "default_action": "Allow"
/// })");
///
/// if (!result) {
///     std::cerr << "Policy error: " << result.error().message << "\n";
/// }
///
/// // Evaluate
/// EvaluationContext ctx = EvaluationContext::for_dot(42);
/// PolicyDecision decision = engine.evaluate(ctx, 0x70, "/admin/users");
/// @endcode
class PolicyEngine {
public:
    PolicyEngine() = default;
    ~PolicyEngine() = default;

    // Non-copyable, non-movable (contains std::shared_mutex)
    PolicyEngine(const PolicyEngine&) = delete;
    PolicyEngine& operator=(const PolicyEngine&) = delete;
    PolicyEngine(PolicyEngine&&) = delete;
    PolicyEngine& operator=(PolicyEngine&&) = delete;

    // ========== Policy Loading ==========

    /// @brief Load policy from JSON string
    ///
    /// Parses the JSON, validates the schema, compiles the decision tree,
    /// and atomically swaps it into the active tree.
    ///
    /// @param json_string JSON policy definition
    /// @return Success or error with details
    [[nodiscard]] Result<void, PolicyErrorInfo> load_policy(std::string_view json_string);

    /// @brief Load policy from file
    ///
    /// Reads the file, stores the path for reload, and calls load_policy().
    ///
    /// @param path Path to JSON policy file
    /// @return Success or error with details
    [[nodiscard]] Result<void, PolicyErrorInfo> load_policy_file(std::string_view path);

    /// @brief Reload policy from previously loaded source
    ///
    /// If loaded from file, re-reads the file. If loaded from string,
    /// re-parses the stored string.
    ///
    /// @return Success or error with details
    [[nodiscard]] Result<void, PolicyErrorInfo> reload_policy();

    // ========== Policy Evaluation ==========

    /// @brief Evaluate context against loaded policy
    ///
    /// Thread-safe, lock-free evaluation.
    ///
    /// @param ctx Evaluation context
    /// @param opcode Operation opcode
    /// @param state_key State key (for state operations)
    /// @return Policy decision
    [[nodiscard]] PolicyDecision evaluate(const EvaluationContext& ctx, std::uint8_t opcode,
                                          std::string_view state_key = "") const;

    // ========== Status Queries ==========

    /// @brief Check if a policy is loaded
    [[nodiscard]] bool has_policy() const noexcept;

    /// @brief Get number of rules in loaded policy
    [[nodiscard]] std::size_t rule_count() const noexcept;

    /// @brief Get policy name (if set)
    [[nodiscard]] std::optional<std::string> policy_name() const noexcept;

    /// @brief Get policy version (if set)
    [[nodiscard]] std::optional<std::string> policy_version() const noexcept;

private:
    /// @brief Internal state shared between readers and writers
    struct PolicyState {
        DecisionTree tree;
        std::string name;
        std::string version;
    };

    /// @brief Parse JSON into Policy structure
    [[nodiscard]] Result<Policy, PolicyErrorInfo> parse_policy(const JsonValue& root);

    /// @brief Parse a single rule from JSON
    [[nodiscard]] Result<Rule, PolicyErrorInfo> parse_rule(const JsonValue& rule_json);

    /// @brief Parse conditions from JSON "if" object
    [[nodiscard]] Result<std::vector<Condition>, PolicyErrorInfo>
    parse_conditions(const JsonValue& if_json);

    /// @brief Parse action from JSON "then" object
    [[nodiscard]] Result<RuleAction, PolicyErrorInfo> parse_action(const JsonValue& then_json);

    /// @brief Parse opcode name to value
    [[nodiscard]] std::optional<std::uint8_t> parse_opcode_name(std::string_view name);

    /// @brief Parse time string "HH:MM" to minutes
    [[nodiscard]] std::optional<std::uint16_t> parse_time(std::string_view time_str);

    /// @brief Parse hex string "0xNNN" to uint64
    [[nodiscard]] std::optional<std::uint64_t> parse_hex(std::string_view hex_str);

    // Reader-writer lock protected active state
    // Readers (evaluate) use shared_lock, writers (load/reload) use unique_lock
    std::shared_ptr<PolicyState> active_state_{nullptr};
    mutable std::shared_mutex state_mutex_;

    // Source for reload
    std::optional<std::string> source_path_;
    std::optional<std::string> source_json_;
};

}  // namespace dotvm::core::policy
