#include "dotvm/core/simd/cpu_features.hpp"

#include <sstream>

// Platform detection for CPU feature introspection
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define DOTVM_X86_TARGET 1
    #if defined(_MSC_VER)
        #include <intrin.h>
        #define DOTVM_CPUID_MSVC 1
    #elif defined(__GNUC__) || defined(__clang__)
        #include <cpuid.h>
        #define DOTVM_CPUID_GCC 1
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define DOTVM_ARM64_TARGET 1
    #if defined(__linux__)
        #include <asm/hwcap.h>
        #include <sys/auxv.h>
        #define DOTVM_ARM_HWCAP 1
    #elif defined(__APPLE__)
        #include <sys/sysctl.h>
        #define DOTVM_ARM_SYSCTL 1
    #endif
#endif

namespace dotvm::core::simd {

namespace {

// ============================================================================
// x86-64 CPUID Implementation
// ============================================================================

#if defined(DOTVM_X86_TARGET)

/// Execute CPUID instruction and return results
inline void cpuid(std::uint32_t leaf, std::uint32_t subleaf, std::uint32_t& eax, std::uint32_t& ebx,
                  std::uint32_t& ecx, std::uint32_t& edx) noexcept {
    #if defined(DOTVM_CPUID_MSVC)
    int regs[4];
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    eax = static_cast<std::uint32_t>(regs[0]);
    ebx = static_cast<std::uint32_t>(regs[1]);
    ecx = static_cast<std::uint32_t>(regs[2]);
    edx = static_cast<std::uint32_t>(regs[3]);
    #elif defined(DOTVM_CPUID_GCC)
    __cpuid_count(leaf, subleaf, eax, ebx, ecx, edx);
    #else
    eax = ebx = ecx = edx = 0;
    #endif
}

/// Execute XGETBV instruction to check OS support for AVX state
inline std::uint64_t xgetbv(std::uint32_t xcr) noexcept {
    #if defined(DOTVM_CPUID_MSVC)
    return _xgetbv(xcr);
    #elif defined(DOTVM_CPUID_GCC)
    std::uint32_t eax, edx;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(xcr));
    return (static_cast<std::uint64_t>(edx) << 32) | eax;
    #else
    (void)xcr;
    return 0;
    #endif
}

/// Detect x86-64 CPU features using CPUID
CpuFeatures detect_x86_features() noexcept {
    CpuFeatures features{};

    std::uint32_t eax, ebx, ecx, edx;

    // Get maximum supported leaf
    cpuid(0, 0, eax, ebx, ecx, edx);
    const std::uint32_t max_leaf = eax;

    if (max_leaf < 1) {
        return features;
    }

    // Leaf 1: Basic feature flags
    cpuid(1, 0, eax, ebx, ecx, edx);

    // EDX flags
    features.sse2 = (edx >> 26) & 1;

    // ECX flags
    features.sse3 = (ecx >> 0) & 1;
    features.ssse3 = (ecx >> 9) & 1;
    features.sse4_1 = (ecx >> 19) & 1;
    features.sse4_2 = (ecx >> 20) & 1;
    features.aesni = (ecx >> 25) & 1;
    features.pclmul = (ecx >> 1) & 1;
    features.fma3 = (ecx >> 12) & 1;

    // Check for OSXSAVE (bit 27) before checking AVX
    const bool osxsave = (ecx >> 27) & 1;
    const bool avx_bit = (ecx >> 28) & 1;

    // Get XCR0 once if OSXSAVE is enabled (avoid duplicate xgetbv calls)
    std::uint64_t xcr0 = 0;
    if (osxsave) {
        xcr0 = xgetbv(0);
    }

    // Verify OS support for AVX state (XCR0 bits 1 and 2 must be set)
    const bool os_avx_support = osxsave && avx_bit && ((xcr0 & 0x6) == 0x6);

    if (os_avx_support) {
        features.avx = true;
    }

    // Leaf 7: Extended feature flags (if available)
    if (max_leaf >= 7) {
        cpuid(7, 0, eax, ebx, ecx, edx);

        // EBX flags
        if (os_avx_support) {
            features.avx2 = (ebx >> 5) & 1;
        }
        features.sha = (ebx >> 29) & 1;

        // AVX-512 requires additional OS support (ZMM state)
        // Bits 5, 6, 7 must be set for AVX-512 (opmask, ZMM_Hi256, Hi16_ZMM)
        const bool os_avx512_support = osxsave && ((xcr0 & 0xE6) == 0xE6);

        if (os_avx512_support) {
            features.avx512f = (ebx >> 16) & 1;
            features.avx512bw = (ebx >> 30) & 1;
            features.avx512vl = (ebx >> 31) & 1;
        }
    }

    return features;
}

#endif  // DOTVM_X86_TARGET

// ============================================================================
// ARM Feature Detection
// ============================================================================

#if defined(DOTVM_ARM64_TARGET)

/// Detect ARM64 CPU features
CpuFeatures detect_arm_features() noexcept {
    CpuFeatures features{};

    #if defined(DOTVM_ARM_HWCAP)
    // Linux: Use getauxval() to query hardware capabilities
    const unsigned long hwcap = getauxval(AT_HWCAP);

    features.neon = true;  // NEON is mandatory on AArch64

        // Check cryptography extensions
        #ifdef HWCAP_AES
    features.neon_aes = (hwcap & HWCAP_AES) != 0;
        #endif

        #ifdef HWCAP_SHA2
    features.neon_sha2 = (hwcap & HWCAP_SHA2) != 0;
        #endif

        #ifdef HWCAP_SVE
    features.sve = (hwcap & HWCAP_SVE) != 0;
        #endif

    #elif defined(DOTVM_ARM_SYSCTL)
    // macOS: Use sysctl to query features
    features.neon = true;  // NEON is mandatory on Apple Silicon

    int has_aes = 0;
    std::size_t size = sizeof(has_aes);
    if (sysctlbyname("hw.optional.arm.FEAT_AES", &has_aes, &size, nullptr, 0) == 0) {
        features.neon_aes = (has_aes != 0);
    }

    int has_sha256 = 0;
    size = sizeof(has_sha256);
    if (sysctlbyname("hw.optional.arm.FEAT_SHA256", &has_sha256, &size, nullptr, 0) == 0) {
        features.neon_sha2 = (has_sha256 != 0);
    }

    #else
    // Fallback: Assume basic NEON support on AArch64
    features.neon = true;
    #endif

    return features;
}

#endif  // DOTVM_ARM64_TARGET

// ============================================================================
// Platform-Independent Detection Entry Point
// ============================================================================

/// Internal function to perform actual detection
CpuFeatures detect_features_impl() noexcept {
#if defined(DOTVM_X86_TARGET)
    return detect_x86_features();
#elif defined(DOTVM_ARM64_TARGET)
    return detect_arm_features();
#else
    // Unknown platform: return empty features (scalar-only)
    return CpuFeatures{};
#endif
}

}  // namespace

