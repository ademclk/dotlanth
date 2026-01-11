/// @file vector_register_file_test.cpp
/// @brief Unit tests for the vector register file

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "dotvm/core/simd/vector_register_file.hpp"

namespace dotvm::core::simd {
namespace {

// ============================================================================
// Static Assertion Tests
// ============================================================================

class VectorRegisterFileStaticTest : public ::testing::Test {};

TEST_F(VectorRegisterFileStaticTest, Constants_AreCorrect) {
    EXPECT_EQ(VECTOR_REGISTER_COUNT, 32u);
    EXPECT_EQ(VREG_ZERO, 0u);
    EXPECT_EQ(MAX_VECTOR_WIDTH_BITS, 512u);
    EXPECT_EQ(MAX_VECTOR_WIDTH_BYTES, 64u);
}

TEST_F(VectorRegisterFileStaticTest, VectorRegister_Alignment) {
    EXPECT_EQ(alignof(VectorRegister), 64u);
    EXPECT_EQ(sizeof(VectorRegister), 64u);
}

TEST_F(VectorRegisterFileStaticTest, VectorRegisterFile_Alignment) {
    EXPECT_EQ(alignof(VectorRegisterFile), 64u);
    EXPECT_EQ(sizeof(VectorRegisterFile), 64u * 32u);
}

TEST_F(VectorRegisterFileStaticTest, VectorRegisterFile_RegisterCount) {
    EXPECT_EQ(VectorRegisterFile::register_count, 32u);
    EXPECT_EQ(VectorRegisterFile::zero_register, 0u);
}

// ============================================================================
// VectorRegister Tests
// ============================================================================

class VectorRegisterTest : public ::testing::Test {
protected:
    VectorRegister reg;
};

TEST_F(VectorRegisterTest, DefaultConstruction_IsZero) {
    EXPECT_TRUE(reg.is_zero());

    for (std::size_t i = 0; i < reg.bytes.size(); ++i) {
        EXPECT_EQ(reg.bytes[i], 0u);
    }
}

TEST_F(VectorRegisterTest, Clear_SetsToZero) {
    reg.qwords[0] = 0xFFFFFFFFFFFFFFFFULL;
    reg.qwords[7] = 0x1234567890ABCDEFULL;

    EXPECT_FALSE(reg.is_zero());

    reg.clear();
    EXPECT_TRUE(reg.is_zero());
}

TEST_F(VectorRegisterTest, ByteAccess_WorksCorrectly) {
    reg.bytes[0] = 0x12;
    reg.bytes[63] = 0xAB;

    EXPECT_EQ(reg.bytes[0], 0x12);
    EXPECT_EQ(reg.bytes[63], 0xAB);
}

TEST_F(VectorRegisterTest, QwordAccess_WorksCorrectly) {
    reg.qwords[0] = 0x0123456789ABCDEFULL;
    reg.qwords[7] = 0xFEDCBA9876543210ULL;

    EXPECT_EQ(reg.qwords[0], 0x0123456789ABCDEFULL);
    EXPECT_EQ(reg.qwords[7], 0xFEDCBA9876543210ULL);
}

TEST_F(VectorRegisterTest, DwordAccess_WorksCorrectly) {
    reg.dwords[0] = 0x12345678;
    reg.dwords[15] = 0x9ABCDEF0;

    EXPECT_EQ(reg.dwords[0], 0x12345678u);
    EXPECT_EQ(reg.dwords[15], 0x9ABCDEF0u);
}

TEST_F(VectorRegisterTest, WordAccess_WorksCorrectly) {
    reg.words[0] = 0x1234;
    reg.words[31] = 0x5678;

    EXPECT_EQ(reg.words[0], 0x1234u);
    EXPECT_EQ(reg.words[31], 0x5678u);
}

TEST_F(VectorRegisterTest, V128Access_WorksCorrectly) {
    Vector128i32 v{1, 2, 3, 4};
    reg.set_v128<std::int32_t>(v);

    auto read_back = reg.as_v128i32();
    EXPECT_EQ(read_back[0], 1);
    EXPECT_EQ(read_back[1], 2);
    EXPECT_EQ(read_back[2], 3);
    EXPECT_EQ(read_back[3], 4);
}

TEST_F(VectorRegisterTest, V256Access_WorksCorrectly) {
    Vector256i32 v{1, 2, 3, 4, 5, 6, 7, 8};
    reg.set_v256<std::int32_t>(v);

    auto read_back = reg.as_v256i32();
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(read_back[i], static_cast<std::int32_t>(i + 1));
    }
}

TEST_F(VectorRegisterTest, V512Access_WorksCorrectly) {
    Vector512i32 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::int32_t>(i * 10);
    }
    reg.set_v512<std::int32_t>(v);

