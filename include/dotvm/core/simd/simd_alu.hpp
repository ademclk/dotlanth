#pragma once

/// @file simd_alu.hpp
/// @brief SIMD Arithmetic Logic Unit for vector operations
///
/// This header provides the SimdAlu class that performs vector arithmetic
/// operations with runtime dispatch to optimal implementations based on
/// detected CPU features. Includes:
/// - Scalar fallback (always available)
/// - x86-64 AVX2 optimized (256-bit)
/// - x86-64 AVX-512 optimized (512-bit)
/// - ARM NEON optimized (128-bit)

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "cpu_features.hpp"
#include "simd_opcodes.hpp"
#include "vector_register_file.hpp"
#include "vector_types.hpp"
#include "../arch_types.hpp"

// Platform-specific intrinsic headers
#if defined(__x86_64__) || defined(_M_X64)
    #if defined(__AVX2__) || defined(__AVX__)
        #include <immintrin.h>
        #define DOTVM_HAS_AVX2_COMPILE 1
    #endif
    #if defined(__AVX512F__) && defined(__AVX512BW__) && defined(__AVX512VL__)
        #define DOTVM_HAS_AVX512_COMPILE 1
    #endif
    // SSE2 is baseline for x86-64
    #if !defined(__AVX2__) && !defined(__AVX__)
        #include <emmintrin.h>  // SSE2
        #include <xmmintrin.h>  // SSE
    #endif
#endif

#if defined(__ARM_NEON) || defined(__aarch64__)
    #include <arm_neon.h>
    #define DOTVM_HAS_NEON_COMPILE 1
#endif

