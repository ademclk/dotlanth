#pragma once

#include <atomic>
#include <bit>
#include <cassert>
#include <compare>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#include "arch_config.hpp"  // for masking functions
#include "arch_types.hpp"   // for Architecture enum

namespace dotvm::core {

// NaN-boxing constants
namespace nan_box {
/// Quiet NaN prefix - all tagged values have this prefix
inline constexpr std::uint64_t QNAN_PREFIX = 0x7FF8'0000'0000'0000ULL;

/// Mask for extracting the type tag (bits 48-63 minus QNAN bits)
inline constexpr std::uint64_t TAG_MASK = 0x0000'FFFF'0000'0000ULL;

/// Mask for 32-bit payload (used by Handle index)
inline constexpr std::uint64_t PAYLOAD_MASK = 0x0000'0000'FFFF'FFFFULL;

/// Mask for full 48-bit payload
inline constexpr std::uint64_t FULL_PAYLOAD = 0x0000'FFFF'FFFF'FFFFULL;

/// Type tags - stored in bits 48-51 (after QNAN_PREFIX)
inline constexpr std::uint64_t TAG_INTEGER = 0x0001ULL << 48;
inline constexpr std::uint64_t TAG_BOOL = 0x0002ULL << 48;
inline constexpr std::uint64_t TAG_HANDLE = 0x0003ULL << 48;
inline constexpr std::uint64_t TAG_NIL = 0x0004ULL << 48;
inline constexpr std::uint64_t TAG_POINTER = 0x0005ULL << 48;

/// Full prefixes for each type (QNAN_PREFIX | TAG)
inline constexpr std::uint64_t INTEGER_PREFIX = QNAN_PREFIX | TAG_INTEGER;
inline constexpr std::uint64_t BOOL_PREFIX = QNAN_PREFIX | TAG_BOOL;
inline constexpr std::uint64_t HANDLE_PREFIX = QNAN_PREFIX | TAG_HANDLE;
inline constexpr std::uint64_t NIL_VALUE = QNAN_PREFIX | TAG_NIL;
inline constexpr std::uint64_t POINTER_PREFIX = QNAN_PREFIX | TAG_POINTER;

/// Masks for type checking
inline constexpr std::uint64_t TYPE_CHECK_MASK = 0x7FFF'0000'0000'0000ULL;
inline constexpr std::uint64_t BOOL_CHECK_MASK = 0x7FFF'FFFF'FFFF'FFFEULL;

/// Mask for tag bits (bits 48-50, where type tags are encoded)
/// Used to distinguish canonical NaN from tagged types
inline constexpr std::uint64_t TAG_BITS_MASK = 0x0007ULL << 48;  // 0x0007'0000'0000'0000

/// Canonical quiet NaN - used when input NaN would conflict with type tags
inline constexpr std::uint64_t CANONICAL_QNAN = QNAN_PREFIX;

/// Maximum generation value that fits in NaN-boxed Handle (16 bits)
inline constexpr std::uint32_t MAX_HANDLE_GENERATION = 0xFFFFU;

/// Number of bits available for Handle generation in NaN-boxing
inline constexpr std::uint32_t HANDLE_GEN_BITS = 16;
}  // namespace nan_box

/// Handle structure: 32-bit index + 32-bit generation
/// @note When stored in a NaN-boxed Value, only 16 bits of generation are preserved.
///       Use nan_box::MAX_HANDLE_GENERATION (0xFFFF) as the upper bound for generation
///       values that will be stored in Values.
struct Handle {
    std::uint32_t index;
    std::uint32_t generation;

    constexpr bool operator==(const Handle&) const noexcept = default;
    constexpr auto operator<=>(const Handle&) const noexcept = default;

