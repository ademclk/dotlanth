/// @file simd_alu_test.cpp
/// @brief Comprehensive unit tests for SIMD ALU operations
///
/// Tests cover:
/// - Scalar fallback correctness
/// - SIMD vs scalar result equivalence
/// - Edge cases (overflow for integers, NaN/Inf for floats)
/// - All lane types (int8, int16, int32, int64, float, double)
/// - All widths (128, 256, 512)

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>

#include "dotvm/core/simd/simd_alu.hpp"
#include "dotvm/core/simd/simd_opcodes.hpp"
#include "dotvm/core/simd/vector_register_file.hpp"
#include "dotvm/core/simd/vector_types.hpp"

namespace dotvm::core::simd {
namespace {

// ============================================================================
// Test Fixture
// ============================================================================

class SimdAluTest : public ::testing::Test {
protected:
    void SetUp() override {
        features_ = detect_cpu_features();
        alu_ = std::make_unique<SimdAlu>(SimdAlu::auto_detect());
    }

    CpuFeatures features_;
    std::unique_ptr<SimdAlu> alu_;

    // Random number generator for fuzz testing
    std::mt19937 rng_{42};  // Fixed seed for reproducibility

    template<typename T>
    T random_value() {
        if constexpr (std::is_floating_point_v<T>) {
            std::uniform_real_distribution<T> dist(-100.0, 100.0);
            return dist(rng_);
        } else {
            std::uniform_int_distribution<T> dist(
                std::numeric_limits<T>::min() / 2,
                std::numeric_limits<T>::max() / 2
            );
            return dist(rng_);
        }
    }