namespace dotvm::core::simd {

// ============================================================================
// Forward Declarations for Implementation Functions
// ============================================================================

// These are defined in simd_alu.cpp for non-inline platform-specific code
namespace detail {

// AVX2 implementations (256-bit)
#if defined(DOTVM_HAS_AVX2_COMPILE)
Vector256i8 vadd_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept;
Vector256i16 vadd_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept;
Vector256i32 vadd_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept;
Vector256i64 vadd_avx2_i64(const Vector256i64& a, const Vector256i64& b) noexcept;
Vector256f32 vadd_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept;
Vector256f64 vadd_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept;

Vector256i8 vsub_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept;
Vector256i16 vsub_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept;
Vector256i32 vsub_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept;
Vector256i64 vsub_avx2_i64(const Vector256i64& a, const Vector256i64& b) noexcept;
Vector256f32 vsub_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept;
Vector256f64 vsub_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept;

Vector256i16 vmul_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept;
Vector256i32 vmul_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept;
Vector256f32 vmul_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept;
Vector256f64 vmul_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept;

Vector256f32 vdiv_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept;
Vector256f64 vdiv_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept;

float vdot_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept;
double vdot_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept;

Vector256f32 vfma_avx2_f32(const Vector256f32& a, const Vector256f32& b, const Vector256f32& c) noexcept;
Vector256f64 vfma_avx2_f64(const Vector256f64& a, const Vector256f64& b, const Vector256f64& c) noexcept;

Vector256i8 vmin_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept;
Vector256i16 vmin_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept;
Vector256i32 vmin_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept;
Vector256f32 vmin_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept;
Vector256f64 vmin_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept;

Vector256i8 vmax_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept;
Vector256i16 vmax_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept;
Vector256i32 vmax_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept;
Vector256f32 vmax_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept;
Vector256f64 vmax_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept;

Vector256i8 vcmpeq_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept;
Vector256i16 vcmpeq_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept;
Vector256i32 vcmpeq_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept;
Vector256i64 vcmpeq_avx2_i64(const Vector256i64& a, const Vector256i64& b) noexcept;
Vector256f32 vcmpeq_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept;
Vector256f64 vcmpeq_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept;

Vector256i8 vcmplt_avx2_i8(const Vector256i8& a, const Vector256i8& b) noexcept;
Vector256i16 vcmplt_avx2_i16(const Vector256i16& a, const Vector256i16& b) noexcept;
Vector256i32 vcmplt_avx2_i32(const Vector256i32& a, const Vector256i32& b) noexcept;
Vector256i64 vcmplt_avx2_i64(const Vector256i64& a, const Vector256i64& b) noexcept;
Vector256f32 vcmplt_avx2_f32(const Vector256f32& a, const Vector256f32& b) noexcept;
Vector256f64 vcmplt_avx2_f64(const Vector256f64& a, const Vector256f64& b) noexcept;

Vector256i32 vblend_avx2_i32(const Vector256i32& mask, const Vector256i32& a, const Vector256i32& b) noexcept;
Vector256f32 vblend_avx2_f32(const Vector256f32& mask, const Vector256f32& a, const Vector256f32& b) noexcept;
Vector256f64 vblend_avx2_f64(const Vector256f64& mask, const Vector256f64& a, const Vector256f64& b) noexcept;
#endif

// AVX-512 implementations (512-bit)
#if defined(DOTVM_HAS_AVX512_COMPILE)
Vector512i8 vadd_avx512_i8(const Vector512i8& a, const Vector512i8& b) noexcept;
Vector512i16 vadd_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept;
Vector512i32 vadd_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept;
Vector512i64 vadd_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept;
Vector512f32 vadd_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept;
Vector512f64 vadd_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept;

Vector512i8 vsub_avx512_i8(const Vector512i8& a, const Vector512i8& b) noexcept;
Vector512i16 vsub_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept;
Vector512i32 vsub_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept;
Vector512i64 vsub_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept;
Vector512f32 vsub_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept;
Vector512f64 vsub_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept;

Vector512i16 vmul_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept;
Vector512i32 vmul_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept;
Vector512i64 vmul_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept;
Vector512f32 vmul_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept;
Vector512f64 vmul_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept;

Vector512f32 vdiv_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept;
Vector512f64 vdiv_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept;

Vector512f32 vfma_avx512_f32(const Vector512f32& a, const Vector512f32& b, const Vector512f32& c) noexcept;
Vector512f64 vfma_avx512_f64(const Vector512f64& a, const Vector512f64& b, const Vector512f64& c) noexcept;

Vector512i8 vmin_avx512_i8(const Vector512i8& a, const Vector512i8& b) noexcept;
Vector512i16 vmin_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept;
Vector512i32 vmin_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept;
Vector512i64 vmin_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept;
Vector512f32 vmin_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept;
Vector512f64 vmin_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept;

Vector512i8 vmax_avx512_i8(const Vector512i8& a, const Vector512i8& b) noexcept;
Vector512i16 vmax_avx512_i16(const Vector512i16& a, const Vector512i16& b) noexcept;
Vector512i32 vmax_avx512_i32(const Vector512i32& a, const Vector512i32& b) noexcept;
Vector512i64 vmax_avx512_i64(const Vector512i64& a, const Vector512i64& b) noexcept;
Vector512f32 vmax_avx512_f32(const Vector512f32& a, const Vector512f32& b) noexcept;
Vector512f64 vmax_avx512_f64(const Vector512f64& a, const Vector512f64& b) noexcept;
#endif

// ARM NEON implementations (128-bit)
#if defined(DOTVM_HAS_NEON_COMPILE)
Vector128i8 vadd_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept;
Vector128i16 vadd_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept;
Vector128i32 vadd_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept;
Vector128i64 vadd_neon_i64(const Vector128i64& a, const Vector128i64& b) noexcept;
Vector128f32 vadd_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept;
Vector128f64 vadd_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept;

Vector128i8 vsub_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept;
Vector128i16 vsub_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept;
Vector128i32 vsub_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept;
Vector128i64 vsub_neon_i64(const Vector128i64& a, const Vector128i64& b) noexcept;
Vector128f32 vsub_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept;
Vector128f64 vsub_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept;

Vector128i8 vmul_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept;
Vector128i16 vmul_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept;
Vector128i32 vmul_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept;
Vector128f32 vmul_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept;
Vector128f64 vmul_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept;

Vector128f32 vdiv_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept;
Vector128f64 vdiv_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept;

float vdot_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept;
double vdot_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept;

Vector128f32 vfma_neon_f32(const Vector128f32& a, const Vector128f32& b, const Vector128f32& c) noexcept;
Vector128f64 vfma_neon_f64(const Vector128f64& a, const Vector128f64& b, const Vector128f64& c) noexcept;

Vector128i8 vmin_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept;
Vector128i16 vmin_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept;
Vector128i32 vmin_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept;
Vector128f32 vmin_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept;
Vector128f64 vmin_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept;

Vector128i8 vmax_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept;
Vector128i16 vmax_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept;
Vector128i32 vmax_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept;
Vector128f32 vmax_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept;
Vector128f64 vmax_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept;

Vector128i8 vcmpeq_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept;
Vector128i16 vcmpeq_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept;
Vector128i32 vcmpeq_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept;
Vector128i64 vcmpeq_neon_i64(const Vector128i64& a, const Vector128i64& b) noexcept;
Vector128f32 vcmpeq_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept;
Vector128f64 vcmpeq_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept;

Vector128i8 vcmplt_neon_i8(const Vector128i8& a, const Vector128i8& b) noexcept;
Vector128i16 vcmplt_neon_i16(const Vector128i16& a, const Vector128i16& b) noexcept;
Vector128i32 vcmplt_neon_i32(const Vector128i32& a, const Vector128i32& b) noexcept;
Vector128i64 vcmplt_neon_i64(const Vector128i64& a, const Vector128i64& b) noexcept;
Vector128f32 vcmplt_neon_f32(const Vector128f32& a, const Vector128f32& b) noexcept;
Vector128f64 vcmplt_neon_f64(const Vector128f64& a, const Vector128f64& b) noexcept;

Vector128i32 vblend_neon_i32(const Vector128i32& mask, const Vector128i32& a, const Vector128i32& b) noexcept;
Vector128f32 vblend_neon_f32(const Vector128f32& mask, const Vector128f32& a, const Vector128f32& b) noexcept;
Vector128f64 vblend_neon_f64(const Vector128f64& mask, const Vector128f64& a, const Vector128f64& b) noexcept;
#endif

}  // namespace detail

// ============================================================================
// Scalar Fallback Implementations (always available)
// ============================================================================

namespace scalar {

/// Generic scalar add for all vector types
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vadd(const Vector<Width, Lane>& a,
                                        const Vector<Width, Lane>& b) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        result[i] = static_cast<Lane>(a[i] + b[i]);
    }
    return result;
}

