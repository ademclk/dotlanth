#pragma once

/// @file arch_config.hpp
/// @brief Architecture-specific configuration and masking operations for 32-bit compatibility mode
///
/// This header provides constants and functions for handling architecture-specific
/// value masking and address computation. In 32-bit mode (Arch32), integer values
/// are masked to 32 bits with proper sign extension, and memory addresses are
/// limited to the 32-bit address space (4GB).

#include <cstdint>
#include <limits>

#include "arch_types.hpp"  // for Architecture enum

namespace dotvm::core {

/// Architecture-specific configuration and masking operations
namespace arch_config {

// ============================================================================
// 32-bit Architecture Constants
// ============================================================================

/// Mask for extracting lower 32 bits from a 64-bit value
inline constexpr std::uint64_t UINT32_MASK = 0x0000'0000'FFFF'FFFFULL;

/// Mask for signed 32-bit integer extraction
inline constexpr std::int64_t INT32_MASK = 0x0000'0000'FFFF'FFFFLL;

/// Sign bit position for 32-bit integers
inline constexpr std::int64_t INT32_SIGN_BIT = 0x0000'0000'8000'0000LL;

/// Maximum value for 32-bit address space (4GB - 1)
inline constexpr std::uint64_t ADDR32_MAX = 0xFFFF'FFFFULL;

/// Minimum signed 32-bit integer value
inline constexpr std::int32_t INT32_MIN_VAL = std::numeric_limits<std::int32_t>::min();

/// Maximum signed 32-bit integer value
inline constexpr std::int32_t INT32_MAX_VAL = std::numeric_limits<std::int32_t>::max();

// ============================================================================
// 64-bit Architecture Constants (48-bit for NaN-boxing)
// ============================================================================

/// Minimum value for 48-bit signed integer (NaN-boxing limit)
inline constexpr std::int64_t INT48_MIN = -(1LL << 47);

/// Maximum value for 48-bit signed integer (NaN-boxing limit)
inline constexpr std::int64_t INT48_MAX = (1LL << 47) - 1;

/// Maximum 48-bit address (used in Arch64 mode)
inline constexpr std::uint64_t ADDR48_MAX = 0x0000'FFFF'FFFF'FFFFULL;

// ============================================================================
// Architecture Detection
// ============================================================================

/// Check if architecture is 32-bit mode
[[nodiscard]] constexpr bool is_arch32(Architecture arch) noexcept {
    return arch == Architecture::Arch32;
}

/// Check if architecture is 64-bit mode
[[nodiscard]] constexpr bool is_arch64(Architecture arch) noexcept {
    return arch == Architecture::Arch64;
}

/// Get the integer bit width for an architecture
[[nodiscard]] constexpr std::size_t int_width(Architecture arch) noexcept {
    return is_arch32(arch) ? 32 : 48;
}

/// Get the address bit width for an architecture
[[nodiscard]] constexpr std::size_t addr_width(Architecture arch) noexcept {
    return is_arch32(arch) ? 32 : 48;
}

// ============================================================================
// Integer Masking Functions
// ============================================================================

/// Mask an integer to 32 bits with sign extension
///
/// @param value The 64-bit value to mask
/// @return The value masked to 32 bits, sign-extended to 64 bits
[[nodiscard]] constexpr std::int64_t mask_int32(std::int64_t value) noexcept {
    // Extract lower 32 bits
    std::int64_t masked = value & INT32_MASK;
    // Sign-extend if bit 31 is set
    if (masked & INT32_SIGN_BIT) {
        masked |= ~INT32_MASK;  // Set upper bits for negative values
    }
    return masked;
}

/// Mask an integer to the appropriate width for the target architecture
///
/// In Arch32 mode, the value is masked to 32 bits and sign-extended.
/// In Arch64 mode, the value is returned unchanged (caller should ensure
/// it fits in the 48-bit NaN-boxing range).
///
/// @param value The integer value to mask
/// @param arch The target architecture
/// @return The masked value appropriate for the architecture
[[nodiscard]] constexpr std::int64_t mask_int(std::int64_t value, Architecture arch) noexcept {
    if (is_arch32(arch)) {
        return mask_int32(value);
    }
    return value;  // Arch64: return unchanged
}

/// Mask an unsigned value to 32 bits (no sign extension)
///
/// @param value The 64-bit unsigned value to mask
/// @return The value masked to 32 bits
[[nodiscard]] constexpr std::uint64_t mask_uint32(std::uint64_t value) noexcept {
    return value & UINT32_MASK;
}

/// Mask an unsigned integer for the target architecture
///
/// @param value The unsigned value to mask
/// @param arch The target architecture
/// @return The masked value
[[nodiscard]] constexpr std::uint64_t mask_uint(std::uint64_t value, Architecture arch) noexcept {
    if (is_arch32(arch)) {
        return mask_uint32(value);
    }
    return value;
}

// ============================================================================
// Address Masking Functions
// ============================================================================

/// Mask a memory address to 32 bits
///
/// @param addr The 64-bit address to mask
/// @return The address masked to 32-bit range
[[nodiscard]] constexpr std::uint64_t mask_addr32(std::uint64_t addr) noexcept {
    return addr & ADDR32_MAX;
}

/// Mask a memory address for the target architecture
///
/// In Arch32 mode, addresses are limited to the 4GB address space.
/// In Arch64 mode, addresses use the full 48-bit canonical range.
///
/// @param addr The address to mask
/// @param arch The target architecture
/// @return The masked address
[[nodiscard]] constexpr std::uint64_t mask_addr(std::uint64_t addr, Architecture arch) noexcept {
    if (is_arch32(arch)) {
        return mask_addr32(addr);
    }
    return addr & ADDR48_MAX;  // Arch64: mask to 48-bit canonical range
}

// ============================================================================
// Pointer Masking Functions
// ============================================================================

/// Mask a pointer address to 32 bits with canonical form handling
///
/// @param ptr The pointer to mask
/// @return A pointer within the 32-bit address space
[[nodiscard]] inline void* mask_ptr32(void* ptr) noexcept {
    auto addr = reinterpret_cast<std::uint64_t>(ptr);
    return reinterpret_cast<void*>(mask_addr32(addr));
}

/// Mask a pointer for the target architecture
///
/// @param ptr The pointer to mask
/// @param arch The target architecture
/// @return The masked pointer
[[nodiscard]] inline void* mask_ptr(void* ptr, Architecture arch) noexcept {
    if (is_arch32(arch)) {
        return mask_ptr32(ptr);
    }
    return ptr;  // Arch64: return unchanged
}

// ============================================================================
// Range Validation Functions
// ============================================================================

/// Check if a value fits in the signed 32-bit range
///
/// @param value The value to check
/// @return true if value is within INT32_MIN..INT32_MAX
[[nodiscard]] constexpr bool fits_in_int32(std::int64_t value) noexcept {
    return value >= INT32_MIN_VAL && value <= INT32_MAX_VAL;
}

/// Check if a value fits in the 48-bit NaN-boxing range
///
/// @param value The value to check
/// @return true if value is within INT48_MIN..INT48_MAX
[[nodiscard]] constexpr bool fits_in_int48(std::int64_t value) noexcept {
    return value >= INT48_MIN && value <= INT48_MAX;
}

/// Check if a value fits in the target architecture's integer range
///
/// @param value The value to check
/// @param arch The target architecture
/// @return true if value fits without overflow/truncation issues
[[nodiscard]] constexpr bool fits_in_arch(std::int64_t value, Architecture arch) noexcept {
    if (is_arch32(arch)) {
        return fits_in_int32(value);
    }
    return fits_in_int48(value);
}

/// Check if an address fits in the target architecture's address space
///
/// @param addr The address to check
/// @param arch The target architecture
/// @return true if address is valid for the architecture
[[nodiscard]] constexpr bool addr_fits_in_arch(std::uint64_t addr, Architecture arch) noexcept {
    if (is_arch32(arch)) {
        return addr <= ADDR32_MAX;
    }
    return addr <= ADDR48_MAX;
}

// ============================================================================
// Shift Amount Masking
// ============================================================================

/// Maximum shift amount for 32-bit mode (0-31)
inline constexpr std::int64_t SHIFT32_MOD = 32;

/// Maximum shift amount for 64-bit mode (0-47 for 48-bit values)
inline constexpr std::int64_t SHIFT48_MOD = 48;

/// Mask a shift amount for the target architecture
///
/// In Arch32 mode, shift amounts wrap to 0-31.
/// In Arch64 mode, shift amounts wrap to 0-47.
/// Negative shift values wrap to positive equivalents.
///
/// @param shift The shift amount
/// @param arch The target architecture
/// @return The masked shift amount (always 0 to width-1)
[[nodiscard]] constexpr std::int64_t mask_shift(std::int64_t shift, Architecture arch) noexcept {
    // For 32-bit, use efficient bitwise AND (31 = 2^5 - 1)
    if (is_arch32(arch)) {
        return shift & 31;
    }
    // For 48-bit, use modulo since 47 is not a bitmask
    // Formula handles negative values correctly: ((x % n) + n) % n
    return ((shift % SHIFT48_MOD) + SHIFT48_MOD) % SHIFT48_MOD;
}

}  // namespace arch_config

}  // namespace dotvm::core