    /// Check if this handle's generation fits within NaN-boxing constraints
    [[nodiscard]] constexpr bool fits_in_value() const noexcept {
        return generation <= nan_box::MAX_HANDLE_GENERATION;
    }
};

static_assert(sizeof(Handle) == 8);

/// Value type enumeration
enum class ValueType : std::uint8_t {
    Float = 0,
    Integer = 1,
    Bool = 2,
    Handle = 3,
    Nil = 4,
    Pointer = 5
};

/// Get the string name of a ValueType
[[nodiscard]] constexpr std::string_view type_name(ValueType t) noexcept {
    switch (t) {
        case ValueType::Float:
            return "Float";
        case ValueType::Integer:
            return "Integer";
        case ValueType::Bool:
            return "Bool";
        case ValueType::Handle:
            return "Handle";
        case ValueType::Nil:
            return "Nil";
        case ValueType::Pointer:
            return "Pointer";
    }
    return "Unknown";
}

/// The core NaN-boxed value class
///
/// Uses IEEE 754 NaN-boxing to store multiple types in a single 64-bit value:
/// - Float: Raw IEEE 754 double (non-NaN values)
/// - Integer: 48-bit signed integer with sign extension
/// - Bool: Single bit in payload
/// - Handle: 32-bit index + 16-bit generation
/// - Nil: Special sentinel value
/// - Pointer: 48-bit canonical x86-64 address
///
/// All methods are constexpr and noexcept for maximum efficiency.
class Value {
public:
    /// Default constructor - creates nil
    constexpr Value() noexcept : bits_{nan_box::NIL_VALUE} {}

    /// Construct from double
    /// @note NaN inputs that would conflict with type tags are canonicalized
    ///       to a standard quiet NaN to prevent type confusion.
    constexpr explicit Value(double f) noexcept {
        auto bits = std::bit_cast<std::uint64_t>(f);
        // Canonicalize NaN values that would conflict with our type tags
        // A conflicting NaN has QNAN_PREFIX set AND has non-zero tag bits (bits 48-50)
        if ((bits & nan_box::QNAN_PREFIX) == nan_box::QNAN_PREFIX) {
            if ((bits & nan_box::TAG_BITS_MASK) != 0) {
                // This NaN would be misinterpreted as a tagged type
                bits = nan_box::CANONICAL_QNAN;
            }
        }
        bits_ = bits;
    }

    /// Construct from 64-bit integer
    /// @note Only 48 bits of precision are preserved; values outside
    ///       [-2^47, 2^47-1] will be truncated.
    constexpr explicit Value(std::int64_t i) noexcept
        : bits_{nan_box::INTEGER_PREFIX | (static_cast<std::uint64_t>(i) & nan_box::FULL_PAYLOAD)} {
    }

    /// Construct from boolean
    constexpr explicit Value(bool b) noexcept
        : bits_{nan_box::BOOL_PREFIX | static_cast<std::uint64_t>(b)} {}

    /// Construct from Handle
    /// @note Only 16 bits of generation are preserved in the Value.
    ///       Generations larger than 0xFFFF will be truncated.
    /// @pre h.generation <= nan_box::MAX_HANDLE_GENERATION (asserted in debug builds)
    constexpr explicit Value(Handle h) noexcept
        : bits_{nan_box::HANDLE_PREFIX |
                (static_cast<std::uint64_t>(h.generation & nan_box::MAX_HANDLE_GENERATION) << 32) |
                static_cast<std::uint64_t>(h.index)} {
        assert(h.generation <= nan_box::MAX_HANDLE_GENERATION &&
               "Handle generation exceeds 16-bit NaN-boxing limit");
    }

    /// Construct from raw pointer
    /// @note Only 48 bits of the address are preserved. This is sufficient
    ///       for canonical x86-64 user-space and kernel-space addresses.
    /// @note Not constexpr because reinterpret_cast is not allowed in constant expressions.
    explicit Value(void* ptr) noexcept
        : bits_{nan_box::POINTER_PREFIX |
                (reinterpret_cast<std::uint64_t>(ptr) & nan_box::FULL_PAYLOAD)} {}

    // Factory methods for clarity

    /// Create a Value from a double
    [[nodiscard]] static constexpr Value from_float(double f) noexcept { return Value{f}; }

    /// Create a Value from an integer
    [[nodiscard]] static constexpr Value from_int(std::int64_t i) noexcept { return Value{i}; }

    /// Create an integer Value with architecture-specific masking
    ///
    /// In Arch32 mode, the value is masked to 32 bits and sign-extended.
    /// In Arch64 mode, the value is stored unchanged (using full 48-bit range).
    ///
    /// @param i The integer value
    /// @param arch The target architecture
    /// @return A Value containing the masked integer
    [[nodiscard]] static constexpr Value from_int(std::int64_t i, Architecture arch) noexcept {
        return Value{arch_config::mask_int(i, arch)};
    }

    /// Create a Value from a boolean
    [[nodiscard]] static constexpr Value from_bool(bool b) noexcept { return Value{b}; }

    /// Create a Value from a Handle
    [[nodiscard]] static constexpr Value from_handle(Handle h) noexcept { return Value{h}; }