/// Generic scalar subtract for all vector types
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vsub(const Vector<Width, Lane>& a,
                                        const Vector<Width, Lane>& b) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        result[i] = static_cast<Lane>(a[i] - b[i]);
    }
    return result;
}

/// Generic scalar multiply for all vector types
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vmul(const Vector<Width, Lane>& a,
                                        const Vector<Width, Lane>& b) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        result[i] = static_cast<Lane>(a[i] * b[i]);
    }
    return result;
}

/// Generic scalar divide for all vector types
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vdiv(const Vector<Width, Lane>& a,
                                        const Vector<Width, Lane>& b) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        if constexpr (std::is_floating_point_v<Lane>) {
            result[i] = a[i] / b[i];  // IEEE semantics for div by zero
        } else {
            result[i] = (b[i] != 0) ? static_cast<Lane>(a[i] / b[i]) : Lane{0};
        }
    }
    return result;
}

/// Generic scalar dot product
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Lane vdot(const Vector<Width, Lane>& a,
                        const Vector<Width, Lane>& b) noexcept {
    Lane sum{0};
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        sum = static_cast<Lane>(sum + a[i] * b[i]);
    }
    return sum;
}

/// Generic scalar fused multiply-add: a * b + c
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vfma(const Vector<Width, Lane>& a,
                                        const Vector<Width, Lane>& b,
                                        const Vector<Width, Lane>& c) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        if constexpr (std::is_floating_point_v<Lane>) {
            result[i] = std::fma(a[i], b[i], c[i]);
        } else {
            result[i] = static_cast<Lane>(a[i] * b[i] + c[i]);
        }
    }
    return result;
}

/// Generic scalar minimum
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vmin(const Vector<Width, Lane>& a,
                                        const Vector<Width, Lane>& b) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        if constexpr (std::is_floating_point_v<Lane>) {
            result[i] = std::fmin(a[i], b[i]);
        } else {
            result[i] = (a[i] < b[i]) ? a[i] : b[i];
        }
    }
    return result;
}

/// Generic scalar maximum
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vmax(const Vector<Width, Lane>& a,
                                        const Vector<Width, Lane>& b) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        if constexpr (std::is_floating_point_v<Lane>) {
            result[i] = std::fmax(a[i], b[i]);
        } else {
            result[i] = (a[i] > b[i]) ? a[i] : b[i];
        }
    }
    return result;
}

/// Generic scalar compare equal
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vcmpeq(const Vector<Width, Lane>& a,
                                          const Vector<Width, Lane>& b) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        if constexpr (std::is_floating_point_v<Lane>) {
            if (a[i] == b[i]) {
                if constexpr (std::is_same_v<Lane, float>) {
                    std::uint32_t bits = 0xFFFFFFFF;
                    std::memcpy(&result[i], &bits, sizeof(Lane));
                } else {
                    std::uint64_t bits = 0xFFFFFFFFFFFFFFFFULL;
                    std::memcpy(&result[i], &bits, sizeof(Lane));
                }
            } else {
                result[i] = Lane{0};
            }
        } else {
            result[i] = (a[i] == b[i]) ? static_cast<Lane>(-1) : Lane{0};
        }
    }
    return result;
}

