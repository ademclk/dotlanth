/// @file arch_register_file_test.cpp
/// @brief Unit tests for the architecture-aware register file

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "dotvm/core/register_file.hpp"

namespace dotvm::core {
namespace {

// ============================================================================
// Construction Tests
// ============================================================================

class ArchRegisterFileConstructionTest : public ::testing::Test {};

TEST_F(ArchRegisterFileConstructionTest, DefaultConstructor_UsesArch64) {
    ArchRegisterFile rf;
    EXPECT_EQ(rf.arch(), Architecture::Arch64);
}

TEST_F(ArchRegisterFileConstructionTest, ExplicitArch32) {
    ArchRegisterFile rf{Architecture::Arch32};
    EXPECT_EQ(rf.arch(), Architecture::Arch32);
}

TEST_F(ArchRegisterFileConstructionTest, SetArch_ChangesArchitecture) {
    ArchRegisterFile rf{Architecture::Arch64};
    rf.set_arch(Architecture::Arch32);
    EXPECT_EQ(rf.arch(), Architecture::Arch32);
}

TEST_F(ArchRegisterFileConstructionTest, Size_Returns256) {
    EXPECT_EQ(ArchRegisterFile::size(), 256u);
}

// ============================================================================
// Write Masking Tests - Arch32
// ============================================================================

class ArchRegisterFileWrite32Test : public ::testing::Test {
protected:
    ArchRegisterFile rf_{Architecture::Arch32};
};

TEST_F(ArchRegisterFileWrite32Test, Write_SmallPositiveInteger_Unchanged) {
    rf_.write(1, Value::from_int(42));
    EXPECT_EQ(rf_.read(1).as_integer(), 42);
}

TEST_F(ArchRegisterFileWrite32Test, Write_SmallNegativeInteger_Unchanged) {
    rf_.write(1, Value::from_int(-42));
    EXPECT_EQ(rf_.read(1).as_integer(), -42);
}

TEST_F(ArchRegisterFileWrite32Test, Write_LargeInteger_Masked) {
    // 0x1'0000'0000 (2^32) should become 0 after masking
    rf_.write(1, Value::from_int(0x1'0000'0000LL));
    EXPECT_EQ(rf_.read(1).as_integer(), 0);
}

TEST_F(ArchRegisterFileWrite32Test, Write_LargeIntegerWithLowerBits_Truncated) {
    // 0x1'2345'6789 should become 0x2345'6789 (positive in 32-bit)
    rf_.write(1, Value::from_int(0x1'2345'6789LL));
    EXPECT_EQ(rf_.read(1).as_integer(), 0x2345'6789LL);
}

TEST_F(ArchRegisterFileWrite32Test, Write_ValueExceeding32Bits_SignExtended) {
    // 0x1'8000'0000 should become INT32_MIN after masking (bit 31 set)
    rf_.write(1, Value::from_int(0x1'8000'0000LL));
    EXPECT_EQ(rf_.read(1).as_integer(), std::numeric_limits<std::int32_t>::min());
}

TEST_F(ArchRegisterFileWrite32Test, WriteInt_Masks) {
    // write_int directly takes an integer and masks it
    rf_.write_int(1, 0x1'FFFF'FFFFLL);
    EXPECT_EQ(rf_.read(1).as_integer(), -1);  // All 32 bits set = -1
}

TEST_F(ArchRegisterFileWrite32Test, WriteRaw_NoMasking) {
    // write_raw bypasses masking
    rf_.write_raw(1, Value::from_int(0x1'0000'0000LL));
    // The value is stored as-is (though it will be masked when read back via as_integer)
    EXPECT_EQ(rf_.read(1).as_integer(), 0x1'0000'0000LL);
}

// ============================================================================
// Write Masking Tests - Arch64
// ============================================================================

class ArchRegisterFileWrite64Test : public ::testing::Test {
protected:
    ArchRegisterFile rf_{Architecture::Arch64};
};

TEST_F(ArchRegisterFileWrite64Test, Write_LargeInteger_Preserved) {
    // In Arch64, large values should be preserved (within 48-bit limit)
    rf_.write(1, Value::from_int(0x1'0000'0000LL));
    EXPECT_EQ(rf_.read(1).as_integer(), 0x1'0000'0000LL);
}

TEST_F(ArchRegisterFileWrite64Test, Write_SmallInteger_Unchanged) {
    rf_.write(1, Value::from_int(42));
    EXPECT_EQ(rf_.read(1).as_integer(), 42);
}

// ============================================================================
// Non-Integer Value Tests
// ============================================================================

class ArchRegisterFileNonIntegerTest : public ::testing::Test {
protected:
    ArchRegisterFile rf_{Architecture::Arch32};
};

TEST_F(ArchRegisterFileNonIntegerTest, Write_Float_NotMasked) {
    rf_.write(1, Value::from_float(3.14159));
    EXPECT_DOUBLE_EQ(rf_.read(1).as_float(), 3.14159);
}

TEST_F(ArchRegisterFileNonIntegerTest, Write_Bool_NotMasked) {
    rf_.write(1, Value::from_bool(true));
    EXPECT_TRUE(rf_.read(1).as_bool());

    rf_.write(2, Value::from_bool(false));
    EXPECT_FALSE(rf_.read(2).as_bool());
}

TEST_F(ArchRegisterFileNonIntegerTest, Write_Handle_NotMasked) {
    Handle h{.index = 42, .generation = 7};
    rf_.write(1, Value::from_handle(h));

    auto result = rf_.read(1).as_handle();
    EXPECT_EQ(result.index, 42u);
    EXPECT_EQ(result.generation, 7u);
}

TEST_F(ArchRegisterFileNonIntegerTest, Write_Nil_NotMasked) {
    rf_.write(1, Value::from_int(42));  // First set to non-nil
    rf_.write(1, Value::nil());
    EXPECT_TRUE(rf_.read(1).is_nil());
}

// ============================================================================
// R0 (Zero Register) Tests
// ============================================================================

class ArchRegisterFileR0Test : public ::testing::Test {
protected:
    ArchRegisterFile rf_{Architecture::Arch32};
};

TEST_F(ArchRegisterFileR0Test, Read_AlwaysZero) {
    EXPECT_EQ(rf_.read(0).as_float(), 0.0);
}

TEST_F(ArchRegisterFileR0Test, Write_IsIgnored) {
    rf_.write(0, Value::from_int(42));
    EXPECT_EQ(rf_.read(0).as_float(), 0.0);
}

TEST_F(ArchRegisterFileR0Test, WriteInt_IsIgnored) {
    rf_.write_int(0, 42);
    EXPECT_EQ(rf_.read(0).as_float(), 0.0);
}

// ============================================================================
// Operator[] Tests
// ============================================================================

class ArchRegisterFileOperatorTest : public ::testing::Test {
protected:
    ArchRegisterFile rf_{Architecture::Arch32};
};

TEST_F(ArchRegisterFileOperatorTest, OperatorBracket_Read) {
    rf_.write(1, Value::from_int(42));
    Value val = rf_[1];  // Implicit conversion via proxy
    EXPECT_EQ(val.as_integer(), 42);
}

TEST_F(ArchRegisterFileOperatorTest, OperatorBracket_Write_MasksValue) {
    rf_[1] = Value::from_int(0x1'0000'0000LL);
    Value val = rf_[1];              // Implicit conversion via proxy
    EXPECT_EQ(val.as_integer(), 0);  // Masked to 32 bits
}

TEST_F(ArchRegisterFileOperatorTest, ConstOperatorBracket_Read) {
    rf_.write(1, Value::from_int(42));
    const ArchRegisterFile& const_rf = rf_;
    EXPECT_EQ(const_rf[1].as_integer(), 42);
}

// ============================================================================
// Clear Tests
// ============================================================================

class ArchRegisterFileClearTest : public ::testing::Test {};

TEST_F(ArchRegisterFileClearTest, Clear_ResetsAllRegisters) {
    ArchRegisterFile rf{Architecture::Arch32};

    // Set some registers
    rf.write(1, Value::from_int(42));
    rf.write(10, Value::from_int(100));
    rf.write(255, Value::from_int(999));

    rf.clear();

    // All should be zero (float 0.0)
    EXPECT_EQ(rf.read(1).as_float(), 0.0);
    EXPECT_EQ(rf.read(10).as_float(), 0.0);
    EXPECT_EQ(rf.read(255).as_float(), 0.0);
}

// ============================================================================
// Raw Access Tests
// ============================================================================

class ArchRegisterFileRawAccessTest : public ::testing::Test {};

TEST_F(ArchRegisterFileRawAccessTest, Raw_ReturnsMutableRegisterFile) {
    ArchRegisterFile rf{Architecture::Arch32};

    // Write through raw() bypasses architecture masking
    rf.raw().write(1, Value::from_int(0x1'0000'0000LL));

    // Reading back gives the unmasked value
    EXPECT_EQ(rf.read(1).as_integer(), 0x1'0000'0000LL);
}

TEST_F(ArchRegisterFileRawAccessTest, RawView_ReturnsAllRegisters) {
    ArchRegisterFile rf{Architecture::Arch32};

    rf.write(1, Value::from_int(42));
    rf.write(2, Value::from_int(100));

    auto view = rf.raw_view();
    EXPECT_EQ(view.size(), 256u);
    EXPECT_EQ(view[1].as_integer(), 42);
    EXPECT_EQ(view[2].as_integer(), 100);
}

// ============================================================================
// Architecture Switching Tests
// ============================================================================

class ArchRegisterFileSwitchTest : public ::testing::Test {};

TEST_F(ArchRegisterFileSwitchTest, SwitchArch_AffectsSubsequentWrites) {
    ArchRegisterFile rf{Architecture::Arch64};

    // Write in Arch64 mode - large value preserved
    rf.write(1, Value::from_int(0x1'0000'0000LL));
    EXPECT_EQ(rf.read(1).as_integer(), 0x1'0000'0000LL);

    // Switch to Arch32
    rf.set_arch(Architecture::Arch32);

    // Write again - now masked
    rf.write(2, Value::from_int(0x1'0000'0000LL));
    EXPECT_EQ(rf.read(2).as_integer(), 0);

    // Existing value in reg 1 is unchanged
    EXPECT_EQ(rf.read(1).as_integer(), 0x1'0000'0000LL);
}

// ============================================================================
// Boundary Value Tests
// ============================================================================

class ArchRegisterFileBoundaryTest : public ::testing::Test {};

TEST_F(ArchRegisterFileBoundaryTest, Arch32_INT32_MAX_Preserved) {
    ArchRegisterFile rf{Architecture::Arch32};

    rf.write(1, Value::from_int(std::numeric_limits<std::int32_t>::max()));
    EXPECT_EQ(rf.read(1).as_integer(), std::numeric_limits<std::int32_t>::max());
}

TEST_F(ArchRegisterFileBoundaryTest, Arch32_INT32_MIN_Preserved) {
    ArchRegisterFile rf{Architecture::Arch32};

    rf.write(1, Value::from_int(std::numeric_limits<std::int32_t>::min()));
    EXPECT_EQ(rf.read(1).as_integer(), std::numeric_limits<std::int32_t>::min());
}

TEST_F(ArchRegisterFileBoundaryTest, Arch32_INT32_MAX_Plus1_WrapsToMIN) {
    ArchRegisterFile rf{Architecture::Arch32};

    rf.write(1, Value::from_int(
                    static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1));
    EXPECT_EQ(rf.read(1).as_integer(), std::numeric_limits<std::int32_t>::min());
}

TEST_F(ArchRegisterFileBoundaryTest, AllRegistersAccessible) {
    ArchRegisterFile rf{Architecture::Arch32};

    // Test writing to all registers except R0
    for (std::uint16_t i = 1; i < 256; ++i) {
        rf.write(static_cast<std::uint8_t>(i), Value::from_int(static_cast<std::int64_t>(i)));
    }

    for (std::uint16_t i = 1; i < 256; ++i) {
        EXPECT_EQ(rf.read(static_cast<std::uint8_t>(i)).as_integer(), static_cast<std::int64_t>(i));
    }
}

}  // namespace
}  // namespace dotvm::core
