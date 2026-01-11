#pragma once

/// @file vector_types.hpp
/// @brief Generic SIMD vector types for the DotVM
///
/// This header provides the Vector template class for fixed-width SIMD operations.
/// Supports 128-bit (SSE/NEON), 256-bit (AVX2), and 512-bit (AVX-512) vector widths
/// with various lane types (int8, int16, int32, int64, float, double).
///
/// All operations are designed to be constexpr where possible for compile-time
/// computation support.

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>

namespace dotvm::core::simd {

// ============================================================================
// Concepts
// ============================================================================

/// Concept for valid SIMD lane types
///
/// Lane types must be arithmetic types of specific sizes that map cleanly
/// to SIMD hardware lanes.
template<typename T>
concept LaneType = std::is_arithmetic_v<T> && (
    std::same_as<T, std::int8_t> || std::same_as<T, std::uint8_t> ||
    std::same_as<T, std::int16_t> || std::same_as<T, std::uint16_t> ||
    std::same_as<T, std::int32_t> || std::same_as<T, std::uint32_t> ||
    std::same_as<T, std::int64_t> || std::same_as<T, std::uint64_t> ||
    std::same_as<T, float> || std::same_as<T, double>
);

/// Concept for valid vector widths
template<std::size_t Width>
concept ValidVectorWidth = (Width == 128 || Width == 256 || Width == 512);

/// Concept for valid lane count given width and lane type
template<std::size_t Width, typename Lane>
concept ValidLaneConfiguration = ValidVectorWidth<Width> && LaneType<Lane> &&
    (Width >= sizeof(Lane) * 8) && ((Width / 8) % sizeof(Lane) == 0);

// ============================================================================
// Vector Template Class
// ============================================================================

/// Fixed-width SIMD vector with typed lanes
///
/// This class provides a portable representation of SIMD vectors that can
/// work across different architectures. The alignment matches hardware
/// requirements for efficient SIMD load/store operations.
///
/// @tparam Width Vector width in bits (128, 256, or 512)
/// @tparam Lane Lane element type (int8, int16, int32, int64, float, double)
///
/// Example:
/// @code
/// Vector<128, std::int32_t> v;  // 4 x i32
/// v[0] = 10;
/// v[1] = 20;
/// @endcode
template<std::size_t Width, LaneType Lane>
    requires ValidLaneConfiguration<Width, Lane>
class alignas(Width / 8) Vector {
public:
    // ========================================================================
    // Type Aliases and Constants
    // ========================================================================

    using value_type = Lane;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = Lane&;
    using const_reference = const Lane&;
    using pointer = Lane*;
    using const_pointer = const Lane*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    /// Width of the vector in bits
    static constexpr std::size_t width_bits = Width;

    /// Width of the vector in bytes
    static constexpr std::size_t width_bytes = Width / 8;

    /// Number of lanes in this vector
    static constexpr std::size_t lane_count = Width / (sizeof(Lane) * 8);

    /// Size of each lane in bytes
    static constexpr std::size_t lane_size = sizeof(Lane);

    // ========================================================================
    // Static Assertions
    // ========================================================================

    static_assert(width_bytes == lane_count * lane_size,
                  "Vector size must equal lane_count * lane_size");
    static_assert(lane_count > 0, "Vector must have at least one lane");
    static_assert(lane_count <= 64, "Vector lane count must not exceed 64");

    // ========================================================================
    // Constructors
    // ========================================================================

    /// Default constructor - zero-initializes all lanes
    constexpr Vector() noexcept : lanes_{} {}

    /// Broadcast constructor - sets all lanes to the same value
    ///
    /// @param value The value to broadcast to all lanes
    constexpr explicit Vector(Lane value) noexcept {
        for (auto& lane : lanes_) {
            lane = value;
        }
    }

    /// Array constructor - initializes from an array
    ///
    /// @param values Array of lane values
    constexpr explicit Vector(const std::array<Lane, lane_count>& values) noexcept
        : lanes_{values} {}

    /// Variadic constructor - initializes lanes from individual values
    ///
    /// @param args Lane values (must provide exactly lane_count values)
    template<typename... Args>
        requires (sizeof...(Args) == lane_count) &&
                 (std::convertible_to<Args, Lane> && ...)
    constexpr Vector(Args... args) noexcept
        : lanes_{static_cast<Lane>(args)...} {}