    auto read_back = reg.as_v512i32();
    for (std::size_t i = 0; i < read_back.size(); ++i) {
        EXPECT_EQ(read_back[i], static_cast<std::int32_t>(i * 10));
    }
}

TEST_F(VectorRegisterTest, FloatAccess_WorksCorrectly) {
    Vector128f32 vf{1.0f, 2.0f, 3.0f, 4.0f};
    reg.set_v128<float>(vf);

    auto read_back = reg.as_v128f32();
    EXPECT_FLOAT_EQ(read_back[0], 1.0f);
    EXPECT_FLOAT_EQ(read_back[1], 2.0f);
    EXPECT_FLOAT_EQ(read_back[2], 3.0f);
    EXPECT_FLOAT_EQ(read_back[3], 4.0f);
}

TEST_F(VectorRegisterTest, DoubleAccess_WorksCorrectly) {
    Vector128f64 vd{1.5, 2.5};
    reg.set_v128<double>(vd);

    auto read_back = reg.as_v128f64();
    EXPECT_DOUBLE_EQ(read_back[0], 1.5);
    EXPECT_DOUBLE_EQ(read_back[1], 2.5);
}

TEST_F(VectorRegisterTest, Comparison_WorksCorrectly) {
    VectorRegister reg1, reg2;

    EXPECT_EQ(reg1, reg2);

    reg1.qwords[0] = 1;
    EXPECT_NE(reg1, reg2);

    reg2.qwords[0] = 1;
    EXPECT_EQ(reg1, reg2);
}

// ============================================================================
// VectorRegisterFile V0 Zero Register Tests
// ============================================================================

class VectorRegisterFileZeroRegisterTest : public ::testing::Test {
protected:
    VectorRegisterFile vrf;
};

TEST_F(VectorRegisterFileZeroRegisterTest, V0_AlwaysReturnsZero) {
    const auto& v0 = vrf.read(0);
    EXPECT_TRUE(v0.is_zero());

    // Check all bytes
    for (std::size_t i = 0; i < v0.bytes.size(); ++i) {
        EXPECT_EQ(v0.bytes[i], 0u);
    }
}

TEST_F(VectorRegisterFileZeroRegisterTest, V0_WriteIsIgnored) {
    VectorRegister non_zero;
    non_zero.qwords[0] = 0xFFFFFFFFFFFFFFFFULL;
    non_zero.qwords[7] = 0x1234567890ABCDEFULL;

    vrf.write(0, non_zero);

    const auto& v0 = vrf.read(0);
    EXPECT_TRUE(v0.is_zero());
}

TEST_F(VectorRegisterFileZeroRegisterTest, V0_WriteV128IsIgnored) {
    Vector128i32 v{100, 200, 300, 400};
    vrf.write_v128i32(0, v);

    auto read_back = vrf.read_v128i32(0);
    EXPECT_TRUE(read_back.is_zero());
}

TEST_F(VectorRegisterFileZeroRegisterTest, V0_WriteV256IsIgnored) {
    Vector256i32 v{1, 2, 3, 4, 5, 6, 7, 8};
    vrf.write_v256i32(0, v);

    auto read_back = vrf.read_v256i32(0);
    EXPECT_TRUE(read_back.is_zero());
}

