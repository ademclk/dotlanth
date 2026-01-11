#pragma once

#include <cstdint>

namespace dotvm::core {

// Core types
class Value;
class RegisterFile;
struct Handle;
enum class ValueType : std::uint8_t;
enum class RegisterClass : std::uint8_t;

// Instruction types
struct DecodedTypeA;
struct DecodedTypeB;
struct DecodedTypeC;
enum class OpcodeCategory : std::uint8_t;
enum class InstructionType : std::uint8_t;

// Memory types
struct HandleEntry;
class HandleTable;
class MemoryManager;
enum class MemoryError : std::uint8_t;

} // namespace dotvm::core
