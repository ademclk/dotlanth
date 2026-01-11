#pragma once

/// @file vector_register_file.hpp
/// @brief Vector register file for SIMD operations in the DotVM
///
/// This header provides the VectorRegisterFile class that manages V0-V31
/// vector registers. Similar to the scalar register file, V0 is hardwired
/// to zero (all zeros) - writes are ignored and reads always return zero.
///
/// The register file supports multiple access widths (128, 256, 512 bits)
/// allowing the same register to be accessed at different granularities.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "vector_types.hpp"

namespace dotvm::core::simd {

// ============================================================================
// Constants
// ============================================================================

/// Number of vector registers (V0-V31)
inline constexpr std::size_t VECTOR_REGISTER_COUNT = 32;

/// Index of the zero register (V0)
inline constexpr std::uint8_t VREG_ZERO = 0;

/// Maximum vector width in bits
inline constexpr std::size_t MAX_VECTOR_WIDTH_BITS = 512;

/// Maximum vector width in bytes
inline constexpr std::size_t MAX_VECTOR_WIDTH_BYTES = MAX_VECTOR_WIDTH_BITS / 8;

// ============================================================================
// VectorRegister Union
// ============================================================================

/// Union representing a single vector register with multiple access widths
///
/// The register can be accessed as 128-bit, 256-bit, or 512-bit vectors
/// with various lane types. All accesses are aliased to the same underlying
/// storage.
///
/// @note The union provides typed accessors for convenient manipulation
///       while maintaining a single 64-byte storage.
union alignas(64) VectorRegister {
    // ========================================================================
    // Raw Storage
    // ========================================================================

    /// Raw byte storage (512 bits = 64 bytes maximum)
    std::array<std::uint8_t, MAX_VECTOR_WIDTH_BYTES> bytes;

    /// 64-bit quadword access (8 quadwords for 512-bit)
    std::array<std::uint64_t, 8> qwords;

    /// 32-bit dword access (16 dwords for 512-bit)
    std::array<std::uint32_t, 16> dwords;

    /// 16-bit word access (32 words for 512-bit)
    std::array<std::uint16_t, 32> words;

    // ========================================================================
    // Constructors
    // ========================================================================

    /// Default constructor - zero initializes
    constexpr VectorRegister() noexcept : bytes{} {}

    // ========================================================================
    // Zero Operations
    // ========================================================================

    /// Zero all bytes in the register
    constexpr void clear() noexcept {
        for (auto& byte : bytes) {
            byte = 0;
        }
    }

    /// Check if register is all zeros
    [[nodiscard]] constexpr bool is_zero() const noexcept {
        for (const auto& qword : qwords) {
            if (qword != 0) {
                return false;
            }
        }
        return true;
    }

    // ========================================================================
    // 128-bit Access
    // ========================================================================

    /// Get the lower 128 bits as a vector of the specified lane type
    template<LaneType Lane>
    [[nodiscard]] Vector<128, Lane> as_v128() const noexcept {
        return Vector<128, Lane>::from_bytes(bytes.data());
    }

    /// Set the lower 128 bits from a vector
    template<LaneType Lane>
    void set_v128(const Vector<128, Lane>& v) noexcept {
        std::copy_n(v.bytes(), 16, bytes.data());
    }

    /// Get i32 lanes from 128-bit portion
    [[nodiscard]] Vector128i32 as_v128i32() const noexcept {
        return as_v128<std::int32_t>();
    }

    /// Get f32 lanes from 128-bit portion
    [[nodiscard]] Vector128f32 as_v128f32() const noexcept {
        return as_v128<float>();
    }

    /// Get i64 lanes from 128-bit portion
    [[nodiscard]] Vector128i64 as_v128i64() const noexcept {
        return as_v128<std::int64_t>();
    }

    /// Get f64 lanes from 128-bit portion
    [[nodiscard]] Vector128f64 as_v128f64() const noexcept {
        return as_v128<double>();
    }

    // ========================================================================
    // 256-bit Access
    // ========================================================================

    /// Get the lower 256 bits as a vector of the specified lane type
    template<LaneType Lane>
    [[nodiscard]] Vector<256, Lane> as_v256() const noexcept {
        return Vector<256, Lane>::from_bytes(bytes.data());
    }

    /// Set the lower 256 bits from a vector
    template<LaneType Lane>
    void set_v256(const Vector<256, Lane>& v) noexcept {
        std::copy_n(v.bytes(), 32, bytes.data());
    }

