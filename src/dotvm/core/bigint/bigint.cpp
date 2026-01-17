/// @file bigint.cpp
/// @brief Implementation of BigInt arbitrary precision integer class

#include <dotvm/core/bigint/bigint.hpp>
#include <dotvm/core/bigint/bigint_ops.hpp>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <charconv>
#include <ranges>
#include <sstream>
#include <utility>

namespace dotvm::core::bigint {

using namespace ops;

// =============================================================================
// Constructor: String Parsing
// =============================================================================

BigInt::BigInt(std::string_view str) {
    if (str.empty()) {
        throw InvalidFormatError("empty string");
    }

    // Handle sign
    std::size_t start = 0;
    bool neg = false;

    if (str[0] == '-') {
        neg = true;
        start = 1;
    } else if (str[0] == '+') {
        start = 1;
    }

    if (start >= str.size()) {
        throw InvalidFormatError("no digits after sign");
    }

    // Check for hex prefix
    if (str.size() > start + 2 && str[start] == '0' && (str[start + 1] == 'x' || str[start + 1] == 'X')) {
        parse_hex(str.substr(start + 2));
    } else {
        parse_decimal(str.substr(start));
    }

    negative_ = neg && !is_zero();
}

void BigInt::parse_decimal(std::string_view str) {
    // Validate and skip leading zeros
    std::size_t start = 0;
    while (start < str.size() - 1 && str[start] == '0') {
        ++start;
    }
    str = str.substr(start);

    if (str.empty()) {
        return;  // Zero
    }

    // Validate all characters are digits
    for (char c : str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            throw InvalidFormatError(std::string("invalid character in decimal: ") + c);
        }
    }

    // Process digits in groups for efficiency
    // We can process up to 19 decimal digits at a time (fits in uint64_t)
    constexpr std::size_t chunk_size = 19;
    // Note: chunk_multiplier would be 10^19, but we calculate multiplier dynamically below
    static_cast<void>(chunk_size);  // Used in loop below

    for (std::size_t i = 0; i < str.size(); i += chunk_size) {
        std::size_t remaining = str.size() - i;
        std::size_t this_chunk = std::min(chunk_size, remaining);

        // Parse this chunk
        limb_type chunk_value = 0;
        limb_type multiplier = 1;

        for (std::size_t j = 0; j < this_chunk; ++j) {
            chunk_value = chunk_value * 10 + static_cast<limb_type>(str[i + j] - '0');
            if (j < this_chunk - 1) {
                multiplier *= 10;
            }
        }

        // Multiply current result by the appropriate power of 10 and add chunk
        if (limbs_.empty()) {
            if (chunk_value != 0) {
                limbs_.push_back(chunk_value);
            }
        } else {
            // Determine the correct multiplier based on chunk size
            limb_type actual_multiplier = 1;
            for (std::size_t k = 0; k < this_chunk; ++k) {
                actual_multiplier *= 10;
            }

            // Multiply existing limbs by multiplier
            limb_type carry = 0;
            for (auto& limb : limbs_) {
                auto [lo, hi] = mul_with_carry(limb, actual_multiplier, carry);
                limb = lo;
                carry = hi;
            }
            if (carry != 0) {
                limbs_.push_back(carry);
            }

            // Add chunk_value
            carry = chunk_value;
            for (auto& limb : limbs_) {
                auto [result, new_carry] = add_with_carry(limb, carry, 0);
                limb = result;
                carry = new_carry;
                if (carry == 0) break;
            }
            if (carry != 0) {
                limbs_.push_back(carry);
            }
        }
    }
}