/// Generic scalar compare less than
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vcmplt(const Vector<Width, Lane>& a,
                                          const Vector<Width, Lane>& b) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        if constexpr (std::is_floating_point_v<Lane>) {
            if (a[i] < b[i]) {
                if constexpr (std::is_same_v<Lane, float>) {
                    std::uint32_t bits = 0xFFFFFFFF;
                    std::memcpy(&result[i], &bits, sizeof(Lane));
                } else {
                    std::uint64_t bits = 0xFFFFFFFFFFFFFFFFULL;
                    std::memcpy(&result[i], &bits, sizeof(Lane));
                }
            } else {
                result[i] = Lane{0};
            }
        } else {
            result[i] = (a[i] < b[i]) ? static_cast<Lane>(-1) : Lane{0};
        }
    }
    return result;
}

/// Generic scalar blend: mask ? a : b
template<std::size_t Width, LaneType Lane>
[[nodiscard]] Vector<Width, Lane> vblend(const Vector<Width, Lane>& mask,
                                          const Vector<Width, Lane>& a,
                                          const Vector<Width, Lane>& b) noexcept {
    Vector<Width, Lane> result;
    for (std::size_t i = 0; i < Vector<Width, Lane>::lane_count; ++i) {
        if constexpr (std::is_floating_point_v<Lane>) {
            if constexpr (std::is_same_v<Lane, float>) {
                std::uint32_t bits;
                std::memcpy(&bits, &mask[i], sizeof(bits));
                result[i] = (bits & 0x80000000) ? a[i] : b[i];
            } else {
                std::uint64_t bits;
                std::memcpy(&bits, &mask[i], sizeof(bits));
                result[i] = (bits & 0x8000000000000000ULL) ? a[i] : b[i];
            }
        } else {
            result[i] = (mask[i] < 0) ? a[i] : b[i];
        }
    }
    return result;
}

}  // namespace scalar

// ============================================================================
// SIMD ALU Class
// ============================================================================

/// SIMD Arithmetic Logic Unit
///
/// Provides vector arithmetic operations with runtime dispatch based on
/// CPU capabilities. The ALU can be configured to use a specific architecture
/// or auto-detect the optimal one.
///
/// @note All operations are performed on the VectorRegisterFile through
///       register indices, following the same pattern as scalar operations.
class SimdAlu {
public:
    // ========================================================================
    // Construction
    // ========================================================================

    /// Construct SimdAlu with specified architecture and CPU features
    ///
    /// @param arch Target SIMD architecture
    /// @param features CPU features (optional, auto-detected if not provided)
    explicit SimdAlu(Architecture arch = Architecture::Arch128,
                     const CpuFeatures* features = nullptr) noexcept
        : arch_{arch},
          features_{features ? *features : detect_cpu_features()} {
        // Validate architecture against CPU capabilities
        if (!features_.supports_arch(arch_)) {
            // Fall back to highest supported
            arch_ = select_optimal_simd_arch();
        }
    }

    /// Construct with auto-detected optimal architecture
    ///
    /// @param features CPU features (optional, auto-detected if not provided)
    [[nodiscard]] static SimdAlu auto_detect(const CpuFeatures* features = nullptr) noexcept {
        const auto& feat = features ? *features : detect_cpu_features();
        return SimdAlu{select_optimal_simd_arch(), &feat};
    }

    // ========================================================================
    // Configuration Access
    // ========================================================================

    /// Get the active SIMD architecture
    [[nodiscard]] Architecture arch() const noexcept { return arch_; }

    /// Get the CPU features
    [[nodiscard]] const CpuFeatures& features() const noexcept { return features_; }

    /// Get the vector width in bits
    [[nodiscard]] std::size_t vector_width_bits() const noexcept {
        return arch_bit_width(arch_);
    }

    /// Get the vector width in bytes
    [[nodiscard]] std::size_t vector_width_bytes() const noexcept {
        return vector_width_bits() / 8;
    }

    /// Check if a specific vector width is supported
    [[nodiscard]] bool supports_width(std::size_t bits) const noexcept {
        return features_.max_vector_width() >= bits;
    }

    // ========================================================================
    // Templated Vector Operations with Runtime Dispatch
    // ========================================================================

    /// Vector add: result[i] = a[i] + b[i]
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vadd(const Vector<Width, Lane>& a,
                                            const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vadd(a, b);
    }

    /// Vector subtract: result[i] = a[i] - b[i]
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vsub(const Vector<Width, Lane>& a,
                                            const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vsub(a, b);
    }

    /// Vector multiply: result[i] = a[i] * b[i]
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vmul(const Vector<Width, Lane>& a,
                                            const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vmul(a, b);
    }