    /// Get i32 lanes from 256-bit portion
    [[nodiscard]] Vector256i32 as_v256i32() const noexcept {
        return as_v256<std::int32_t>();
    }

    /// Get f32 lanes from 256-bit portion
    [[nodiscard]] Vector256f32 as_v256f32() const noexcept {
        return as_v256<float>();
    }

    /// Get i64 lanes from 256-bit portion
    [[nodiscard]] Vector256i64 as_v256i64() const noexcept {
        return as_v256<std::int64_t>();
    }

    /// Get f64 lanes from 256-bit portion
    [[nodiscard]] Vector256f64 as_v256f64() const noexcept {
        return as_v256<double>();
    }

    // ========================================================================
    // 512-bit Access
    // ========================================================================

    /// Get the full 512 bits as a vector of the specified lane type
    template<LaneType Lane>
    [[nodiscard]] Vector<512, Lane> as_v512() const noexcept {
        return Vector<512, Lane>::from_bytes(bytes.data());
    }

    /// Set all 512 bits from a vector
    template<LaneType Lane>
    void set_v512(const Vector<512, Lane>& v) noexcept {
        std::copy_n(v.bytes(), 64, bytes.data());
    }

    /// Get i32 lanes from full 512-bit register
    [[nodiscard]] Vector512i32 as_v512i32() const noexcept {
        return as_v512<std::int32_t>();
    }

    /// Get f32 lanes from full 512-bit register
    [[nodiscard]] Vector512f32 as_v512f32() const noexcept {
        return as_v512<float>();
    }

    /// Get i64 lanes from full 512-bit register
    [[nodiscard]] Vector512i64 as_v512i64() const noexcept {
        return as_v512<std::int64_t>();
    }

    /// Get f64 lanes from full 512-bit register
    [[nodiscard]] Vector512f64 as_v512f64() const noexcept {
        return as_v512<double>();
    }

    // ========================================================================
    // Comparison
    // ========================================================================

    /// Equality comparison
    [[nodiscard]] constexpr bool operator==(const VectorRegister& other) const noexcept {
        return bytes == other.bytes;
    }

    /// Inequality comparison
    [[nodiscard]] constexpr bool operator!=(const VectorRegister& other) const noexcept {
        return bytes != other.bytes;
    }
};

// Static assertions for VectorRegister
static_assert(sizeof(VectorRegister) == 64, "VectorRegister must be 64 bytes");
static_assert(alignof(VectorRegister) == 64, "VectorRegister must be 64-byte aligned");

// ============================================================================
// VectorRegisterFile Class
// ============================================================================

/// Vector register file containing V0-V31
///
/// This class manages 32 vector registers, each capable of holding up to
/// 512 bits of data. Like the scalar register file, V0 is a special "zero
/// register" - writes to V0 are silently ignored, and reads always return
/// all zeros.
///
/// The register file is cache-line aligned for optimal memory access patterns.
///
/// @note All access methods have noexcept guarantees for predictable performance.
class alignas(64) VectorRegisterFile {
public:
    // ========================================================================
    // Constants
    // ========================================================================

    /// Number of registers
    static constexpr std::size_t register_count = VECTOR_REGISTER_COUNT;

    /// Index of the zero register
    static constexpr std::uint8_t zero_register = VREG_ZERO;

    // ========================================================================
    // Constructors
    // ========================================================================

    /// Default constructor - zero-initializes all registers
    constexpr VectorRegisterFile() noexcept {
        for (auto& reg : regs_) {
            reg.clear();
        }
    }

    // ========================================================================
    // Raw Register Access
    // ========================================================================

    /// Read a register (V0 always returns zero)
    ///
    /// @param idx Register index (0-31)
    /// @return Reference to the register (zero register for V0)
    [[nodiscard]] const VectorRegister& read(std::uint8_t idx) const noexcept {
        if (idx == VREG_ZERO) [[unlikely]] {
            return zero_reg_;
        }
        return regs_[idx];
    }

    /// Write to a register (writes to V0 are ignored)
    ///
    /// @param idx Register index (0-31)
    /// @param value Value to write
    void write(std::uint8_t idx, const VectorRegister& value) noexcept {
        if (idx == VREG_ZERO) [[unlikely]] {
            return;  // V0 is hardwired to zero
        }
        regs_[idx] = value;
    }

    // ========================================================================
    // 128-bit Access
    // ========================================================================

