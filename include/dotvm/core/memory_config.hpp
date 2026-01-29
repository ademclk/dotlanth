/// @file memory_config.hpp
/// @brief Memory system configuration constants and utility functions.
///
/// This header defines the configuration parameters for the DotVM memory
/// management system, including:
/// - Page size and alignment constants
/// - Maximum allocation and table size limits
/// - Generation counter bounds for handle validation
/// - Utility functions for page alignment calculations
///
/// These constants are designed to balance security with performance,
/// providing predictable memory behavior while preventing resource exhaustion.

#pragma once

#include <cstddef>
#include <cstdint>

namespace dotvm::core {

/// @brief Memory configuration constants for the handle-based memory system.
namespace mem_config {
/// @brief Page size in bytes (4KB granularity).
inline constexpr std::size_t PAGE_SIZE = 4096;

/// @brief Log2 of PAGE_SIZE for shift operations.
inline constexpr std::size_t PAGE_SHIFT = 12;

/// @brief Mask for extracting page offset (PAGE_SIZE - 1).
inline constexpr std::size_t PAGE_MASK = PAGE_SIZE - 1;

/// @brief Maximum allocation size (64MB default).
inline constexpr std::size_t MAX_ALLOCATION_SIZE = 64 * 1024 * 1024;

/// @brief Minimum allocation size (one page).
inline constexpr std::size_t MIN_ALLOCATION_SIZE = PAGE_SIZE;

/// @brief Sentinel value indicating an invalid handle index.
inline constexpr std::uint32_t INVALID_INDEX = 0xFFFF'FFFFU;

/// @brief Initial generation value for new entries.
inline constexpr std::uint32_t INITIAL_GENERATION = 1;

/// @brief Maximum generation value (16-bit due to NaN-boxing constraint in Value).
/// When stored in a NaN-boxed Value, only 16 bits are available for generation.
inline constexpr std::uint32_t MAX_GENERATION = 0xFFFFU;

/// @brief Initial capacity for the handle table.
inline constexpr std::size_t INITIAL_TABLE_CAPACITY = 64;

/// @brief Maximum number of handles (~1M).
inline constexpr std::size_t MAX_TABLE_SIZE = 1U << 20;

/// @brief Alignment requirement for allocations (same as PAGE_SIZE).
inline constexpr std::size_t ALLOCATION_ALIGNMENT = PAGE_SIZE;
}  // namespace mem_config

/// Rounds a size up to the nearest page boundary.
/// @param size The size to align.
/// @return The aligned size (multiple of PAGE_SIZE), or 0 if overflow would occur.
/// @note Returns 0 on overflow - caller must check for this condition.
[[nodiscard]] inline constexpr std::size_t align_to_page(std::size_t size) noexcept {
    // Check for overflow: if size + PAGE_MASK would wrap around
    if (size > SIZE_MAX - mem_config::PAGE_MASK) {
        return 0;  // Indicate overflow
    }
    return (size + mem_config::PAGE_MASK) & ~mem_config::PAGE_MASK;
}

/// Checks if a size is page-aligned.
/// @param size The size to check.
/// @return true if size is a multiple of PAGE_SIZE.
[[nodiscard]] inline constexpr bool is_page_aligned(std::size_t size) noexcept {
    return (size & mem_config::PAGE_MASK) == 0;
}

/// Checks if an address is page-aligned.
/// @param addr The address to check.
/// @return true if address is aligned to PAGE_SIZE.
[[nodiscard]] inline constexpr bool is_address_page_aligned(std::uintptr_t addr) noexcept {
    return (addr & mem_config::PAGE_MASK) == 0;
}

/// Validates that a size is within allocation limits.
/// @param size The requested allocation size.
/// @return true if size > 0 and size <= MAX_ALLOCATION_SIZE.
[[nodiscard]] inline constexpr bool is_valid_allocation_size(std::size_t size) noexcept {
    return size > 0 && size <= mem_config::MAX_ALLOCATION_SIZE;
}

/// Calculates the number of pages needed for a given size.
/// @param size The size in bytes.
/// @return Number of pages (rounded up).
[[nodiscard]] inline constexpr std::size_t pages_for_size(std::size_t size) noexcept {
    return (size + mem_config::PAGE_MASK) >> mem_config::PAGE_SHIFT;
}

// Compile-time validation
static_assert(mem_config::PAGE_SIZE == (1ULL << mem_config::PAGE_SHIFT),
              "PAGE_SIZE must equal 2^PAGE_SHIFT");
static_assert((mem_config::PAGE_SIZE & mem_config::PAGE_MASK) == 0,
              "PAGE_SIZE must be a power of 2");
static_assert(mem_config::MAX_GENERATION <= 0xFFFFU,
              "MAX_GENERATION must fit in 16 bits for NaN-boxing");
static_assert(mem_config::INVALID_INDEX == 0xFFFF'FFFFU, "INVALID_INDEX should be max uint32_t");

// Constexpr function tests
static_assert(align_to_page(0) == 0);
static_assert(align_to_page(1) == mem_config::PAGE_SIZE);
static_assert(align_to_page(4096) == 4096);
static_assert(align_to_page(4097) == 8192);
// Overflow protection tests
static_assert(align_to_page(SIZE_MAX) == 0);         // SIZE_MAX + PAGE_MASK overflows
static_assert(align_to_page(SIZE_MAX - 1000) == 0);  // Still overflows
// SIZE_MAX - PAGE_MASK is the boundary: (SIZE_MAX - PAGE_MASK) + PAGE_MASK = SIZE_MAX exactly
static_assert(align_to_page(SIZE_MAX - mem_config::PAGE_MASK) != 0);      // Does NOT overflow
static_assert(align_to_page(SIZE_MAX - mem_config::PAGE_MASK + 1) == 0);  // DOES overflow
static_assert(is_page_aligned(0));
static_assert(is_page_aligned(4096));
static_assert(!is_page_aligned(4095));
static_assert(!is_page_aligned(1));
static_assert(is_valid_allocation_size(1));
static_assert(is_valid_allocation_size(mem_config::MAX_ALLOCATION_SIZE));
static_assert(!is_valid_allocation_size(0));
static_assert(!is_valid_allocation_size(mem_config::MAX_ALLOCATION_SIZE + 1));
static_assert(pages_for_size(0) == 0);
static_assert(pages_for_size(1) == 1);
static_assert(pages_for_size(4096) == 1);
static_assert(pages_for_size(4097) == 2);

}  // namespace dotvm::core
