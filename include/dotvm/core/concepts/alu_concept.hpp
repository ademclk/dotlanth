#pragma once

/// @file alu_concept.hpp
/// @brief C++20 concept for ALU (Arithmetic Logic Unit) abstractions
///
/// Defines the AluInterface concept that enables compile-time polymorphism
/// for ALU implementations without virtual function overhead. This allows
/// for mock ALUs in testing and alternative implementations.

#include <concepts>
#include <cstdint>

#include "../arch_types.hpp"
#include "../value.hpp"

namespace dotvm::core::concepts {

/// Concept for basic ALU arithmetic operations
///
/// A type satisfies BasicAluArithmetic if it provides the core
/// arithmetic operations: add, sub, mul, div, mod, neg, abs
template <typename T>
concept BasicAluArithmetic = requires(const T& alu, Value a, Value b) {
    { alu.add(a, b) } -> std::same_as<Value>;
    { alu.sub(a, b) } -> std::same_as<Value>;
    { alu.mul(a, b) } -> std::same_as<Value>;
    { alu.div(a, b) } -> std::same_as<Value>;
    { alu.mod(a, b) } -> std::same_as<Value>;
    { alu.neg(a) } -> std::same_as<Value>;
    { alu.abs(a) } -> std::same_as<Value>;
};

/// Concept for ALU bitwise operations
///
/// A type satisfies AluBitwiseOps if it provides bitwise operations:
/// and, or, xor, not
template <typename T>
concept AluBitwiseOps = requires(const T& alu, Value a, Value b) {
    { alu.bit_and(a, b) } -> std::same_as<Value>;
    { alu.bit_or(a, b) } -> std::same_as<Value>;
    { alu.bit_xor(a, b) } -> std::same_as<Value>;
    { alu.bit_not(a) } -> std::same_as<Value>;
};

/// Concept for ALU shift operations
///
/// A type satisfies AluShiftOps if it provides shift operations:
/// shl (logical left), shr (logical right), sar (arithmetic right)
template <typename T>
concept AluShiftOps = requires(const T& alu, Value a, Value b) {
    { alu.shl(a, b) } -> std::same_as<Value>;
    { alu.shr(a, b) } -> std::same_as<Value>;
    { alu.sar(a, b) } -> std::same_as<Value>;
};

/// Concept for ALU signed comparison operations
///
/// A type satisfies AluSignedComparison if it provides signed comparisons:
/// eq, ne, lt, le, gt, ge
template <typename T>
concept AluSignedComparison = requires(const T& alu, Value a, Value b) {
    { alu.cmp_eq(a, b) } -> std::same_as<Value>;
    { alu.cmp_ne(a, b) } -> std::same_as<Value>;
    { alu.cmp_lt(a, b) } -> std::same_as<Value>;
    { alu.cmp_le(a, b) } -> std::same_as<Value>;
    { alu.cmp_gt(a, b) } -> std::same_as<Value>;
    { alu.cmp_ge(a, b) } -> std::same_as<Value>;
};

/// Concept for ALU unsigned comparison operations
///
/// A type satisfies AluUnsignedComparison if it provides unsigned comparisons:
/// ltu, leu, gtu, geu
template <typename T>
concept AluUnsignedComparison = requires(const T& alu, Value a, Value b) {
    { alu.cmp_ltu(a, b) } -> std::same_as<Value>;
    { alu.cmp_leu(a, b) } -> std::same_as<Value>;
    { alu.cmp_gtu(a, b) } -> std::same_as<Value>;
    { alu.cmp_geu(a, b) } -> std::same_as<Value>;
};

/// Complete ALU interface concept
///
/// A type satisfies AluInterface if it provides:
/// - All arithmetic operations (add, sub, mul, div, mod, neg, abs)
/// - All bitwise operations (and, or, xor, not)
/// - All shift operations (shl, shr, sar)
/// - All comparison operations (eq, ne, lt, le, gt, ge, ltu, leu, gtu, geu)
/// - Architecture configuration (arch(), set_arch())
///
/// This enables zero-overhead abstraction for ALU access, allowing different
/// implementations (standard, architecture-aware, mock for testing) to be
/// used interchangeably at compile time.
///
/// @example
/// ```cpp
/// template<AluInterface Alu>
/// Value execute_add(const Alu& alu, Value a, Value b) {
///     return alu.add(a, b);
/// }
/// ```
template <typename T>
concept AluInterface =
    BasicAluArithmetic<T> && AluBitwiseOps<T> && AluShiftOps<T> && AluSignedComparison<T> &&
    AluUnsignedComparison<T> && requires(T& alu, const T& calu, Architecture arch) {
        // Architecture configuration
        { calu.arch() } -> std::same_as<Architecture>;
        { alu.set_arch(arch) } -> std::same_as<void>;
    };

/// Concept for minimal ALU (just arithmetic, no architecture awareness)
///
/// Useful for simple implementations that don't need architecture masking
template <typename T>
concept MinimalAlu = BasicAluArithmetic<T>;

/// Verify that a type satisfies the AluInterface concept at compile time
/// @example static_assert(is_alu<MyAlu>);
template <typename T>
inline constexpr bool is_alu = AluInterface<T>;

/// Verify that a type satisfies the minimal ALU requirements
template <typename T>
inline constexpr bool is_minimal_alu = MinimalAlu<T>;

}  // namespace dotvm::core::concepts
