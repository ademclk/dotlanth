#pragma once

#include <bit>
#include <compare>
#include <cstdint>
#include <type_traits>

namespace dotvm::core {

// NaN-boxing constants
namespace nan_box {
    inline constexpr std::uint64_t QNAN_PREFIX     = 0x7FF8'0000'0000'0000ULL;
    inline constexpr std::uint64_t TAG_MASK        = 0x0000'FFFF'0000'0000ULL;
    inline constexpr std::uint64_t PAYLOAD_MASK    = 0x0000'0000'FFFF'FFFFULL;
    inline constexpr std::uint64_t FULL_PAYLOAD    = 0x0000'FFFF'FFFF'FFFFULL;

    inline constexpr std::uint64_t TAG_INTEGER     = 0x0001ULL << 48;
    inline constexpr std::uint64_t TAG_BOOL        = 0x0002ULL << 48;
    inline constexpr std::uint64_t TAG_HANDLE      = 0x0003ULL << 48;
    inline constexpr std::uint64_t TAG_NIL         = 0x0004ULL << 48;
    inline constexpr std::uint64_t TAG_POINTER     = 0x0005ULL << 48;

    inline constexpr std::uint64_t INTEGER_PREFIX  = QNAN_PREFIX | TAG_INTEGER;
    inline constexpr std::uint64_t BOOL_PREFIX     = QNAN_PREFIX | TAG_BOOL;
    inline constexpr std::uint64_t HANDLE_PREFIX   = QNAN_PREFIX | TAG_HANDLE;
    inline constexpr std::uint64_t NIL_VALUE       = QNAN_PREFIX | TAG_NIL;
    inline constexpr std::uint64_t POINTER_PREFIX  = QNAN_PREFIX | TAG_POINTER;
} // namespace nan_box

// Handle structure: 32-bit index + 32-bit generation
struct Handle {
    std::uint32_t index;
    std::uint32_t generation;

    constexpr bool operator==(const Handle&) const noexcept = default;
    constexpr auto operator<=>(const Handle&) const noexcept = default;
};

static_assert(sizeof(Handle) == 8);

// Value type enumeration
enum class ValueType : std::uint8_t {
    Float   = 0,
    Integer = 1,
    Bool    = 2,
    Handle  = 3,
    Nil     = 4,
    Pointer = 5
};

// The core NaN-boxed value class
class Value {
public:
    // Default constructor - creates nil
    constexpr Value() noexcept : bits_{nan_box::NIL_VALUE} {}

    // Explicit constructors for each type
    constexpr explicit Value(double f) noexcept
        : bits_{std::bit_cast<std::uint64_t>(f)} {}

    constexpr explicit Value(std::int64_t i) noexcept
        : bits_{nan_box::INTEGER_PREFIX |
                (static_cast<std::uint64_t>(i) & nan_box::FULL_PAYLOAD)} {}

    constexpr explicit Value(bool b) noexcept
        : bits_{nan_box::BOOL_PREFIX | static_cast<std::uint64_t>(b)} {}

    constexpr explicit Value(Handle h) noexcept
        : bits_{nan_box::HANDLE_PREFIX |
                (static_cast<std::uint64_t>(h.generation) << 32) |
                static_cast<std::uint64_t>(h.index)} {}

    constexpr explicit Value(void* ptr) noexcept
        : bits_{nan_box::POINTER_PREFIX |
                (reinterpret_cast<std::uint64_t>(ptr) & nan_box::FULL_PAYLOAD)} {}

    // Factory methods for clarity
    [[nodiscard]] static constexpr Value from_float(double f) noexcept {
        return Value{f};
    }

    [[nodiscard]] static constexpr Value from_int(std::int64_t i) noexcept {
        return Value{i};
    }

    [[nodiscard]] static constexpr Value from_bool(bool b) noexcept {
        return Value{b};
    }

    [[nodiscard]] static constexpr Value from_handle(Handle h) noexcept {
        return Value{h};
    }

    [[nodiscard]] static constexpr Value from_handle(std::uint32_t idx,
                                                      std::uint32_t gen) noexcept {
        return Value{Handle{idx, gen}};
    }

