/// @file vector_types_test.cpp
/// @brief Unit tests for SIMD vector types

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>

#include "dotvm/core/simd/vector_types.hpp"

namespace dotvm::core::simd {
namespace {

// ============================================================================
// Static Assertion Tests (Compile-Time Verification)
// ============================================================================

class VectorStaticAssertionsTest : public ::testing::Test {};

TEST_F(VectorStaticAssertionsTest, AlignmentRequirements) {
    // 128-bit vectors must be 16-byte aligned
    EXPECT_EQ(alignof(Vector128i32), 16u);
    EXPECT_EQ(alignof(Vector128f32), 16u);
    EXPECT_EQ(alignof(Vector128i64), 16u);
    EXPECT_EQ(alignof(Vector128f64), 16u);

    // 256-bit vectors must be 32-byte aligned
    EXPECT_EQ(alignof(Vector256i32), 32u);
    EXPECT_EQ(alignof(Vector256f32), 32u);
    EXPECT_EQ(alignof(Vector256i64), 32u);
    EXPECT_EQ(alignof(Vector256f64), 32u);

    // 512-bit vectors must be 64-byte aligned
    EXPECT_EQ(alignof(Vector512i32), 64u);
    EXPECT_EQ(alignof(Vector512f32), 64u);
    EXPECT_EQ(alignof(Vector512i64), 64u);
    EXPECT_EQ(alignof(Vector512f64), 64u);
}

TEST_F(VectorStaticAssertionsTest, SizeRequirements) {
    // 128-bit vectors must be 16 bytes
    EXPECT_EQ(sizeof(Vector128i8), 16u);
    EXPECT_EQ(sizeof(Vector128i16), 16u);
    EXPECT_EQ(sizeof(Vector128i32), 16u);
    EXPECT_EQ(sizeof(Vector128i64), 16u);
    EXPECT_EQ(sizeof(Vector128f32), 16u);
    EXPECT_EQ(sizeof(Vector128f64), 16u);

    // 256-bit vectors must be 32 bytes
    EXPECT_EQ(sizeof(Vector256i8), 32u);
    EXPECT_EQ(sizeof(Vector256i16), 32u);
    EXPECT_EQ(sizeof(Vector256i32), 32u);
    EXPECT_EQ(sizeof(Vector256i64), 32u);
    EXPECT_EQ(sizeof(Vector256f32), 32u);
    EXPECT_EQ(sizeof(Vector256f64), 32u);

    // 512-bit vectors must be 64 bytes
    EXPECT_EQ(sizeof(Vector512i8), 64u);
    EXPECT_EQ(sizeof(Vector512i16), 64u);
    EXPECT_EQ(sizeof(Vector512i32), 64u);
    EXPECT_EQ(sizeof(Vector512i64), 64u);
    EXPECT_EQ(sizeof(Vector512f32), 64u);
    EXPECT_EQ(sizeof(Vector512f64), 64u);
}

TEST_F(VectorStaticAssertionsTest, LaneCountRequirements) {
    // 128-bit lane counts
    EXPECT_EQ(Vector128i8::lane_count, 16u);
    EXPECT_EQ(Vector128i16::lane_count, 8u);
    EXPECT_EQ(Vector128i32::lane_count, 4u);
    EXPECT_EQ(Vector128i64::lane_count, 2u);
    EXPECT_EQ(Vector128f32::lane_count, 4u);
    EXPECT_EQ(Vector128f64::lane_count, 2u);

    // 256-bit lane counts
    EXPECT_EQ(Vector256i8::lane_count, 32u);
    EXPECT_EQ(Vector256i16::lane_count, 16u);
    EXPECT_EQ(Vector256i32::lane_count, 8u);
    EXPECT_EQ(Vector256i64::lane_count, 4u);
    EXPECT_EQ(Vector256f32::lane_count, 8u);
    EXPECT_EQ(Vector256f64::lane_count, 4u);

    // 512-bit lane counts
    EXPECT_EQ(Vector512i8::lane_count, 64u);
    EXPECT_EQ(Vector512i16::lane_count, 32u);
    EXPECT_EQ(Vector512i32::lane_count, 16u);
    EXPECT_EQ(Vector512i64::lane_count, 8u);
    EXPECT_EQ(Vector512f32::lane_count, 16u);
    EXPECT_EQ(Vector512f64::lane_count, 8u);
}

// ============================================================================
// Construction Tests
// ============================================================================

class VectorConstructionTest : public ::testing::Test {};

TEST_F(VectorConstructionTest, DefaultConstruction_ZeroInitializes) {
    Vector128i32 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        EXPECT_EQ(v[i], 0);
    }