    /// Read 128-bit vector from register
    ///
    /// @tparam Lane Lane element type
    /// @param idx Register index (0-31)
    /// @return 128-bit vector (zero for V0)
    template<LaneType Lane>
    [[nodiscard]] Vector<128, Lane> read_v128(std::uint8_t idx) const noexcept {
        return read(idx).template as_v128<Lane>();
    }

    /// Write 128-bit vector to register (writes to V0 are ignored)
    ///
    /// @tparam Lane Lane element type
    /// @param idx Register index (0-31)
    /// @param v Vector to write
    template<LaneType Lane>
    void write_v128(std::uint8_t idx, const Vector<128, Lane>& v) noexcept {
        if (idx == VREG_ZERO) [[unlikely]] {
            return;
        }
        // Clear upper bits when writing smaller vectors
        regs_[idx].clear();
        regs_[idx].template set_v128<Lane>(v);
    }

    // ========================================================================
    // 256-bit Access
    // ========================================================================

    /// Read 256-bit vector from register
    ///
    /// @tparam Lane Lane element type
    /// @param idx Register index (0-31)
    /// @return 256-bit vector (zero for V0)
    template<LaneType Lane>
    [[nodiscard]] Vector<256, Lane> read_v256(std::uint8_t idx) const noexcept {
        return read(idx).template as_v256<Lane>();
    }

    /// Write 256-bit vector to register (writes to V0 are ignored)
    ///
    /// @tparam Lane Lane element type
    /// @param idx Register index (0-31)
    /// @param v Vector to write
    template<LaneType Lane>
    void write_v256(std::uint8_t idx, const Vector<256, Lane>& v) noexcept {
        if (idx == VREG_ZERO) [[unlikely]] {
            return;
        }
        // Clear upper bits when writing smaller vectors
        regs_[idx].clear();
        regs_[idx].template set_v256<Lane>(v);
    }

    // ========================================================================
    // 512-bit Access
    // ========================================================================

    /// Read 512-bit vector from register
    ///
    /// @tparam Lane Lane element type
    /// @param idx Register index (0-31)
    /// @return 512-bit vector (zero for V0)
    template<LaneType Lane>
    [[nodiscard]] Vector<512, Lane> read_v512(std::uint8_t idx) const noexcept {
        return read(idx).template as_v512<Lane>();
    }

    /// Write 512-bit vector to register (writes to V0 are ignored)
    ///
    /// @tparam Lane Lane element type
    /// @param idx Register index (0-31)
    /// @param v Vector to write
    template<LaneType Lane>
    void write_v512(std::uint8_t idx, const Vector<512, Lane>& v) noexcept {
        if (idx == VREG_ZERO) [[unlikely]] {
            return;
        }
        regs_[idx].template set_v512<Lane>(v);
    }

    // ========================================================================
    // Convenience Typed Access
    // ========================================================================

    // --- 128-bit ---

    [[nodiscard]] Vector128i32 read_v128i32(std::uint8_t idx) const noexcept {
        return read_v128<std::int32_t>(idx);
    }

    void write_v128i32(std::uint8_t idx, const Vector128i32& v) noexcept {
        write_v128<std::int32_t>(idx, v);
    }

    [[nodiscard]] Vector128f32 read_v128f32(std::uint8_t idx) const noexcept {
        return read_v128<float>(idx);
    }

    void write_v128f32(std::uint8_t idx, const Vector128f32& v) noexcept {
        write_v128<float>(idx, v);
    }

    [[nodiscard]] Vector128i64 read_v128i64(std::uint8_t idx) const noexcept {
        return read_v128<std::int64_t>(idx);
    }

    void write_v128i64(std::uint8_t idx, const Vector128i64& v) noexcept {
        write_v128<std::int64_t>(idx, v);
    }

    [[nodiscard]] Vector128f64 read_v128f64(std::uint8_t idx) const noexcept {
        return read_v128<double>(idx);
    }

    void write_v128f64(std::uint8_t idx, const Vector128f64& v) noexcept {
        write_v128<double>(idx, v);
    }

    // --- 256-bit ---

    [[nodiscard]] Vector256i32 read_v256i32(std::uint8_t idx) const noexcept {
        return read_v256<std::int32_t>(idx);
    }

    void write_v256i32(std::uint8_t idx, const Vector256i32& v) noexcept {
        write_v256<std::int32_t>(idx, v);
    }

    [[nodiscard]] Vector256f32 read_v256f32(std::uint8_t idx) const noexcept {
        return read_v256<float>(idx);
    }