    [[nodiscard]] static constexpr Value nil() noexcept {
        return Value{};
    }

    [[nodiscard]] static constexpr Value zero() noexcept {
        return Value{0.0};
    }

    // Type queries
    [[nodiscard]] constexpr ValueType type() const noexcept {
        if (is_float()) return ValueType::Float;
        if (is_integer()) return ValueType::Integer;
        if (is_bool()) return ValueType::Bool;
        if (is_handle()) return ValueType::Handle;
        if (is_nil()) return ValueType::Nil;
        if (is_pointer()) return ValueType::Pointer;
        return ValueType::Float; // Fallback (should not reach)
    }

    [[nodiscard]] constexpr bool is_float() const noexcept {
        // Not a qNaN with our prefix
        return (bits_ & 0x7FF8'0000'0000'0000ULL) != nan_box::QNAN_PREFIX;
    }

    [[nodiscard]] constexpr bool is_integer() const noexcept {
        return (bits_ & 0x7FFF'0000'0000'0000ULL) == nan_box::INTEGER_PREFIX;
    }

    [[nodiscard]] constexpr bool is_bool() const noexcept {
        return (bits_ & 0x7FFF'FFFF'FFFF'FFFEULL) == nan_box::BOOL_PREFIX;
    }

    [[nodiscard]] constexpr bool is_handle() const noexcept {
        return (bits_ & 0x7FFF'0000'0000'0000ULL) == nan_box::HANDLE_PREFIX;
    }

    [[nodiscard]] constexpr bool is_nil() const noexcept {
        return bits_ == nan_box::NIL_VALUE;
    }

    [[nodiscard]] constexpr bool is_pointer() const noexcept {
        return (bits_ & 0x7FFF'0000'0000'0000ULL) == nan_box::POINTER_PREFIX;
    }

    // Value accessors
    [[nodiscard]] constexpr double as_float() const noexcept {
        return std::bit_cast<double>(bits_);
    }

    [[nodiscard]] constexpr std::int64_t as_integer() const noexcept {
        // Sign-extend from 48 bits to 64 bits
        const auto val = static_cast<std::int64_t>(bits_ & nan_box::FULL_PAYLOAD);
        constexpr std::int64_t sign_bit = 1LL << 47;
        return (val ^ sign_bit) - sign_bit;
    }

    [[nodiscard]] constexpr bool as_bool() const noexcept {
        return (bits_ & 1) != 0;
    }

    [[nodiscard]] constexpr Handle as_handle() const noexcept {
        return Handle{
            .index = static_cast<std::uint32_t>(bits_ & 0xFFFF'FFFF),
            .generation = static_cast<std::uint32_t>((bits_ >> 32) & 0xFFFF)
        };
    }

    [[nodiscard]] void* as_pointer() const noexcept {
        // Sign-extend from 48 bits for canonical x86-64 addresses
        std::uint64_t addr = bits_ & nan_box::FULL_PAYLOAD;
        if (addr & (1ULL << 47)) {
            addr |= 0xFFFF'0000'0000'0000ULL;
        }
        return reinterpret_cast<void*>(addr);
    }

    // Raw access (for serialization, debugging)
    [[nodiscard]] constexpr std::uint64_t raw_bits() const noexcept {
        return bits_;
    }

    [[nodiscard]] static constexpr Value from_raw(std::uint64_t bits) noexcept {
        Value v;
        v.bits_ = bits;
        return v;
    }

    // Comparison operators
    constexpr bool operator==(const Value& other) const noexcept = default;

    // Truthiness (for conditionals)
    [[nodiscard]] constexpr bool is_truthy() const noexcept {
        if (is_nil()) return false;
        if (is_bool()) return as_bool();
        if (is_integer()) return as_integer() != 0;
        if (is_float()) return as_float() != 0.0;
        return true; // handles and pointers are truthy
    }

private:
    std::uint64_t bits_;
};

// Ensure Value is exactly 8 bytes
static_assert(sizeof(Value) == 8, "Value must be exactly 8 bytes");
static_assert(std::is_trivially_copyable_v<Value>, "Value must be trivially copyable");
static_assert(std::is_trivially_destructible_v<Value>, "Value must be trivially destructible");

} // namespace dotvm::core