    Vector256f64 vf;
    for (std::size_t i = 0; i < vf.size(); ++i) {
        EXPECT_EQ(vf[i], 0.0);
    }
}

TEST_F(VectorConstructionTest, BroadcastConstruction_SetsAllLanes) {
    Vector128i32 v{42};
    for (std::size_t i = 0; i < v.size(); ++i) {
        EXPECT_EQ(v[i], 42);
    }

    Vector256f32 vf{3.14f};
    for (std::size_t i = 0; i < vf.size(); ++i) {
        EXPECT_FLOAT_EQ(vf[i], 3.14f);
    }
}

TEST_F(VectorConstructionTest, ArrayConstruction_CopiesValues) {
    std::array<std::int32_t, 4> arr = {1, 2, 3, 4};
    Vector128i32 v{arr};

    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
    EXPECT_EQ(v[3], 4);
}

TEST_F(VectorConstructionTest, VariadicConstruction_SetsIndividualLanes) {
    Vector128i32 v{10, 20, 30, 40};
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[1], 20);
    EXPECT_EQ(v[2], 30);
    EXPECT_EQ(v[3], 40);

    Vector128f64 vf{1.5, 2.5};
    EXPECT_DOUBLE_EQ(vf[0], 1.5);
    EXPECT_DOUBLE_EQ(vf[1], 2.5);
}

TEST_F(VectorConstructionTest, ZeroFactory_CreatesZeroVector) {
    auto v = Vector256i64::zero();
    for (std::size_t i = 0; i < v.size(); ++i) {
        EXPECT_EQ(v[i], 0);
    }
}

TEST_F(VectorConstructionTest, OnesFactory_CreatesOnesVector) {
    auto vi = Vector128i32::ones();
    for (std::size_t i = 0; i < vi.size(); ++i) {
        EXPECT_EQ(vi[i], 1);
    }

    auto vf = Vector128f32::ones();
    for (std::size_t i = 0; i < vf.size(); ++i) {
        EXPECT_FLOAT_EQ(vf[i], 1.0f);
    }
}

TEST_F(VectorConstructionTest, FromBytes_LoadsRawData) {
    alignas(16) std::array<std::uint8_t, 16> bytes = {
        0x01, 0x00, 0x00, 0x00,  // 1
        0x02, 0x00, 0x00, 0x00,  // 2
        0x03, 0x00, 0x00, 0x00,  // 3
        0x04, 0x00, 0x00, 0x00   // 4
    };

    auto v = Vector128i32::from_bytes(bytes.data());
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
    EXPECT_EQ(v[3], 4);
}

// ============================================================================
// Lane Access Tests
// ============================================================================

class VectorLaneAccessTest : public ::testing::Test {};

TEST_F(VectorLaneAccessTest, OperatorBracket_ReadWrite) {
    Vector128i32 v;
    v[0] = 100;
    v[1] = 200;
    v[2] = 300;
    v[3] = 400;

    EXPECT_EQ(v[0], 100);
    EXPECT_EQ(v[1], 200);
    EXPECT_EQ(v[2], 300);
    EXPECT_EQ(v[3], 400);
}

TEST_F(VectorLaneAccessTest, At_BoundsChecked) {
    Vector128i32 v{1, 2, 3, 4};

    EXPECT_EQ(v.at(0), 1);
    EXPECT_EQ(v.at(3), 4);

    EXPECT_THROW((void)v.at(4), std::out_of_range);
    EXPECT_THROW((void)v.at(100), std::out_of_range);
}