    /// Copy from raw bytes
    ///
    /// @param bytes Raw byte data to copy (must be at least width_bytes bytes)
    static constexpr Vector from_bytes(const std::uint8_t* bytes) noexcept {
        Vector v;
        const auto* src = bytes;
        auto* dst = reinterpret_cast<std::uint8_t*>(v.lanes_.data());
        for (std::size_t i = 0; i < width_bytes; ++i) {
            dst[i] = src[i];
        }
        return v;
    }

    /// Create a zero vector
    [[nodiscard]] static constexpr Vector zero() noexcept {
        return Vector{};
    }

    /// Create a vector with all lanes set to one
    [[nodiscard]] static constexpr Vector ones() noexcept {
        if constexpr (std::is_floating_point_v<Lane>) {
            return Vector{static_cast<Lane>(1.0)};
        } else {
            return Vector{static_cast<Lane>(1)};
        }
    }

    /// Create a vector with all bits set to 1
    [[nodiscard]] static constexpr Vector all_bits_set() noexcept {
        Vector v;
        if constexpr (std::is_floating_point_v<Lane>) {
            // For floating point, set all bits via bit_cast
            if constexpr (std::same_as<Lane, float>) {
                constexpr std::uint32_t all_ones = ~std::uint32_t{0};
                Lane val = std::bit_cast<Lane>(all_ones);
                for (auto& lane : v.lanes_) {
                    lane = val;
                }
            } else {
                constexpr std::uint64_t all_ones = ~std::uint64_t{0};
                Lane val = std::bit_cast<Lane>(all_ones);
                for (auto& lane : v.lanes_) {
                    lane = val;
                }
            }
        } else {
            for (auto& lane : v.lanes_) {
                lane = static_cast<Lane>(~static_cast<std::make_unsigned_t<Lane>>(0));
            }
        }
        return v;
    }

    // ========================================================================
    // Lane Access
    // ========================================================================

    /// Get lane by index (bounds-checked in debug builds)
    ///
    /// @param idx Lane index
    /// @return Reference to the lane
    [[nodiscard]] constexpr reference operator[](size_type idx) noexcept {
        return lanes_[idx];
    }

    /// Get lane by index (const)
    ///
    /// @param idx Lane index
    /// @return Const reference to the lane
    [[nodiscard]] constexpr const_reference operator[](size_type idx) const noexcept {
        return lanes_[idx];
    }

    /// Get lane by index with bounds checking
    ///
    /// @param idx Lane index
    /// @return Reference to the lane
    /// @throws std::out_of_range if idx >= lane_count
    [[nodiscard]] constexpr reference at(size_type idx) {
        if (idx >= lane_count) {
            throw std::out_of_range("Vector lane index out of range");
        }
        return lanes_[idx];
    }

    /// Get lane by index with bounds checking (const)
    ///
    /// @param idx Lane index
    /// @return Const reference to the lane
    /// @throws std::out_of_range if idx >= lane_count
    [[nodiscard]] constexpr const_reference at(size_type idx) const {
        if (idx >= lane_count) {
            throw std::out_of_range("Vector lane index out of range");
        }
        return lanes_[idx];
    }

    /// Get the first lane
    [[nodiscard]] constexpr reference front() noexcept {
        return lanes_.front();
    }

    /// Get the first lane (const)
    [[nodiscard]] constexpr const_reference front() const noexcept {
        return lanes_.front();
    }

    /// Get the last lane
    [[nodiscard]] constexpr reference back() noexcept {
        return lanes_.back();
    }

    /// Get the last lane (const)
    [[nodiscard]] constexpr const_reference back() const noexcept {
        return lanes_.back();
    }

    // ========================================================================
    // Data Access
    // ========================================================================

    /// Get pointer to lane data
    [[nodiscard]] constexpr pointer data() noexcept {
        return lanes_.data();
    }

    /// Get const pointer to lane data
    [[nodiscard]] constexpr const_pointer data() const noexcept {
        return lanes_.data();
    }

    /// Get pointer to raw bytes
    [[nodiscard]] std::uint8_t* bytes() noexcept {
        return reinterpret_cast<std::uint8_t*>(lanes_.data());
    }

    /// Get const pointer to raw bytes
    [[nodiscard]] const std::uint8_t* bytes() const noexcept {
        return reinterpret_cast<const std::uint8_t*>(lanes_.data());
    }

    /// Get the underlying array
    [[nodiscard]] constexpr const std::array<Lane, lane_count>& lanes() const noexcept {
        return lanes_;
    }

    /// Get mutable access to underlying array
    [[nodiscard]] constexpr std::array<Lane, lane_count>& lanes() noexcept {
        return lanes_;
    }