    void write_v256f32(std::uint8_t idx, const Vector256f32& v) noexcept {
        write_v256<float>(idx, v);
    }

    [[nodiscard]] Vector256i64 read_v256i64(std::uint8_t idx) const noexcept {
        return read_v256<std::int64_t>(idx);
    }

    void write_v256i64(std::uint8_t idx, const Vector256i64& v) noexcept {
        write_v256<std::int64_t>(idx, v);
    }

    [[nodiscard]] Vector256f64 read_v256f64(std::uint8_t idx) const noexcept {
        return read_v256<double>(idx);
    }

    void write_v256f64(std::uint8_t idx, const Vector256f64& v) noexcept {
        write_v256<double>(idx, v);
    }

    // --- 512-bit ---

    [[nodiscard]] Vector512i32 read_v512i32(std::uint8_t idx) const noexcept {
        return read_v512<std::int32_t>(idx);
    }

    void write_v512i32(std::uint8_t idx, const Vector512i32& v) noexcept {
        write_v512<std::int32_t>(idx, v);
    }

    [[nodiscard]] Vector512f32 read_v512f32(std::uint8_t idx) const noexcept {
        return read_v512<float>(idx);
    }

    void write_v512f32(std::uint8_t idx, const Vector512f32& v) noexcept {
        write_v512<float>(idx, v);
    }

    [[nodiscard]] Vector512i64 read_v512i64(std::uint8_t idx) const noexcept {
        return read_v512<std::int64_t>(idx);
    }

    void write_v512i64(std::uint8_t idx, const Vector512i64& v) noexcept {
        write_v512<std::int64_t>(idx, v);
    }

    [[nodiscard]] Vector512f64 read_v512f64(std::uint8_t idx) const noexcept {
        return read_v512<double>(idx);
    }

    void write_v512f64(std::uint8_t idx, const Vector512f64& v) noexcept {
        write_v512<double>(idx, v);
    }

    // ========================================================================
    // Proxy Class for Operator[] Access
    // ========================================================================

    /// Proxy class for non-const operator[] access
    class VectorRegisterProxy {
    public:
        constexpr VectorRegisterProxy(VectorRegisterFile& rf, std::uint8_t idx) noexcept
            : rf_{rf}, idx_{idx} {}

        /// Implicit conversion to const VectorRegister reference
        operator const VectorRegister&() const noexcept {
            return rf_.read(idx_);
        }

        /// Assignment from VectorRegister
        VectorRegisterProxy& operator=(const VectorRegister& value) noexcept {
            rf_.write(idx_, value);
            return *this;
        }

    private:
        VectorRegisterFile& rf_;
        std::uint8_t idx_;
    };

    /// Array-style access (non-const)
    [[nodiscard]] VectorRegisterProxy operator[](std::uint8_t idx) noexcept {
        return VectorRegisterProxy{*this, idx};
    }

    /// Array-style access (const)
    [[nodiscard]] const VectorRegister& operator[](std::uint8_t idx) const noexcept {
        return read(idx);
    }

    // ========================================================================
    // Bulk Operations
    // ========================================================================

    /// Clear all registers (reset to zero)
    void clear() noexcept {
        for (auto& reg : regs_) {
            reg.clear();
        }
    }

    /// Get the number of registers
    [[nodiscard]] static constexpr std::size_t size() noexcept {
        return register_count;
    }

    /// Get the byte size of the register file
    [[nodiscard]] static constexpr std::size_t byte_size() noexcept {
        return sizeof(VectorRegisterFile);
    }

    /// Get raw view of all registers (for serialization/debugging)
    [[nodiscard]] std::span<const VectorRegister, VECTOR_REGISTER_COUNT>
    raw_view() const noexcept {
        return std::span<const VectorRegister, VECTOR_REGISTER_COUNT>{regs_};
    }

private:
    /// The 32 vector registers (V0-V31)
    /// Note: V0 slot exists but is never written to
    std::array<VectorRegister, VECTOR_REGISTER_COUNT> regs_;

    /// Pre-initialized zero register for V0 reads
    static inline const VectorRegister zero_reg_{};
};

// Static assertions for VectorRegisterFile
static_assert(alignof(VectorRegisterFile) == 64,
              "VectorRegisterFile must be cache-line aligned");
static_assert(sizeof(VectorRegisterFile) == 64 * VECTOR_REGISTER_COUNT,
              "VectorRegisterFile size must be 32 * 64 bytes");

}  // namespace dotvm::core::simd