TEST_F(VectorLaneAccessTest, FrontBack_AccessFirstLast) {
    Vector128i32 v{10, 20, 30, 40};

    EXPECT_EQ(v.front(), 10);
    EXPECT_EQ(v.back(), 40);

    v.front() = 100;
    v.back() = 400;

    EXPECT_EQ(v[0], 100);
    EXPECT_EQ(v[3], 400);
}

TEST_F(VectorLaneAccessTest, Data_ReturnsPointer) {
    Vector128i32 v{1, 2, 3, 4};

    const std::int32_t* ptr = v.data();
    EXPECT_EQ(ptr[0], 1);
    EXPECT_EQ(ptr[1], 2);
    EXPECT_EQ(ptr[2], 3);
    EXPECT_EQ(ptr[3], 4);
}

TEST_F(VectorLaneAccessTest, Bytes_ReturnsRawBytePointer) {
    Vector128i32 v{0x04030201, 0, 0, 0};

    const std::uint8_t* bytes = v.bytes();
    // Little-endian check
    EXPECT_EQ(bytes[0], 0x01);
    EXPECT_EQ(bytes[1], 0x02);
    EXPECT_EQ(bytes[2], 0x03);
    EXPECT_EQ(bytes[3], 0x04);
}

TEST_F(VectorLaneAccessTest, Lanes_ReturnsUnderlyingArray) {
    Vector128i32 v{1, 2, 3, 4};

    const auto& lanes = v.lanes();
    EXPECT_EQ(lanes.size(), 4u);
    EXPECT_EQ(lanes[0], 1);
    EXPECT_EQ(lanes[3], 4);
}

// ============================================================================
// Size and Capacity Tests
// ============================================================================

class VectorSizeTest : public ::testing::Test {};

TEST_F(VectorSizeTest, Size_ReturnsLaneCount) {
    EXPECT_EQ(Vector128i32::size(), 4u);
    EXPECT_EQ(Vector256i32::size(), 8u);
    EXPECT_EQ(Vector512i32::size(), 16u);

    Vector128i64 v;
    EXPECT_EQ(v.size(), 2u);
}

TEST_F(VectorSizeTest, Empty_AlwaysFalse) {
    EXPECT_FALSE(Vector128i32::empty());
    EXPECT_FALSE(Vector256f64::empty());
    EXPECT_FALSE(Vector512i8::empty());
}

TEST_F(VectorSizeTest, MaxSize_SameAsSize) {
    EXPECT_EQ(Vector128i32::max_size(), Vector128i32::size());
    EXPECT_EQ(Vector256i64::max_size(), Vector256i64::size());
}

TEST_F(VectorSizeTest, StaticConstants_AreCorrect) {
    EXPECT_EQ(Vector128i32::width_bits, 128u);
    EXPECT_EQ(Vector128i32::width_bytes, 16u);
    EXPECT_EQ(Vector128i32::lane_size, 4u);

    EXPECT_EQ(Vector512f64::width_bits, 512u);
    EXPECT_EQ(Vector512f64::width_bytes, 64u);
    EXPECT_EQ(Vector512f64::lane_size, 8u);
}

// ============================================================================
// Iterator Tests
// ============================================================================

class VectorIteratorTest : public ::testing::Test {};

TEST_F(VectorIteratorTest, RangeBasedFor_Works) {
    Vector128i32 v{1, 2, 3, 4};

    std::int32_t sum = 0;
    for (const auto& lane : v) {
        sum += lane;
    }
    EXPECT_EQ(sum, 10);
}

TEST_F(VectorIteratorTest, BeginEnd_IteratorPair) {
    Vector128i32 v{10, 20, 30, 40};

    auto it = v.begin();
    EXPECT_EQ(*it, 10);
    ++it;
    EXPECT_EQ(*it, 20);

    auto end_it = v.end();
    EXPECT_EQ(std::distance(v.begin(), end_it), 4);
}

