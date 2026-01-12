#pragma once

/// @file alu.hpp
/// @brief Arithmetic Logic Unit with architecture-aware operations
///
/// This header provides the ALU class which performs integer arithmetic and
/// bitwise operations with automatic result masking based on the target
/// architecture. In 32-bit mode (Arch32), all results are masked to 32 bits
/// with proper sign extension, implementing standard 32-bit wrap-around semantics.

#include <cstdint>

#include "arch_config.hpp"
#include "arch_types.hpp"
#include "value.hpp"

namespace dotvm::core {

/// Arithmetic Logic Unit - performs architecture-aware integer operations
///
/// The ALU automatically masks results to the appropriate bit width based on
/// the configured architecture:
/// - Arch32: Results masked to 32 bits with sign extension (wrap-around on overflow)
/// - Arch64: Results use full 48-bit range (NaN-boxing limit)
///
/// All operations assume integer operands. Behavior is undefined for non-integer Values.
class ALU {
public:
    /// Construct an ALU for the specified architecture
    ///
    /// @param arch Target architecture (default: Arch64)
    explicit constexpr ALU(Architecture arch = Architecture::Arch64) noexcept
        : arch_{arch} {}

    // =========================================================================
    // Configuration
    // =========================================================================

    /// Get the current architecture
    [[nodiscard]] constexpr Architecture arch() const noexcept { return arch_; }

    /// Set the architecture
    constexpr void set_arch(Architecture arch) noexcept { arch_ = arch; }

    // =========================================================================
    // Basic Arithmetic Operations
    // =========================================================================

