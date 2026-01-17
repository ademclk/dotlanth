#pragma once

/// @file arch_types.hpp
/// @brief Minimal header defining architecture types to avoid circular dependencies
///
/// This header provides the Architecture enum and related basic types that are
/// needed across multiple headers. It is intentionally minimal to avoid
/// circular include dependencies between value.hpp, bytecode.hpp, and arch_config.hpp.

#include <cstddef>
#include <cstdint>

namespace dotvm::core {

/// Target architecture for bytecode execution
///
/// This enum determines how values are masked, how arithmetic operations
/// truncate results, and the addressable memory range.
///
/// @note Arch32 uses 32-bit integers and 4GB address space
/// @note Arch64 uses 48-bit integers (NaN-boxing limit) and 48-bit address space
/// @note Arch128/256/512 are SIMD-enabled modes requiring corresponding CPU support
enum class Architecture : std::uint8_t {
    Arch32 = 0,   ///< 32-bit compatibility mode
    Arch64 = 1,   ///< 64-bit mode (default)
    Arch128 = 2,  ///< 128-bit SIMD mode (SSE/NEON)
    Arch256 = 3,  ///< 256-bit SIMD mode (AVX2)
    Arch512 = 4   ///< 512-bit SIMD mode (AVX-512)
};

/// Maximum valid architecture value
inline constexpr std::uint8_t ARCHITECTURE_MAX_VALUE =
    static_cast<std::uint8_t>(Architecture::Arch512);

/// Check if architecture value is valid
[[nodiscard]] constexpr bool is_valid_architecture(Architecture arch) noexcept {
    return static_cast<std::uint8_t>(arch) <= ARCHITECTURE_MAX_VALUE;
}

/// Check if architecture is a SIMD-enabled mode
[[nodiscard]] constexpr bool is_simd_architecture(Architecture arch) noexcept {
    return arch == Architecture::Arch128 || arch == Architecture::Arch256 ||
           arch == Architecture::Arch512;
}

/// Check if architecture is a scalar (non-SIMD) mode
[[nodiscard]] constexpr bool is_scalar_architecture(Architecture arch) noexcept {
    return arch == Architecture::Arch32 || arch == Architecture::Arch64;
}

/// Get the bit width for an architecture
[[nodiscard]] constexpr std::size_t arch_bit_width(Architecture arch) noexcept {
    switch (arch) {
        case Architecture::Arch32:
            return 32;
        case Architecture::Arch64:
            return 64;
        case Architecture::Arch128:
            return 128;
        case Architecture::Arch256:
            return 256;
        case Architecture::Arch512:
            return 512;
    }
    return 64;  // Default fallback
}

}  // namespace dotvm::core