void BigInt::parse_hex(std::string_view str) {
    // Skip leading zeros
    std::size_t start = 0;
    while (start < str.size() - 1 && str[start] == '0') {
        ++start;
    }
    str = str.substr(start);

    if (str.empty() || (str.size() == 1 && str[0] == '0')) {
        return;  // Zero
    }

    // Process hex digits from right to left, 16 digits (64 bits) at a time
    constexpr std::size_t hex_per_limb = 16;

    for (std::size_t i = 0; i < str.size(); i += hex_per_limb) {
        std::size_t chunk_start;
        std::size_t chunk_len;

        if (str.size() >= hex_per_limb + i) {
            chunk_start = str.size() - hex_per_limb - i;
            chunk_len = hex_per_limb;
        } else if (i < str.size()) {
            chunk_start = 0;
            chunk_len = str.size() - i;
        } else {
            break;
        }

        limb_type value = 0;
        for (std::size_t j = 0; j < chunk_len; ++j) {
            auto digit = hex_char_to_value(str[chunk_start + j]);
            if (digit == 255) {
                throw InvalidFormatError(std::string("invalid hex character: ") + str[chunk_start + j]);
            }
            value = (value << 4) | digit;
        }
        limbs_.push_back(value);
    }

    normalize();
}

// =============================================================================
// Internal Helpers
// =============================================================================

void BigInt::normalize() {
    while (!limbs_.empty() && limbs_.back() == 0) {
        limbs_.pop_back();
    }
    // Zero is never negative
    if (limbs_.empty()) {
        negative_ = false;
    }
}

BigInt::limb_type BigInt::get_limb(std::size_t index) const noexcept {
    if (index < limbs_.size()) {
        return limbs_[index];
    }
    return 0;
}

void BigInt::ensure_limbs(std::size_t n) {
    if (limbs_.size() < n) {
        limbs_.resize(n, 0);
    }
}

bool BigInt::get_bit(std::size_t pos) const noexcept {
    std::size_t limb_idx = pos / limb_bits;
    std::size_t bit_idx = pos % limb_bits;

    if (limb_idx >= limbs_.size()) {
        return false;
    }

    return (limbs_[limb_idx] >> bit_idx) & 1;
}

void BigInt::set_bit(std::size_t pos, bool value) {
    std::size_t limb_idx = pos / limb_bits;
    std::size_t bit_idx = pos % limb_bits;

    ensure_limbs(limb_idx + 1);

    if (value) {
        limbs_[limb_idx] |= (limb_type{1} << bit_idx);
    } else {
        limbs_[limb_idx] &= ~(limb_type{1} << bit_idx);
    }
}

int BigInt::compare_magnitudes(const BigInt& a, const BigInt& b) noexcept {
    if (a.limbs_.size() != b.limbs_.size()) {
        return a.limbs_.size() < b.limbs_.size() ? -1 : 1;
    }

    // Compare from most significant to least significant
    for (std::size_t i = a.limbs_.size(); i-- > 0;) {
        if (a.limbs_[i] != b.limbs_[i]) {
            return a.limbs_[i] < b.limbs_[i] ? -1 : 1;
        }
    }

    return 0;
}

// =============================================================================
// Addition/Subtraction of Magnitudes
// =============================================================================

BigInt BigInt::add_magnitudes(const BigInt& a, const BigInt& b) {
    const BigInt& larger = a.limbs_.size() >= b.limbs_.size() ? a : b;
    const BigInt& smaller = a.limbs_.size() >= b.limbs_.size() ? b : a;

    BigInt result;
    result.limbs_.reserve(larger.limbs_.size() + 1);

    limb_type carry = 0;
    for (std::size_t i = 0; i < larger.limbs_.size(); ++i) {
        limb_type b_val = i < smaller.limbs_.size() ? smaller.limbs_[i] : 0;
        auto [sum, new_carry] = add_with_carry(larger.limbs_[i], b_val, carry);
        result.limbs_.push_back(sum);
        carry = new_carry;
    }

    if (carry != 0) {
        result.limbs_.push_back(carry);
    }

    return result;
}

BigInt BigInt::sub_magnitudes(const BigInt& a, const BigInt& b) {
    // Assumes |a| >= |b|
    BigInt result;
    result.limbs_.reserve(a.limbs_.size());

    limb_type borrow = 0;
    for (std::size_t i = 0; i < a.limbs_.size(); ++i) {
        limb_type b_val = i < b.limbs_.size() ? b.limbs_[i] : 0;
        auto [diff, new_borrow] = sub_with_borrow(a.limbs_[i], b_val, borrow);
        result.limbs_.push_back(diff);
        borrow = new_borrow;
    }

    assert(borrow == 0 && "sub_magnitudes: a was smaller than b");
    result.normalize();
    return result;
}