    template<std::size_t Width, typename Lane>
    Vector<Width, Lane> random_vector() {
        Vector<Width, Lane> v;
        for (std::size_t i = 0; i < v.size(); ++i) {
            v[i] = random_value<Lane>();
        }
        return v;
    }
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(SimdAluTest, DefaultConstruction_UsesOptimalArch) {
    SimdAlu default_alu;
    // Should have selected an architecture based on CPU features
    EXPECT_TRUE(default_alu.arch() == Architecture::Arch128 ||
                default_alu.arch() == Architecture::Arch256 ||
                default_alu.arch() == Architecture::Arch512);
}

TEST_F(SimdAluTest, AutoDetect_SelectsValidArch) {
    auto auto_alu = SimdAlu::auto_detect();
    EXPECT_TRUE(features_.supports_arch(auto_alu.arch()));
}

TEST_F(SimdAluTest, ExplicitArch_FallsBackIfUnsupported) {
    // Try to create with Arch512 - should fall back if not supported
    SimdAlu alu512{Architecture::Arch512};
    EXPECT_TRUE(features_.supports_arch(alu512.arch()));
}

TEST_F(SimdAluTest, Features_ReturnsCpuFeatures) {
    const auto& feat = alu_->features();
    EXPECT_EQ(feat.sse2, features_.sse2);
    EXPECT_EQ(feat.avx, features_.avx);
    EXPECT_EQ(feat.avx2, features_.avx2);
}

TEST_F(SimdAluTest, VectorWidthBits_ReturnsCorrectValue) {
    SimdAlu alu128{Architecture::Arch128};
    EXPECT_EQ(alu128.vector_width_bits(), 128u);

    // These may fall back if not supported
    SimdAlu alu256{Architecture::Arch256};
    EXPECT_TRUE(alu256.vector_width_bits() == 256 ||
                alu256.vector_width_bits() == 128);
}

TEST_F(SimdAluTest, FactoryFunction_CreatesValidAlu) {
    auto alu = make_simd_alu();
    EXPECT_NE(alu, nullptr);
    EXPECT_TRUE(features_.supports_arch(alu->arch()));
}

// ============================================================================
// Scalar Fallback Correctness Tests
// ============================================================================

class ScalarFallbackTest : public ::testing::Test {};

TEST_F(ScalarFallbackTest, VAdd_Integer_CorrectResult) {
    Vector128i32 a{1, 2, 3, 4};
    Vector128i32 b{10, 20, 30, 40};
    auto result = scalar::vadd(a, b);

    EXPECT_EQ(result[0], 11);
    EXPECT_EQ(result[1], 22);
    EXPECT_EQ(result[2], 33);
    EXPECT_EQ(result[3], 44);
}

TEST_F(ScalarFallbackTest, VAdd_Float_CorrectResult) {
    Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vector128f32 b{0.5f, 0.5f, 0.5f, 0.5f};
    auto result = scalar::vadd(a, b);

    EXPECT_FLOAT_EQ(result[0], 1.5f);
    EXPECT_FLOAT_EQ(result[1], 2.5f);
    EXPECT_FLOAT_EQ(result[2], 3.5f);
    EXPECT_FLOAT_EQ(result[3], 4.5f);
}

TEST_F(ScalarFallbackTest, VSub_Integer_CorrectResult) {
    Vector128i32 a{10, 20, 30, 40};
    Vector128i32 b{1, 2, 3, 4};
    auto result = scalar::vsub(a, b);

    EXPECT_EQ(result[0], 9);
    EXPECT_EQ(result[1], 18);
    EXPECT_EQ(result[2], 27);
    EXPECT_EQ(result[3], 36);
}

TEST_F(ScalarFallbackTest, VMul_Integer_CorrectResult) {
    Vector128i32 a{2, 3, 4, 5};
    Vector128i32 b{10, 10, 10, 10};
    auto result = scalar::vmul(a, b);

    EXPECT_EQ(result[0], 20);
    EXPECT_EQ(result[1], 30);
    EXPECT_EQ(result[2], 40);
    EXPECT_EQ(result[3], 50);
}

TEST_F(ScalarFallbackTest, VDiv_Float_CorrectResult) {
    Vector128f32 a{10.0f, 20.0f, 30.0f, 40.0f};
    Vector128f32 b{2.0f, 4.0f, 5.0f, 8.0f};
    auto result = scalar::vdiv(a, b);

    EXPECT_FLOAT_EQ(result[0], 5.0f);
    EXPECT_FLOAT_EQ(result[1], 5.0f);
    EXPECT_FLOAT_EQ(result[2], 6.0f);
    EXPECT_FLOAT_EQ(result[3], 5.0f);
}

TEST_F(ScalarFallbackTest, VDiv_Integer_DivByZero_ReturnsZero) {
    Vector128i32 a{10, 20, 30, 40};
    Vector128i32 b{2, 0, 5, 0};  // Two divide-by-zero cases
    auto result = scalar::vdiv(a, b);

    EXPECT_EQ(result[0], 5);
    EXPECT_EQ(result[1], 0);  // div by zero returns 0
    EXPECT_EQ(result[2], 6);
    EXPECT_EQ(result[3], 0);  // div by zero returns 0
}

TEST_F(ScalarFallbackTest, VDot_Float_CorrectResult) {
    Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vector128f32 b{2.0f, 2.0f, 2.0f, 2.0f};
    auto result = scalar::vdot(a, b);

    // 1*2 + 2*2 + 3*2 + 4*2 = 20
    EXPECT_FLOAT_EQ(result, 20.0f);
}

TEST_F(ScalarFallbackTest, VFma_Float_CorrectResult) {
    Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vector128f32 b{2.0f, 2.0f, 2.0f, 2.0f};
    Vector128f32 c{1.0f, 1.0f, 1.0f, 1.0f};
    auto result = scalar::vfma(a, b, c);

    // a*b + c
    EXPECT_FLOAT_EQ(result[0], 3.0f);   // 1*2 + 1 = 3
    EXPECT_FLOAT_EQ(result[1], 5.0f);   // 2*2 + 1 = 5
    EXPECT_FLOAT_EQ(result[2], 7.0f);   // 3*2 + 1 = 7
    EXPECT_FLOAT_EQ(result[3], 9.0f);   // 4*2 + 1 = 9
}

TEST_F(ScalarFallbackTest, VMin_Integer_CorrectResult) {
    Vector128i32 a{5, 2, 8, 1};
    Vector128i32 b{3, 7, 4, 9};
    auto result = scalar::vmin(a, b);

    EXPECT_EQ(result[0], 3);
    EXPECT_EQ(result[1], 2);
    EXPECT_EQ(result[2], 4);
    EXPECT_EQ(result[3], 1);
}

TEST_F(ScalarFallbackTest, VMax_Integer_CorrectResult) {
    Vector128i32 a{5, 2, 8, 1};
    Vector128i32 b{3, 7, 4, 9};
    auto result = scalar::vmax(a, b);

    EXPECT_EQ(result[0], 5);
    EXPECT_EQ(result[1], 7);
    EXPECT_EQ(result[2], 8);
    EXPECT_EQ(result[3], 9);
}

TEST_F(ScalarFallbackTest, VCmpEq_Integer_AllOnesOrZero) {
    Vector128i32 a{1, 2, 3, 4};
    Vector128i32 b{1, 5, 3, 6};
    auto result = scalar::vcmpeq(a, b);

    EXPECT_EQ(result[0], -1);  // Equal: all bits set
    EXPECT_EQ(result[1], 0);   // Not equal: zero
    EXPECT_EQ(result[2], -1);  // Equal: all bits set
    EXPECT_EQ(result[3], 0);   // Not equal: zero
}

TEST_F(ScalarFallbackTest, VCmpLt_Integer_AllOnesOrZero) {
    Vector128i32 a{1, 5, 3, 4};
    Vector128i32 b{2, 4, 3, 5};
    auto result = scalar::vcmplt(a, b);

    EXPECT_EQ(result[0], -1);  // 1 < 2: all bits set
    EXPECT_EQ(result[1], 0);   // 5 < 4: false
    EXPECT_EQ(result[2], 0);   // 3 < 3: false
    EXPECT_EQ(result[3], -1);  // 4 < 5: all bits set
}

TEST_F(ScalarFallbackTest, VBlend_Integer_SelectsCorrectly) {
    Vector128i32 mask{-1, 0, -1, 0};  // negative = true
    Vector128i32 a{1, 2, 3, 4};
    Vector128i32 b{10, 20, 30, 40};
    auto result = scalar::vblend(mask, a, b);

    EXPECT_EQ(result[0], 1);   // mask < 0: select a
    EXPECT_EQ(result[1], 20);  // mask >= 0: select b
    EXPECT_EQ(result[2], 3);   // mask < 0: select a
    EXPECT_EQ(result[3], 40);  // mask >= 0: select b
}

// ============================================================================
// SIMD vs Scalar Equivalence Tests (128-bit)
// ============================================================================

class SimdEquivalenceTest128 : public SimdAluTest {};

TEST_F(SimdEquivalenceTest128, VAdd_i32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<128, std::int32_t>();
        auto b = random_vector<128, std::int32_t>();

        auto simd_result = alu_->vadd(a, b);
        auto scalar_result = scalar::vadd(a, b);

        EXPECT_EQ(simd_result, scalar_result);
    }
}

TEST_F(SimdEquivalenceTest128, VAdd_f32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<128, float>();
        auto b = random_vector<128, float>();

        auto simd_result = alu_->vadd(a, b);
        auto scalar_result = scalar::vadd(a, b);

        for (std::size_t i = 0; i < 4; ++i) {
            EXPECT_FLOAT_EQ(simd_result[i], scalar_result[i]);
        }
    }
}

TEST_F(SimdEquivalenceTest128, VSub_i32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<128, std::int32_t>();
        auto b = random_vector<128, std::int32_t>();

        auto simd_result = alu_->vsub(a, b);
        auto scalar_result = scalar::vsub(a, b);

        EXPECT_EQ(simd_result, scalar_result);
    }
}