    // ========================================================================
    // Size Information
    // ========================================================================

    /// Get the number of lanes
    [[nodiscard]] static constexpr size_type size() noexcept {
        return lane_count;
    }

    /// Check if vector is empty (always false for valid vectors)
    [[nodiscard]] static constexpr bool empty() noexcept {
        return false;
    }

    /// Get the maximum number of lanes (same as size for fixed vectors)
    [[nodiscard]] static constexpr size_type max_size() noexcept {
        return lane_count;
    }

    // ========================================================================
    // Iterators
    // ========================================================================

    [[nodiscard]] constexpr iterator begin() noexcept {
        return lanes_.data();
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return lanes_.data();
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return lanes_.data();
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        return lanes_.data() + lane_count;
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        return lanes_.data() + lane_count;
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return lanes_.data() + lane_count;
    }

    [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
        return reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
        return const_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
        return const_reverse_iterator(end());
    }

    [[nodiscard]] constexpr reverse_iterator rend() noexcept {
        return reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
        return const_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
        return const_reverse_iterator(begin());
    }

    // ========================================================================
    // Modifiers
    // ========================================================================

    /// Fill all lanes with a value
    ///
    /// @param value The value to fill with
    constexpr void fill(Lane value) noexcept {
        for (auto& lane : lanes_) {
            lane = value;
        }
    }

    /// Swap contents with another vector
    constexpr void swap(Vector& other) noexcept {
        lanes_.swap(other.lanes_);
    }

    // ========================================================================
    // Comparison Operators
    // ========================================================================

    /// Equality comparison (all lanes must be equal)
    [[nodiscard]] constexpr bool operator==(const Vector& other) const noexcept {
        return lanes_ == other.lanes_;
    }

    /// Inequality comparison
    [[nodiscard]] constexpr bool operator!=(const Vector& other) const noexcept {
        return lanes_ != other.lanes_;
    }

    /// Lexicographic comparison
    [[nodiscard]] constexpr auto operator<=>(const Vector& other) const noexcept {
        return lanes_ <=> other.lanes_;
    }

    // ========================================================================
    // Lane-wise Operations
    // ========================================================================

    /// Check if all lanes are zero
    [[nodiscard]] constexpr bool is_zero() const noexcept {
        for (const auto& lane : lanes_) {
            if (lane != Lane{}) {
                return false;
            }
        }
        return true;
    }

    /// Get horizontal sum of all lanes
    [[nodiscard]] constexpr Lane horizontal_sum() const noexcept {
        Lane sum{};
        for (const auto& lane : lanes_) {
            sum += lane;
        }
        return sum;
    }

    /// Get minimum lane value
    [[nodiscard]] constexpr Lane min_lane() const noexcept {
        Lane result = lanes_[0];
        for (std::size_t i = 1; i < lane_count; ++i) {
            if (lanes_[i] < result) {
                result = lanes_[i];
            }
        }
        return result;
    }