    /// Vector divide: result[i] = a[i] / b[i]
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vdiv(const Vector<Width, Lane>& a,
                                            const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vdiv(a, b);
    }

    /// Dot product: result = sum(a[i] * b[i])
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Lane vdot(const Vector<Width, Lane>& a,
                            const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vdot(a, b);
    }

    /// Fused multiply-add: result[i] = a[i] * b[i] + c[i]
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vfma(const Vector<Width, Lane>& a,
                                            const Vector<Width, Lane>& b,
                                            const Vector<Width, Lane>& c) const noexcept {
        return dispatch_vfma(a, b, c);
    }

    /// Vector minimum: result[i] = min(a[i], b[i])
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vmin(const Vector<Width, Lane>& a,
                                            const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vmin(a, b);
    }

    /// Vector maximum: result[i] = max(a[i], b[i])
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vmax(const Vector<Width, Lane>& a,
                                            const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vmax(a, b);
    }

    /// Compare equal: result[i] = (a[i] == b[i]) ? all_ones : 0
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vcmpeq(const Vector<Width, Lane>& a,
                                              const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vcmpeq(a, b);
    }

    /// Compare less than: result[i] = (a[i] < b[i]) ? all_ones : 0
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vcmplt(const Vector<Width, Lane>& a,
                                              const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vcmplt(a, b);
    }

    /// Blend: result[i] = mask[i] ? a[i] : b[i]
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> vblend(const Vector<Width, Lane>& mask,
                                              const Vector<Width, Lane>& a,
                                              const Vector<Width, Lane>& b) const noexcept {
        return dispatch_vblend(mask, a, b);
    }

    // ========================================================================
    // Legacy Non-Templated Operations (for compatibility with existing code)
    // ========================================================================

    // --- 128-bit operations ---
    [[nodiscard]] Vector128i32 add_v128i32(const Vector128i32& a,
                                           const Vector128i32& b) const noexcept {
        return vadd(a, b);
    }

    [[nodiscard]] Vector128i32 sub_v128i32(const Vector128i32& a,
                                           const Vector128i32& b) const noexcept {
        return vsub(a, b);
    }

    [[nodiscard]] Vector128i32 mul_v128i32(const Vector128i32& a,
                                           const Vector128i32& b) const noexcept {
        return vmul(a, b);
    }

    [[nodiscard]] Vector128f32 add_v128f32(const Vector128f32& a,
                                           const Vector128f32& b) const noexcept {
        return vadd(a, b);
    }

    [[nodiscard]] Vector128f32 sub_v128f32(const Vector128f32& a,
                                           const Vector128f32& b) const noexcept {
        return vsub(a, b);
    }

    [[nodiscard]] Vector128f32 mul_v128f32(const Vector128f32& a,
                                           const Vector128f32& b) const noexcept {
        return vmul(a, b);
    }

    [[nodiscard]] Vector128f32 div_v128f32(const Vector128f32& a,
                                           const Vector128f32& b) const noexcept {
        return vdiv(a, b);
    }

    [[nodiscard]] Vector128f32 fma_v128f32(const Vector128f32& a,
                                           const Vector128f32& b,
                                           const Vector128f32& c) const noexcept {
        return vfma(a, b, c);
    }

    [[nodiscard]] float dot_v128f32(const Vector128f32& a,
                                    const Vector128f32& b) const noexcept {
        return vdot(a, b);
    }

    // --- 256-bit operations ---
    [[nodiscard]] Vector256i32 add_v256i32(const Vector256i32& a,
                                           const Vector256i32& b) const noexcept {
        return vadd(a, b);
    }

    [[nodiscard]] Vector256i32 sub_v256i32(const Vector256i32& a,
                                           const Vector256i32& b) const noexcept {
        return vsub(a, b);
    }

    [[nodiscard]] Vector256i32 mul_v256i32(const Vector256i32& a,
                                           const Vector256i32& b) const noexcept {
        return vmul(a, b);
    }

    [[nodiscard]] Vector256f32 add_v256f32(const Vector256f32& a,
                                           const Vector256f32& b) const noexcept {
        return vadd(a, b);
    }

    [[nodiscard]] Vector256f32 fma_v256f32(const Vector256f32& a,
                                           const Vector256f32& b,
                                           const Vector256f32& c) const noexcept {
        return vfma(a, b, c);
    }

    [[nodiscard]] float dot_v256f32(const Vector256f32& a,
                                    const Vector256f32& b) const noexcept {
        return vdot(a, b);
    }

    // --- 512-bit operations ---
    [[nodiscard]] Vector512i32 add_v512i32(const Vector512i32& a,
                                           const Vector512i32& b) const noexcept {
        return vadd(a, b);
    }