// =============================================================================
// Arithmetic Operators
// =============================================================================

BigInt BigInt::operator+(const BigInt& rhs) const {
    if (is_zero()) return rhs;
    if (rhs.is_zero()) return *this;

    // Same sign: add magnitudes
    if (negative_ == rhs.negative_) {
        BigInt result = add_magnitudes(*this, rhs);
        result.negative_ = negative_;
        return result;
    }

    // Different signs: subtract magnitudes
    int cmp = compare_magnitudes(*this, rhs);
    if (cmp == 0) {
        return BigInt{};  // Zero
    }

    if (cmp > 0) {
        BigInt result = sub_magnitudes(*this, rhs);
        result.negative_ = negative_;
        return result;
    } else {
        BigInt result = sub_magnitudes(rhs, *this);
        result.negative_ = rhs.negative_;
        return result;
    }
}

BigInt BigInt::operator-(const BigInt& rhs) const {
    if (rhs.is_zero()) return *this;
    if (is_zero()) return -rhs;

    // a - b = a + (-b)
    BigInt neg_rhs = rhs;
    neg_rhs.negative_ = !neg_rhs.negative_;
    return *this + neg_rhs;
}

BigInt BigInt::operator-() const {
    if (is_zero()) return *this;
    BigInt result = *this;
    result.negative_ = !result.negative_;
    return result;
}

BigInt BigInt::operator+() const {
    return *this;
}

// =============================================================================
// Multiplication
// =============================================================================

BigInt BigInt::multiply_simple(const BigInt& a, const BigInt& b) {
    if (a.is_zero() || b.is_zero()) {
        return BigInt{};
    }

    BigInt result;
    result.limbs_.resize(a.limbs_.size() + b.limbs_.size(), 0);

    for (std::size_t i = 0; i < a.limbs_.size(); ++i) {
        limb_type carry = 0;
        for (std::size_t j = 0; j < b.limbs_.size(); ++j) {
            auto [lo, hi] = mul_add_with_carry(a.limbs_[i], b.limbs_[j], result.limbs_[i + j], carry);
            result.limbs_[i + j] = lo;
            carry = hi;
        }
        result.limbs_[i + b.limbs_.size()] = carry;
    }

    result.normalize();
    return result;
}

BigInt BigInt::multiply_karatsuba(const BigInt& a, const BigInt& b) {
    // Base case: use simple multiplication for small numbers
    if (a.limbs_.size() < KARATSUBA_THRESHOLD || b.limbs_.size() < KARATSUBA_THRESHOLD) {
        return multiply_simple(a, b);
    }

    // Find the split point (half of the larger number)
    std::size_t m = std::max(a.limbs_.size(), b.limbs_.size()) / 2;

    // Split a = a1 * B^m + a0
    BigInt a0, a1;
    a0.limbs_.assign(a.limbs_.begin(), a.limbs_.begin() + static_cast<std::ptrdiff_t>(std::min(m, a.limbs_.size())));
    if (m < a.limbs_.size()) {
        a1.limbs_.assign(a.limbs_.begin() + static_cast<std::ptrdiff_t>(m), a.limbs_.end());
    }
    a0.normalize();
    a1.normalize();

    // Split b = b1 * B^m + b0
    BigInt b0, b1;
    b0.limbs_.assign(b.limbs_.begin(), b.limbs_.begin() + static_cast<std::ptrdiff_t>(std::min(m, b.limbs_.size())));
    if (m < b.limbs_.size()) {
        b1.limbs_.assign(b.limbs_.begin() + static_cast<std::ptrdiff_t>(m), b.limbs_.end());
    }
    b0.normalize();
    b1.normalize();

    // z0 = a0 * b0
    // z2 = a1 * b1
    // z1 = (a0 + a1) * (b0 + b1) - z0 - z2
    BigInt z0 = multiply_karatsuba(a0, b0);
    BigInt z2 = multiply_karatsuba(a1, b1);
    BigInt z1 = multiply_karatsuba(a0 + a1, b0 + b1) - z0 - z2;

    // result = z2 * B^(2m) + z1 * B^m + z0
    BigInt result = z0;

    // Add z1 * B^m
    if (!z1.is_zero()) {
        BigInt z1_shifted;
        z1_shifted.limbs_.resize(m, 0);
        z1_shifted.limbs_.insert(z1_shifted.limbs_.end(), z1.limbs_.begin(), z1.limbs_.end());
        result = result + z1_shifted;
    }

    // Add z2 * B^(2m)
    if (!z2.is_zero()) {
        BigInt z2_shifted;
        z2_shifted.limbs_.resize(2 * m, 0);
        z2_shifted.limbs_.insert(z2_shifted.limbs_.end(), z2.limbs_.begin(), z2.limbs_.end());
        result = result + z2_shifted;
    }

    return result;
}