TEST_F(VectorIteratorTest, ConstIterator_Works) {
    const Vector128i32 v{1, 2, 3, 4};

    auto sum = std::accumulate(v.cbegin(), v.cend(), 0);
    EXPECT_EQ(sum, 10);
}

TEST_F(VectorIteratorTest, ReverseIterator_Works) {
    Vector128i32 v{1, 2, 3, 4};

    std::vector<std::int32_t> reversed;
    for (auto it = v.rbegin(); it != v.rend(); ++it) {
        reversed.push_back(*it);
    }

    EXPECT_EQ(reversed[0], 4);
    EXPECT_EQ(reversed[1], 3);
    EXPECT_EQ(reversed[2], 2);
    EXPECT_EQ(reversed[3], 1);
}

TEST_F(VectorIteratorTest, ModificationViaIterator_Works) {
    Vector128i32 v{1, 2, 3, 4};

    for (auto& lane : v) {
        lane *= 10;
    }

    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[1], 20);
    EXPECT_EQ(v[2], 30);
    EXPECT_EQ(v[3], 40);
}

// ============================================================================
// Modifier Tests
// ============================================================================

class VectorModifierTest : public ::testing::Test {};

TEST_F(VectorModifierTest, Fill_SetsAllLanes) {
    Vector128i32 v{1, 2, 3, 4};
    v.fill(42);

    for (std::size_t i = 0; i < v.size(); ++i) {
        EXPECT_EQ(v[i], 42);
    }
}

TEST_F(VectorModifierTest, Swap_ExchangesContents) {
    Vector128i32 a{1, 2, 3, 4};
    Vector128i32 b{10, 20, 30, 40};

    a.swap(b);

    EXPECT_EQ(a[0], 10);
    EXPECT_EQ(b[0], 1);
}

TEST_F(VectorModifierTest, FreeSwap_ExchangesContents) {
    Vector128i32 a{1, 2, 3, 4};
    Vector128i32 b{10, 20, 30, 40};

    swap(a, b);

    EXPECT_EQ(a[0], 10);
    EXPECT_EQ(b[0], 1);
}

// ============================================================================
// Comparison Tests
// ============================================================================

class VectorComparisonTest : public ::testing::Test {};