TEST_F(VectorRegisterFileZeroRegisterTest, V0_WriteV512IsIgnored) {
    Vector512i32 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::int32_t>(i + 1);
    }
    vrf.write_v512i32(0, v);

    auto read_back = vrf.read_v512i32(0);
    EXPECT_TRUE(read_back.is_zero());
}

TEST_F(VectorRegisterFileZeroRegisterTest, V0_OperatorWriteIsIgnored) {
    VectorRegister non_zero;
    non_zero.qwords[0] = 0x12345678ABCDEF00ULL;

    vrf[0] = non_zero;

    const VectorRegister& v0 = vrf[0];
    EXPECT_TRUE(v0.is_zero());
}

TEST_F(VectorRegisterFileZeroRegisterTest, V0_RepeatedWritesIgnored) {
    for (std::size_t i = 0; i < 100; ++i) {
        VectorRegister r;
        r.qwords[i % 8] = static_cast<std::uint64_t>(i);
        vrf.write(0, r);
    }

    const auto& v0 = vrf.read(0);
    EXPECT_TRUE(v0.is_zero());
}

// ============================================================================
// VectorRegisterFile V1-V31 Read/Write Tests
// ============================================================================

class VectorRegisterFileReadWriteTest : public ::testing::Test {
protected:
    VectorRegisterFile vrf;
};

TEST_F(VectorRegisterFileReadWriteTest, Write_V1ToV31_Works) {
    for (std::uint8_t i = 1; i < 32; ++i) {
        VectorRegister r;
        r.qwords[0] = static_cast<std::uint64_t>(i) * 100;
        vrf.write(i, r);
    }

    for (std::uint8_t i = 1; i < 32; ++i) {
        const auto& r = vrf.read(i);
        EXPECT_EQ(r.qwords[0], static_cast<std::uint64_t>(i) * 100);
    }
}

TEST_F(VectorRegisterFileReadWriteTest, WriteV128_Works) {
    Vector128i32 v{10, 20, 30, 40};
    vrf.write_v128i32(1, v);

    auto read_back = vrf.read_v128i32(1);
    EXPECT_EQ(read_back[0], 10);
    EXPECT_EQ(read_back[1], 20);
    EXPECT_EQ(read_back[2], 30);
    EXPECT_EQ(read_back[3], 40);
}

TEST_F(VectorRegisterFileReadWriteTest, WriteV256_Works) {
    Vector256i64 v{100, 200, 300, 400};
    vrf.write_v256i64(5, v);

    auto read_back = vrf.read_v256i64(5);
    EXPECT_EQ(read_back[0], 100);
    EXPECT_EQ(read_back[1], 200);
    EXPECT_EQ(read_back[2], 300);
    EXPECT_EQ(read_back[3], 400);
}

TEST_F(VectorRegisterFileReadWriteTest, WriteV512_Works) {
    Vector512i32 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::int32_t>(i * 7);
    }
    vrf.write_v512i32(31, v);

    auto read_back = vrf.read_v512i32(31);
    for (std::size_t i = 0; i < read_back.size(); ++i) {
        EXPECT_EQ(read_back[i], static_cast<std::int32_t>(i * 7));
    }
}

TEST_F(VectorRegisterFileReadWriteTest, WriteFloat_Works) {
    Vector128f32 vf{1.5f, 2.5f, 3.5f, 4.5f};
    vrf.write_v128f32(10, vf);

    auto read_back = vrf.read_v128f32(10);
    EXPECT_FLOAT_EQ(read_back[0], 1.5f);
    EXPECT_FLOAT_EQ(read_back[1], 2.5f);
    EXPECT_FLOAT_EQ(read_back[2], 3.5f);
    EXPECT_FLOAT_EQ(read_back[3], 4.5f);
}