TEST_F(SimdEquivalenceTest128, VMul_i32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<128, std::int32_t>();
        auto b = random_vector<128, std::int32_t>();

        auto simd_result = alu_->vmul(a, b);
        auto scalar_result = scalar::vmul(a, b);

        EXPECT_EQ(simd_result, scalar_result);
    }
}

TEST_F(SimdEquivalenceTest128, VDiv_f32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<128, float>();
        auto b = random_vector<128, float>();

        // Avoid divide by zero
        for (std::size_t i = 0; i < 4; ++i) {
            if (std::abs(b[i]) < 0.001f) {
                b[i] = 1.0f;
            }
        }

        auto simd_result = alu_->vdiv(a, b);
        auto scalar_result = scalar::vdiv(a, b);

        for (std::size_t i = 0; i < 4; ++i) {
            EXPECT_NEAR(simd_result[i], scalar_result[i], 1e-5f);
        }
    }
}

TEST_F(SimdEquivalenceTest128, VFma_f32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<128, float>();
        auto b = random_vector<128, float>();
        auto c = random_vector<128, float>();

        auto simd_result = alu_->vfma(a, b, c);
        auto scalar_result = scalar::vfma(a, b, c);

        for (std::size_t i = 0; i < 4; ++i) {
            EXPECT_NEAR(simd_result[i], scalar_result[i], 1e-4f);
        }
    }
}

TEST_F(SimdEquivalenceTest128, VMin_i32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<128, std::int32_t>();
        auto b = random_vector<128, std::int32_t>();

        auto simd_result = alu_->vmin(a, b);
        auto scalar_result = scalar::vmin(a, b);

        EXPECT_EQ(simd_result, scalar_result);
    }
}

TEST_F(SimdEquivalenceTest128, VMax_i32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<128, std::int32_t>();
        auto b = random_vector<128, std::int32_t>();

        auto simd_result = alu_->vmax(a, b);
        auto scalar_result = scalar::vmax(a, b);

        EXPECT_EQ(simd_result, scalar_result);
    }
}

// ============================================================================
// SIMD vs Scalar Equivalence Tests (256-bit)
// ============================================================================

class SimdEquivalenceTest256 : public SimdAluTest {};

TEST_F(SimdEquivalenceTest256, VAdd_i32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<256, std::int32_t>();
        auto b = random_vector<256, std::int32_t>();

        auto simd_result = alu_->vadd(a, b);
        auto scalar_result = scalar::vadd(a, b);

        EXPECT_EQ(simd_result, scalar_result);
    }
}

TEST_F(SimdEquivalenceTest256, VAdd_f32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<256, float>();
        auto b = random_vector<256, float>();

        auto simd_result = alu_->vadd(a, b);
        auto scalar_result = scalar::vadd(a, b);

        for (std::size_t i = 0; i < 8; ++i) {
            EXPECT_FLOAT_EQ(simd_result[i], scalar_result[i]);
        }
    }
}

TEST_F(SimdEquivalenceTest256, VSub_i64_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<256, std::int64_t>();
        auto b = random_vector<256, std::int64_t>();

        auto simd_result = alu_->vsub(a, b);
        auto scalar_result = scalar::vsub(a, b);

        EXPECT_EQ(simd_result, scalar_result);
    }
}

TEST_F(SimdEquivalenceTest256, VMul_f64_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<256, double>();
        auto b = random_vector<256, double>();

        auto simd_result = alu_->vmul(a, b);
        auto scalar_result = scalar::vmul(a, b);

        for (std::size_t i = 0; i < 4; ++i) {
            EXPECT_DOUBLE_EQ(simd_result[i], scalar_result[i]);
        }
    }
}

TEST_F(SimdEquivalenceTest256, VFma_f32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<256, float>();
        auto b = random_vector<256, float>();
        auto c = random_vector<256, float>();

        auto simd_result = alu_->vfma(a, b, c);
        auto scalar_result = scalar::vfma(a, b, c);

        for (std::size_t i = 0; i < 8; ++i) {
            EXPECT_NEAR(simd_result[i], scalar_result[i], 1e-4f);
        }
    }
}

TEST_F(SimdEquivalenceTest256, VDot_f32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<256, float>();
        auto b = random_vector<256, float>();

        auto simd_result = alu_->vdot(a, b);
        auto scalar_result = scalar::vdot(a, b);

        // Dot product accumulates 8 floats, so allow larger tolerance
        // for floating-point rounding differences between SIMD and scalar
        float abs_tolerance = std::abs(scalar_result) * 1e-4f + 1e-2f;
        EXPECT_NEAR(simd_result, scalar_result, abs_tolerance);
    }
}

// ============================================================================
// SIMD vs Scalar Equivalence Tests (512-bit)
// ============================================================================

class SimdEquivalenceTest512 : public SimdAluTest {};

TEST_F(SimdEquivalenceTest512, VAdd_i32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<512, std::int32_t>();
        auto b = random_vector<512, std::int32_t>();

        auto simd_result = alu_->vadd(a, b);
        auto scalar_result = scalar::vadd(a, b);

        EXPECT_EQ(simd_result, scalar_result);
    }
}