BigInt BigInt::operator*(const BigInt& rhs) const {
    if (is_zero() || rhs.is_zero()) {
        return BigInt{};
    }

    BigInt result;
    if (limbs_.size() >= KARATSUBA_THRESHOLD && rhs.limbs_.size() >= KARATSUBA_THRESHOLD) {
        result = multiply_karatsuba(*this, rhs);
    } else {
        result = multiply_simple(*this, rhs);
    }

    result.negative_ = negative_ != rhs.negative_;
    return result;
}

// =============================================================================
// Division (Knuth's Algorithm D)
// =============================================================================

std::pair<BigInt, BigInt> BigInt::divide_knuth(const BigInt& dividend, const BigInt& divisor) {
    // This implements a simplified version of Knuth's Algorithm D
    // For efficiency with very large numbers

    if (divisor.is_zero()) {
        throw DivisionByZeroError();
    }

    int cmp = compare_magnitudes(dividend, divisor);
    if (cmp < 0) {
        return {BigInt{}, dividend};
    }
    if (cmp == 0) {
        return {BigInt{1}, BigInt{}};
    }

    // Single limb divisor - use simple division
    if (divisor.limbs_.size() == 1) {
        BigInt quotient;
        quotient.limbs_.resize(dividend.limbs_.size());

        limb_type remainder = 0;
        for (std::size_t i = dividend.limbs_.size(); i-- > 0;) {
            auto [q, r] = div_wide(remainder, dividend.limbs_[i], divisor.limbs_[0]);
            quotient.limbs_[i] = q;
            remainder = r;
        }

        quotient.normalize();

        BigInt rem;
        if (remainder != 0) {
            rem.limbs_.push_back(remainder);
        }

        return {quotient, rem};
    }

    // Multi-limb division using grade-school long division
    // Normalize: shift so that the leading bit of divisor is 1
    int shift = clz(divisor.limbs_.back());

    BigInt u = dividend << static_cast<std::size_t>(shift);
    BigInt v = divisor << static_cast<std::size_t>(shift);

    std::size_t n = v.limbs_.size();
    std::size_t m = u.limbs_.size() - n;

    BigInt quotient;
    quotient.limbs_.resize(m + 1, 0);

    // Ensure u has an extra leading limb
    u.limbs_.push_back(0);

    for (std::size_t j = m + 1; j-- > 0;) {
        // Estimate quotient digit
        dlimb_t uu = (static_cast<dlimb_t>(u.limbs_[j + n]) << limb_bits) + u.limbs_[j + n - 1];
        dlimb_t qhat = uu / v.limbs_[n - 1];
        dlimb_t rhat = uu % v.limbs_[n - 1];

        // Adjust qhat
        while (qhat >= LIMB_BASE ||
               (n >= 2 && qhat * v.limbs_[n - 2] > (rhat << limb_bits) + u.get_limb(j + n - 2))) {
            --qhat;
            rhat += v.limbs_[n - 1];
            if (rhat >= LIMB_BASE) break;
        }

        // Multiply and subtract
        limb_type borrow = 0;
        for (std::size_t i = 0; i < n; ++i) {
            auto [lo, hi] = mul_with_carry(static_cast<limb_type>(qhat), v.limbs_[i], 0);
            auto [diff, b1] = sub_with_borrow(u.limbs_[j + i], lo, borrow);
            u.limbs_[j + i] = diff;
            borrow = hi + b1;
        }
        auto [diff, b] = sub_with_borrow(u.limbs_[j + n], borrow, 0);
        u.limbs_[j + n] = diff;

        quotient.limbs_[j] = static_cast<limb_type>(qhat);

        // If we subtracted too much, add back
        if (b != 0) {
            --quotient.limbs_[j];
            limb_type carry = 0;
            for (std::size_t i = 0; i < n; ++i) {
                auto [sum, c] = add_with_carry(u.limbs_[j + i], v.limbs_[i], carry);
                u.limbs_[j + i] = sum;
                carry = c;
            }
            u.limbs_[j + n] += carry;
        }
    }

    quotient.normalize();

    // Remainder needs to be un-normalized
    u.normalize();
    BigInt remainder = u >> static_cast<std::size_t>(shift);

    return {quotient, remainder};
}

