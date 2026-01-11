#pragma once

/// @file concepts.hpp
/// @brief C++20 concepts for type-safe DotVM operations
///
/// This header provides concepts for compile-time type checking,
/// enabling better error messages and more expressive interfaces.

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace dotvm::core::concepts {

// =============================================================================
// Memory-Related Concepts
// =============================================================================

/// @brief Types that can be safely stored in VM memory
///
/// Requires trivially copyable types that aren't raw pointers.
/// This ensures memory operations are safe and deterministic.
template<typename T>
concept MemoryStorable =
    std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && !std::is_reference_v<T>;

/// @brief Types that can be used as memory sizes
template<typename T>
concept MemorySize = std::unsigned_integral<T> && sizeof(T) >= sizeof(std::uint32_t);

// =============================================================================
// Value-Related Concepts
// =============================================================================

/// @brief Numeric types that can be represented in the VM value system
template<typename T>
concept NumericValue = std::integral<T> || std::floating_point<T>;

/// @brief Integer types suitable for VM operations (fits in 48-bit NaN-boxed storage)
template<typename T>
concept VmInteger = std::signed_integral<T> && sizeof(T) <= sizeof(std::int64_t);

/// @brief Floating point types suitable for VM operations
template<typename T>
concept VmFloat = std::floating_point<T> && sizeof(T) <= sizeof(double);

// =============================================================================
// Instruction-Related Concepts
// =============================================================================

/// @brief Valid register index type
template<typename T>
concept RegisterIndex = std::unsigned_integral<T> && sizeof(T) <= sizeof(std::uint8_t);

/// @brief Valid opcode type
template<typename T>
concept Opcode = std::unsigned_integral<T> && sizeof(T) == sizeof(std::uint8_t);

/// @brief Valid instruction type (32-bit encoding)
template<typename T>
concept Instruction = std::unsigned_integral<T> && sizeof(T) == sizeof(std::uint32_t);

// =============================================================================
// Handler Concepts
// =============================================================================

/// @brief Concept for instruction handler callable types
///
/// Handlers must be invocable with an instruction word and return a boolean
/// indicating success.
template<typename H>
concept InstructionHandler = requires(H handler, std::uint32_t instruction) {
    { handler(instruction) } -> std::convertible_to<bool>;
};

/// @brief Concept for opcode decoder functions
template<typename F>
concept OpcodeDecoder = requires(F func, std::uint32_t instruction) {
    { func(instruction) } -> std::same_as<std::uint8_t>;
};

// =============================================================================
// Architecture Concepts
// =============================================================================

/// @brief Valid address type for the VM
template<typename T>
concept VmAddress = std::unsigned_integral<T> && sizeof(T) <= sizeof(std::uint64_t);

/// @brief Types that can be used as generation counters
template<typename T>
concept GenerationCounter = std::unsigned_integral<T> && sizeof(T) >= sizeof(std::uint32_t);

// =============================================================================
// Result/Error Concepts
// =============================================================================

/// @brief Types that can represent success/failure states
template<typename T>
concept ErrorType = std::is_enum_v<T> || std::integral<T>;

/// @brief Concept for Result-like types with is_ok/is_err methods
template<typename R>
concept ResultLike = requires(R r) {
    { r.is_ok() } -> std::convertible_to<bool>;
    { r.is_err() } -> std::convertible_to<bool>;
};

// =============================================================================
// Callable Concepts
// =============================================================================

/// @brief Concept for binary operations on values
template<typename Op, typename V>
concept BinaryValueOp = requires(Op op, V a, V b) {
    { op(a, b) } -> std::same_as<V>;
};

/// @brief Concept for unary operations on values
template<typename Op, typename V>
concept UnaryValueOp = requires(Op op, V a) {
    { op(a) } -> std::same_as<V>;
};

/// @brief Concept for comparison operations
template<typename Op, typename V>
concept ComparisonOp = requires(Op op, V a, V b) {
    { op(a, b) } -> std::convertible_to<bool>;
};

}  // namespace dotvm::core::concepts
