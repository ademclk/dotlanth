#pragma once

/// @file register_file_concept.hpp
/// @brief C++20 concept for register file abstractions
///
/// Defines the RegisterFileInterface concept that enables compile-time
/// polymorphism without virtual function overhead. Any type satisfying
/// this concept can be used with templated VM components.

#include <concepts>
#include <cstddef>
#include <cstdint>

#include "../value.hpp"

namespace dotvm::core::concepts {

/// Concept for register file implementations
///
/// A type satisfies RegisterFileInterface if it provides:
/// - read(idx) -> Value : Read a register by index
/// - write(idx, val) -> void : Write a value to a register
/// - size() -> convertible to size_t : Number of registers
///
/// This enables zero-overhead abstraction for register file access,
/// allowing different implementations (standard, architecture-aware,
/// mock for testing) to be used interchangeably at compile time.
///
/// @example
/// ```cpp
/// template<RegisterFileInterface RF>
/// void execute_add(RF& regs, std::uint8_t rd, std::uint8_t rs1, std::uint8_t rs2) {
///     regs.write(rd, add(regs.read(rs1), regs.read(rs2)));
/// }
/// ```
template<typename T>
concept RegisterFileInterface = requires(T& rf, const T& crf, std::uint8_t idx, Value val) {
    // Read a register value by index
    { crf.read(idx) } -> std::same_as<Value>;

    // Write a value to a register by index
    { rf.write(idx, val) } -> std::same_as<void>;

    // Get the number of registers (optional but recommended)
    { crf.size() } -> std::convertible_to<std::size_t>;
};

/// Concept for architecture-aware register files
///
/// Extends RegisterFileInterface with architecture configuration
template<typename T>
concept ArchAwareRegisterFile = RegisterFileInterface<T> && requires(const T& crf) {
    // Get current architecture
    { crf.arch() } -> std::same_as<Architecture>;
};

/// Verify that a type satisfies the RegisterFileInterface concept at compile time
/// @example static_assert(is_register_file<MyRegisterFile>);
template<typename T>
inline constexpr bool is_register_file = RegisterFileInterface<T>;

}  // namespace dotvm::core::concepts