    /// Add two integer Values
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return Sum, masked to architecture width
    [[nodiscard]] constexpr Value add(Value a, Value b) const noexcept {
        auto result = a.as_integer() + b.as_integer();
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Subtract two integer Values
    ///
    /// @param a Minuend (integer)
    /// @param b Subtrahend (integer)
    /// @return Difference (a - b), masked to architecture width
    [[nodiscard]] constexpr Value sub(Value a, Value b) const noexcept {
        auto result = a.as_integer() - b.as_integer();
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Multiply two integer Values
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return Product, masked to architecture width
    [[nodiscard]] constexpr Value mul(Value a, Value b) const noexcept {
        auto result = a.as_integer() * b.as_integer();
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Divide two integer Values (truncated toward zero)
    ///
    /// Division by zero returns zero (no exception).
    ///
    /// @param a Dividend (integer)
    /// @param b Divisor (integer)
    /// @return Quotient (a / b), masked to architecture width
    [[nodiscard]] constexpr Value div(Value a, Value b) const noexcept {
        auto divisor = b.as_integer();
        if (divisor == 0) {
            return Value::from_int(0);  // Division by zero returns 0
        }
        auto result = a.as_integer() / divisor;
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Modulo (remainder) of two integer Values
    ///
    /// Division by zero returns zero (no exception).
    ///
    /// @param a Dividend (integer)
    /// @param b Divisor (integer)
    /// @return Remainder (a % b), masked to architecture width
    [[nodiscard]] constexpr Value mod(Value a, Value b) const noexcept {
        auto divisor = b.as_integer();
        if (divisor == 0) {
            return Value::from_int(0);  // Mod by zero returns 0
        }
        auto result = a.as_integer() % divisor;
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Negate an integer Value
    ///
    /// @param a Operand (integer)
    /// @return Negated value (-a), masked to architecture width
    [[nodiscard]] constexpr Value neg(Value a) const noexcept {
        auto result = -a.as_integer();
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Absolute value of an integer Value
    ///
    /// @param a Operand (integer)
    /// @return Absolute value (|a|), masked to architecture width
    [[nodiscard]] constexpr Value abs(Value a) const noexcept {
        auto val = a.as_integer();
        auto result = val < 0 ? -val : val;
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    // =========================================================================
    // Bitwise Operations
    // =========================================================================

    /// Bitwise AND of two integer Values
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return Bitwise AND result, masked to architecture width
    [[nodiscard]] constexpr Value bit_and(Value a, Value b) const noexcept {
        auto result = a.as_integer() & b.as_integer();
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Bitwise OR of two integer Values
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return Bitwise OR result, masked to architecture width
    [[nodiscard]] constexpr Value bit_or(Value a, Value b) const noexcept {
        auto result = a.as_integer() | b.as_integer();
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Bitwise XOR of two integer Values
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return Bitwise XOR result, masked to architecture width
    [[nodiscard]] constexpr Value bit_xor(Value a, Value b) const noexcept {
        auto result = a.as_integer() ^ b.as_integer();
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Bitwise NOT (complement) of an integer Value
    ///
    /// @param a Operand (integer)
    /// @return Bitwise NOT result (~a), masked to architecture width
    [[nodiscard]] constexpr Value bit_not(Value a) const noexcept {
        auto result = ~a.as_integer();
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    // =========================================================================
    // Shift Operations
    // =========================================================================

    /// Shift left (logical)
    ///
    /// Shift amount is masked to the valid range for the architecture:
    /// - Arch32: 0-31
    /// - Arch64: 0-47
    ///
    /// @param a Value to shift (integer)
    /// @param b Shift amount (integer)
    /// @return Left-shifted result, masked to architecture width
    [[nodiscard]] constexpr Value shl(Value a, Value b) const noexcept {
        auto shift = arch_config::mask_shift(b.as_integer(), arch_);
        auto result = a.as_integer() << shift;
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Shift right logical (unsigned)
    ///
    /// Performs unsigned (logical) right shift - zeros are shifted in from the left.
    /// Shift amount is masked to the valid range for the architecture.
    ///
    /// @param a Value to shift (integer, treated as unsigned)
    /// @param b Shift amount (integer)
    /// @return Right-shifted result, masked to architecture width
    [[nodiscard]] constexpr Value shr(Value a, Value b) const noexcept {
        auto shift = arch_config::mask_shift(b.as_integer(), arch_);

        // Treat the value as unsigned for logical shift
        std::uint64_t ua = static_cast<std::uint64_t>(a.as_integer());

        // Mask to architecture width before shifting
        if (arch_config::is_arch32(arch_)) {
            ua &= arch_config::UINT32_MASK;
        }

        auto result = static_cast<std::int64_t>(ua >> shift);
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    /// Shift right arithmetic (signed)
    ///
    /// Performs signed (arithmetic) right shift - sign bit is replicated.
    /// Shift amount is masked to the valid range for the architecture.
    ///
    /// @param a Value to shift (integer, treated as signed)
    /// @param b Shift amount (integer)
    /// @return Right-shifted result, masked to architecture width
    [[nodiscard]] constexpr Value sar(Value a, Value b) const noexcept {
        auto shift = arch_config::mask_shift(b.as_integer(), arch_);

        // For arithmetic shift, we need to handle the sign properly for 32-bit
        std::int64_t val = a.as_integer();
        if (arch_config::is_arch32(arch_)) {
            // First mask to 32-bit (with sign extension to get proper signed value)
            val = arch_config::mask_int32(val);
        }

        auto result = val >> shift;  // C++ guarantees arithmetic shift for signed
        return Value::from_int(arch_config::mask_int(result, arch_));
    }

    // =========================================================================
    // Rotate Operations
    // =========================================================================

    /// Rotate left
    ///
    /// Rotates bits left by the specified amount. Bits shifted out from the
    /// left are wrapped around to the right. Rotate amount is taken modulo
    /// the architecture width (32 for Arch32, 64 for Arch64).
    ///
    /// @param a Value to rotate (integer)
    /// @param b Rotate amount (integer)
    /// @return Rotated result, masked to architecture width
    [[nodiscard]] constexpr Value rol(Value a, Value b) const noexcept {
        std::uint64_t val = static_cast<std::uint64_t>(a.as_integer());

        if (arch_config::is_arch32(arch_)) {
            val &= arch_config::UINT32_MASK;
            // Use unsigned modulo for positive result
            auto rotate = static_cast<int>(static_cast<std::uint64_t>(b.as_integer()) % 32U);
            if (rotate == 0) {
                return Value::from_int(arch_config::mask_int(
                    static_cast<std::int64_t>(val), arch_));
            }
            auto result = ((val << rotate) | (val >> (32 - rotate))) &
                          arch_config::UINT32_MASK;
            return Value::from_int(arch_config::mask_int(
                static_cast<std::int64_t>(result), arch_));
        } else {
            // Use unsigned modulo for positive result
            auto rotate = static_cast<int>(static_cast<std::uint64_t>(b.as_integer()) % 64U);
            if (rotate == 0) {
                return Value::from_int(static_cast<std::int64_t>(val));
            }
            auto result = (val << rotate) | (val >> (64 - rotate));
            return Value::from_int(static_cast<std::int64_t>(result));
        }
    }

    /// Rotate right
    ///
    /// Rotates bits right by the specified amount. Bits shifted out from the
    /// right are wrapped around to the left. Rotate amount is taken modulo
    /// the architecture width (32 for Arch32, 64 for Arch64).
    ///
    /// @param a Value to rotate (integer)
    /// @param b Rotate amount (integer)
    /// @return Rotated result, masked to architecture width
    [[nodiscard]] constexpr Value ror(Value a, Value b) const noexcept {
        std::uint64_t val = static_cast<std::uint64_t>(a.as_integer());

        if (arch_config::is_arch32(arch_)) {
            val &= arch_config::UINT32_MASK;
            // Use unsigned modulo for positive result
            auto rotate = static_cast<int>(static_cast<std::uint64_t>(b.as_integer()) % 32U);
            if (rotate == 0) {
                return Value::from_int(arch_config::mask_int(
                    static_cast<std::int64_t>(val), arch_));
            }
            auto result = ((val >> rotate) | (val << (32 - rotate))) &
                          arch_config::UINT32_MASK;
            return Value::from_int(arch_config::mask_int(
                static_cast<std::int64_t>(result), arch_));
        } else {
            // Use unsigned modulo for positive result
            auto rotate = static_cast<int>(static_cast<std::uint64_t>(b.as_integer()) % 64U);
            if (rotate == 0) {
                return Value::from_int(static_cast<std::int64_t>(val));
            }
            auto result = (val >> rotate) | (val << (64 - rotate));
            return Value::from_int(static_cast<std::int64_t>(result));
        }
    }

    // =========================================================================
    // Comparison Operations
    // =========================================================================

    /// Compare equal
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return 1 if a == b, 0 otherwise
    [[nodiscard]] constexpr Value cmp_eq(Value a, Value b) const noexcept {
        return Value::from_int(a.as_integer() == b.as_integer() ? 1 : 0);
    }

    /// Compare not equal
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return 1 if a != b, 0 otherwise
    [[nodiscard]] constexpr Value cmp_ne(Value a, Value b) const noexcept {
        return Value::from_int(a.as_integer() != b.as_integer() ? 1 : 0);
    }

    /// Compare less than (signed)
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return 1 if a < b, 0 otherwise
    [[nodiscard]] constexpr Value cmp_lt(Value a, Value b) const noexcept {
        // For Arch32, mask values first to ensure proper signed comparison
        auto av = arch_config::mask_int(a.as_integer(), arch_);
        auto bv = arch_config::mask_int(b.as_integer(), arch_);
        return Value::from_int(av < bv ? 1 : 0);
    }

    /// Compare less than or equal (signed)
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return 1 if a <= b, 0 otherwise
    [[nodiscard]] constexpr Value cmp_le(Value a, Value b) const noexcept {
        auto av = arch_config::mask_int(a.as_integer(), arch_);
        auto bv = arch_config::mask_int(b.as_integer(), arch_);
        return Value::from_int(av <= bv ? 1 : 0);
    }

    /// Compare greater than (signed)
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return 1 if a > b, 0 otherwise
    [[nodiscard]] constexpr Value cmp_gt(Value a, Value b) const noexcept {
        auto av = arch_config::mask_int(a.as_integer(), arch_);
        auto bv = arch_config::mask_int(b.as_integer(), arch_);
        return Value::from_int(av > bv ? 1 : 0);
    }

    /// Compare greater than or equal (signed)
    ///
    /// @param a First operand (integer)
    /// @param b Second operand (integer)
    /// @return 1 if a >= b, 0 otherwise
    [[nodiscard]] constexpr Value cmp_ge(Value a, Value b) const noexcept {
        auto av = arch_config::mask_int(a.as_integer(), arch_);
        auto bv = arch_config::mask_int(b.as_integer(), arch_);
        return Value::from_int(av >= bv ? 1 : 0);
    }

    // =========================================================================
    // Unsigned Comparison Operations
    // =========================================================================

    /// Compare less than (unsigned)
    ///
    /// @param a First operand (integer, treated as unsigned)
    /// @param b Second operand (integer, treated as unsigned)
    /// @return 1 if a < b (unsigned), 0 otherwise
    [[nodiscard]] constexpr Value cmp_ltu(Value a, Value b) const noexcept {
        auto au = arch_config::mask_uint(
            static_cast<std::uint64_t>(a.as_integer()), arch_);
        auto bu = arch_config::mask_uint(
            static_cast<std::uint64_t>(b.as_integer()), arch_);
        return Value::from_int(au < bu ? 1 : 0);
    }

    /// Compare less than or equal (unsigned)
    ///
    /// @param a First operand (integer, treated as unsigned)
    /// @param b Second operand (integer, treated as unsigned)
    /// @return 1 if a <= b (unsigned), 0 otherwise
    [[nodiscard]] constexpr Value cmp_leu(Value a, Value b) const noexcept {
        auto au = arch_config::mask_uint(
            static_cast<std::uint64_t>(a.as_integer()), arch_);
        auto bu = arch_config::mask_uint(
            static_cast<std::uint64_t>(b.as_integer()), arch_);
        return Value::from_int(au <= bu ? 1 : 0);
    }

    /// Compare greater than (unsigned)
    ///
    /// @param a First operand (integer, treated as unsigned)
    /// @param b Second operand (integer, treated as unsigned)
    /// @return 1 if a > b (unsigned), 0 otherwise
    [[nodiscard]] constexpr Value cmp_gtu(Value a, Value b) const noexcept {
        auto au = arch_config::mask_uint(
            static_cast<std::uint64_t>(a.as_integer()), arch_);
        auto bu = arch_config::mask_uint(
            static_cast<std::uint64_t>(b.as_integer()), arch_);
        return Value::from_int(au > bu ? 1 : 0);
    }

    /// Compare greater than or equal (unsigned)
    ///
    /// @param a First operand (integer, treated as unsigned)
    /// @param b Second operand (integer, treated as unsigned)
    /// @return 1 if a >= b (unsigned), 0 otherwise
    [[nodiscard]] constexpr Value cmp_geu(Value a, Value b) const noexcept {
        auto au = arch_config::mask_uint(
            static_cast<std::uint64_t>(a.as_integer()), arch_);
        auto bu = arch_config::mask_uint(
            static_cast<std::uint64_t>(b.as_integer()), arch_);
        return Value::from_int(au >= bu ? 1 : 0);
    }

private:
    Architecture arch_;
};

}  // namespace dotvm::core
