#pragma once

/// @file memory_manager_concept.hpp
/// @brief C++20 concept for memory manager abstractions
///
/// Defines the MemoryManagerInterface concept that enables compile-time
/// polymorphism for memory management without virtual function overhead.
/// This allows for mock memory managers in testing and alternative
/// implementations (e.g., arena allocators, pooled allocators).

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

#include "../memory_config.hpp"

namespace dotvm::core::concepts {

/// Concept for basic memory allocation operations
///
/// A type satisfies BasicMemoryOps if it provides:
/// - allocate(size) -> Result<Handle>
/// - deallocate(handle) -> Error
/// - get_ptr(handle) -> Result<void*>
/// - is_valid(handle) -> bool
template <typename T>
concept BasicMemoryOps = requires(T& mm, const T& cmm, std::size_t size, Handle h) {
    // Allocate memory and return a handle
    { mm.allocate(size) };  // Returns some result type with Handle

    // Deallocate memory by handle
    { mm.deallocate(h) };  // Returns some error type

    // Get raw pointer from handle (mutable)
    { mm.get_ptr(h) };  // Returns some result type with void*

    // Get raw pointer from handle (const)
    { cmm.get_ptr(h) };  // Returns some result type with const void*

    // Check if a handle is valid
    { cmm.is_valid(h) } -> std::same_as<bool>;
};

/// Concept for typed memory read/write operations
///
/// A type satisfies TypedMemoryOps if it provides templated read/write
/// for trivially copyable types
template <typename T>
concept TypedMemoryOps =
    requires(T& mm, const T& cmm, Handle h, std::size_t offset, std::int32_t val) {
        // Typed read operation
        { cmm.template read<std::int32_t>(h, offset) };

        // Typed write operation
        { mm.template write<std::int32_t>(h, offset, val) };
    };

/// Concept for bulk memory operations
///
/// A type satisfies BulkMemoryOps if it provides:
/// - write_bytes(handle, offset, src, count) -> Error
/// - read_bytes(handle, offset, dst, count) -> Error
template <typename T>
concept BulkMemoryOps = requires(T& mm, const T& cmm, Handle h, std::size_t offset, const void* src,
                                 void* dst, std::size_t count) {
    { mm.write_bytes(h, offset, src, count) };
    { cmm.read_bytes(h, offset, dst, count) };
};

/// Concept for memory statistics queries
///
/// A type satisfies MemoryStats if it provides:
/// - active_allocations() -> size_t
/// - total_allocated_bytes() -> size_t
/// - max_allocation_size() -> size_t
template <typename T>
concept MemoryStats = requires(const T& cmm) {
    { cmm.active_allocations() } -> std::convertible_to<std::size_t>;
    { cmm.total_allocated_bytes() } -> std::convertible_to<std::size_t>;
    { cmm.max_allocation_size() } -> std::convertible_to<std::size_t>;
};

/// Concept for memory size queries
///
/// A type satisfies MemorySizeQueries if it provides:
/// - get_size(handle) -> Result<size_t>
template <typename T>
concept MemorySizeQueries = requires(const T& cmm, Handle h) {
    { cmm.get_size(h) };  // Returns some result type with size_t
};

/// Complete MemoryManager interface concept
///
/// A type satisfies MemoryManagerInterface if it provides:
/// - Basic allocation/deallocation operations
/// - Typed read/write operations
/// - Bulk memory operations
/// - Statistics queries
/// - Size queries
///
/// This enables zero-overhead abstraction for memory management,
/// allowing different implementations to be used interchangeably:
/// - Standard handle-based MemoryManager
/// - Mock memory manager for testing
/// - Arena allocator for batch allocations
/// - Pooled allocator for fixed-size objects
///
/// @example
/// ```cpp
/// template<MemoryManagerInterface MM>
/// void store_value(MM& mm, Handle h, std::size_t offset, Value val) {
///     mm.template write<Value>(h, offset, val);
/// }
/// ```
template <typename T>
concept MemoryManagerInterface = BasicMemoryOps<T> && TypedMemoryOps<T> && BulkMemoryOps<T> &&
                                 MemoryStats<T> && MemorySizeQueries<T>;

/// Concept for minimal memory manager (just allocate/deallocate)
///
/// Useful for simple implementations that only need basic operations
template <typename T>
concept MinimalMemoryManager = BasicMemoryOps<T>;

/// Verify that a type satisfies MemoryManagerInterface at compile time
/// @example static_assert(is_memory_manager<MyMemoryManager>);
template <typename T>
inline constexpr bool is_memory_manager = MemoryManagerInterface<T>;

/// Verify that a type satisfies minimal memory manager requirements
template <typename T>
inline constexpr bool is_minimal_memory_manager = MinimalMemoryManager<T>;

}  // namespace dotvm::core::concepts