    [[nodiscard]] Vector512i32 sub_v512i32(const Vector512i32& a,
                                           const Vector512i32& b) const noexcept {
        return vsub(a, b);
    }

    [[nodiscard]] Vector512f32 add_v512f32(const Vector512f32& a,
                                           const Vector512f32& b) const noexcept {
        return vadd(a, b);
    }

    [[nodiscard]] Vector512f32 fma_v512f32(const Vector512f32& a,
                                           const Vector512f32& b,
                                           const Vector512f32& c) const noexcept {
        return vfma(a, b, c);
    }

    [[nodiscard]] float dot_v512f32(const Vector512f32& a,
                                    const Vector512f32& b) const noexcept {
        return vdot(a, b);
    }

    // ========================================================================
    // Register-Based Operations
    // ========================================================================

    /// Perform vector add on register file (V[vd] = V[vs1] + V[vs2])
    void vadd_i32(VectorRegisterFile& regs,
                  std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2) const noexcept {
        switch (arch_) {
            case Architecture::Arch128: {
                auto a = regs.read_v128i32(vs1);
                auto b = regs.read_v128i32(vs2);
                regs.write_v128i32(vd, vadd(a, b));
                break;
            }
            case Architecture::Arch256: {
                auto a = regs.read_v256i32(vs1);
                auto b = regs.read_v256i32(vs2);
                regs.write_v256i32(vd, vadd(a, b));
                break;
            }
            case Architecture::Arch512: {
                auto a = regs.read_v512i32(vs1);
                auto b = regs.read_v512i32(vs2);
                regs.write_v512i32(vd, vadd(a, b));
                break;
            }
            default:
                auto a = regs.read_v128i32(vs1);
                auto b = regs.read_v128i32(vs2);
                regs.write_v128i32(vd, vadd(a, b));
                break;
        }
    }

    /// Perform vector subtract on register file (V[vd] = V[vs1] - V[vs2])
    void vsub_i32(VectorRegisterFile& regs,
                  std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2) const noexcept {
        switch (arch_) {
            case Architecture::Arch128: {
                auto a = regs.read_v128i32(vs1);
                auto b = regs.read_v128i32(vs2);
                regs.write_v128i32(vd, vsub(a, b));
                break;
            }
            case Architecture::Arch256: {
                auto a = regs.read_v256i32(vs1);
                auto b = regs.read_v256i32(vs2);
                regs.write_v256i32(vd, vsub(a, b));
                break;
            }
            case Architecture::Arch512: {
                auto a = regs.read_v512i32(vs1);
                auto b = regs.read_v512i32(vs2);
                regs.write_v512i32(vd, vsub(a, b));
                break;
            }
            default:
                auto a = regs.read_v128i32(vs1);
                auto b = regs.read_v128i32(vs2);
                regs.write_v128i32(vd, vsub(a, b));
                break;
        }
    }

    /// Perform vector multiply on register file (V[vd] = V[vs1] * V[vs2])
    void vmul_i32(VectorRegisterFile& regs,
                  std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2) const noexcept {
        switch (arch_) {
            case Architecture::Arch128: {
                auto a = regs.read_v128i32(vs1);
                auto b = regs.read_v128i32(vs2);
                regs.write_v128i32(vd, vmul(a, b));
                break;
            }
            case Architecture::Arch256: {
                auto a = regs.read_v256i32(vs1);
                auto b = regs.read_v256i32(vs2);
                regs.write_v256i32(vd, vmul(a, b));
                break;
            }
            default:
                auto a = regs.read_v128i32(vs1);
                auto b = regs.read_v128i32(vs2);
                regs.write_v128i32(vd, vmul(a, b));
                break;
        }
    }

private:
    Architecture arch_;
    CpuFeatures features_;

    // ========================================================================
    // Dispatch Helpers - Select best implementation at runtime
    // ========================================================================

