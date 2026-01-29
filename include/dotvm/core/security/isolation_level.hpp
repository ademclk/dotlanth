#pragma once

/// @file isolation_level.hpp
/// @brief SEC-007 Isolation Level definitions for DotVM multi-Dot execution
///
/// This header defines the IsolationLevel enum and helper predicates for
/// determining isolation requirements based on the configured level.
/// Isolation levels control memory boundaries, syscall restrictions, and
/// capability limitations for Dot execution.

#include <cstdint>
#include <format>
#include <string_view>

namespace dotvm::core::security {

// ============================================================================
// IsolationLevel Enum
// ============================================================================

/// @brief Isolation levels for Dot execution sandboxing
///
/// Isolation levels are ordered from least restrictive to most restrictive:
/// - None: Shared memory allowed, no isolation boundaries
/// - Basic: Per-Dot HandleTable, explicit grants required for cross-Dot access
/// - Strict: Basic + syscall whitelist enforcement + no Network/Filesystem access
///
/// Higher isolation levels inherit all restrictions from lower levels.
enum class IsolationLevel : std::uint8_t {
    /// No isolation - shared memory allowed between Dots
    ///
    /// Use for trusted code or single-Dot execution where
    /// memory sharing is acceptable.
    None = 0,

    /// Basic isolation - per-Dot HandleTable with explicit grants
    ///
    /// Each Dot gets its own HandleTable namespace. Cross-Dot memory
    /// access requires explicit handle grants from parent to child.
    /// Use for semi-trusted code that needs resource isolation.
    Basic = 1,

    /// Strict isolation - Basic + syscall whitelist + capability restrictions
    ///
    /// Includes all Basic restrictions plus:
    /// - Syscall whitelist enforcement (only safe syscalls allowed)
    /// - Network capability denied
    /// - Filesystem capability denied
    /// Use for untrusted code execution.
    Strict = 2
};

// ============================================================================
// IsolationLevel Helper Predicates
// ============================================================================

/// @brief Check if isolation level requires per-Dot memory isolation
///
/// @param level The isolation level to check
/// @return true if Basic or Strict level (requires separate HandleTable per Dot)
[[nodiscard]] constexpr bool requires_memory_isolation(IsolationLevel level) noexcept {
    return level >= IsolationLevel::Basic;
}

/// @brief Check if isolation level requires syscall whitelist enforcement
///
/// @param level The isolation level to check
/// @return true if Strict level (requires syscall validation)
[[nodiscard]] constexpr bool requires_syscall_whitelist(IsolationLevel level) noexcept {
    return level >= IsolationLevel::Strict;
}

/// @brief Check if isolation level restricts network access
///
/// @param level The isolation level to check
/// @return true if Strict level (network capability denied)
[[nodiscard]] constexpr bool restricts_network(IsolationLevel level) noexcept {
    return level >= IsolationLevel::Strict;
}

/// @brief Check if isolation level restricts filesystem access
///
/// @param level The isolation level to check
/// @return true if Strict level (filesystem capability denied)
[[nodiscard]] constexpr bool restricts_filesystem(IsolationLevel level) noexcept {
    return level >= IsolationLevel::Strict;
}

/// @brief Check if cross-Dot access requires explicit grants
///
/// @param level The isolation level to check
/// @return true if Basic or Strict level (grants required for cross-Dot memory access)
[[nodiscard]] constexpr bool requires_explicit_grants(IsolationLevel level) noexcept {
    return level >= IsolationLevel::Basic;
}

// ============================================================================
// IsolationLevel String Conversion
// ============================================================================

/// @brief Convert IsolationLevel to human-readable string
///
/// @param level The isolation level to convert
/// @return String representation of the isolation level
[[nodiscard]] constexpr std::string_view to_string(IsolationLevel level) noexcept {
    switch (level) {
        case IsolationLevel::None:
            return "None";
        case IsolationLevel::Basic:
            return "Basic";
        case IsolationLevel::Strict:
            return "Strict";
    }
    return "Unknown";
}

}  // namespace dotvm::core::security

// ============================================================================
// std::formatter specialization for IsolationLevel
// ============================================================================

template <>
struct std::formatter<dotvm::core::security::IsolationLevel> : std::formatter<std::string_view> {
    auto format(dotvm::core::security::IsolationLevel e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
