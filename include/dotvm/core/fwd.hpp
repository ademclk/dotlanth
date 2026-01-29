/// @file fwd.hpp
/// @brief Forward declarations for DotVM core types.
///
/// This header provides forward declarations for all major types in the
/// dotvm::core namespace. Include this header when you only need type
/// declarations without full definitions, reducing compilation dependencies.

#pragma once

#include <cstdint>

namespace dotvm::core {

// ============================================================================
// Core Types
// ============================================================================

/// @brief NaN-boxed value type for the VM.
class Value;
/// @brief 256-entry register file storing Values.
class RegisterFile;
/// @brief Architecture-aware register file wrapper with automatic value masking.
class ArchRegisterFile;
/// @brief Memory handle with index and generation for use-after-free detection.
struct Handle;
/// @brief Enumeration of value types stored in NaN-boxed Values.
enum class ValueType : std::uint8_t;
/// @brief Classification of registers (zero, caller-saved, callee-saved, general).
enum class RegisterClass : std::uint8_t;

// ============================================================================
// Architecture Types
// ============================================================================

/// @brief Target architecture (32-bit or 64-bit).
enum class Architecture : std::uint8_t;
/// @brief Arithmetic Logic Unit for VM operations.
class ALU;
/// @brief Configuration parameters for VM initialization.
struct VmConfig;
/// @brief Execution context holding VM state.
class VmContext;

// ============================================================================
// Instruction Types
// ============================================================================

/// @brief Decoded Type A instruction (register-register operations).
struct DecodedTypeA;
/// @brief Decoded Type B instruction (register-immediate operations).
struct DecodedTypeB;
/// @brief Decoded Type C instruction (offset/jump operations).
struct DecodedTypeC;
/// @brief Classification of opcodes by functional category.
enum class OpcodeCategory : std::uint8_t;
/// @brief Instruction format type (A, B, C, S, D, M).
enum class InstructionType : std::uint8_t;

// ============================================================================
// Memory Types
// ============================================================================

/// @brief Entry in the handle table tracking a single allocation.
struct HandleEntry;
/// @brief Table managing handle entries with free list for slot reuse.
class HandleTable;
/// @brief Safe memory manager with generation-based allocation.
class MemoryManager;
/// @brief Error codes for memory operations.
enum class MemoryError : std::uint8_t;

}  // namespace dotvm::core
