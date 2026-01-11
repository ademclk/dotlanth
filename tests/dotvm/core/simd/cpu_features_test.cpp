/// @file cpu_features_test.cpp
/// @brief Unit tests for CPU feature detection and architecture extension

#include <gtest/gtest.h>

#include <iostream>

#include "dotvm/core/arch_types.hpp"
#include "dotvm/core/simd/cpu_features.hpp"

namespace dotvm::core::simd {
namespace {

// ============================================================================
// Architecture Enum Extension Tests
// ============================================================================

class ArchitectureEnumTest : public ::testing::Test {};

TEST_F(ArchitectureEnumTest, IsValidArchitecture_AllValuesValid) {
    EXPECT_TRUE(is_valid_architecture(Architecture::Arch32));
    EXPECT_TRUE(is_valid_architecture(Architecture::Arch64));
    EXPECT_TRUE(is_valid_architecture(Architecture::Arch128));
    EXPECT_TRUE(is_valid_architecture(Architecture::Arch256));
    EXPECT_TRUE(is_valid_architecture(Architecture::Arch512));
}

TEST_F(ArchitectureEnumTest, IsValidArchitecture_InvalidValue) {
    // Value 5 and above should be invalid
    EXPECT_FALSE(is_valid_architecture(static_cast<Architecture>(5)));
    EXPECT_FALSE(is_valid_architecture(static_cast<Architecture>(255)));
}

TEST_F(ArchitectureEnumTest, IsSimdArchitecture_CorrectClassification) {
    EXPECT_FALSE(is_simd_architecture(Architecture::Arch32));
    EXPECT_FALSE(is_simd_architecture(Architecture::Arch64));
    EXPECT_TRUE(is_simd_architecture(Architecture::Arch128));
    EXPECT_TRUE(is_simd_architecture(Architecture::Arch256));
    EXPECT_TRUE(is_simd_architecture(Architecture::Arch512));
}

TEST_F(ArchitectureEnumTest, IsScalarArchitecture_CorrectClassification) {
    EXPECT_TRUE(is_scalar_architecture(Architecture::Arch32));
    EXPECT_TRUE(is_scalar_architecture(Architecture::Arch64));
    EXPECT_FALSE(is_scalar_architecture(Architecture::Arch128));
    EXPECT_FALSE(is_scalar_architecture(Architecture::Arch256));
    EXPECT_FALSE(is_scalar_architecture(Architecture::Arch512));
}

TEST_F(ArchitectureEnumTest, ArchBitWidth_ReturnsCorrectValues) {
    EXPECT_EQ(arch_bit_width(Architecture::Arch32), 32u);
    EXPECT_EQ(arch_bit_width(Architecture::Arch64), 64u);
    EXPECT_EQ(arch_bit_width(Architecture::Arch128), 128u);
    EXPECT_EQ(arch_bit_width(Architecture::Arch256), 256u);
    EXPECT_EQ(arch_bit_width(Architecture::Arch512), 512u);
}

TEST_F(ArchitectureEnumTest, EnumValues_AreSequential) {
    EXPECT_EQ(static_cast<std::uint8_t>(Architecture::Arch32), 0u);
    EXPECT_EQ(static_cast<std::uint8_t>(Architecture::Arch64), 1u);
    EXPECT_EQ(static_cast<std::uint8_t>(Architecture::Arch128), 2u);
    EXPECT_EQ(static_cast<std::uint8_t>(Architecture::Arch256), 3u);
    EXPECT_EQ(static_cast<std::uint8_t>(Architecture::Arch512), 4u);
}

// ============================================================================
// CPU Feature Detection Tests
// ============================================================================

class CpuFeaturesDetectionTest : public ::testing::Test {};

TEST_F(CpuFeaturesDetectionTest, DetectCpuFeatures_ReturnsSameInstance) {
    // detect_cpu_features() should return cached results
    const CpuFeatures& features1 = detect_cpu_features();
    const CpuFeatures& features2 = detect_cpu_features();

    // Should be the same address (cached singleton)
    EXPECT_EQ(&features1, &features2);
}

TEST_F(CpuFeaturesDetectionTest, DetectCpuFeatures_ReturnsValidFeatures) {
    const CpuFeatures& features = detect_cpu_features();

    // At minimum, one platform should be detected (or neither on unknown platform)
    // Can't be both x86 and ARM
    EXPECT_FALSE(features.is_x86() && features.is_arm());

    // Max vector width should be sensible
    const auto width = features.max_vector_width();
    EXPECT_TRUE(width == 64 || width == 128 || width == 256 || width == 512);
}

TEST_F(CpuFeaturesDetectionTest, FeatureString_NotEmpty) {
    const CpuFeatures& features = detect_cpu_features();
    const std::string feature_str = features.feature_string();

    // Should produce some output
    EXPECT_FALSE(feature_str.empty());
}

// ============================================================================
// CpuFeatures Struct Tests
// ============================================================================

class CpuFeaturesStructTest : public ::testing::Test {};

TEST_F(CpuFeaturesStructTest, DefaultConstruction_AllFalse) {
    CpuFeatures features{};

    // All flags should default to false
    EXPECT_FALSE(features.sse2);
    EXPECT_FALSE(features.sse3);
    EXPECT_FALSE(features.ssse3);
    EXPECT_FALSE(features.sse4_1);
    EXPECT_FALSE(features.sse4_2);
    EXPECT_FALSE(features.avx);
    EXPECT_FALSE(features.avx2);
    EXPECT_FALSE(features.avx512f);
    EXPECT_FALSE(features.avx512bw);
    EXPECT_FALSE(features.avx512vl);
    EXPECT_FALSE(features.fma3);
    EXPECT_FALSE(features.aesni);
    EXPECT_FALSE(features.sha);
    EXPECT_FALSE(features.pclmul);
    EXPECT_FALSE(features.neon);
    EXPECT_FALSE(features.neon_aes);
    EXPECT_FALSE(features.neon_sha2);
    EXPECT_FALSE(features.sve);
}

TEST_F(CpuFeaturesStructTest, MaxVectorWidth_NoFeatures_Returns64) {
    CpuFeatures features{};
    EXPECT_EQ(features.max_vector_width(), 64u);
}

TEST_F(CpuFeaturesStructTest, MaxVectorWidth_SSE2_Returns128) {
    CpuFeatures features{};
    features.sse2 = true;
    EXPECT_EQ(features.max_vector_width(), 128u);
}

TEST_F(CpuFeaturesStructTest, MaxVectorWidth_NEON_Returns128) {
    CpuFeatures features{};
    features.neon = true;
    EXPECT_EQ(features.max_vector_width(), 128u);
}

TEST_F(CpuFeaturesStructTest, MaxVectorWidth_AVX2_Returns256) {
    CpuFeatures features{};
    features.sse2 = true;
    features.avx = true;
    features.avx2 = true;
    EXPECT_EQ(features.max_vector_width(), 256u);
}

TEST_F(CpuFeaturesStructTest, MaxVectorWidth_AVX512_Returns512) {
    CpuFeatures features{};
    features.sse2 = true;
    features.avx = true;
    features.avx2 = true;
    features.avx512f = true;
    EXPECT_EQ(features.max_vector_width(), 512u);
}

// ============================================================================
// Architecture Support Tests
// ============================================================================

class ArchSupportTest : public ::testing::Test {};

TEST_F(ArchSupportTest, SupportsArch_ScalarArchitectures_AlwaysSupported) {
    CpuFeatures features{};  // No SIMD features

    // Scalar architectures always supported
    EXPECT_TRUE(features.supports_arch(Architecture::Arch32));
    EXPECT_TRUE(features.supports_arch(Architecture::Arch64));
}

TEST_F(ArchSupportTest, SupportsArch_Arch128_RequiresSSE2OrNEON) {
    CpuFeatures features{};
    EXPECT_FALSE(features.supports_arch(Architecture::Arch128));

    features.sse2 = true;
    EXPECT_TRUE(features.supports_arch(Architecture::Arch128));

    features.sse2 = false;
    features.neon = true;
    EXPECT_TRUE(features.supports_arch(Architecture::Arch128));
}

TEST_F(ArchSupportTest, SupportsArch_Arch256_RequiresAVX2) {
    CpuFeatures features{};
    EXPECT_FALSE(features.supports_arch(Architecture::Arch256));

    features.avx = true;  // AVX alone not enough
    EXPECT_FALSE(features.supports_arch(Architecture::Arch256));

    features.avx2 = true;
    EXPECT_TRUE(features.supports_arch(Architecture::Arch256));
}

TEST_F(ArchSupportTest, SupportsArch_Arch512_RequiresFullAVX512) {
    CpuFeatures features{};
    EXPECT_FALSE(features.supports_arch(Architecture::Arch512));

    features.avx512f = true;  // Foundation alone not enough
    EXPECT_FALSE(features.supports_arch(Architecture::Arch512));

    features.avx512bw = true;  // Still need VL
    EXPECT_FALSE(features.supports_arch(Architecture::Arch512));

    features.avx512vl = true;  // Now all required
    EXPECT_TRUE(features.supports_arch(Architecture::Arch512));
}

// ============================================================================
// Platform Detection Tests
// ============================================================================

class PlatformDetectionTest : public ::testing::Test {};

TEST_F(PlatformDetectionTest, IsX86_WithSSE2_ReturnsTrue) {
    CpuFeatures features{};
    features.sse2 = true;
    EXPECT_TRUE(features.is_x86());
    EXPECT_FALSE(features.is_arm());
}

TEST_F(PlatformDetectionTest, IsArm_WithNEON_ReturnsTrue) {
    CpuFeatures features{};
    features.neon = true;
    EXPECT_TRUE(features.is_arm());
    EXPECT_FALSE(features.is_x86());
}

TEST_F(PlatformDetectionTest, NeitherPlatform_WhenNoFeatures) {
    CpuFeatures features{};
    EXPECT_FALSE(features.is_x86());
    EXPECT_FALSE(features.is_arm());
}

// ============================================================================
// Crypto Acceleration Tests
// ============================================================================

class CryptoAccelerationTest : public ::testing::Test {};

TEST_F(CryptoAccelerationTest, HasAesAcceleration_X86) {
    CpuFeatures features{};
    EXPECT_FALSE(features.has_aes_acceleration());

    features.aesni = true;
    EXPECT_TRUE(features.has_aes_acceleration());
}

TEST_F(CryptoAccelerationTest, HasAesAcceleration_ARM) {
    CpuFeatures features{};
    EXPECT_FALSE(features.has_aes_acceleration());

    features.neon_aes = true;
    EXPECT_TRUE(features.has_aes_acceleration());
}

TEST_F(CryptoAccelerationTest, HasShaAcceleration_X86) {
    CpuFeatures features{};
    EXPECT_FALSE(features.has_sha_acceleration());

    features.sha = true;
    EXPECT_TRUE(features.has_sha_acceleration());
}

TEST_F(CryptoAccelerationTest, HasShaAcceleration_ARM) {
    CpuFeatures features{};
    EXPECT_FALSE(features.has_sha_acceleration());

    features.neon_sha2 = true;
    EXPECT_TRUE(features.has_sha_acceleration());
}

// ============================================================================
// Helper Function Tests
// ============================================================================

class HelperFunctionTest : public ::testing::Test {};

TEST_F(HelperFunctionTest, IsVectorWidthSupported_RespectsDetectedFeatures) {
    // This tests the actual detected features, so results depend on hardware
    const CpuFeatures& features = detect_cpu_features();
    const auto max_width = features.max_vector_width();

    // 64-bit (scalar) should always work
    EXPECT_TRUE(is_vector_width_supported(64));

    // Widths up to max_width should be supported
    if (max_width >= 128) {
        EXPECT_TRUE(is_vector_width_supported(128));
    }
    if (max_width >= 256) {
        EXPECT_TRUE(is_vector_width_supported(256));
    }
    if (max_width >= 512) {
        EXPECT_TRUE(is_vector_width_supported(512));
    }
}

TEST_F(HelperFunctionTest, SelectOptimalSimdArch_ReturnsValidArchitecture) {
    const Architecture optimal = select_optimal_simd_arch();

    // Should be a valid architecture
    EXPECT_TRUE(is_valid_architecture(optimal));

    // Should be at least Arch64
    EXPECT_GE(static_cast<std::uint8_t>(optimal),
              static_cast<std::uint8_t>(Architecture::Arch64));
}

// ============================================================================
// Constexpr Validation Tests
// ============================================================================

class ConstexprTest : public ::testing::Test {};

TEST_F(ConstexprTest, ArchEnumFunctions_AreConstexpr) {
    // These should all compile as constexpr
    constexpr bool valid32 = is_valid_architecture(Architecture::Arch32);
    constexpr bool valid512 = is_valid_architecture(Architecture::Arch512);
    constexpr bool invalid = is_valid_architecture(static_cast<Architecture>(10));

    constexpr bool simd128 = is_simd_architecture(Architecture::Arch128);
    constexpr bool simd64 = is_simd_architecture(Architecture::Arch64);

    constexpr bool scalar32 = is_scalar_architecture(Architecture::Arch32);
    constexpr bool scalar256 = is_scalar_architecture(Architecture::Arch256);

    constexpr std::size_t width32 = arch_bit_width(Architecture::Arch32);
    constexpr std::size_t width512 = arch_bit_width(Architecture::Arch512);

    EXPECT_TRUE(valid32);
    EXPECT_TRUE(valid512);
    EXPECT_FALSE(invalid);
    EXPECT_TRUE(simd128);
    EXPECT_FALSE(simd64);
    EXPECT_TRUE(scalar32);
    EXPECT_FALSE(scalar256);
    EXPECT_EQ(width32, 32u);
    EXPECT_EQ(width512, 512u);
}

TEST_F(ConstexprTest, CpuFeaturesMethods_AreConstexpr) {
    constexpr CpuFeatures features{};

    constexpr auto width = features.max_vector_width();
    constexpr bool supports32 = features.supports_arch(Architecture::Arch32);
    constexpr bool supports128 = features.supports_arch(Architecture::Arch128);
    constexpr bool is_x86 = features.is_x86();
    constexpr bool is_arm = features.is_arm();
    constexpr bool has_aes = features.has_aes_acceleration();
    constexpr bool has_sha = features.has_sha_acceleration();

    EXPECT_EQ(width, 64u);
    EXPECT_TRUE(supports32);
    EXPECT_FALSE(supports128);
    EXPECT_FALSE(is_x86);
    EXPECT_FALSE(is_arm);
    EXPECT_FALSE(has_aes);
    EXPECT_FALSE(has_sha);
}

// ============================================================================
// Runtime Detection Sanity Test
// ============================================================================

class RuntimeSanityTest : public ::testing::Test {};

TEST_F(RuntimeSanityTest, PrintDetectedFeatures) {
    // This is mainly for diagnostic purposes when running tests
    const CpuFeatures& features = detect_cpu_features();

    std::cout << "\n=== CPU Feature Detection Results ===" << std::endl;
    std::cout << features.feature_string() << std::endl;
    std::cout << "Max vector width: " << features.max_vector_width() << " bits" << std::endl;
    std::cout << "Optimal SIMD arch: Arch"
              << arch_bit_width(select_optimal_simd_arch()) << std::endl;
    std::cout << "======================================\n" << std::endl;

    // Just verify we can run this without crashing
    SUCCEED();
}

}  // namespace
}  // namespace dotvm::core::simd