TEST_F(VectorComparisonTest, Equality_SameLanes) {
    Vector128i32 a{1, 2, 3, 4};
    Vector128i32 b{1, 2, 3, 4};
    Vector128i32 c{1, 2, 3, 5};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST_F(VectorComparisonTest, Inequality_DifferentLanes) {
    Vector128f32 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vector128f32 b{1.0f, 2.0f, 3.0f, 4.1f};

    EXPECT_NE(a, b);
}

TEST_F(VectorComparisonTest, SpaceshipOperator_Lexicographic) {
    Vector128i32 a{1, 2, 3, 4};
    Vector128i32 b{1, 2, 3, 5};
    Vector128i32 c{1, 2, 3, 4};

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(a <= c);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(c >= a);
}

// ============================================================================
// Lane-wise Operation Tests
// ============================================================================

class VectorLaneOpsTest : public ::testing::Test {};

TEST_F(VectorLaneOpsTest, IsZero_AllZeros) {
    Vector128i32 v;
    EXPECT_TRUE(v.is_zero());

    v[0] = 1;
    EXPECT_FALSE(v.is_zero());
}

TEST_F(VectorLaneOpsTest, HorizontalSum_AddsAllLanes) {
    Vector128i32 v{1, 2, 3, 4};
    EXPECT_EQ(v.horizontal_sum(), 10);

    Vector128f32 vf{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_FLOAT_EQ(vf.horizontal_sum(), 10.0f);
}

TEST_F(VectorLaneOpsTest, MinLane_FindsMinimum) {
    Vector128i32 v{5, 2, 8, 1};
    EXPECT_EQ(v.min_lane(), 1);

    Vector128f32 vf{5.0f, -2.0f, 8.0f, 1.0f};
    EXPECT_FLOAT_EQ(vf.min_lane(), -2.0f);
}

TEST_F(VectorLaneOpsTest, MaxLane_FindsMaximum) {
    Vector128i32 v{5, 2, 8, 1};
    EXPECT_EQ(v.max_lane(), 8);

    Vector128f32 vf{5.0f, 2.0f, 8.0f, 1.0f};
    EXPECT_FLOAT_EQ(vf.max_lane(), 8.0f);
}

// ============================================================================
// Different Width Tests
// ============================================================================

class Vector128Test : public ::testing::Test {};

TEST_F(Vector128Test, AllLaneTypes_WorkCorrectly) {
    // i8: 16 lanes
    Vector128i8 vi8;
    for (std::size_t i = 0; i < vi8.size(); ++i) {
        vi8[i] = static_cast<std::int8_t>(i);
    }
    EXPECT_EQ(vi8[0], 0);
    EXPECT_EQ(vi8[15], 15);

    // u16: 8 lanes
    Vector128u16 vu16;
    for (std::size_t i = 0; i < vu16.size(); ++i) {
        vu16[i] = static_cast<std::uint16_t>(i * 100);
    }
    EXPECT_EQ(vu16[7], 700u);

    // f64: 2 lanes
    Vector128f64 vf64{1.5, 2.5};
    EXPECT_DOUBLE_EQ(vf64[0], 1.5);
    EXPECT_DOUBLE_EQ(vf64[1], 2.5);
}

class Vector256Test : public ::testing::Test {};

TEST_F(Vector256Test, AllLaneTypes_WorkCorrectly) {
    // i32: 8 lanes
    Vector256i32 vi32{1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(vi32.horizontal_sum(), 36);

    // f32: 8 lanes
    Vector256f32 vf32{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    EXPECT_FLOAT_EQ(vf32.horizontal_sum(), 36.0f);

    // i64: 4 lanes
    Vector256i64 vi64{100, 200, 300, 400};
    EXPECT_EQ(vi64.horizontal_sum(), 1000);
}

class Vector512Test : public ::testing::Test {};

TEST_F(Vector512Test, AllLaneTypes_WorkCorrectly) {
    // i32: 16 lanes
    Vector512i32 vi32;
    for (std::size_t i = 0; i < vi32.size(); ++i) {
        vi32[i] = static_cast<std::int32_t>(i + 1);
    }
    // Sum of 1 to 16 = 136
    EXPECT_EQ(vi32.horizontal_sum(), 136);

    // i64: 8 lanes
    Vector512i64 vi64;
    for (std::size_t i = 0; i < vi64.size(); ++i) {
        vi64[i] = static_cast<std::int64_t>(i * 10);
    }
    // Sum of 0, 10, 20, 30, 40, 50, 60, 70 = 280
    EXPECT_EQ(vi64.horizontal_sum(), 280);

    // f32: 16 lanes
    Vector512f32 vf32{1.0f};  // All lanes set to 1.0
    EXPECT_FLOAT_EQ(vf32.horizontal_sum(), 16.0f);
}

// ============================================================================
// Constexpr Tests
// ============================================================================

class VectorConstexprTest : public ::testing::Test {};

TEST_F(VectorConstexprTest, ConstexprConstruction_Works) {
    constexpr Vector128i32 v{1, 2, 3, 4};
    static_assert(v[0] == 1);
    static_assert(v[3] == 4);
    static_assert(v.size() == 4);
}

TEST_F(VectorConstexprTest, ConstexprZero_Works) {
    constexpr auto v = Vector128i32::zero();
    static_assert(v[0] == 0);
    static_assert(v[3] == 0);
}

TEST_F(VectorConstexprTest, ConstexprOnes_Works) {
    constexpr auto v = Vector128i32::ones();
    static_assert(v[0] == 1);
    static_assert(v[3] == 1);
}

TEST_F(VectorConstexprTest, ConstexprStaticValues_Work) {
    static_assert(Vector128i32::width_bits == 128);
    static_assert(Vector128i32::width_bytes == 16);
    static_assert(Vector128i32::lane_count == 4);
    static_assert(Vector128i32::lane_size == 4);
}

TEST_F(VectorConstexprTest, ConstexprComparison_Works) {
    constexpr Vector128i32 a{1, 2, 3, 4};
    constexpr Vector128i32 b{1, 2, 3, 4};
    constexpr Vector128i32 c{1, 2, 3, 5};

    static_assert(a == b);
    static_assert(a != c);
}

TEST_F(VectorConstexprTest, ConstexprIsZero_Works) {
    constexpr Vector128i32 v{};
    static_assert(v.is_zero());

    constexpr Vector128i32 v2{0, 0, 0, 1};
    static_assert(!v2.is_zero());
}

// ============================================================================
// Edge Cases
// ============================================================================

class VectorEdgeCasesTest : public ::testing::Test {};

TEST_F(VectorEdgeCasesTest, NegativeIntegers_WorkCorrectly) {
    Vector128i32 v{-1, -2, -3, -4};
    EXPECT_EQ(v[0], -1);
    EXPECT_EQ(v[3], -4);
    EXPECT_EQ(v.horizontal_sum(), -10);
}

TEST_F(VectorEdgeCasesTest, MaxMinValues_WorkCorrectly) {
    Vector128i32 v{
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max(),
        0,
        -1
    };

    EXPECT_EQ(v[0], std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(v[1], std::numeric_limits<std::int32_t>::max());
    EXPECT_EQ(v.min_lane(), std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(v.max_lane(), std::numeric_limits<std::int32_t>::max());
}

TEST_F(VectorEdgeCasesTest, FloatingPointSpecialValues_WorkCorrectly) {
    Vector128f32 v{
        0.0f,
        -0.0f,
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()
    };

    EXPECT_EQ(v[0], 0.0f);
    EXPECT_EQ(v[2], std::numeric_limits<float>::infinity());
    EXPECT_EQ(v[3], -std::numeric_limits<float>::infinity());
}

TEST_F(VectorEdgeCasesTest, AllBitsSet_WorksForIntegers) {
    auto v = Vector128i32::all_bits_set();
    for (std::size_t i = 0; i < v.size(); ++i) {
        EXPECT_EQ(v[i], -1);  // All bits set = -1 for signed int
    }

    auto vu = Vector128u32::all_bits_set();
    for (std::size_t i = 0; i < vu.size(); ++i) {
        EXPECT_EQ(vu[i], std::numeric_limits<std::uint32_t>::max());
    }
}

// ============================================================================
// Concept Verification Tests
// ============================================================================

class VectorConceptTest : public ::testing::Test {};

TEST_F(VectorConceptTest, LaneTypeConcept_CorrectTypes) {
    // Valid lane types
    static_assert(LaneType<std::int8_t>);
    static_assert(LaneType<std::uint8_t>);
    static_assert(LaneType<std::int16_t>);
    static_assert(LaneType<std::uint16_t>);
    static_assert(LaneType<std::int32_t>);
    static_assert(LaneType<std::uint32_t>);
    static_assert(LaneType<std::int64_t>);
    static_assert(LaneType<std::uint64_t>);
    static_assert(LaneType<float>);
    static_assert(LaneType<double>);

    // Invalid lane types
    static_assert(!LaneType<bool>);
    static_assert(!LaneType<char>);  // char might not be the same as int8
    static_assert(!LaneType<void*>);
    static_assert(!LaneType<std::string>);
}

TEST_F(VectorConceptTest, ValidVectorWidthConcept_CorrectWidths) {
    static_assert(ValidVectorWidth<128>);
    static_assert(ValidVectorWidth<256>);
    static_assert(ValidVectorWidth<512>);

    static_assert(!ValidVectorWidth<64>);
    static_assert(!ValidVectorWidth<1024>);
    static_assert(!ValidVectorWidth<0>);
}

}  // namespace
}  // namespace dotvm::core::simd