TEST_F(VectorRegisterFileReadWriteTest, WriteDouble_Works) {
    Vector256f64 vd{1.1, 2.2, 3.3, 4.4};
    vrf.write_v256f64(15, vd);

    auto read_back = vrf.read_v256f64(15);
    EXPECT_DOUBLE_EQ(read_back[0], 1.1);
    EXPECT_DOUBLE_EQ(read_back[1], 2.2);
    EXPECT_DOUBLE_EQ(read_back[2], 3.3);
    EXPECT_DOUBLE_EQ(read_back[3], 4.4);
}

TEST_F(VectorRegisterFileReadWriteTest, OperatorAccess_Works) {
    VectorRegister r;
    r.qwords[0] = 0xABCDEF0123456789ULL;
    vrf[5] = r;

    const VectorRegister& read_back = vrf[5];
    EXPECT_EQ(read_back.qwords[0], 0xABCDEF0123456789ULL);
}

TEST_F(VectorRegisterFileReadWriteTest, WriteSmaller_ClearsUpperBits) {
    // First write a full 512-bit value
    Vector512i32 big_v;
    for (std::size_t i = 0; i < big_v.size(); ++i) {
        big_v[i] = -1;  // All bits set
    }
    vrf.write_v512i32(3, big_v);

    // Now write a smaller 128-bit value
    Vector128i32 small_v{1, 2, 3, 4};
    vrf.write_v128i32(3, small_v);

    // The lower 128 bits should match small_v
    auto lower = vrf.read_v128i32(3);
    EXPECT_EQ(lower[0], 1);
    EXPECT_EQ(lower[1], 2);
    EXPECT_EQ(lower[2], 3);
    EXPECT_EQ(lower[3], 4);

    // Upper bits should be cleared (check via raw register)
    const auto& raw = vrf.read(3);
    EXPECT_EQ(raw.qwords[2], 0u);
    EXPECT_EQ(raw.qwords[3], 0u);
    EXPECT_EQ(raw.qwords[4], 0u);
    EXPECT_EQ(raw.qwords[5], 0u);
    EXPECT_EQ(raw.qwords[6], 0u);
    EXPECT_EQ(raw.qwords[7], 0u);
}

// ============================================================================
// VectorRegisterFile Width Access Tests
// ============================================================================

class VectorRegisterFileWidthAccessTest : public ::testing::Test {
protected:
    VectorRegisterFile vrf;
};

TEST_F(VectorRegisterFileWidthAccessTest, V128i32_ReadWrite) {
    Vector128i32 v{10, 20, 30, 40};
    vrf.write_v128i32(1, v);
    auto r = vrf.read_v128i32(1);
    EXPECT_EQ(r, v);
}

TEST_F(VectorRegisterFileWidthAccessTest, V128f32_ReadWrite) {
    Vector128f32 v{1.0f, 2.0f, 3.0f, 4.0f};
    vrf.write_v128f32(2, v);
    auto r = vrf.read_v128f32(2);
    for (std::size_t i = 0; i < r.size(); ++i) {
        EXPECT_FLOAT_EQ(r[i], v[i]);
    }
}

TEST_F(VectorRegisterFileWidthAccessTest, V128i64_ReadWrite) {
    Vector128i64 v{1000, 2000};
    vrf.write_v128i64(3, v);
    auto r = vrf.read_v128i64(3);
    EXPECT_EQ(r, v);
}

TEST_F(VectorRegisterFileWidthAccessTest, V128f64_ReadWrite) {
    Vector128f64 v{1.5, 2.5};
    vrf.write_v128f64(4, v);
    auto r = vrf.read_v128f64(4);
    EXPECT_DOUBLE_EQ(r[0], 1.5);
    EXPECT_DOUBLE_EQ(r[1], 2.5);
}

TEST_F(VectorRegisterFileWidthAccessTest, V256i32_ReadWrite) {
    Vector256i32 v{1, 2, 3, 4, 5, 6, 7, 8};
    vrf.write_v256i32(5, v);
    auto r = vrf.read_v256i32(5);
    EXPECT_EQ(r, v);
}