    /// Create a Value from handle components
    [[nodiscard]] static constexpr Value from_handle(std::uint32_t idx,
                                                     std::uint32_t gen) noexcept {
        return Value{Handle{.index = idx, .generation = gen}};
    }

    /// Create a nil Value
    [[nodiscard]] static constexpr Value nil() noexcept { return Value{}; }

    /// Create a zero float Value
    [[nodiscard]] static constexpr Value zero() noexcept {
        Value v;
        v.bits_ = std::bit_cast<std::uint64_t>(0.0);
        return v;
    }

    // Type queries

    /// Get the type of this Value
    [[nodiscard]] constexpr ValueType type() const noexcept {
        if (is_float()) {
            return ValueType::Float;
        }
        if (is_integer()) {
            return ValueType::Integer;
        }
        if (is_bool()) {
            return ValueType::Bool;
        }
        if (is_handle()) {
            return ValueType::Handle;
        }
        if (is_nil()) {
            return ValueType::Nil;
        }
        if (is_pointer()) {
            return ValueType::Pointer;
        }
        return ValueType::Float;  // Fallback (should not reach)
    }

    /// Check if this Value holds a float
    /// A float is any value that is NOT a tagged type.
    /// This includes regular floats, infinities, and quiet NaN values.
    [[nodiscard]] constexpr bool is_float() const noexcept {
        // If QNAN prefix bits are not all set, it's definitely a float
        if ((bits_ & nan_box::QNAN_PREFIX) != nan_box::QNAN_PREFIX) {
            return true;
        }
        // QNAN prefix is set; check if tag bits (48-50) are zero (canonical NaN)
        // Tagged types have non-zero bits in the tag region
        return (bits_ & nan_box::TAG_BITS_MASK) == 0;
    }

    /// Check if this Value holds an integer
    [[nodiscard]] constexpr bool is_integer() const noexcept {
        return (bits_ & nan_box::TYPE_CHECK_MASK) == nan_box::INTEGER_PREFIX;
    }

    /// Check if this Value holds a boolean
    [[nodiscard]] constexpr bool is_bool() const noexcept {
        return (bits_ & nan_box::BOOL_CHECK_MASK) == nan_box::BOOL_PREFIX;
    }

    /// Check if this Value holds a Handle
    [[nodiscard]] constexpr bool is_handle() const noexcept {
        return (bits_ & nan_box::TYPE_CHECK_MASK) == nan_box::HANDLE_PREFIX;
    }

    /// Check if this Value is nil
    [[nodiscard]] constexpr bool is_nil() const noexcept { return bits_ == nan_box::NIL_VALUE; }

    /// Check if this Value holds a pointer
    [[nodiscard]] constexpr bool is_pointer() const noexcept {
        return (bits_ & nan_box::TYPE_CHECK_MASK) == nan_box::POINTER_PREFIX;
    }

    /// Check if this Value holds a numeric type (float or integer)
    [[nodiscard]] constexpr bool is_numeric() const noexcept { return is_float() || is_integer(); }

    // Value accessors

    /// Get the float value
    /// @pre is_float() == true
    [[nodiscard]] constexpr double as_float() const noexcept {
        return std::bit_cast<double>(bits_);
    }

    /// Get the integer value (sign-extended from 48 bits)
    /// @pre is_integer() == true
    [[nodiscard]] constexpr std::int64_t as_integer() const noexcept {
        // Sign-extend from 48 bits to 64 bits
        const auto val = static_cast<std::int64_t>(bits_ & nan_box::FULL_PAYLOAD);
        constexpr std::int64_t SIGN_BIT_48 = 1LL << 47;
        return (val ^ SIGN_BIT_48) - SIGN_BIT_48;
    }

    /// Get the boolean value
    /// @pre is_bool() == true
    [[nodiscard]] constexpr bool as_bool() const noexcept { return (bits_ & 1) != 0; }

