#pragma once

/// @file bigint.hpp
/// @brief Arbitrary precision integer class for DotVM
///
/// Provides BigInt, a class for representing and manipulating integers
/// of arbitrary size. Uses little-endian limb storage for efficient
/// arithmetic operations.

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dotvm::core::bigint {

/// @brief Exception thrown when dividing by zero
class DivisionByZeroError : public std::runtime_error {
public:
    DivisionByZeroError() : std::runtime_error("Division by zero") {}
};

/// @brief Exception thrown for invalid string input
class InvalidFormatError : public std::runtime_error {
public:
    explicit InvalidFormatError(const std::string& msg)
        : std::runtime_error("Invalid BigInt format: " + msg) {}
};

/// @brief Arbitrary precision integer class
///
/// BigInt represents signed integers of arbitrary size using a vector of
/// 64-bit limbs stored in little-endian order (limbs_[0] is least significant).
///
/// @example
/// ```cpp
/// BigInt a("123456789012345678901234567890");
/// BigInt b = BigInt(100) * BigInt(200);
/// BigInt c = BigInt::gcd(a, b);
/// ```
class BigInt {
public:
    /// @brief Type used for individual limbs (64-bit unsigned)
    using limb_type = std::uint64_t;

    /// @brief Number of bits per limb
    static constexpr std::size_t limb_bits = 64;

    /// @brief Maximum value of a single limb
    static constexpr limb_type limb_max = ~limb_type{0};

    // =========================================================================
    // Constructors
    // =========================================================================

    /// @brief Default constructor - creates zero
    constexpr BigInt() noexcept = default;

    /// @brief Construct from any integral type
    /// @tparam T Any integral type (int, long, uint64_t, etc.)
    /// @param value The integer value
    template <std::integral T>
    constexpr explicit BigInt(T value) noexcept;

    /// @brief Construct from string representation
    /// @param str String in decimal (e.g., "123", "-456") or hex (e.g., "0x1A2B")
    /// @throws InvalidFormatError if the string format is invalid
    explicit BigInt(std::string_view str);

    // =========================================================================
    // Query Operations
    // =========================================================================

    /// @brief Check if value is zero
    [[nodiscard]] constexpr bool is_zero() const noexcept;

    /// @brief Check if value is negative
    [[nodiscard]] constexpr bool is_negative() const noexcept;

    /// @brief Check if value is positive (greater than zero)
    [[nodiscard]] constexpr bool is_positive() const noexcept;

    /// @brief Get the number of bits needed to represent this value
    /// @return Number of bits (0 returns 0)
    [[nodiscard]] constexpr std::size_t bit_length() const noexcept;

    /// @brief Get the number of limbs used
    [[nodiscard]] constexpr std::size_t limb_count() const noexcept;

    /// @brief Get the sign of the number
    /// @return -1, 0, or 1
    [[nodiscard]] constexpr int sign() const noexcept;

    // =========================================================================
    // Comparison Operators
    // =========================================================================

    /// @brief Three-way comparison operator
    [[nodiscard]] constexpr std::strong_ordering operator<=>(const BigInt& rhs) const noexcept;

    /// @brief Equality comparison
    [[nodiscard]] constexpr bool operator==(const BigInt& rhs) const noexcept;

    // =========================================================================
    // Arithmetic Operators
    // =========================================================================

    /// @brief Addition
    [[nodiscard]] BigInt operator+(const BigInt& rhs) const;

    /// @brief Subtraction
    [[nodiscard]] BigInt operator-(const BigInt& rhs) const;

    /// @brief Multiplication
    [[nodiscard]] BigInt operator*(const BigInt& rhs) const;

    /// @brief Division
    /// @throws DivisionByZeroError if rhs is zero
    [[nodiscard]] BigInt operator/(const BigInt& rhs) const;

    /// @brief Modulo (remainder)
    /// @throws DivisionByZeroError if rhs is zero
    [[nodiscard]] BigInt operator%(const BigInt& rhs) const;

    /// @brief Unary negation
    [[nodiscard]] BigInt operator-() const;

    /// @brief Unary plus (returns copy)
    [[nodiscard]] BigInt operator+() const;

    // =========================================================================
    // Compound Assignment Operators
    // =========================================================================

    /// @brief Addition assignment
    BigInt& operator+=(const BigInt& rhs);

    /// @brief Subtraction assignment
    BigInt& operator-=(const BigInt& rhs);

    /// @brief Multiplication assignment
    BigInt& operator*=(const BigInt& rhs);

    /// @brief Division assignment
    /// @throws DivisionByZeroError if rhs is zero
    BigInt& operator/=(const BigInt& rhs);

    /// @brief Modulo assignment
    /// @throws DivisionByZeroError if rhs is zero
    BigInt& operator%=(const BigInt& rhs);

    // =========================================================================
    // Increment/Decrement Operators
    // =========================================================================

    /// @brief Pre-increment
    BigInt& operator++();

    /// @brief Post-increment
    BigInt operator++(int);

    /// @brief Pre-decrement
    BigInt& operator--();

    /// @brief Post-decrement
    BigInt operator--(int);

    // =========================================================================
    // Bitwise Operators
    // =========================================================================

    /// @brief Left shift
    [[nodiscard]] BigInt operator<<(std::size_t shift) const;

    /// @brief Right shift
    [[nodiscard]] BigInt operator>>(std::size_t shift) const;

    /// @brief Bitwise AND
    [[nodiscard]] BigInt operator&(const BigInt& rhs) const;

    /// @brief Bitwise OR
    [[nodiscard]] BigInt operator|(const BigInt& rhs) const;

    /// @brief Bitwise XOR
    [[nodiscard]] BigInt operator^(const BigInt& rhs) const;

    // =========================================================================
    // Extended Operations
    // =========================================================================

    /// @brief Compute power (exponentiation)
    /// @param exponent Non-negative exponent
    /// @return this^exponent
    [[nodiscard]] BigInt pow(std::uint64_t exponent) const;

    /// @brief Compute greatest common divisor
    /// @param a First integer
    /// @param b Second integer
    /// @return GCD(|a|, |b|)
    [[nodiscard]] static BigInt gcd(const BigInt& a, const BigInt& b);

    /// @brief Compute least common multiple
    /// @param a First integer
    /// @param b Second integer
    /// @return LCM(|a|, |b|)
    [[nodiscard]] static BigInt lcm(const BigInt& a, const BigInt& b);

    /// @brief Compute modular exponentiation
    /// @param exp Exponent (non-negative)
    /// @param mod Modulus (positive)
    /// @return this^exp mod mod
    /// @throws DivisionByZeroError if mod is zero
    [[nodiscard]] BigInt mod_pow(const BigInt& exp, const BigInt& mod) const;

    /// @brief Get absolute value
    [[nodiscard]] BigInt abs() const;

    /// @brief Perform division and return both quotient and remainder
    /// @param divisor The divisor
    /// @return Pair of (quotient, remainder)
    /// @throws DivisionByZeroError if divisor is zero
    [[nodiscard]] std::pair<BigInt, BigInt> divmod(const BigInt& divisor) const;

    // =========================================================================
    // Conversion Operations
    // =========================================================================

    /// @brief Convert to decimal string
    [[nodiscard]] std::string to_string() const;

    /// @brief Convert to hexadecimal string (prefixed with 0x)
    [[nodiscard]] std::string to_hex_string() const;

    /// @brief Try to convert to built-in integer type
    /// @tparam T Target integer type
    /// @return The value if it fits, std::nullopt otherwise
    template <std::integral T>
    [[nodiscard]] constexpr std::optional<T> try_to() const noexcept;

    /// @brief Convert to built-in integer type (unchecked)
    /// @tparam T Target integer type
    /// @return The value truncated to type T
    template <std::integral T>
    [[nodiscard]] constexpr T to() const noexcept;

    // =========================================================================
    // Static Factory Methods
    // =========================================================================

    /// @brief Create from raw limbs
    /// @param limbs Little-endian limb vector
    /// @param negative Whether the value is negative
    [[nodiscard]] static BigInt from_limbs(std::vector<limb_type> limbs, bool negative = false);

    /// @brief Compute factorial
    /// @param n Non-negative integer
    /// @return n!
    [[nodiscard]] static BigInt factorial(std::uint64_t n);

    /// @brief Compute nth Fibonacci number
    /// @param n Index (0-based)
    /// @return F(n)
    [[nodiscard]] static BigInt fibonacci(std::uint64_t n);

private:
    /// @brief Limbs stored in little-endian order (limbs_[0] is least significant)
    std::vector<limb_type> limbs_;

    /// @brief Sign flag (true if negative)
    bool negative_ = false;

    // =========================================================================
    // Internal Helper Methods
    // =========================================================================

    /// @brief Remove leading zero limbs
    void normalize();

    /// @brief Add magnitudes (ignoring signs)
    [[nodiscard]] static BigInt add_magnitudes(const BigInt& a, const BigInt& b);

    /// @brief Subtract magnitudes (assumes |a| >= |b|)
    [[nodiscard]] static BigInt sub_magnitudes(const BigInt& a, const BigInt& b);

    /// @brief Compare magnitudes (ignoring signs)
    /// @return -1 if |a| < |b|, 0 if |a| == |b|, 1 if |a| > |b|
    [[nodiscard]] static int compare_magnitudes(const BigInt& a, const BigInt& b) noexcept;

    /// @brief Multiply using grade-school algorithm
    [[nodiscard]] static BigInt multiply_simple(const BigInt& a, const BigInt& b);

    /// @brief Multiply using Karatsuba algorithm
    [[nodiscard]] static BigInt multiply_karatsuba(const BigInt& a, const BigInt& b);

    /// @brief Divide using Knuth's Algorithm D
    [[nodiscard]] static std::pair<BigInt, BigInt> divide_knuth(const BigInt& dividend,
                                                                const BigInt& divisor);

    /// @brief Parse decimal string into BigInt
    void parse_decimal(std::string_view str);

    /// @brief Parse hexadecimal string into BigInt
    void parse_hex(std::string_view str);

    /// @brief Get bit at position
    [[nodiscard]] bool get_bit(std::size_t pos) const noexcept;

    /// @brief Set bit at position
    void set_bit(std::size_t pos, bool value = true);

    /// @brief Ensure at least n limbs
    void ensure_limbs(std::size_t n);

    /// @brief Access limb with bounds checking
    [[nodiscard]] limb_type get_limb(std::size_t index) const noexcept;
};

// =============================================================================
// Template Implementation
// =============================================================================

template <std::integral T>
constexpr BigInt::BigInt(T value) noexcept {
    if (value == 0) {
        // Default state is already zero
        return;
    }

    if constexpr (std::is_signed_v<T>) {
        if (value < 0) {
            negative_ = true;
            // Handle minimum value specially to avoid overflow
            if (value == std::numeric_limits<T>::min()) {
                // For minimum signed value, negate by treating as unsigned
                using U = std::make_unsigned_t<T>;
                auto uval = static_cast<U>(-(value + 1)) + U{1};
                if constexpr (sizeof(T) <= sizeof(limb_type)) {
                    limbs_.push_back(static_cast<limb_type>(uval));
                } else {
                    // For __int128 or larger types
                    while (uval > 0) {
                        limbs_.push_back(static_cast<limb_type>(uval));
                        uval >>= limb_bits;
                    }
                }
            } else {
                auto abs_val = static_cast<std::make_unsigned_t<T>>(-value);
                if constexpr (sizeof(T) <= sizeof(limb_type)) {
                    limbs_.push_back(static_cast<limb_type>(abs_val));
                } else {
                    while (abs_val > 0) {
                        limbs_.push_back(static_cast<limb_type>(abs_val));
                        abs_val >>= limb_bits;
                    }
                }
            }
        } else {
            auto uval = static_cast<std::make_unsigned_t<T>>(value);
            if constexpr (sizeof(T) <= sizeof(limb_type)) {
                limbs_.push_back(static_cast<limb_type>(uval));
            } else {
                while (uval > 0) {
                    limbs_.push_back(static_cast<limb_type>(uval));
                    uval >>= limb_bits;
                }
            }
        }
    } else {
        // Unsigned type
        if constexpr (sizeof(T) <= sizeof(limb_type)) {
            limbs_.push_back(static_cast<limb_type>(value));
        } else {
            auto uval = value;
            while (uval > 0) {
                limbs_.push_back(static_cast<limb_type>(uval));
                uval >>= limb_bits;
            }
        }
    }
}

constexpr bool BigInt::is_zero() const noexcept {
    return limbs_.empty();
}

constexpr bool BigInt::is_negative() const noexcept {
    return negative_ && !is_zero();
}

constexpr bool BigInt::is_positive() const noexcept {
    return !negative_ && !is_zero();
}

constexpr std::size_t BigInt::limb_count() const noexcept {
    return limbs_.size();
}

constexpr int BigInt::sign() const noexcept {
    if (is_zero())
        return 0;
    return negative_ ? -1 : 1;
}

constexpr std::size_t BigInt::bit_length() const noexcept {
    if (limbs_.empty()) {
        return 0;
    }

    // Find the highest set bit in the most significant limb
    limb_type msb = limbs_.back();
    std::size_t bits = (limbs_.size() - 1) * limb_bits;

    // Count bits in the most significant limb
    while (msb != 0) {
        ++bits;
        msb >>= 1;
    }

    return bits;
}

constexpr std::strong_ordering BigInt::operator<=>(const BigInt& rhs) const noexcept {
    // Handle sign differences
    if (negative_ != rhs.negative_) {
        if (is_zero() && rhs.is_zero()) {
            return std::strong_ordering::equal;
        }
        return negative_ ? std::strong_ordering::less : std::strong_ordering::greater;
    }

    // Both have same sign - compare magnitudes
    int mag_cmp = compare_magnitudes(*this, rhs);

    if (mag_cmp == 0) {
        return std::strong_ordering::equal;
    }

    // If negative, reverse the comparison
    if (negative_) {
        return mag_cmp > 0 ? std::strong_ordering::less : std::strong_ordering::greater;
    } else {
        return mag_cmp > 0 ? std::strong_ordering::greater : std::strong_ordering::less;
    }
}

constexpr bool BigInt::operator==(const BigInt& rhs) const noexcept {
    if (is_zero() && rhs.is_zero()) {
        return true;
    }
    return negative_ == rhs.negative_ && limbs_ == rhs.limbs_;
}

template <std::integral T>
constexpr std::optional<T> BigInt::try_to() const noexcept {
    if (is_zero()) {
        return T{0};
    }

    constexpr std::size_t target_bits = sizeof(T) * 8;

    if constexpr (std::is_signed_v<T>) {
        // For signed types, check if value fits
        if (bit_length() > target_bits - 1) {
            // Could be min value which needs special handling
            if (negative_ && bit_length() == target_bits && limbs_.size() == 1) {
                // Check if it's exactly the minimum value
                auto min_abs = static_cast<limb_type>(-(std::numeric_limits<T>::min() + 1)) + 1;
                if (limbs_[0] == min_abs) {
                    return std::numeric_limits<T>::min();
                }
            }
            return std::nullopt;
        }

        auto val = static_cast<T>(limbs_[0]);
        return negative_ ? -val : val;
    } else {
        // For unsigned types, negative values don't fit
        if (negative_) {
            return std::nullopt;
        }

        if (bit_length() > target_bits) {
            return std::nullopt;
        }

        return static_cast<T>(limbs_[0]);
    }
}

template <std::integral T>
constexpr T BigInt::to() const noexcept {
    if (is_zero()) {
        return T{0};
    }

    T val = static_cast<T>(limbs_[0]);
    if constexpr (std::is_signed_v<T>) {
        return negative_ ? -val : val;
    } else {
        return val;
    }
}

}  // namespace dotvm::core::bigint