TEST_F(VectorRegisterFileWidthAccessTest, V256f32_ReadWrite) {
    Vector256f32 v{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    vrf.write_v256f32(6, v);
    auto r = vrf.read_v256f32(6);
    for (std::size_t i = 0; i < r.size(); ++i) {
        EXPECT_FLOAT_EQ(r[i], v[i]);
    }
}

TEST_F(VectorRegisterFileWidthAccessTest, V256i64_ReadWrite) {
    Vector256i64 v{100, 200, 300, 400};
    vrf.write_v256i64(7, v);
    auto r = vrf.read_v256i64(7);
    EXPECT_EQ(r, v);
}

TEST_F(VectorRegisterFileWidthAccessTest, V256f64_ReadWrite) {
    Vector256f64 v{1.1, 2.2, 3.3, 4.4};
    vrf.write_v256f64(8, v);
    auto r = vrf.read_v256f64(8);
    for (std::size_t i = 0; i < r.size(); ++i) {
        EXPECT_DOUBLE_EQ(r[i], v[i]);
    }
}

TEST_F(VectorRegisterFileWidthAccessTest, V512i32_ReadWrite) {
    Vector512i32 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::int32_t>(i);
    }
    vrf.write_v512i32(9, v);
    auto r = vrf.read_v512i32(9);
    EXPECT_EQ(r, v);
}

TEST_F(VectorRegisterFileWidthAccessTest, V512f32_ReadWrite) {
    Vector512f32 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<float>(i) * 0.5f;
    }
    vrf.write_v512f32(10, v);
    auto r = vrf.read_v512f32(10);
    for (std::size_t i = 0; i < r.size(); ++i) {
        EXPECT_FLOAT_EQ(r[i], v[i]);
    }
}

TEST_F(VectorRegisterFileWidthAccessTest, V512i64_ReadWrite) {
    Vector512i64 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::int64_t>(i) * 1000;
    }
    vrf.write_v512i64(11, v);
    auto r = vrf.read_v512i64(11);
    EXPECT_EQ(r, v);
}

TEST_F(VectorRegisterFileWidthAccessTest, V512f64_ReadWrite) {
    Vector512f64 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<double>(i) * 1.5;
    }
    vrf.write_v512f64(12, v);
    auto r = vrf.read_v512f64(12);
    for (std::size_t i = 0; i < r.size(); ++i) {
        EXPECT_DOUBLE_EQ(r[i], v[i]);
    }
}

// ============================================================================
// VectorRegisterFile Bulk Operations Tests
// ============================================================================

class VectorRegisterFileBulkTest : public ::testing::Test {
protected:
    VectorRegisterFile vrf;
};

TEST_F(VectorRegisterFileBulkTest, Clear_ResetsAllRegisters) {
    // Write to all registers
    for (std::uint8_t i = 1; i < 32; ++i) {
        VectorRegister r;
        r.qwords[0] = static_cast<std::uint64_t>(i) * 100;
        vrf.write(i, r);
    }

    // Clear
    vrf.clear();

    // Verify all are zero (including V0 which should always be zero)
    for (std::uint8_t i = 0; i < 32; ++i) {
        const auto& r = vrf.read(i);
        EXPECT_TRUE(r.is_zero()) << "Register V" << static_cast<int>(i) << " not zero";
    }
}

TEST_F(VectorRegisterFileBulkTest, Size_Returns32) {
    EXPECT_EQ(vrf.size(), 32u);
    EXPECT_EQ(VectorRegisterFile::size(), 32u);
}

TEST_F(VectorRegisterFileBulkTest, ByteSize_Returns2048) {
    // 32 registers * 64 bytes each = 2048 bytes
    EXPECT_EQ(VectorRegisterFile::byte_size(), 2048u);
}