    /// Get maximum lane value
    [[nodiscard]] constexpr Lane max_lane() const noexcept {
        Lane result = lanes_[0];
        for (std::size_t i = 1; i < lane_count; ++i) {
            if (lanes_[i] > result) {
                result = lanes_[i];
            }
        }
        return result;
    }

private:
    std::array<Lane, lane_count> lanes_;
};

// ============================================================================
// Static Assertions for Alignment
// ============================================================================

static_assert(alignof(Vector<128, std::int32_t>) == 16,
              "128-bit vector must be 16-byte aligned");
static_assert(alignof(Vector<256, std::int32_t>) == 32,
              "256-bit vector must be 32-byte aligned");
static_assert(alignof(Vector<512, std::int32_t>) == 64,
              "512-bit vector must be 64-byte aligned");

static_assert(sizeof(Vector<128, std::int32_t>) == 16,
              "128-bit vector must be 16 bytes");
static_assert(sizeof(Vector<256, std::int32_t>) == 32,
              "256-bit vector must be 32 bytes");
static_assert(sizeof(Vector<512, std::int32_t>) == 64,
              "512-bit vector must be 64 bytes");

// ============================================================================
// Type Aliases for Common Vector Types
// ============================================================================

// 128-bit vectors
using Vector128i8  = Vector<128, std::int8_t>;    ///< 16 x i8
using Vector128u8  = Vector<128, std::uint8_t>;   ///< 16 x u8
using Vector128i16 = Vector<128, std::int16_t>;   ///< 8 x i16
using Vector128u16 = Vector<128, std::uint16_t>;  ///< 8 x u16
using Vector128i32 = Vector<128, std::int32_t>;   ///< 4 x i32
using Vector128u32 = Vector<128, std::uint32_t>;  ///< 4 x u32
using Vector128i64 = Vector<128, std::int64_t>;   ///< 2 x i64
using Vector128u64 = Vector<128, std::uint64_t>;  ///< 2 x u64
using Vector128f32 = Vector<128, float>;          ///< 4 x f32
using Vector128f64 = Vector<128, double>;         ///< 2 x f64

// 256-bit vectors
using Vector256i8  = Vector<256, std::int8_t>;    ///< 32 x i8
using Vector256u8  = Vector<256, std::uint8_t>;   ///< 32 x u8
using Vector256i16 = Vector<256, std::int16_t>;   ///< 16 x i16
using Vector256u16 = Vector<256, std::uint16_t>;  ///< 16 x u16
using Vector256i32 = Vector<256, std::int32_t>;   ///< 8 x i32
using Vector256u32 = Vector<256, std::uint32_t>;  ///< 8 x u32
using Vector256i64 = Vector<256, std::int64_t>;   ///< 4 x i64
using Vector256u64 = Vector<256, std::uint64_t>;  ///< 4 x u64
using Vector256f32 = Vector<256, float>;          ///< 8 x f32
using Vector256f64 = Vector<256, double>;         ///< 4 x f64

// 512-bit vectors
using Vector512i8  = Vector<512, std::int8_t>;    ///< 64 x i8
using Vector512u8  = Vector<512, std::uint8_t>;   ///< 64 x u8
using Vector512i16 = Vector<512, std::int16_t>;   ///< 32 x i16
using Vector512u16 = Vector<512, std::uint16_t>;  ///< 32 x u16
using Vector512i32 = Vector<512, std::int32_t>;   ///< 16 x i32
using Vector512u32 = Vector<512, std::uint32_t>;  ///< 16 x u32
using Vector512i64 = Vector<512, std::int64_t>;   ///< 8 x i64
using Vector512u64 = Vector<512, std::uint64_t>;  ///< 8 x u64
using Vector512f32 = Vector<512, float>;          ///< 16 x f32
using Vector512f64 = Vector<512, double>;         ///< 8 x f64

// ============================================================================
// Lane Count Verification Static Assertions
// ============================================================================

// 128-bit vectors
static_assert(Vector128i8::lane_count == 16, "Vector128i8 must have 16 lanes");
static_assert(Vector128i16::lane_count == 8, "Vector128i16 must have 8 lanes");
static_assert(Vector128i32::lane_count == 4, "Vector128i32 must have 4 lanes");
static_assert(Vector128i64::lane_count == 2, "Vector128i64 must have 2 lanes");
static_assert(Vector128f32::lane_count == 4, "Vector128f32 must have 4 lanes");
static_assert(Vector128f64::lane_count == 2, "Vector128f64 must have 2 lanes");

// 256-bit vectors
static_assert(Vector256i8::lane_count == 32, "Vector256i8 must have 32 lanes");
static_assert(Vector256i16::lane_count == 16, "Vector256i16 must have 16 lanes");
static_assert(Vector256i32::lane_count == 8, "Vector256i32 must have 8 lanes");
static_assert(Vector256i64::lane_count == 4, "Vector256i64 must have 4 lanes");
static_assert(Vector256f32::lane_count == 8, "Vector256f32 must have 8 lanes");
static_assert(Vector256f64::lane_count == 4, "Vector256f64 must have 4 lanes");

// 512-bit vectors
static_assert(Vector512i8::lane_count == 64, "Vector512i8 must have 64 lanes");
static_assert(Vector512i16::lane_count == 32, "Vector512i16 must have 32 lanes");
static_assert(Vector512i32::lane_count == 16, "Vector512i32 must have 16 lanes");
static_assert(Vector512i64::lane_count == 8, "Vector512i64 must have 8 lanes");
static_assert(Vector512f32::lane_count == 16, "Vector512f32 must have 16 lanes");
static_assert(Vector512f64::lane_count == 8, "Vector512f64 must have 8 lanes");

// ============================================================================
// Free Function Operations
// ============================================================================

/// Swap two vectors
template<std::size_t Width, LaneType Lane>
    requires ValidLaneConfiguration<Width, Lane>
constexpr void swap(Vector<Width, Lane>& a, Vector<Width, Lane>& b) noexcept {
    a.swap(b);
}

}  // namespace dotvm::core::simd
