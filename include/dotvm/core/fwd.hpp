#pragma once

#include <cstdint>

namespace dotvm::core {

// Core types
class Value;
class RegisterFile;
struct Handle;
enum class ValueType : std::uint8_t;
enum class RegisterClass : std::uint8_t;

} // namespace dotvm::core
