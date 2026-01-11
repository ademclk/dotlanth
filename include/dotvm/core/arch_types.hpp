#pragma once

/// @file arch_types.hpp
/// @brief Minimal header defining architecture types to avoid circular dependencies
///
/// This header provides the Architecture enum and related basic types that are
/// needed across multiple headers. It is intentionally minimal to avoid
/// circular include dependencies between value.hpp, bytecode.hpp, and arch_config.hpp.

#include <cstdint>

namespace dotvm::core {

/// Target architecture for bytecode execution
///
/// This enum determines how values are masked, how arithmetic operations
/// truncate results, and the addressable memory range.
///
/// @note Arch32 uses 32-bit integers and 4GB address space
/// @note Arch64 uses 48-bit integers (NaN-boxing limit) and 48-bit address space
enum class Architecture : std::uint8_t {
    Arch32 = 0,  ///< 32-bit compatibility mode
    Arch64 = 1   ///< 64-bit mode (default)
};

/// Check if architecture value is valid
[[nodiscard]] constexpr bool is_valid_architecture(Architecture arch) noexcept {
    return arch == Architecture::Arch32 || arch == Architecture::Arch64;
}

}  // namespace dotvm::core
