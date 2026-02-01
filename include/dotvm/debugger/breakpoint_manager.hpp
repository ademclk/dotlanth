#pragma once

/// @file breakpoint_manager.hpp
/// @brief TOOL-011 Debug Client - Extended breakpoint management
///
/// Provides advanced breakpoint features including conditions,
/// enable/disable, and hit counting.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dotvm::debugger {

/// @brief A breakpoint with extended features
struct Breakpoint {
    std::uint32_t id;               ///< Unique breakpoint ID
    std::size_t address;            ///< Address (PC value) where breakpoint is set
    bool enabled{true};             ///< Whether breakpoint is active
    std::string condition;          ///< Conditional expression (empty = unconditional)
    std::uint32_t hit_count{0};     ///< Number of times this breakpoint has been hit
    std::uint32_t ignore_count{0};  ///< Skip this many hits before breaking
    std::string comment;            ///< User comment/description
};

/// @brief Result of checking if we should break
struct BreakCheckResult {
    bool should_break{false};         ///< Whether execution should pause
    std::optional<std::uint32_t> id;  ///< ID of breakpoint that triggered (if any)
};

/// @brief Manages breakpoints with extended features
///
/// Extends the basic breakpoint functionality with:
/// - Unique IDs for each breakpoint
/// - Enable/disable without removing
/// - Conditional breakpoints (expression-based)
/// - Hit counting and ignore counts
class BreakpointManager {
public:
    /// @brief Construct the breakpoint manager
    BreakpointManager() = default;

    /// @brief Set a breakpoint at an address
    /// @param address The PC value to break at
    /// @return The ID of the new breakpoint
    std::uint32_t set(std::size_t address);

    /// @brief Set a breakpoint with a condition
    /// @param address The PC value to break at
    /// @param condition Expression that must be true to break
    /// @return The ID of the new breakpoint
    std::uint32_t set_conditional(std::size_t address, std::string condition);

    /// @brief Remove a breakpoint by ID
    /// @param id The breakpoint ID to remove
    /// @return true if the breakpoint existed and was removed
    bool remove(std::uint32_t id);

    /// @brief Remove all breakpoints at an address
    /// @param address The address to clear
    /// @return Number of breakpoints removed
    std::size_t remove_at_address(std::size_t address);

    /// @brief Enable a breakpoint
    /// @param id The breakpoint ID to enable
    /// @return true if the breakpoint exists
    bool enable(std::uint32_t id);

    /// @brief Disable a breakpoint
    /// @param id The breakpoint ID to disable
    /// @return true if the breakpoint exists
    bool disable(std::uint32_t id);

    /// @brief Set a condition on a breakpoint
    /// @param id The breakpoint ID
    /// @param condition The condition expression
    /// @return true if the breakpoint exists
    bool set_condition(std::uint32_t id, std::string condition);

    /// @brief Set ignore count for a breakpoint
    /// @param id The breakpoint ID
    /// @param count Number of hits to ignore
    /// @return true if the breakpoint exists
    bool set_ignore_count(std::uint32_t id, std::uint32_t count);

    /// @brief Get a breakpoint by ID
    /// @param id The breakpoint ID
    /// @return Pointer to breakpoint or nullptr if not found
    [[nodiscard]] const Breakpoint* get(std::uint32_t id) const;

    /// @brief Get all breakpoints
    [[nodiscard]] std::vector<const Breakpoint*> list() const;

    /// @brief Get all breakpoints at an address
    [[nodiscard]] std::vector<const Breakpoint*> at_address(std::size_t address) const;

    /// @brief Check if we should break at an address
    ///
    /// This method:
    /// 1. Finds enabled breakpoints at the address
    /// 2. Evaluates conditions (if any)
    /// 3. Handles ignore counts
    /// 4. Increments hit counts
    ///
    /// @param address The current PC
    /// @param condition_evaluator Function to evaluate condition expressions
    /// @return Result indicating whether to break and which breakpoint triggered
    template <typename ConditionEvaluator>
    [[nodiscard]] BreakCheckResult check(std::size_t address, ConditionEvaluator&& evaluator) {
        auto it = address_index_.find(address);
        if (it == address_index_.end()) {
            return {};
        }

        for (std::uint32_t id : it->second) {
            auto bp_it = breakpoints_.find(id);
            if (bp_it == breakpoints_.end()) {
                continue;
            }

            Breakpoint& bp = bp_it->second;
            if (!bp.enabled) {
                continue;
            }

            // Increment hit count
            bp.hit_count++;

            // Check ignore count
            if (bp.ignore_count > 0) {
                bp.ignore_count--;
                continue;
            }

            // Evaluate condition
            if (!bp.condition.empty()) {
                if (!evaluator(bp.condition)) {
                    continue;
                }
            }

            return {true, bp.id};
        }

        return {};
    }

    /// @brief Simplified check without condition evaluation
    [[nodiscard]] BreakCheckResult check_simple(std::size_t address);

    /// @brief Check if there's any enabled breakpoint at an address
    [[nodiscard]] bool has_breakpoint_at(std::size_t address) const;

    /// @brief Get all addresses with breakpoints (for ExecutionEngine integration)
    [[nodiscard]] std::vector<std::size_t> get_active_addresses() const;

    /// @brief Clear all breakpoints
    void clear();

    /// @brief Get total number of breakpoints
    [[nodiscard]] std::size_t count() const noexcept { return breakpoints_.size(); }

private:
    std::uint32_t next_id_{1};
    std::unordered_map<std::uint32_t, Breakpoint> breakpoints_;
    std::unordered_map<std::size_t, std::vector<std::uint32_t>> address_index_;

    void update_address_index(std::uint32_t id, std::size_t address);
    void remove_from_address_index(std::uint32_t id, std::size_t address);
};

}  // namespace dotvm::debugger