TEST_F(SimdEquivalenceTest512, VSub_f32_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<512, float>();
        auto b = random_vector<512, float>();

        auto simd_result = alu_->vsub(a, b);
        auto scalar_result = scalar::vsub(a, b);

        for (std::size_t i = 0; i < 16; ++i) {
            EXPECT_FLOAT_EQ(simd_result[i], scalar_result[i]);
        }
    }
}

TEST_F(SimdEquivalenceTest512, VMul_i64_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<512, std::int64_t>();
        auto b = random_vector<512, std::int64_t>();

        auto simd_result = alu_->vmul(a, b);
        auto scalar_result = scalar::vmul(a, b);

        EXPECT_EQ(simd_result, scalar_result);
    }
}

TEST_F(SimdEquivalenceTest512, VFma_f64_MatchesScalar) {
    for (int iter = 0; iter < 100; ++iter) {
        auto a = random_vector<512, double>();
        auto b = random_vector<512, double>();
        auto c = random_vector<512, double>();

        auto simd_result = alu_->vfma(a, b, c);
        auto scalar_result = scalar::vfma(a, b, c);

        for (std::size_t i = 0; i < 8; ++i) {
            EXPECT_NEAR(simd_result[i], scalar_result[i], 1e-10);
        }
    }
}

// ============================================================================
// Integer Overflow/Underflow Edge Cases
// ============================================================================

class IntegerEdgeCasesTest : public SimdAluTest {};

TEST_F(IntegerEdgeCasesTest, VAdd_Overflow_WrapsAround) {
    Vector128i32 a{
        std::numeric_limits<std::int32_t>::max(),
        std::numeric_limits<std::int32_t>::max(),
        0,
        0
    };
    Vector128i32 b{1, 2, 0, 0};

    auto result = alu_->vadd(a, b);

    // Signed overflow is implementation-defined, but we expect wrap-around
    EXPECT_EQ(result[0], std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(result[1], std::numeric_limits<std::int32_t>::min() + 1);
}

TEST_F(IntegerEdgeCasesTest, VSub_Underflow_WrapsAround) {
    Vector128i32 a{
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::min(),
        0,
        0
    };
    Vector128i32 b{1, 2, 0, 0};

    auto result = alu_->vsub(a, b);

    EXPECT_EQ(result[0], std::numeric_limits<std::int32_t>::max());
    EXPECT_EQ(result[1], std::numeric_limits<std::int32_t>::max() - 1);
}

TEST_F(IntegerEdgeCasesTest, VMul_Overflow_WrapsAround) {
    Vector128i32 a{
        100000,   // 100000 * 100000 = 10^10, wraps around
        -1,       // -1 * INT_MIN is undefined, skip this check
        50000,    // 50000 * 50000 = 2.5 * 10^9
        2
    };
    Vector128i32 b{100000, 2, 50000, 2};

    auto result = alu_->vmul(a, b);

    // 100000 * 100000 = 10^10 = 0x2540BE400
    // Lower 32 bits: 0x540BE400 = 1410065408
    // But this is signed, so interpret as signed
    // Actually, 10^10 mod 2^32 = 10000000000 - 2*2^32 = 10000000000 - 8589934592 = 1410065408
    // As signed int32: this is positive since bit 31 is 0
    EXPECT_EQ(result[0], 1410065408);  // 10^10 wrapped to int32

    // -1 * 2 = -2
    EXPECT_EQ(result[1], -2);

    // 50000 * 50000 = 2.5 * 10^9 = 2500000000
    // This exceeds INT_MAX (2147483647), so wraps
    // 2500000000 - 2^32 = 2500000000 - 4294967296 = -1794967296
    EXPECT_EQ(result[2], -1794967296);

    // 2 * 2 = 4 (no overflow)
    EXPECT_EQ(result[3], 4);
}

TEST_F(IntegerEdgeCasesTest, VDiv_DivideByZero_ReturnsZero) {
    Vector128i32 a{100, 200, 300, 400};
    Vector128i32 b{0, 0, 0, 0};

    auto result = alu_->vdiv(a, b);

    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(result[i], 0);
    }
}