TEST_F(VectorRegisterFileBulkTest, RawView_ReturnsAllRegisters) {
    // Write distinctive values
    for (std::uint8_t i = 1; i < 32; ++i) {
        VectorRegister r;
        r.qwords[0] = static_cast<std::uint64_t>(i);
        vrf.write(i, r);
    }

    auto view = vrf.raw_view();
    EXPECT_EQ(view.size(), 32u);

    // V0 should be zero (even though we can't write to it)
    // Note: The raw_view shows the actual storage, which might not be zero
    // for V0's slot, but read(0) always returns the static zero register

    for (std::uint8_t i = 1; i < 32; ++i) {
        EXPECT_EQ(view[i].qwords[0], static_cast<std::uint64_t>(i));
    }
}

// ============================================================================
// Mixed Width Access Tests
// ============================================================================

class VectorRegisterFileMixedWidthTest : public ::testing::Test {
protected:
    VectorRegisterFile vrf;
};

TEST_F(VectorRegisterFileMixedWidthTest, Write512_Read128_ReturnsLowerBits) {
    Vector512i32 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::int32_t>(i + 1);
    }
    vrf.write_v512i32(5, v);

    // Reading 128 bits should give first 4 elements
    auto v128 = vrf.read_v128i32(5);
    EXPECT_EQ(v128[0], 1);
    EXPECT_EQ(v128[1], 2);
    EXPECT_EQ(v128[2], 3);
    EXPECT_EQ(v128[3], 4);
}

TEST_F(VectorRegisterFileMixedWidthTest, Write512_Read256_ReturnsLowerBits) {
    Vector512i32 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::int32_t>((i + 1) * 10);
    }
    vrf.write_v512i32(6, v);

    // Reading 256 bits should give first 8 elements
    auto v256 = vrf.read_v256i32(6);
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(v256[i], static_cast<std::int32_t>((i + 1) * 10));
    }
}

TEST_F(VectorRegisterFileMixedWidthTest, DifferentLaneTypes_SameStorage) {
    // Write as i32
    Vector128i32 vi{0x3F800000, 0x40000000, 0x40400000, 0x40800000};  // 1.0, 2.0, 3.0, 4.0 as bit patterns
    vrf.write_v128i32(7, vi);

    // Read as f32
    auto vf = vrf.read_v128f32(7);
    EXPECT_FLOAT_EQ(vf[0], 1.0f);
    EXPECT_FLOAT_EQ(vf[1], 2.0f);
    EXPECT_FLOAT_EQ(vf[2], 3.0f);
    EXPECT_FLOAT_EQ(vf[3], 4.0f);
}

// ============================================================================
// Template Access Tests
// ============================================================================

class VectorRegisterFileTemplateTest : public ::testing::Test {
protected:
    VectorRegisterFile vrf;
};

TEST_F(VectorRegisterFileTemplateTest, GenericV128Access) {
    Vector<128, std::int32_t> v{1, 2, 3, 4};
    vrf.write_v128<std::int32_t>(1, v);

    auto r = vrf.read_v128<std::int32_t>(1);
    EXPECT_EQ(r[0], 1);
    EXPECT_EQ(r[3], 4);
}

TEST_F(VectorRegisterFileTemplateTest, GenericV256Access) {
    Vector<256, double> v{1.0, 2.0, 3.0, 4.0};
    vrf.write_v256<double>(2, v);

    auto r = vrf.read_v256<double>(2);
    EXPECT_DOUBLE_EQ(r[0], 1.0);
    EXPECT_DOUBLE_EQ(r[3], 4.0);
}

TEST_F(VectorRegisterFileTemplateTest, GenericV512Access) {
    Vector<512, std::uint64_t> v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = i * 100;
    }
    vrf.write_v512<std::uint64_t>(3, v);

    auto r = vrf.read_v512<std::uint64_t>(3);
    for (std::size_t i = 0; i < r.size(); ++i) {
        EXPECT_EQ(r[i], i * 100);
    }
}