std::pair<BigInt, BigInt> BigInt::divmod(const BigInt& divisor) const {
    if (divisor.is_zero()) {
        throw DivisionByZeroError();
    }

    if (is_zero()) {
        return {BigInt{}, BigInt{}};
    }

    auto [q, r] = divide_knuth(abs(), divisor.abs());

    // Adjust signs
    // Quotient is negative if signs differ
    if (negative_ != divisor.negative_ && !q.is_zero()) {
        q.negative_ = true;
    }

    // Remainder has the sign of the dividend
    if (negative_ && !r.is_zero()) {
        r.negative_ = true;
    }

    return {q, r};
}

BigInt BigInt::operator/(const BigInt& rhs) const {
    return divmod(rhs).first;
}

BigInt BigInt::operator%(const BigInt& rhs) const {
    return divmod(rhs).second;
}

// =============================================================================
// Compound Assignment Operators
// =============================================================================

BigInt& BigInt::operator+=(const BigInt& rhs) {
    *this = *this + rhs;
    return *this;
}

BigInt& BigInt::operator-=(const BigInt& rhs) {
    *this = *this - rhs;
    return *this;
}

BigInt& BigInt::operator*=(const BigInt& rhs) {
    *this = *this * rhs;
    return *this;
}

BigInt& BigInt::operator/=(const BigInt& rhs) {
    *this = *this / rhs;
    return *this;
}

BigInt& BigInt::operator%=(const BigInt& rhs) {
    *this = *this % rhs;
    return *this;
}

// =============================================================================
// Increment/Decrement
// =============================================================================

BigInt& BigInt::operator++() {
    *this += BigInt{1};
    return *this;
}

BigInt BigInt::operator++(int) {
    BigInt tmp = *this;
    ++(*this);
    return tmp;
}

BigInt& BigInt::operator--() {
    *this -= BigInt{1};
    return *this;
}

BigInt BigInt::operator--(int) {
    BigInt tmp = *this;
    --(*this);
    return tmp;
}

// =============================================================================
// Bitwise Operations
// =============================================================================

BigInt BigInt::operator<<(std::size_t shift) const {
    if (is_zero() || shift == 0) {
        return *this;
    }

    std::size_t limb_shift = shift / limb_bits;
    std::size_t bit_shift = shift % limb_bits;

    BigInt result;
    result.limbs_.resize(limbs_.size() + limb_shift + 1, 0);

    // Copy with limb shift
    for (std::size_t i = 0; i < limbs_.size(); ++i) {
        result.limbs_[i + limb_shift] = limbs_[i];
    }

    // Bit shift within limbs
    if (bit_shift != 0) {
        limb_type carry = 0;
        for (std::size_t i = limb_shift; i < result.limbs_.size(); ++i) {
            limb_type new_carry = result.limbs_[i] >> (limb_bits - bit_shift);
            result.limbs_[i] = (result.limbs_[i] << bit_shift) | carry;
            carry = new_carry;
        }
    }

    result.negative_ = negative_;
    result.normalize();
    return result;
}