    // --- VADD dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vadd(const Vector<Width, Lane>& a,
                                                     const Vector<Width, Lane>& b) const noexcept {
        // 256-bit AVX2 dispatch
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256) {
            if (features_.avx2) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vadd_avx2_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vadd_avx2_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vadd_avx2_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vadd_avx2_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vadd_avx2_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vadd_avx2_f64(a, b);
            }
        }
        #endif

        // 512-bit AVX-512 dispatch
        #if defined(DOTVM_HAS_AVX512_COMPILE)
        if constexpr (Width == 512) {
            if (features_.avx512f && features_.avx512bw) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vadd_avx512_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vadd_avx512_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vadd_avx512_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vadd_avx512_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vadd_avx512_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vadd_avx512_f64(a, b);
            }
        }
        #endif

        // 128-bit NEON dispatch
        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vadd_neon_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vadd_neon_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vadd_neon_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vadd_neon_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vadd_neon_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vadd_neon_f64(a, b);
            }
        }
        #endif

        // Scalar fallback
        return scalar::vadd(a, b);
    }

    // --- VSUB dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vsub(const Vector<Width, Lane>& a,
                                                     const Vector<Width, Lane>& b) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256) {
            if (features_.avx2) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vsub_avx2_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vsub_avx2_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vsub_avx2_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vsub_avx2_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vsub_avx2_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vsub_avx2_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_AVX512_COMPILE)
        if constexpr (Width == 512) {
            if (features_.avx512f && features_.avx512bw) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vsub_avx512_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vsub_avx512_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vsub_avx512_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vsub_avx512_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vsub_avx512_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vsub_avx512_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vsub_neon_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vsub_neon_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vsub_neon_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vsub_neon_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vsub_neon_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vsub_neon_f64(a, b);
            }
        }
        #endif

        return scalar::vsub(a, b);
    }

    // --- VMUL dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vmul(const Vector<Width, Lane>& a,
                                                     const Vector<Width, Lane>& b) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256) {
            if (features_.avx2) {
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vmul_avx2_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vmul_avx2_i32(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vmul_avx2_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vmul_avx2_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_AVX512_COMPILE)
        if constexpr (Width == 512) {
            if (features_.avx512f && features_.avx512bw) {
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vmul_avx512_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vmul_avx512_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vmul_avx512_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vmul_avx512_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vmul_avx512_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vmul_neon_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vmul_neon_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vmul_neon_i32(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vmul_neon_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vmul_neon_f64(a, b);
            }
        }
        #endif

        return scalar::vmul(a, b);
    }

    // --- VDIV dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vdiv(const Vector<Width, Lane>& a,
                                                     const Vector<Width, Lane>& b) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256 && std::is_floating_point_v<Lane>) {
            if (features_.avx2) {
                if constexpr (std::is_same_v<Lane, float>)  return detail::vdiv_avx2_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>) return detail::vdiv_avx2_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_AVX512_COMPILE)
        if constexpr (Width == 512 && std::is_floating_point_v<Lane>) {
            if (features_.avx512f) {
                if constexpr (std::is_same_v<Lane, float>)  return detail::vdiv_avx512_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>) return detail::vdiv_avx512_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128 && std::is_floating_point_v<Lane>) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, float>)  return detail::vdiv_neon_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>) return detail::vdiv_neon_f64(a, b);
            }
        }
        #endif

        return scalar::vdiv(a, b);
    }

    // --- VDOT dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Lane dispatch_vdot(const Vector<Width, Lane>& a,
                                      const Vector<Width, Lane>& b) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256 && std::is_floating_point_v<Lane>) {
            if (features_.avx2 && features_.fma3) {
                if constexpr (std::is_same_v<Lane, float>)  return detail::vdot_avx2_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>) return detail::vdot_avx2_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128 && std::is_floating_point_v<Lane>) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, float>)  return detail::vdot_neon_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>) return detail::vdot_neon_f64(a, b);
            }
        }
        #endif

        return scalar::vdot(a, b);
    }

    // --- VFMA dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vfma(const Vector<Width, Lane>& a,
                                                     const Vector<Width, Lane>& b,
                                                     const Vector<Width, Lane>& c) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256 && std::is_floating_point_v<Lane>) {
            if (features_.avx2 && features_.fma3) {
                if constexpr (std::is_same_v<Lane, float>)  return detail::vfma_avx2_f32(a, b, c);
                if constexpr (std::is_same_v<Lane, double>) return detail::vfma_avx2_f64(a, b, c);
            }
        }
        #endif

        #if defined(DOTVM_HAS_AVX512_COMPILE)
        if constexpr (Width == 512 && std::is_floating_point_v<Lane>) {
            if (features_.avx512f) {
                if constexpr (std::is_same_v<Lane, float>)  return detail::vfma_avx512_f32(a, b, c);
                if constexpr (std::is_same_v<Lane, double>) return detail::vfma_avx512_f64(a, b, c);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128 && std::is_floating_point_v<Lane>) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, float>)  return detail::vfma_neon_f32(a, b, c);
                if constexpr (std::is_same_v<Lane, double>) return detail::vfma_neon_f64(a, b, c);
            }
        }
        #endif

        return scalar::vfma(a, b, c);
    }

    // --- VMIN dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vmin(const Vector<Width, Lane>& a,
                                                     const Vector<Width, Lane>& b) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256) {
            if (features_.avx2) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vmin_avx2_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vmin_avx2_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vmin_avx2_i32(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vmin_avx2_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vmin_avx2_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_AVX512_COMPILE)
        if constexpr (Width == 512) {
            if (features_.avx512f && features_.avx512bw) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vmin_avx512_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vmin_avx512_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vmin_avx512_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vmin_avx512_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vmin_avx512_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vmin_avx512_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vmin_neon_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vmin_neon_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vmin_neon_i32(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vmin_neon_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vmin_neon_f64(a, b);
            }
        }
        #endif

        return scalar::vmin(a, b);
    }

    // --- VMAX dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vmax(const Vector<Width, Lane>& a,
                                                     const Vector<Width, Lane>& b) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256) {
            if (features_.avx2) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vmax_avx2_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vmax_avx2_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vmax_avx2_i32(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vmax_avx2_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vmax_avx2_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_AVX512_COMPILE)
        if constexpr (Width == 512) {
            if (features_.avx512f && features_.avx512bw) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vmax_avx512_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vmax_avx512_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vmax_avx512_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vmax_avx512_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vmax_avx512_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vmax_avx512_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vmax_neon_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vmax_neon_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vmax_neon_i32(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vmax_neon_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vmax_neon_f64(a, b);
            }
        }
        #endif

        return scalar::vmax(a, b);
    }

    // --- VCMPEQ dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vcmpeq(const Vector<Width, Lane>& a,
                                                       const Vector<Width, Lane>& b) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256) {
            if (features_.avx2) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vcmpeq_avx2_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vcmpeq_avx2_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vcmpeq_avx2_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vcmpeq_avx2_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vcmpeq_avx2_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vcmpeq_avx2_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vcmpeq_neon_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vcmpeq_neon_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vcmpeq_neon_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vcmpeq_neon_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vcmpeq_neon_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vcmpeq_neon_f64(a, b);
            }
        }
        #endif

        return scalar::vcmpeq(a, b);
    }

    // --- VCMPLT dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vcmplt(const Vector<Width, Lane>& a,
                                                       const Vector<Width, Lane>& b) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256) {
            if (features_.avx2) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vcmplt_avx2_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vcmplt_avx2_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vcmplt_avx2_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vcmplt_avx2_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vcmplt_avx2_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vcmplt_avx2_f64(a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, std::int8_t>)  return detail::vcmplt_neon_i8(a, b);
                if constexpr (std::is_same_v<Lane, std::int16_t>) return detail::vcmplt_neon_i16(a, b);
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vcmplt_neon_i32(a, b);
                if constexpr (std::is_same_v<Lane, std::int64_t>) return detail::vcmplt_neon_i64(a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vcmplt_neon_f32(a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vcmplt_neon_f64(a, b);
            }
        }
        #endif

        return scalar::vcmplt(a, b);
    }

    // --- VBLEND dispatch ---
    template<std::size_t Width, LaneType Lane>
    [[nodiscard]] Vector<Width, Lane> dispatch_vblend(const Vector<Width, Lane>& mask,
                                                       const Vector<Width, Lane>& a,
                                                       const Vector<Width, Lane>& b) const noexcept {
        #if defined(DOTVM_HAS_AVX2_COMPILE)
        if constexpr (Width == 256) {
            if (features_.avx2) {
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vblend_avx2_i32(mask, a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vblend_avx2_f32(mask, a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vblend_avx2_f64(mask, a, b);
            }
        }
        #endif

        #if defined(DOTVM_HAS_NEON_COMPILE)
        if constexpr (Width == 128) {
            if (features_.neon) {
                if constexpr (std::is_same_v<Lane, std::int32_t>) return detail::vblend_neon_i32(mask, a, b);
                if constexpr (std::is_same_v<Lane, float>)        return detail::vblend_neon_f32(mask, a, b);
                if constexpr (std::is_same_v<Lane, double>)       return detail::vblend_neon_f64(mask, a, b);
            }
        }
        #endif

        return scalar::vblend(mask, a, b);
    }
};

// ============================================================================
// Factory Functions
// ============================================================================

/// Create a SimdAlu with the optimal architecture for the current CPU
[[nodiscard]] inline std::unique_ptr<SimdAlu> make_simd_alu() noexcept {
    return std::make_unique<SimdAlu>(SimdAlu::auto_detect());
}

/// Create a SimdAlu with a specific architecture
///
/// @param arch Target architecture
/// @return SimdAlu instance, falling back to supported arch if necessary
[[nodiscard]] inline std::unique_ptr<SimdAlu> make_simd_alu(Architecture arch) noexcept {
    return std::make_unique<SimdAlu>(arch);
}

}  // namespace dotvm::core::simd
