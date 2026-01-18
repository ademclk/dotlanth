#pragma once

/// @file evaluation_context.hpp
/// @brief SEC-009 Evaluation context for policy decisions
///
/// Contains all information needed to evaluate a policy rule.

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

namespace dotvm::core {
namespace security {
class SecurityContext;  // Forward declaration
}  // namespace security
}  // namespace dotvm::core

namespace dotvm::core::policy {

/// @brief Context for policy evaluation
///
/// Contains all the information needed to evaluate policy rules.
/// Populated by SecurityContext before calling PolicyEngine::evaluate().
struct EvaluationContext {
    // ========== Identity ==========

    /// Dot ID of the executing entity
    std::uint64_t dot_id{0};

    /// Capability ID being used
    std::uint64_t capability_id{0};

    // ========== Capabilities ==========

    /// Set of capability names the context has
    std::unordered_set<std::string> capabilities;

    // ========== Operation Context ==========

    /// State key for state operations (empty for non-state ops)
    std::string_view state_key;

    /// Memory address for memory operations
    std::uint64_t memory_address{0};

    /// Size of the operation (bytes for memory, etc.)
    std::uint64_t operation_size{0};

    // ========== Call Chain ==========

    /// Current call stack depth
    std::uint32_t call_depth{0};

    /// Root caller Dot ID (original entry point)
    std::uint64_t root_caller_dot{0};

    // ========== Resource Usage ==========

    /// Current memory usage in bytes
    std::uint64_t memory_used{0};

    /// Memory limit in bytes
    std::uint64_t memory_limit{0};

    /// Instructions executed
    std::uint64_t instructions_executed{0};

    // ========== Time Context ==========

    /// Current timestamp
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};

    // ========== Helper Methods ==========

    /// Check if context has a named capability
    [[nodiscard]] bool has_capability(std::string_view name) const {
        return capabilities.find(std::string{name}) != capabilities.end();
    }

    /// Get memory usage as percentage (0-100)
    [[nodiscard]] std::uint8_t memory_percent() const noexcept {
        if (memory_limit == 0) return 0;
        return static_cast<std::uint8_t>((memory_used * 100) / memory_limit);
    }

    /// Get current time as minutes since midnight UTC
    [[nodiscard]] std::uint16_t current_time_minutes() const {
        auto time_t = std::chrono::system_clock::to_time_t(timestamp);
        std::tm* tm = std::gmtime(&time_t);
        if (!tm) return 0;
        return static_cast<std::uint16_t>(tm->tm_hour * 60 + tm->tm_min);
    }

    // ========== Factory Methods ==========

    /// Create a minimal context with just dot_id
    [[nodiscard]] static EvaluationContext for_dot(std::uint64_t dot_id) {
        EvaluationContext ctx;
        ctx.dot_id = dot_id;
        return ctx;
    }
};

}  // namespace dotvm::core::policy
