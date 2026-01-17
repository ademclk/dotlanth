#pragma once

/// @file bigint_ops.hpp
/// @brief Low-level operations for BigInt implementation
///
/// Provides primitive arithmetic operations on limbs and helper functions
/// for implementing BigInt algorithms.

#include <array>
#include <bit>
#include <cstdint>
#include <utility>

namespace dotvm::core::bigint::ops {

/// @brief Type used for single limbs
using limb_t = std::uint64_t;

/// @brief Type for double-width intermediate results
using dlimb_t = __uint128_t;

/// @brief Number of bits per limb
constexpr std::size_t LIMB_BITS = 64;

/// @brief Base for limb arithmetic (2^64)
constexpr dlimb_t LIMB_BASE = dlimb_t{1} << LIMB_BITS;

// =============================================================================
// Basic Limb Operations
// =============================================================================

/// @brief Add two limbs with carry
/// @param a First limb
/// @param b Second limb
/// @param carry Input carry (0 or 1)
/// @return Pair of (result, output_carry)
[[nodiscard]] constexpr std::pair<limb_t, limb_t> add_with_carry(limb_t a, limb_t b,
                                                                 limb_t carry) noexcept {
    dlimb_t sum = static_cast<dlimb_t>(a) + static_cast<dlimb_t>(b) + static_cast<dlimb_t>(carry);
    return {static_cast<limb_t>(sum), static_cast<limb_t>(sum >> LIMB_BITS)};
}

/// @brief Subtract two limbs with borrow
/// @param a Minuend
/// @param b Subtrahend
/// @param borrow Input borrow (0 or 1)
/// @return Pair of (result, output_borrow)
[[nodiscard]] constexpr std::pair<limb_t, limb_t> sub_with_borrow(limb_t a, limb_t b,
                                                                  limb_t borrow) noexcept {
    dlimb_t diff = static_cast<dlimb_t>(a) - static_cast<dlimb_t>(b) - static_cast<dlimb_t>(borrow);
    limb_t result = static_cast<limb_t>(diff);
    // If diff was negative (underflow), the high bits will be all 1s
    limb_t new_borrow = (diff >> LIMB_BITS) != 0 ? 1 : 0;
    return {result, new_borrow};
}

/// @brief Multiply two limbs and add carry, producing double-width result
/// @param a First limb
/// @param b Second limb
/// @param carry Carry to add
/// @return Pair of (low_result, high_result)
[[nodiscard]] constexpr std::pair<limb_t, limb_t> mul_with_carry(limb_t a, limb_t b,
                                                                 limb_t carry) noexcept {
    dlimb_t prod = static_cast<dlimb_t>(a) * static_cast<dlimb_t>(b) + static_cast<dlimb_t>(carry);
    return {static_cast<limb_t>(prod), static_cast<limb_t>(prod >> LIMB_BITS)};
}

/// @brief Multiply two limbs and add two carries
/// @param a First limb
/// @param b Second limb
/// @param carry1 First carry
/// @param carry2 Second carry
/// @return Pair of (low_result, high_result)
[[nodiscard]] constexpr std::pair<limb_t, limb_t>
mul_add_with_carry(limb_t a, limb_t b, limb_t carry1, limb_t carry2) noexcept {
    dlimb_t prod = static_cast<dlimb_t>(a) * static_cast<dlimb_t>(b) +
                   static_cast<dlimb_t>(carry1) + static_cast<dlimb_t>(carry2);
    return {static_cast<limb_t>(prod), static_cast<limb_t>(prod >> LIMB_BITS)};
}

/// @brief Divide double-width dividend by single limb divisor
/// @param high High part of dividend
/// @param low Low part of dividend
/// @param divisor Single-limb divisor (must be non-zero)
/// @return Pair of (quotient, remainder)
[[nodiscard]] constexpr std::pair<limb_t, limb_t> div_wide(limb_t high, limb_t low,
                                                           limb_t divisor) noexcept {
    dlimb_t dividend = (static_cast<dlimb_t>(high) << LIMB_BITS) | static_cast<dlimb_t>(low);
    limb_t quotient = static_cast<limb_t>(dividend / divisor);
    limb_t remainder = static_cast<limb_t>(dividend % divisor);
    return {quotient, remainder};
}

// =============================================================================
// Bit Manipulation
// =============================================================================

/// @brief Count leading zeros in a limb
[[nodiscard]] constexpr int clz(limb_t x) noexcept {
    if (x == 0)
        return static_cast<int>(LIMB_BITS);
    return std::countl_zero(x);
}

/// @brief Count trailing zeros in a limb
[[nodiscard]] constexpr int ctz(limb_t x) noexcept {
    if (x == 0)
        return static_cast<int>(LIMB_BITS);
    return std::countr_zero(x);
}

/// @brief Get number of set bits in a limb
[[nodiscard]] constexpr int popcount(limb_t x) noexcept {
    return std::popcount(x);
}

/// @brief Shift a limb array left by a number of bits (< LIMB_BITS)
/// @param limbs Array of limbs (modified in place)
/// @param n Number of limbs
/// @param shift Number of bits to shift (0 <= shift < LIMB_BITS)
/// @return Overflow from the most significant limb
template <std::size_t N>
[[nodiscard]] constexpr limb_t shift_left_bits(std::array<limb_t, N>& limbs, int shift) noexcept {
    if (shift == 0)
        return 0;

    limb_t carry = 0;
    int rshift = static_cast<int>(LIMB_BITS) - shift;

    for (std::size_t i = 0; i < N; ++i) {
        limb_t new_carry = limbs[i] >> rshift;
        limbs[i] = (limbs[i] << shift) | carry;
        carry = new_carry;
    }

    return carry;
}

/// @brief Shift a limb array right by a number of bits (< LIMB_BITS)
/// @param limbs Array of limbs (modified in place)
/// @param n Number of limbs
/// @param shift Number of bits to shift (0 <= shift < LIMB_BITS)
/// @return Underflow from the least significant limb
template <std::size_t N>
[[nodiscard]] constexpr limb_t shift_right_bits(std::array<limb_t, N>& limbs, int shift) noexcept {
    if (shift == 0)
        return 0;

    limb_t carry = 0;
    int lshift = static_cast<int>(LIMB_BITS) - shift;

    for (std::size_t i = N; i-- > 0;) {
        limb_t new_carry = limbs[i] << lshift;
        limbs[i] = (limbs[i] >> shift) | carry;
        carry = new_carry;
    }

    return carry;
}

// =============================================================================
// Digit Conversion
// =============================================================================

/// @brief Convert a single hex character to value (0-15)
/// @return Value or 255 if invalid
[[nodiscard]] constexpr std::uint8_t hex_char_to_value(char c) noexcept {
    if (c >= '0' && c <= '9')
        return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f')
        return static_cast<std::uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return static_cast<std::uint8_t>(c - 'A' + 10);
    return 255;
}

/// @brief Convert a value (0-15) to hex character
[[nodiscard]] constexpr char value_to_hex_char(std::uint8_t v, bool uppercase = false) noexcept {
    if (v < 10)
        return static_cast<char>('0' + v);
    return static_cast<char>((uppercase ? 'A' : 'a') + v - 10);
}

/// @brief Convert a single decimal character to value (0-9)
/// @return Value or 255 if invalid
[[nodiscard]] constexpr std::uint8_t dec_char_to_value(char c) noexcept {
    if (c >= '0' && c <= '9')
        return static_cast<std::uint8_t>(c - '0');
    return 255;
}

// =============================================================================
// Knuth's Algorithm D Constants
// =============================================================================

/// @brief Threshold for switching from simple to Karatsuba multiplication
/// This is the number of limbs above which Karatsuba becomes beneficial
constexpr std::size_t KARATSUBA_THRESHOLD = 64;

/// @brief Threshold for switching from simple to Knuth division
constexpr std::size_t KNUTH_THRESHOLD = 2;

}  // namespace dotvm::core::bigint::ops