BigInt BigInt::operator>>(std::size_t shift) const {
    if (is_zero() || shift == 0) {
        return *this;
    }

    std::size_t limb_shift = shift / limb_bits;
    std::size_t bit_shift = shift % limb_bits;

    if (limb_shift >= limbs_.size()) {
        return BigInt{};
    }

    BigInt result;
    result.limbs_.resize(limbs_.size() - limb_shift);

    // Copy with limb shift
    for (std::size_t i = 0; i < result.limbs_.size(); ++i) {
        result.limbs_[i] = limbs_[i + limb_shift];
    }

    // Bit shift within limbs
    if (bit_shift != 0) {
        limb_type carry = 0;
        for (std::size_t i = result.limbs_.size(); i-- > 0;) {
            limb_type new_carry = result.limbs_[i] << (limb_bits - bit_shift);
            result.limbs_[i] = (result.limbs_[i] >> bit_shift) | carry;
            carry = new_carry;
        }
    }

    result.negative_ = negative_;
    result.normalize();
    return result;
}

BigInt BigInt::operator&(const BigInt& rhs) const {
    if (is_zero() || rhs.is_zero()) {
        return BigInt{};
    }

    BigInt result;
    std::size_t min_size = std::min(limbs_.size(), rhs.limbs_.size());
    result.limbs_.reserve(min_size);

    for (std::size_t i = 0; i < min_size; ++i) {
        result.limbs_.push_back(limbs_[i] & rhs.limbs_[i]);
    }

    result.normalize();
    return result;
}

BigInt BigInt::operator|(const BigInt& rhs) const {
    if (is_zero()) return rhs;
    if (rhs.is_zero()) return *this;

    BigInt result;
    std::size_t max_size = std::max(limbs_.size(), rhs.limbs_.size());
    result.limbs_.reserve(max_size);

    for (std::size_t i = 0; i < max_size; ++i) {
        result.limbs_.push_back(get_limb(i) | rhs.get_limb(i));
    }

    result.normalize();
    return result;
}

BigInt BigInt::operator^(const BigInt& rhs) const {
    if (is_zero()) return rhs;
    if (rhs.is_zero()) return *this;

    BigInt result;
    std::size_t max_size = std::max(limbs_.size(), rhs.limbs_.size());
    result.limbs_.reserve(max_size);

    for (std::size_t i = 0; i < max_size; ++i) {
        result.limbs_.push_back(get_limb(i) ^ rhs.get_limb(i));
    }

    result.normalize();
    return result;
}

// =============================================================================
// Extended Operations
// =============================================================================

BigInt BigInt::pow(std::uint64_t exponent) const {
    if (exponent == 0) {
        return BigInt{1};
    }

    if (is_zero()) {
        return BigInt{};
    }

    if (exponent == 1) {
        return *this;
    }

    // Square-and-multiply algorithm
    BigInt result{1};
    BigInt base = *this;

    while (exponent > 0) {
        if (exponent & 1) {
            result *= base;
        }
        base *= base;
        exponent >>= 1;
    }

    return result;
}

BigInt BigInt::gcd(const BigInt& a, const BigInt& b) {
    // Binary GCD (Stein's algorithm)
    if (a.is_zero()) return b.abs();
    if (b.is_zero()) return a.abs();

    BigInt u = a.abs();
    BigInt v = b.abs();

    // Find common factors of 2
    std::size_t shift = 0;
    while (!u.get_bit(0) && !v.get_bit(0)) {
        u = u >> 1;
        v = v >> 1;
        ++shift;
    }

    // Remove remaining factors of 2 from u
    while (!u.get_bit(0)) {
        u = u >> 1;
    }

    do {
        // Remove factors of 2 from v
        while (!v.get_bit(0)) {
            v = v >> 1;
        }

        // Now u and v are both odd, so u - v is even
        if (u > v) {
            std::swap(u, v);
        }
        v = v - u;
    } while (!v.is_zero());

    return u << shift;
}

BigInt BigInt::lcm(const BigInt& a, const BigInt& b) {
    if (a.is_zero() || b.is_zero()) {
        return BigInt{};
    }
    return (a.abs() / gcd(a, b)) * b.abs();
}