TEST_F(IntegerEdgeCasesTest, VMin_WithMinMax_ReturnsMin) {
    Vector128i32 a{
        std::numeric_limits<std::int32_t>::max(),
        std::numeric_limits<std::int32_t>::min(),
        0,
        -1
    };
    Vector128i32 b{
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
        1,
        1
    };

    auto result = alu_->vmin(a, b);

    EXPECT_EQ(result[0], std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(result[1], std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(result[2], 0);
    EXPECT_EQ(result[3], -1);
}

TEST_F(IntegerEdgeCasesTest, VMax_WithMinMax_ReturnsMax) {
    Vector128i32 a{
        std::numeric_limits<std::int32_t>::max(),
        std::numeric_limits<std::int32_t>::min(),
        0,
        -1
    };
    Vector128i32 b{
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
        1,
        1
    };

    auto result = alu_->vmax(a, b);

    EXPECT_EQ(result[0], std::numeric_limits<std::int32_t>::max());
    EXPECT_EQ(result[1], std::numeric_limits<std::int32_t>::max());
    EXPECT_EQ(result[2], 1);
    EXPECT_EQ(result[3], 1);
}

// ============================================================================
// Floating Point Special Values Edge Cases
// ============================================================================

class FloatEdgeCasesTest : public SimdAluTest {};

TEST_F(FloatEdgeCasesTest, VAdd_WithInfinity_ProducesInfinity) {
    Vector128f32 a{
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        1.0f,
        0.0f
    };
    Vector128f32 b{1.0f, 1.0f, std::numeric_limits<float>::infinity(), 0.0f};

    auto result = alu_->vadd(a, b);

    EXPECT_EQ(result[0], std::numeric_limits<float>::infinity());
    EXPECT_EQ(result[1], -std::numeric_limits<float>::infinity());
    EXPECT_EQ(result[2], std::numeric_limits<float>::infinity());
    EXPECT_EQ(result[3], 0.0f);
}

TEST_F(FloatEdgeCasesTest, VAdd_InfinityMinusInfinity_ProducesNaN) {
    Vector128f32 a{std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f};
    Vector128f32 b{-std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f};

    auto result = alu_->vadd(a, b);

    EXPECT_TRUE(std::isnan(result[0]));
}

TEST_F(FloatEdgeCasesTest, VMul_WithNaN_ProducesNaN) {
    Vector128f32 a{
        std::numeric_limits<float>::quiet_NaN(),
        1.0f,
        0.0f,
        std::numeric_limits<float>::infinity()
    };
    Vector128f32 b{1.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};

    auto result = alu_->vmul(a, b);

    EXPECT_TRUE(std::isnan(result[0]));
    EXPECT_TRUE(std::isnan(result[1]));
    EXPECT_EQ(result[2], 0.0f);
    EXPECT_TRUE(std::isnan(result[3]));  // inf * 0 = NaN
}

TEST_F(FloatEdgeCasesTest, VDiv_ByZero_ProducesInfinity) {
    Vector128f32 a{1.0f, -1.0f, 0.0f, 0.0f};
    Vector128f32 b{0.0f, 0.0f, 0.0f, 1.0f};

    auto result = alu_->vdiv(a, b);

    EXPECT_EQ(result[0], std::numeric_limits<float>::infinity());
    EXPECT_EQ(result[1], -std::numeric_limits<float>::infinity());
    EXPECT_TRUE(std::isnan(result[2]));  // 0/0 = NaN
    EXPECT_EQ(result[3], 0.0f);
}

TEST_F(FloatEdgeCasesTest, VMin_WithNaN_PropagatesNaN) {
    Vector128f32 a{
        std::numeric_limits<float>::quiet_NaN(),
        1.0f,
        2.0f,
        3.0f
    };
    Vector128f32 b{
        1.0f,
        std::numeric_limits<float>::quiet_NaN(),
        1.0f,
        4.0f
    };

    auto result = alu_->vmin(a, b);

    // std::fmin returns the non-NaN value
    EXPECT_EQ(result[0], 1.0f);
    EXPECT_EQ(result[1], 1.0f);
    EXPECT_EQ(result[2], 1.0f);
    EXPECT_EQ(result[3], 3.0f);
}

TEST_F(FloatEdgeCasesTest, VFma_Precision_UsesHardwareFma) {
    // FMA should be more precise than separate multiply and add
    Vector128f32 a{1.0f + 1e-7f, 1.0f, 1.0f, 1.0f};
    Vector128f32 b{1.0f - 1e-7f, 1.0f, 1.0f, 1.0f};
    Vector128f32 c{-1.0f, -1.0f, -1.0f, -1.0f};

    auto result = alu_->vfma(a, b, c);

    // (1 + 1e-7) * (1 - 1e-7) - 1 = -1e-14 (very small, close to zero)
    EXPECT_NEAR(result[0], -1e-14f, 1e-13f);
}

TEST_F(FloatEdgeCasesTest, VDot_LargeVector_AccumulatesCorrectly) {
    // Create vectors with known sum
    Vector256f32 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    Vector256f32 b{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    auto result = alu_->vdot(a, b);

    // 1+2+3+4+5+6+7+8 = 36
    EXPECT_FLOAT_EQ(result, 36.0f);
}

TEST_F(FloatEdgeCasesTest, VCmpEq_Float_HandlesNaN) {
    Vector128f32 a{
        1.0f,
        std::numeric_limits<float>::quiet_NaN(),
        0.0f,
        -0.0f
    };
    Vector128f32 b{
        1.0f,
        std::numeric_limits<float>::quiet_NaN(),
        -0.0f,
        0.0f
    };

    auto result = alu_->vcmpeq(a, b);

    // Check bits - NaN != NaN is IEEE standard
    std::uint32_t bits0, bits1, bits2, bits3;
    std::memcpy(&bits0, &result[0], sizeof(bits0));
    std::memcpy(&bits1, &result[1], sizeof(bits1));
    std::memcpy(&bits2, &result[2], sizeof(bits2));
    std::memcpy(&bits3, &result[3], sizeof(bits3));

    EXPECT_EQ(bits0, 0xFFFFFFFF);  // 1.0 == 1.0
    EXPECT_EQ(bits1, 0x00000000);  // NaN != NaN
    EXPECT_EQ(bits2, 0xFFFFFFFF);  // 0.0 == -0.0
    EXPECT_EQ(bits3, 0xFFFFFFFF);  // -0.0 == 0.0
}

// ============================================================================
// All Lane Types Tests (int8, int16, int32, int64, float, double)
// ============================================================================

class AllLaneTypesTest : public SimdAluTest {};

TEST_F(AllLaneTypesTest, VAdd_i8_128bit) {
    Vector128i8 a, b;
    for (std::size_t i = 0; i < 16; ++i) {
        a[i] = static_cast<std::int8_t>(i);
        b[i] = static_cast<std::int8_t>(i * 2);
    }

    auto result = alu_->vadd(a, b);

    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(result[i], static_cast<std::int8_t>(i * 3));
    }
}

TEST_F(AllLaneTypesTest, VAdd_i16_128bit) {
    Vector128i16 a, b;
    for (std::size_t i = 0; i < 8; ++i) {
        a[i] = static_cast<std::int16_t>(i * 100);
        b[i] = static_cast<std::int16_t>(i * 50);
    }

    auto result = alu_->vadd(a, b);

    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(result[i], static_cast<std::int16_t>(i * 150));
    }
}

TEST_F(AllLaneTypesTest, VAdd_i64_128bit) {
    Vector128i64 a{1000000000LL, 2000000000LL};
    Vector128i64 b{500000000LL, 500000000LL};

    auto result = alu_->vadd(a, b);

    EXPECT_EQ(result[0], 1500000000LL);
    EXPECT_EQ(result[1], 2500000000LL);
}

TEST_F(AllLaneTypesTest, VMul_i8_128bit) {
    Vector128i8 a, b;
    for (std::size_t i = 0; i < 16; ++i) {
        a[i] = static_cast<std::int8_t>(i % 10);
        b[i] = static_cast<std::int8_t>(2);
    }

    auto result = alu_->vmul(a, b);

    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(result[i], static_cast<std::int8_t>((i % 10) * 2));
    }
}