    /// Get the Handle value
    /// @pre is_handle() == true
    /// @note Generation is only 16 bits (masked from original)
    [[nodiscard]] constexpr Handle as_handle() const noexcept {
        return Handle{.index = static_cast<std::uint32_t>(bits_ & 0xFFFF'FFFF),
                      .generation = static_cast<std::uint32_t>((bits_ >> 32) & 0xFFFF)};
    }

    /// Get the pointer value (sign-extended for canonical x86-64 addresses)
    /// @pre is_pointer() == true
    [[nodiscard]] void* as_pointer() const noexcept {
        // Sign-extend from 48 bits for canonical x86-64 addresses
        std::uint64_t addr = bits_ & nan_box::FULL_PAYLOAD;
        if ((addr & (1ULL << 47)) != 0) {
            addr |= 0xFFFF'0000'0000'0000ULL;
        }
        // NOLINTNEXTLINE(performance-no-int-to-ptr) - intentional for VM pointer representation
        return reinterpret_cast<void*>(addr);
    }

    /// Get numeric value as double (works for both float and integer)
    /// @pre is_numeric() == true
    [[nodiscard]] constexpr double as_number() const noexcept {
        if (is_float()) {
            return as_float();
        }
        return static_cast<double>(as_integer());
    }

    // Raw access (for serialization, debugging)

    /// Get the raw 64-bit representation
    [[nodiscard]] constexpr std::uint64_t raw_bits() const noexcept { return bits_; }

    /// Create a Value from raw bits (use with caution)
    [[nodiscard]] static constexpr Value from_raw(std::uint64_t bits) noexcept {
        Value v;
        v.bits_ = bits;
        return v;
    }

    // Comparison operators
    constexpr bool operator==(const Value& other) const noexcept = default;

    // Architecture-aware operations

    /// Mask this Value's integer to the target architecture width
    ///
    /// If this Value is an integer, returns a new Value with the integer
    /// masked to the appropriate width for the architecture. For non-integer
    /// Values, returns a copy unchanged.
    ///
    /// @param arch The target architecture
    /// @return A Value with the integer masked to architecture width
    [[nodiscard]] constexpr Value mask_to_arch(Architecture arch) const noexcept {
        if (!is_integer()) {
            return *this;
        }
        return from_int(arch_config::mask_int(as_integer(), arch));
    }

    /// Truthiness (for conditionals)
    /// nil and false are falsy; everything else is truthy
    [[nodiscard]] constexpr bool is_truthy() const noexcept {
        if (is_nil()) {
            return false;
        }
        if (is_bool()) {
            return as_bool();
        }
        if (is_integer()) {
            return as_integer() != 0;
        }
        if (is_float()) {
            return as_float() != 0.0;
        }
        return true;  // handles and pointers are truthy
    }

private:
    std::uint64_t bits_;
};

// Ensure Value is exactly 8 bytes and suitable for atomic operations
static_assert(sizeof(Value) == 8, "Value must be exactly 8 bytes");
static_assert(std::is_trivially_copyable_v<Value>, "Value must be trivially copyable");
static_assert(std::is_trivially_destructible_v<Value>, "Value must be trivially destructible");
static_assert(std::atomic<Value>::is_always_lock_free, "Value should be lock-free atomic");

// ============================================================================
// Value Operations
// ============================================================================

/// Arithmetic and comparison operations for Values
namespace value_ops {

/// Add two Values
/// Float + Float -> Float
/// Int + Int -> Int
/// Float + Int or Int + Float -> Float
/// Other combinations -> nil
[[nodiscard]] constexpr Value add(Value a, Value b) noexcept {
    if (a.is_float() && b.is_float()) {
        return Value::from_float(a.as_float() + b.as_float());
    }
    if (a.is_integer() && b.is_integer()) {
        // Note: potential overflow, wraps in 48-bit space
        return Value::from_int(a.as_integer() + b.as_integer());
    }
    if (a.is_numeric() && b.is_numeric()) {
        // Mixed: promote to float
        return Value::from_float(a.as_number() + b.as_number());
    }
    return Value::nil();
}

/// Subtract two Values
[[nodiscard]] constexpr Value sub(Value a, Value b) noexcept {
    if (a.is_float() && b.is_float()) {
        return Value::from_float(a.as_float() - b.as_float());
    }
    if (a.is_integer() && b.is_integer()) {
        return Value::from_int(a.as_integer() - b.as_integer());
    }
    if (a.is_numeric() && b.is_numeric()) {
        return Value::from_float(a.as_number() - b.as_number());
    }
    return Value::nil();
}

/// Multiply two Values
[[nodiscard]] constexpr Value mul(Value a, Value b) noexcept {
    if (a.is_float() && b.is_float()) {
        return Value::from_float(a.as_float() * b.as_float());
    }
    if (a.is_integer() && b.is_integer()) {
        return Value::from_int(a.as_integer() * b.as_integer());
    }
    if (a.is_numeric() && b.is_numeric()) {
        return Value::from_float(a.as_number() * b.as_number());
    }
    return Value::nil();
}

/// Divide two Values
/// @note Integer division truncates toward zero
/// @note Division by zero returns nil (not infinity or NaN for integers)
[[nodiscard]] constexpr Value div(Value a, Value b) noexcept {
    if (a.is_float() && b.is_float()) {
        return Value::from_float(a.as_float() / b.as_float());
    }
    if (a.is_integer() && b.is_integer()) {
        if (b.as_integer() == 0) {
            return Value::nil();
        }
        return Value::from_int(a.as_integer() / b.as_integer());
    }
    if (a.is_numeric() && b.is_numeric()) {
        return Value::from_float(a.as_number() / b.as_number());
    }
    return Value::nil();
}

/// Modulo operation for integers
/// @note Only works on integers; returns nil for floats
[[nodiscard]] constexpr Value mod(Value a, Value b) noexcept {
    if (a.is_integer() && b.is_integer()) {
        if (b.as_integer() == 0) {
            return Value::nil();
        }
        return Value::from_int(a.as_integer() % b.as_integer());
    }
    return Value::nil();
}

/// Negate a Value
[[nodiscard]] constexpr Value neg(Value a) noexcept {
    if (a.is_float()) {
        return Value::from_float(-a.as_float());
    }
    if (a.is_integer()) {
        return Value::from_int(-a.as_integer());
    }
    return Value::nil();
}

/// Compare two Values
/// Returns partial_ordering because NaN comparisons are unordered
[[nodiscard]] constexpr std::partial_ordering compare(Value a, Value b) noexcept {
    // Same type comparisons
    if (a.is_float() && b.is_float()) {
        return a.as_float() <=> b.as_float();
    }
    if (a.is_integer() && b.is_integer()) {
        return a.as_integer() <=> b.as_integer();
    }
    if (a.is_bool() && b.is_bool()) {
        return static_cast<int>(a.as_bool()) <=> static_cast<int>(b.as_bool());
    }
    // Mixed numeric: promote to float
    if (a.is_numeric() && b.is_numeric()) {
        return a.as_number() <=> b.as_number();
    }
    // Different types or non-comparable
    return std::partial_ordering::unordered;
}

/// Check if a < b
[[nodiscard]] constexpr bool less_than(Value a, Value b) noexcept {
    auto cmp = compare(a, b);
    return cmp == std::partial_ordering::less;
}

/// Check if a > b
[[nodiscard]] constexpr bool greater_than(Value a, Value b) noexcept {
    auto cmp = compare(a, b);
    return cmp == std::partial_ordering::greater;
}

/// Check if a <= b
[[nodiscard]] constexpr bool less_equal(Value a, Value b) noexcept {
    auto cmp = compare(a, b);
    return cmp == std::partial_ordering::less || cmp == std::partial_ordering::equivalent;
}

/// Check if a >= b
[[nodiscard]] constexpr bool greater_equal(Value a, Value b) noexcept {
    auto cmp = compare(a, b);
    return cmp == std::partial_ordering::greater || cmp == std::partial_ordering::equivalent;
}

}  // namespace value_ops

// ============================================================================
// String Representation
// ============================================================================

/// Convert a Value to a human-readable string
[[nodiscard]] inline std::string to_string(Value v) {
    switch (v.type()) {
        case ValueType::Float:
            return std::format("{}", v.as_float());
        case ValueType::Integer:
            return std::format("{}", v.as_integer());
        case ValueType::Bool:
            return v.as_bool() ? "true" : "false";
        case ValueType::Nil:
            return "nil";
        case ValueType::Handle: {
            auto h = v.as_handle();
            return std::format("Handle{{idx={}, gen={}}}", h.index, h.generation);
        }
        case ValueType::Pointer:
            return std::format("Ptr({})", v.as_pointer());
    }
    return "Unknown";
}

}  // namespace dotvm::core

// ============================================================================
// std::formatter specialization
// ============================================================================

template <>
struct std::formatter<dotvm::core::Value> : std::formatter<std::string> {
    auto format(dotvm::core::Value v, std::format_context& ctx) const {
        return std::formatter<std::string>::format(dotvm::core::to_string(v), ctx);
    }
};

template <>
struct std::formatter<dotvm::core::ValueType> : std::formatter<std::string_view> {
    auto format(dotvm::core::ValueType t, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(dotvm::core::type_name(t), ctx);
    }
};