BigInt BigInt::mod_pow(const BigInt& exp, const BigInt& mod) const {
    if (mod.is_zero()) {
        throw DivisionByZeroError();
    }

    if (mod == BigInt{1}) {
        return BigInt{};
    }

    if (exp.is_zero()) {
        return BigInt{1};
    }

    if (exp.is_negative()) {
        throw std::invalid_argument("mod_pow: negative exponent not supported");
    }

    // Square-and-multiply with modular reduction
    BigInt result{1};
    BigInt base = *this % mod;

    // Make base positive
    if (base.is_negative()) {
        base = base + mod;
    }

    BigInt e = exp;
    while (!e.is_zero()) {
        if (e.get_bit(0)) {
            result = (result * base) % mod;
        }
        base = (base * base) % mod;
        e = e >> 1;
    }

    return result;
}

BigInt BigInt::abs() const {
    BigInt result = *this;
    result.negative_ = false;
    return result;
}

// =============================================================================
// Conversion to String
// =============================================================================

std::string BigInt::to_string() const {
    if (is_zero()) {
        return "0";
    }

    // For efficiency, we convert in chunks
    std::string result;
    BigInt temp = abs();

    // Divisor for extracting decimal digits (10^18 fits in a limb)
    constexpr limb_type chunk_divisor = 1'000'000'000'000'000'000ULL;
    constexpr int chunk_digits = 18;

    std::vector<std::string> chunks;

    while (!temp.is_zero()) {
        auto [q, r] = temp.divmod(BigInt{chunk_divisor});
        limb_type remainder = r.is_zero() ? 0 : r.limbs_[0];

        // Convert remainder to string
        std::string chunk = std::to_string(remainder);
        chunks.push_back(chunk);

        temp = q;
    }

    // Build result from chunks (reverse order)
    result = chunks.back();  // First chunk doesn't need padding
    for (std::size_t i = chunks.size() - 1; i-- > 0;) {
        // Pad with leading zeros
        std::string padded = std::string(static_cast<std::size_t>(chunk_digits) - chunks[i].length(), '0') + chunks[i];
        result += padded;
    }

    if (negative_) {
        result = "-" + result;
    }

    return result;
}

std::string BigInt::to_hex_string() const {
    if (is_zero()) {
        return "0x0";
    }

    std::string result;
    result.reserve(limbs_.size() * 16 + 3);

    // Convert each limb to hex, starting from the most significant
    bool first = true;
    for (std::size_t i = limbs_.size(); i-- > 0;) {
        std::array<char, 17> buf{};
        auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + 16, limbs_[i], 16);

        if (first) {
            result.append(buf.data(), static_cast<std::size_t>(ptr - buf.data()));
            first = false;
        } else {
            // Pad with leading zeros
            std::size_t len = static_cast<std::size_t>(ptr - buf.data());
            result.append(16 - len, '0');
            result.append(buf.data(), len);
        }
    }

    result = (negative_ ? "-0x" : "0x") + result;
    return result;
}

// =============================================================================
// Factory Methods
// =============================================================================

BigInt BigInt::from_limbs(std::vector<limb_type> limbs, bool negative) {
    BigInt result;
    result.limbs_ = std::move(limbs);
    result.negative_ = negative;
    result.normalize();
    return result;
}

BigInt BigInt::factorial(std::uint64_t n) {
    if (n <= 1) {
        return BigInt{1};
    }

    BigInt result{1};
    for (std::uint64_t i = 2; i <= n; ++i) {
        result *= BigInt{i};
    }
    return result;
}

BigInt BigInt::fibonacci(std::uint64_t n) {
    if (n == 0) {
        return BigInt{};
    }
    if (n == 1) {
        return BigInt{1};
    }

    // Fast doubling method
    BigInt a{};
    BigInt b{1};

    // Find the highest set bit
    std::uint64_t mask = std::uint64_t{1} << 63;
    while (!(mask & n)) {
        mask >>= 1;
    }

    while (mask > 0) {
        // F(2k) = F(k) * (2*F(k+1) - F(k))
        // F(2k+1) = F(k+1)^2 + F(k)^2
        BigInt c = a * ((b << 1) - a);
        BigInt d = a * a + b * b;

        a = c;
        b = d;

        if (n & mask) {
            BigInt tmp = a + b;
            a = b;
            b = tmp;
        }

        mask >>= 1;
    }

    return a;
}

}  // namespace dotvm::core::bigint