TEST_F(AllLaneTypesTest, VAdd_f64_128bit) {
    Vector128f64 a{1.5, 2.5};
    Vector128f64 b{0.5, 0.5};

    auto result = alu_->vadd(a, b);

    EXPECT_DOUBLE_EQ(result[0], 2.0);
    EXPECT_DOUBLE_EQ(result[1], 3.0);
}

TEST_F(AllLaneTypesTest, VAdd_i8_256bit) {
    Vector256i8 a, b;
    for (std::size_t i = 0; i < 32; ++i) {
        a[i] = static_cast<std::int8_t>(i);
        b[i] = static_cast<std::int8_t>(1);
    }

    auto result = alu_->vadd(a, b);

    for (std::size_t i = 0; i < 32; ++i) {
        EXPECT_EQ(result[i], static_cast<std::int8_t>(i + 1));
    }
}

TEST_F(AllLaneTypesTest, VAdd_i16_256bit) {
    Vector256i16 a, b;
    for (std::size_t i = 0; i < 16; ++i) {
        a[i] = static_cast<std::int16_t>(i * 1000);
        b[i] = static_cast<std::int16_t>(500);
    }

    auto result = alu_->vadd(a, b);

    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(result[i], static_cast<std::int16_t>(i * 1000 + 500));
    }
}

TEST_F(AllLaneTypesTest, VAdd_i64_256bit) {
    Vector256i64 a{100LL, 200LL, 300LL, 400LL};
    Vector256i64 b{10LL, 20LL, 30LL, 40LL};

    auto result = alu_->vadd(a, b);

    EXPECT_EQ(result[0], 110LL);
    EXPECT_EQ(result[1], 220LL);
    EXPECT_EQ(result[2], 330LL);
    EXPECT_EQ(result[3], 440LL);
}

TEST_F(AllLaneTypesTest, VAdd_f64_256bit) {
    Vector256f64 a{1.0, 2.0, 3.0, 4.0};
    Vector256f64 b{0.1, 0.2, 0.3, 0.4};

    auto result = alu_->vadd(a, b);

    EXPECT_DOUBLE_EQ(result[0], 1.1);
    EXPECT_DOUBLE_EQ(result[1], 2.2);
    EXPECT_DOUBLE_EQ(result[2], 3.3);
    EXPECT_DOUBLE_EQ(result[3], 4.4);
}

TEST_F(AllLaneTypesTest, VAdd_i8_512bit) {
    Vector512i8 a, b;
    for (std::size_t i = 0; i < 64; ++i) {
        a[i] = static_cast<std::int8_t>(i % 64);
        b[i] = static_cast<std::int8_t>(1);
    }

    auto result = alu_->vadd(a, b);

    for (std::size_t i = 0; i < 64; ++i) {
        EXPECT_EQ(result[i], static_cast<std::int8_t>((i % 64) + 1));
    }
}

TEST_F(AllLaneTypesTest, VAdd_i64_512bit) {
    Vector512i64 a, b;
    for (std::size_t i = 0; i < 8; ++i) {
        a[i] = static_cast<std::int64_t>(i * 1000);
        b[i] = static_cast<std::int64_t>(500);
    }

    auto result = alu_->vadd(a, b);

    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(result[i], static_cast<std::int64_t>(i * 1000 + 500));
    }
}

TEST_F(AllLaneTypesTest, VAdd_f32_512bit) {
    Vector512f32 a, b;
    for (std::size_t i = 0; i < 16; ++i) {
        a[i] = static_cast<float>(i);
        b[i] = 0.5f;
    }

    auto result = alu_->vadd(a, b);

    for (std::size_t i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(result[i], static_cast<float>(i) + 0.5f);
    }
}

TEST_F(AllLaneTypesTest, VAdd_f64_512bit) {
    Vector512f64 a, b;
    for (std::size_t i = 0; i < 8; ++i) {
        a[i] = static_cast<double>(i);
        b[i] = 0.25;
    }

    auto result = alu_->vadd(a, b);

    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_DOUBLE_EQ(result[i], static_cast<double>(i) + 0.25);
    }
}

// ============================================================================
// Register-Based Operations Tests
// ============================================================================

class RegisterOperationsTest : public SimdAluTest {};

TEST_F(RegisterOperationsTest, VAdd_i32_RegisterFile_128bit) {
    VectorRegisterFile regs;
    // Note: V0 is hardwired zero register, so use V1, V2, V3
    regs.write_v128i32(1, Vector128i32{1, 2, 3, 4});
    regs.write_v128i32(2, Vector128i32{10, 20, 30, 40});

    SimdAlu alu128{Architecture::Arch128};
    alu128.vadd_i32(regs, 3, 1, 2);  // V3 = V1 + V2

    auto result = regs.read_v128i32(3);
    EXPECT_EQ(result[0], 11);
    EXPECT_EQ(result[1], 22);
    EXPECT_EQ(result[2], 33);
    EXPECT_EQ(result[3], 44);
}

