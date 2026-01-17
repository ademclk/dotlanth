#pragma once

/// @file concepts.hpp
/// @brief Main header for DotVM C++20 concepts
///
/// This header provides a single include point for all DotVM concepts
/// and includes compile-time verification that standard types satisfy
/// their respective concepts.
///
/// Concepts enable zero-overhead polymorphism by using compile-time
/// constraints instead of virtual functions. This allows:
/// - No vtable overhead (better performance)
/// - Better inlining opportunities
/// - Compile-time error detection
/// - Easy mock implementations for testing

#include "alu_concept.hpp"
#include "memory_manager_concept.hpp"
#include "register_file_concept.hpp"

// Include concrete types for static_assert verification
#include "../alu.hpp"
#include "../memory.hpp"
#include "../register_file.hpp"

namespace dotvm::core::concepts {

// ============================================================================
// Compile-Time Concept Verification
// ============================================================================

// Verify RegisterFile types satisfy concepts
static_assert(RegisterFileInterface<RegisterFile>,
              "RegisterFile must satisfy RegisterFileInterface concept");
static_assert(RegisterFileInterface<ArchRegisterFile>,
              "ArchRegisterFile must satisfy RegisterFileInterface concept");
static_assert(ArchAwareRegisterFile<ArchRegisterFile>,
              "ArchRegisterFile must satisfy ArchAwareRegisterFile concept");

// Verify ALU satisfies concepts
static_assert(AluInterface<ALU>, "ALU must satisfy AluInterface concept");
static_assert(BasicAluArithmetic<ALU>, "ALU must satisfy BasicAluArithmetic concept");
static_assert(AluBitwiseOps<ALU>, "ALU must satisfy AluBitwiseOps concept");
static_assert(AluShiftOps<ALU>, "ALU must satisfy AluShiftOps concept");
static_assert(AluSignedComparison<ALU>, "ALU must satisfy AluSignedComparison concept");
static_assert(AluUnsignedComparison<ALU>, "ALU must satisfy AluUnsignedComparison concept");

// Verify MemoryManager satisfies concepts
static_assert(MemoryManagerInterface<MemoryManager>,
              "MemoryManager must satisfy MemoryManagerInterface concept");
static_assert(BasicMemoryOps<MemoryManager>, "MemoryManager must satisfy BasicMemoryOps concept");
static_assert(TypedMemoryOps<MemoryManager>, "MemoryManager must satisfy TypedMemoryOps concept");
static_assert(BulkMemoryOps<MemoryManager>, "MemoryManager must satisfy BulkMemoryOps concept");
static_assert(MemoryStats<MemoryManager>, "MemoryManager must satisfy MemoryStats concept");

// ============================================================================
// Convenience Type Aliases
// ============================================================================

/// Type alias for any type satisfying RegisterFileInterface
template <typename T>
    requires RegisterFileInterface<T>
using AnyRegisterFile = T;

/// Type alias for any type satisfying AluInterface
template <typename T>
    requires AluInterface<T>
using AnyAlu = T;

/// Type alias for any type satisfying MemoryManagerInterface
template <typename T>
    requires MemoryManagerInterface<T>
using AnyMemoryManager = T;

}  // namespace dotvm::core::concepts