// ============================================================================
// Proxy Class Tests
// ============================================================================

class VectorRegisterFileProxyTest : public ::testing::Test {
protected:
    VectorRegisterFile vrf;
};

TEST_F(VectorRegisterFileProxyTest, ProxyRead_Works) {
    VectorRegister r;
    r.qwords[0] = 0x12345678ABCDEF00ULL;
    vrf.write(5, r);

    // Use proxy for read via implicit conversion
    const VectorRegister& read_via_proxy = vrf[5];
    EXPECT_EQ(read_via_proxy.qwords[0], 0x12345678ABCDEF00ULL);
}

TEST_F(VectorRegisterFileProxyTest, ProxyWrite_Works) {
    VectorRegister r;
    r.qwords[0] = 0xABCDEF0123456789ULL;

    // Use proxy for write
    vrf[10] = r;

    const auto& read_back = vrf.read(10);
    EXPECT_EQ(read_back.qwords[0], 0xABCDEF0123456789ULL);
}

TEST_F(VectorRegisterFileProxyTest, ProxyChainedOperations) {
    VectorRegister r1, r2;
    r1.qwords[0] = 111;
    r2.qwords[0] = 222;

    vrf[1] = r1;
    vrf[2] = r2;

    // Read via const operator[]
    const VectorRegisterFile& cvrf = vrf;
    EXPECT_EQ(cvrf[1].qwords[0], 111u);
    EXPECT_EQ(cvrf[2].qwords[0], 222u);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

class VectorRegisterFileEdgeCasesTest : public ::testing::Test {
protected:
    VectorRegisterFile vrf;
};

TEST_F(VectorRegisterFileEdgeCasesTest, AllRegisters_IndependentStorage) {
    // Write unique values to all registers
    for (std::uint8_t i = 1; i < 32; ++i) {
        Vector512i64 v;
        for (std::size_t j = 0; j < v.size(); ++j) {
            v[j] = static_cast<std::int64_t>(i * 1000 + j);
        }
        vrf.write_v512i64(i, v);
    }

    // Verify each register has its unique value
    for (std::uint8_t i = 1; i < 32; ++i) {
        auto v = vrf.read_v512i64(i);
        for (std::size_t j = 0; j < v.size(); ++j) {
            EXPECT_EQ(v[j], static_cast<std::int64_t>(i * 1000 + j))
                << "Mismatch at V" << static_cast<int>(i) << "[" << j << "]";
        }
    }
}

TEST_F(VectorRegisterFileEdgeCasesTest, LastRegister_V31_Works) {
    Vector512i32 v;
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<std::int32_t>(31000 + i);
    }
    vrf.write_v512i32(31, v);

    auto r = vrf.read_v512i32(31);
    for (std::size_t i = 0; i < r.size(); ++i) {
        EXPECT_EQ(r[i], static_cast<std::int32_t>(31000 + i));
    }
}

TEST_F(VectorRegisterFileEdgeCasesTest, FirstNonZeroRegister_V1_Works) {
    Vector128f64 v{1234.5678, 9876.5432};
    vrf.write_v128f64(1, v);

    auto r = vrf.read_v128f64(1);
    EXPECT_DOUBLE_EQ(r[0], 1234.5678);
    EXPECT_DOUBLE_EQ(r[1], 9876.5432);
}

TEST_F(VectorRegisterFileEdgeCasesTest, OverwriteRegister_ReplacesValue) {
    vrf.write_v128i32(5, Vector128i32{1, 2, 3, 4});

    // Verify first write
    auto r1 = vrf.read_v128i32(5);
    EXPECT_EQ(r1[0], 1);

    // Overwrite
    vrf.write_v128i32(5, Vector128i32{10, 20, 30, 40});

    // Verify overwrite
    auto r2 = vrf.read_v128i32(5);
    EXPECT_EQ(r2[0], 10);
    EXPECT_EQ(r2[3], 40);
}

}  // namespace
}  // namespace dotvm::core::simd