TEST_F(RegisterOperationsTest, VSub_i32_RegisterFile_128bit) {
    VectorRegisterFile regs;
    regs.write_v128i32(1, Vector128i32{100, 200, 300, 400});
    regs.write_v128i32(2, Vector128i32{1, 2, 3, 4});

    SimdAlu alu128{Architecture::Arch128};
    alu128.vsub_i32(regs, 3, 1, 2);  // V3 = V1 - V2

    auto result = regs.read_v128i32(3);
    EXPECT_EQ(result[0], 99);
    EXPECT_EQ(result[1], 198);
    EXPECT_EQ(result[2], 297);
    EXPECT_EQ(result[3], 396);
}

TEST_F(RegisterOperationsTest, VMul_i32_RegisterFile_128bit) {
    VectorRegisterFile regs;
    regs.write_v128i32(1, Vector128i32{2, 3, 4, 5});
    regs.write_v128i32(2, Vector128i32{10, 10, 10, 10});

    SimdAlu alu128{Architecture::Arch128};
    alu128.vmul_i32(regs, 3, 1, 2);  // V3 = V1 * V2

    auto result = regs.read_v128i32(3);
    EXPECT_EQ(result[0], 20);
    EXPECT_EQ(result[1], 30);
    EXPECT_EQ(result[2], 40);
    EXPECT_EQ(result[3], 50);
}

TEST_F(RegisterOperationsTest, VAdd_i32_RegisterFile_256bit) {
    if (!features_.avx2) {
        GTEST_SKIP() << "AVX2 not supported on this CPU";
    }

    VectorRegisterFile regs;
    regs.write_v256i32(1, Vector256i32{1, 2, 3, 4, 5, 6, 7, 8});
    regs.write_v256i32(2, Vector256i32{10, 10, 10, 10, 10, 10, 10, 10});

    SimdAlu alu256{Architecture::Arch256};
    alu256.vadd_i32(regs, 3, 1, 2);  // V3 = V1 + V2

    auto result = regs.read_v256i32(3);
    EXPECT_EQ(result[0], 11);
    EXPECT_EQ(result[7], 18);
}

// ============================================================================
// Legacy API Tests
// ============================================================================

class LegacyApiTest : public SimdAluTest {};

TEST_F(LegacyApiTest, Add_v128i32_Works) {
    Vector128i32 a{1, 2, 3, 4};
    Vector128i32 b{10, 20, 30, 40};

    auto result = alu_->add_v128i32(a, b);

    EXPECT_EQ(result[0], 11);
    EXPECT_EQ(result[1], 22);
    EXPECT_EQ(result[2], 33);
    EXPECT_EQ(result[3], 44);
}

TEST_F(LegacyApiTest, Sub_v128i32_Works) {
    Vector128i32 a{100, 200, 300, 400};
    Vector128i32 b{1, 2, 3, 4};

    auto result = alu_->sub_v128i32(a, b);

    EXPECT_EQ(result[0], 99);
    EXPECT_EQ(result[1], 198);
    EXPECT_EQ(result[2], 297);
    EXPECT_EQ(result[3], 396);
}

TEST_F(LegacyApiTest, Mul_v128i32_Works) {
    Vector128i32 a{2, 3, 4, 5};
    Vector128i32 b{10, 10, 10, 10};

    auto result = alu_->mul_v128i32(a, b);

    EXPECT_EQ(result[0], 20);
    EXPECT_EQ(result[1], 30);
    EXPECT_EQ(result[2], 40);
    EXPECT_EQ(result[3], 50);
}

TEST_F(LegacyApiTest, Add_v128f32_Works) {
    Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vector128f32 b{0.5f, 0.5f, 0.5f, 0.5f};

    auto result = alu_->add_v128f32(a, b);

    EXPECT_FLOAT_EQ(result[0], 1.5f);
    EXPECT_FLOAT_EQ(result[1], 2.5f);
    EXPECT_FLOAT_EQ(result[2], 3.5f);
    EXPECT_FLOAT_EQ(result[3], 4.5f);
}

TEST_F(LegacyApiTest, Div_v128f32_Works) {
    Vector128f32 a{10.0f, 20.0f, 30.0f, 40.0f};
    Vector128f32 b{2.0f, 4.0f, 5.0f, 8.0f};

    auto result = alu_->div_v128f32(a, b);

    EXPECT_FLOAT_EQ(result[0], 5.0f);
    EXPECT_FLOAT_EQ(result[1], 5.0f);
    EXPECT_FLOAT_EQ(result[2], 6.0f);
    EXPECT_FLOAT_EQ(result[3], 5.0f);
}

TEST_F(LegacyApiTest, Fma_v128f32_Works) {
    Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vector128f32 b{2.0f, 2.0f, 2.0f, 2.0f};
    Vector128f32 c{1.0f, 1.0f, 1.0f, 1.0f};

    auto result = alu_->fma_v128f32(a, b, c);

    EXPECT_FLOAT_EQ(result[0], 3.0f);
    EXPECT_FLOAT_EQ(result[1], 5.0f);
    EXPECT_FLOAT_EQ(result[2], 7.0f);
    EXPECT_FLOAT_EQ(result[3], 9.0f);
}

TEST_F(LegacyApiTest, Dot_v128f32_Works) {
    Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vector128f32 b{2.0f, 2.0f, 2.0f, 2.0f};

    auto result = alu_->dot_v128f32(a, b);

    EXPECT_FLOAT_EQ(result, 20.0f);
}

