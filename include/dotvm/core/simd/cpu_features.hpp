#pragma once

/// @file cpu_features.hpp
/// @brief CPU feature detection for SIMD and cryptographic instruction support
///
/// This header provides runtime detection of CPU capabilities including:
/// - x86-64: SSE, AVX, AVX2, AVX-512, AES-NI, SHA extensions
/// - ARM: NEON, Cryptography extensions
///
/// Detection is performed once at program startup and cached for subsequent queries.

#include <cstddef>
#include <cstdint>
#include <string>

#include "../arch_types.hpp"

namespace dotvm::core::simd {

// ============================================================================
// CPU Feature Flags Structure
// ============================================================================

/// CPU feature flags for SIMD and cryptographic instruction support
///
/// Features are detected at runtime using CPUID (x86-64) or system registers (ARM).
/// On unsupported platforms, all features default to false.
struct CpuFeatures {
    // ========================================================================
    // x86-64 SIMD Features
    // ========================================================================

    /// SSE2 (128-bit integer SIMD) - baseline for x86-64
    bool sse2 : 1 = false;

    /// SSE3 (128-bit SIMD with horizontal operations)
    bool sse3 : 1 = false;

    /// SSSE3 (Supplemental SSE3)
    bool ssse3 : 1 = false;

    /// SSE4.1 (128-bit SIMD with blending, rounding)
    bool sse4_1 : 1 = false;

    /// SSE4.2 (128-bit SIMD with string/text processing)
    bool sse4_2 : 1 = false;

    /// AVX (256-bit floating-point SIMD)
    bool avx : 1 = false;

    /// AVX2 (256-bit integer SIMD)
    bool avx2 : 1 = false;

    /// AVX-512 Foundation (512-bit SIMD base)
    bool avx512f : 1 = false;

    /// AVX-512 Byte/Word instructions
    bool avx512bw : 1 = false;

    /// AVX-512 Vector Length extensions
    bool avx512vl : 1 = false;

    /// FMA3 (Fused Multiply-Add, 3 operands)
    bool fma3 : 1 = false;

    // ========================================================================
    // x86-64 Cryptographic Extensions
    // ========================================================================

    /// AES-NI (Hardware AES encryption/decryption)
    bool aesni : 1 = false;

    /// SHA extensions (Hardware SHA-1/SHA-256)
    bool sha : 1 = false;

    /// PCLMULQDQ (Carry-less multiplication for GCM)
    bool pclmul : 1 = false;

    // ========================================================================
    // ARM Features
    // ========================================================================

    /// ARM NEON (128-bit SIMD)
    bool neon : 1 = false;

    /// ARM Cryptography Extensions - AES
    bool neon_aes : 1 = false;

    /// ARM Cryptography Extensions - SHA256
    bool neon_sha2 : 1 = false;

    /// ARM Scalable Vector Extension (variable width SIMD)
    bool sve : 1 = false;

    // ========================================================================
    // Query Methods
    // ========================================================================

    /// Get the maximum supported vector width in bits
    ///
    /// @return Maximum SIMD width (512, 256, 128) or 64 for scalar-only
    [[nodiscard]] constexpr std::size_t max_vector_width() const noexcept {
        if (avx512f)
            return 512;
        if (avx2 || avx)
            return 256;
        if (sse2 || neon)
            return 128;
        return 64;  // Scalar fallback
    }

    /// Check if a specific architecture is supported by the CPU
    ///
    /// @param arch Target architecture to check
    /// @return true if CPU supports the architecture's requirements
    [[nodiscard]] constexpr bool supports_arch(Architecture arch) const noexcept {
        switch (arch) {
            case Architecture::Arch32:
            case Architecture::Arch64:
                return true;  // Always supported

            case Architecture::Arch128:
                return sse2 || neon;  // SSE2 (x86) or NEON (ARM)

            case Architecture::Arch256:
                return avx2;  // Requires AVX2 for integer operations

            case Architecture::Arch512:
                return avx512f && avx512bw && avx512vl;  // Full AVX-512 suite
        }
        return false;
    }

    /// Check if this is an x86-64 CPU
    [[nodiscard]] constexpr bool is_x86() const noexcept {
        // SSE2 is mandatory for x86-64, so its presence indicates x86
        return sse2 && !neon;
    }

    /// Check if this is an ARM CPU
    [[nodiscard]] constexpr bool is_arm() const noexcept { return neon && !sse2; }

    /// Check if AES hardware acceleration is available
    [[nodiscard]] constexpr bool has_aes_acceleration() const noexcept { return aesni || neon_aes; }

    /// Check if SHA hardware acceleration is available
    [[nodiscard]] constexpr bool has_sha_acceleration() const noexcept { return sha || neon_sha2; }

    /// Get a human-readable string describing detected features
    ///
    /// @return Feature string (e.g., "x86-64: SSE2 AVX2 AES-NI")
    [[nodiscard]] std::string feature_string() const;
};

// ============================================================================
// Detection Functions
// ============================================================================

/// Detect CPU features at runtime
///
/// This function performs CPU feature detection using platform-specific mechanisms:
/// - x86-64: CPUID instruction with XGETBV for AVX state verification
/// - ARM: System register queries or compile-time detection
/// - Other: All features default to false
///
/// Results are cached after the first call for efficiency.
///
/// @return Reference to the cached CpuFeatures structure
[[nodiscard]] const CpuFeatures& detect_cpu_features() noexcept;

/// Check if a specific vector width is supported
///
/// @param bits Vector width in bits (128, 256, or 512)
/// @return true if the CPU supports vectors of this width
[[nodiscard]] inline bool is_vector_width_supported(std::size_t bits) noexcept {
    const auto& features = detect_cpu_features();
    return features.max_vector_width() >= bits;
}

/// Select the optimal SIMD architecture based on CPU capabilities
///
/// @return The highest-capability SIMD architecture supported
[[nodiscard]] inline Architecture select_optimal_simd_arch() noexcept {
    const auto& features = detect_cpu_features();

    if (features.supports_arch(Architecture::Arch512)) {
        return Architecture::Arch512;
    }
    if (features.supports_arch(Architecture::Arch256)) {
        return Architecture::Arch256;
    }
    if (features.supports_arch(Architecture::Arch128)) {
        return Architecture::Arch128;
    }
    return Architecture::Arch64;
}

}  // namespace dotvm::core::simd
