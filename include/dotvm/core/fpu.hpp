#pragma once

/// @file fpu.hpp
/// @brief IEEE 754 floating-point utilities for DotVM
///
/// This header provides helper functions and constants for IEEE 754
/// compliant floating-point operations, including special value
/// classification and manipulation.

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dotvm::core::fpu {

// ============================================================================
// IEEE 754 Special Value Constants
// ============================================================================

/// Canonical quiet NaN (quiet bit set)
inline constexpr double QNAN = std::numeric_limits<double>::quiet_NaN();

/// Positive infinity
inline constexpr double POS_INF = std::numeric_limits<double>::infinity();

/// Negative infinity
inline constexpr double NEG_INF = -std::numeric_limits<double>::infinity();

/// Positive zero
inline constexpr double POS_ZERO = 0.0;

/// Negative zero (distinct bit pattern from positive zero)
inline constexpr double NEG_ZERO = -0.0;

/// Smallest positive denormal (subnormal) number
inline constexpr double MIN_DENORMAL = std::numeric_limits<double>::denorm_min();

/// Smallest positive normal number
inline constexpr double MIN_NORMAL = std::numeric_limits<double>::min();

/// Largest finite value
inline constexpr double MAX_FINITE = std::numeric_limits<double>::max();

/// Machine epsilon (smallest x such that 1.0 + x != 1.0)
inline constexpr double EPSILON = std::numeric_limits<double>::epsilon();

// ============================================================================
// IEEE 754 Bit Pattern Constants
// ============================================================================

/// Sign bit mask for IEEE 754 double
inline constexpr std::uint64_t SIGN_MASK = 0x8000'0000'0000'0000ULL;

/// Exponent field mask for IEEE 754 double
inline constexpr std::uint64_t EXPONENT_MASK = 0x7FF0'0000'0000'0000ULL;

/// Mantissa (significand) field mask for IEEE 754 double
inline constexpr std::uint64_t MANTISSA_MASK = 0x000F'FFFF'FFFF'FFFFULL;

/// Quiet NaN bit (bit 51) - set for quiet NaN, clear for signaling NaN
inline constexpr std::uint64_t QUIET_NAN_BIT = 0x0008'0000'0000'0000ULL;

/// Bit pattern for positive zero
inline constexpr std::uint64_t POS_ZERO_BITS = 0x0000'0000'0000'0000ULL;

/// Bit pattern for negative zero
inline constexpr std::uint64_t NEG_ZERO_BITS = 0x8000'0000'0000'0000ULL;

// ============================================================================
// Special Value Classification Functions
// ============================================================================

/// Check if value is positive zero
[[nodiscard]] constexpr bool is_positive_zero(double d) noexcept {
    return std::bit_cast<std::uint64_t>(d) == POS_ZERO_BITS;
}

/// Check if value is negative zero
[[nodiscard]] constexpr bool is_negative_zero(double d) noexcept {
    return std::bit_cast<std::uint64_t>(d) == NEG_ZERO_BITS;
}

/// Check if value is any zero (+0.0 or -0.0)
[[nodiscard]] constexpr bool is_zero(double d) noexcept {
    // Mask off sign bit and check if result is zero
    return (std::bit_cast<std::uint64_t>(d) & ~SIGN_MASK) == 0;
}

/// Check if value is positive infinity
[[nodiscard]] inline bool is_positive_inf(double d) noexcept {
    return std::isinf(d) && d > 0;
}

/// Check if value is negative infinity
[[nodiscard]] inline bool is_negative_inf(double d) noexcept {
    return std::isinf(d) && d < 0;
}

/// Check if value is a denormal (subnormal) number
/// Denormals have exponent = 0 and mantissa != 0
[[nodiscard]] inline bool is_denormal(double d) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(d);
    const std::uint64_t exponent = bits & EXPONENT_MASK;
    const std::uint64_t mantissa = bits & MANTISSA_MASK;
    return exponent == 0 && mantissa != 0;
}

/// Check if value is a signaling NaN
/// Signaling NaN has the quiet bit (bit 51) clear
/// Note: Behavior may be implementation-defined on some platforms
[[nodiscard]] inline bool is_signaling_nan(double d) noexcept {
    if (!std::isnan(d)) return false;
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(d);
    return (bits & QUIET_NAN_BIT) == 0;
}

/// Check if value is a quiet NaN
/// Quiet NaN has the quiet bit (bit 51) set
[[nodiscard]] inline bool is_quiet_nan(double d) noexcept {
    if (!std::isnan(d)) return false;
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(d);
    return (bits & QUIET_NAN_BIT) != 0;
}

/// Check if value is finite (not NaN or infinity)
[[nodiscard]] inline bool is_finite(double d) noexcept {
    return std::isfinite(d);
}

/// Check if value is normal (not zero, denormal, infinity, or NaN)
[[nodiscard]] inline bool is_normal(double d) noexcept {
    return std::isnormal(d);
}

// ============================================================================
// NaN Propagation and Special Value Creation
// ============================================================================

/// Propagate NaN according to IEEE 754 rules
/// If either input is NaN, returns canonical quiet NaN
/// Otherwise returns the first operand (should not happen in normal use)
[[nodiscard]] inline double propagate_nan(double a, double b) noexcept {
    if (std::isnan(a)) return QNAN;
    if (std::isnan(b)) return QNAN;
    return a;
}

/// Create infinity with the specified sign
/// @param negative If true, returns negative infinity
[[nodiscard]] constexpr double make_inf(bool negative) noexcept {
    return negative ? NEG_INF : POS_INF;
}

/// Create zero with the specified sign
/// @param negative If true, returns negative zero
[[nodiscard]] constexpr double make_zero(bool negative) noexcept {
    return negative ? NEG_ZERO : POS_ZERO;
}

/// Get the sign of a floating-point value
/// @return true if negative (including -0.0 and -NaN)
[[nodiscard]] constexpr bool signbit(double d) noexcept {
    return (std::bit_cast<std::uint64_t>(d) & SIGN_MASK) != 0;
}

/// Copy the sign from one value to another
/// @param magnitude Value to use for magnitude
/// @param sign_source Value to copy sign from
[[nodiscard]] inline double copysign(double magnitude, double sign_source) noexcept {
    return std::copysign(magnitude, sign_source);
}

// ============================================================================
// Integer/Float Conversion Limits
// ============================================================================

/// Maximum int64 that can be exactly represented in double (2^53)
inline constexpr std::int64_t MAX_EXACT_INT64 = 1LL << 53;

/// Minimum int64 that can be exactly represented in double (-2^53)
inline constexpr std::int64_t MIN_EXACT_INT64 = -(1LL << 53);

/// Check if an int64 can be exactly represented as a double
[[nodiscard]] constexpr bool can_represent_exactly(std::int64_t i) noexcept {
    return i >= MIN_EXACT_INT64 && i <= MAX_EXACT_INT64;
}

}  // namespace dotvm::core::fpu