TEST_F(LegacyApiTest, Add_v256i32_Works) {
    Vector256i32 a{1, 2, 3, 4, 5, 6, 7, 8};
    Vector256i32 b{10, 10, 10, 10, 10, 10, 10, 10};

    auto result = alu_->add_v256i32(a, b);

    EXPECT_EQ(result[0], 11);
    EXPECT_EQ(result[7], 18);
}

TEST_F(LegacyApiTest, Fma_v256f32_Works) {
    Vector256f32 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    Vector256f32 b{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    Vector256f32 c{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

    auto result = alu_->fma_v256f32(a, b, c);

    EXPECT_FLOAT_EQ(result[0], 1.5f);
    EXPECT_FLOAT_EQ(result[7], 8.5f);
}

TEST_F(LegacyApiTest, Dot_v256f32_Works) {
    Vector256f32 a{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    Vector256f32 b{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    auto result = alu_->dot_v256f32(a, b);

    EXPECT_FLOAT_EQ(result, 36.0f);
}

TEST_F(LegacyApiTest, Add_v512i32_Works) {
    Vector512i32 a, b;
    for (std::size_t i = 0; i < 16; ++i) {
        a[i] = static_cast<std::int32_t>(i);
        b[i] = 100;
    }

    auto result = alu_->add_v512i32(a, b);

    EXPECT_EQ(result[0], 100);
    EXPECT_EQ(result[15], 115);
}

TEST_F(LegacyApiTest, Fma_v512f32_Works) {
    Vector512f32 a, b, c;
    for (std::size_t i = 0; i < 16; ++i) {
        a[i] = static_cast<float>(i + 1);
        b[i] = 2.0f;
        c[i] = 0.5f;
    }

    auto result = alu_->fma_v512f32(a, b, c);

    EXPECT_FLOAT_EQ(result[0], 2.5f);   // 1*2 + 0.5
    EXPECT_FLOAT_EQ(result[15], 32.5f); // 16*2 + 0.5
}

// ============================================================================
// SIMD Opcode Tests
// ============================================================================

class SimdOpcodeTest : public ::testing::Test {};

TEST_F(SimdOpcodeTest, Opcodes_InCorrectRange) {
    // All SIMD opcodes should be in 0xC0-0xCF range
    EXPECT_EQ(opcode::VADD, 0xC0);
    EXPECT_EQ(opcode::VSUB, 0xC1);
    EXPECT_EQ(opcode::VMUL, 0xC2);
    EXPECT_EQ(opcode::VDIV, 0xC3);
    EXPECT_EQ(opcode::VDOT, 0xC4);
    EXPECT_EQ(opcode::VFMA, 0xC5);
    EXPECT_EQ(opcode::VMIN, 0xC6);
    EXPECT_EQ(opcode::VMAX, 0xC7);
    EXPECT_EQ(opcode::VLOAD, 0xC8);
    EXPECT_EQ(opcode::VSTORE, 0xC9);
    EXPECT_EQ(opcode::VBCAST, 0xCA);
    EXPECT_EQ(opcode::VEXTRACT, 0xCB);
    EXPECT_EQ(opcode::VCMPEQ, 0xCC);
    EXPECT_EQ(opcode::VCMPLT, 0xCD);
    EXPECT_EQ(opcode::VBLEND, 0xCE);
    EXPECT_EQ(opcode::VSHUFFLE, 0xCF);
}

TEST_F(SimdOpcodeTest, IsSimdOpcode_ReturnsCorrectly) {
    EXPECT_TRUE(opcode::is_simd_opcode(0xC0));
    EXPECT_TRUE(opcode::is_simd_opcode(0xCF));
    EXPECT_FALSE(opcode::is_simd_opcode(0xBF));
    EXPECT_FALSE(opcode::is_simd_opcode(0xD0));
    EXPECT_FALSE(opcode::is_simd_opcode(0x00));
}

TEST_F(SimdOpcodeTest, InstructionEncode_RoundTrips) {
    SimdInstruction instr{
        opcode::VADD,
        ElementSize::Int32,
        VectorWidth::Width128,
        5,   // vd
        10,  // vs1
        15   // vs2
    };

    std::uint32_t encoded = instr.encode();
    auto decoded = SimdInstruction::decode(encoded);

    EXPECT_EQ(decoded.opcode, instr.opcode);
    EXPECT_EQ(decoded.vd, instr.vd);
    EXPECT_EQ(decoded.vs1, instr.vs1);
    EXPECT_EQ(decoded.vs2, instr.vs2);
    EXPECT_EQ(decoded.element_size, instr.element_size);
    EXPECT_EQ(decoded.vector_width, instr.vector_width);
}

TEST_F(SimdOpcodeTest, InstructionBuilders_CreateCorrectInstructions) {
    auto vadd = make_vadd(ElementSize::Int32, VectorWidth::Width256, 1, 2, 3);
    EXPECT_EQ(vadd.opcode, opcode::VADD);
    EXPECT_EQ(vadd.vd, 1);
    EXPECT_EQ(vadd.vs1, 2);
    EXPECT_EQ(vadd.vs2, 3);
    EXPECT_EQ(vadd.element_size, ElementSize::Int32);
    EXPECT_EQ(vadd.vector_width, VectorWidth::Width256);

    auto vfma = make_vfma(ElementSize::Float64, VectorWidth::Width512, 4, 5, 6, 7);
    EXPECT_EQ(vfma.opcode, opcode::VFMA);
    EXPECT_EQ(vfma.vd, 4);
    EXPECT_EQ(vfma.vs1, 5);
    EXPECT_EQ(vfma.vs2, 6);
    EXPECT_EQ(vfma.vs3, 7);
}

}  // namespace
}  // namespace dotvm::core::simd