// ============================================================================
// Public API Implementation
// ============================================================================

const CpuFeatures& detect_cpu_features() noexcept {
    // Thread-safe initialization (C++11 guarantees)
    static const CpuFeatures cached = detect_features_impl();
    return cached;
}

std::string CpuFeatures::feature_string() const {
    std::ostringstream oss;

    if (is_x86()) {
        oss << "x86-64:";
        if (sse2)
            oss << " SSE2";
        if (sse3)
            oss << " SSE3";
        if (ssse3)
            oss << " SSSE3";
        if (sse4_1)
            oss << " SSE4.1";
        if (sse4_2)
            oss << " SSE4.2";
        if (avx)
            oss << " AVX";
        if (avx2)
            oss << " AVX2";
        if (fma3)
            oss << " FMA3";
        if (avx512f)
            oss << " AVX-512F";
        if (avx512bw)
            oss << " AVX-512BW";
        if (avx512vl)
            oss << " AVX-512VL";
        if (aesni)
            oss << " AES-NI";
        if (sha)
            oss << " SHA";
        if (pclmul)
            oss << " PCLMUL";
    } else if (is_arm()) {
        oss << "ARM:";
        if (neon)
            oss << " NEON";
        if (neon_aes)
            oss << " AES";
        if (neon_sha2)
            oss << " SHA2";
        if (sve)
            oss << " SVE";
    } else {
        oss << "Unknown (scalar only)";
    }

    return oss.str();
}

}  // namespace dotvm::core::simd
