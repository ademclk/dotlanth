#include <dotvm/core/register_file.hpp>

#include <gtest/gtest.h>

using namespace dotvm::core;

class RegisterFileTest : public ::testing::Test {
protected:
    RegisterFile rf;
};

// Size and alignment
TEST_F(RegisterFileTest, SizeAndAlignment) {
    EXPECT_EQ(RegisterFile::size(), 256u);
    // Allow for alignment padding (256 * 8 = 2048, rounded up to cache line)
    EXPECT_LE(RegisterFile::byte_size(), 256u * 8u + 64u);
    EXPECT_EQ(alignof(RegisterFile), 64u);  // Cache-line aligned
}

// R0 hardwired zero behavior - read
TEST_F(RegisterFileTest, R0AlwaysReturnsZero) {
    Value r0 = rf.read(0);
    EXPECT_TRUE(r0.is_float());
    EXPECT_EQ(r0.as_float(), 0.0);
}

// R0 hardwired zero behavior - write ignored
TEST_F(RegisterFileTest, R0WriteIsIgnored) {
    rf.write(0, Value::from_int(42));
    Value r0 = rf.read(0);
    EXPECT_TRUE(r0.is_float());
    EXPECT_EQ(r0.as_float(), 0.0);
}

// R0 via operator[]
TEST_F(RegisterFileTest, R0OperatorAlwaysReturnsZero) {
    rf[0] = Value::from_int(100);
    Value r0 = rf[0];
    EXPECT_EQ(r0.as_float(), 0.0);
}

// Normal register read/write
TEST_F(RegisterFileTest, NormalRegisterReadWrite) {
    for (int i = 1; i < 256; ++i) {
        auto reg = static_cast<std::uint8_t>(i);
        Value v = Value::from_int(static_cast<std::int64_t>(i) * 100);
        rf.write(reg, v);
        Value read_back = rf.read(reg);
        EXPECT_EQ(read_back.as_integer(), static_cast<std::int64_t>(i) * 100);
    }
}

// Operator[] access
TEST_F(RegisterFileTest, OperatorAccess) {
    rf[10] = Value::from_float(3.14);
    Value v = rf[10];
    EXPECT_DOUBLE_EQ(v.as_float(), 3.14);
}

// Different value types
TEST_F(RegisterFileTest, DifferentValueTypes) {
    rf.write(1, Value::from_int(42));
    rf.write(2, Value::from_float(2.718));
    rf.write(3, Value::from_bool(true));
    rf.write(4, Value::from_handle(100, 5));
    rf.write(5, Value::nil());

    EXPECT_TRUE(rf.read(1).is_integer());
    EXPECT_EQ(rf.read(1).as_integer(), 42);

    EXPECT_TRUE(rf.read(2).is_float());
    EXPECT_DOUBLE_EQ(rf.read(2).as_float(), 2.718);

    EXPECT_TRUE(rf.read(3).is_bool());
    EXPECT_TRUE(rf.read(3).as_bool());

    EXPECT_TRUE(rf.read(4).is_handle());
    EXPECT_EQ(rf.read(4).as_handle().index, 100u);
    EXPECT_EQ(rf.read(4).as_handle().generation, 5u);

    EXPECT_TRUE(rf.read(5).is_nil());
}

// Register classification
TEST_F(RegisterFileTest, RegisterClassification) {
    EXPECT_EQ(classify_register(0), RegisterClass::Zero);

    for (std::uint8_t i = 1; i <= 15; ++i) {
        EXPECT_EQ(classify_register(i), RegisterClass::CallerSaved);
        EXPECT_TRUE(is_caller_saved(i));
        EXPECT_FALSE(is_callee_saved(i));
    }

    for (std::uint8_t i = 16; i <= 31; ++i) {
        EXPECT_EQ(classify_register(i), RegisterClass::CalleeSaved);
        EXPECT_FALSE(is_caller_saved(i));
        EXPECT_TRUE(is_callee_saved(i));
    }

    for (int i = 32; i <= 255; ++i) {
        EXPECT_EQ(classify_register(static_cast<std::uint8_t>(i)), RegisterClass::General);
        EXPECT_TRUE(is_general(static_cast<std::uint8_t>(i)));
    }
}

// Bulk save/restore caller-saved
TEST_F(RegisterFileTest, CallerSavedSaveRestore) {
    // Set up caller-saved registers
    for (std::uint8_t i = 1; i <= 15; ++i) {
        rf.write(i, Value::from_int(static_cast<std::int64_t>(i) * 10));
    }

    // Save
    std::array<Value, reg_range::CALLER_SAVED_COUNT> saved;
    rf.save_caller_saved(saved);

    // Clear
    rf.clear();

    // Verify cleared
    for (std::uint8_t i = 1; i <= 15; ++i) {
        EXPECT_EQ(rf.read(i).as_float(), 0.0);
    }

    // Restore
    rf.restore_caller_saved(saved);

    // Verify restored
    for (std::uint8_t i = 1; i <= 15; ++i) {
        EXPECT_EQ(rf.read(i).as_integer(), static_cast<std::int64_t>(i) * 10);
    }
}

// Bulk save/restore callee-saved
TEST_F(RegisterFileTest, CalleeSavedSaveRestore) {
    // Set up callee-saved registers
    for (std::uint8_t i = 16; i <= 31; ++i) {
        rf.write(i, Value::from_int(static_cast<std::int64_t>(i) * 20));
    }

    // Save
    std::array<Value, reg_range::CALLEE_SAVED_COUNT> saved;
    rf.save_callee_saved(saved);

    // Clear
    rf.clear();

    // Restore
    rf.restore_callee_saved(saved);

    // Verify restored
    for (std::uint8_t i = 16; i <= 31; ++i) {
        EXPECT_EQ(rf.read(i).as_integer(), static_cast<std::int64_t>(i) * 20);
    }
}

// Clear operation
TEST_F(RegisterFileTest, Clear) {
    // Write to various registers
    rf.write(1, Value::from_int(100));
    rf.write(100, Value::from_float(3.14));
    rf.write(255, Value::from_bool(true));

    // Clear
    rf.clear();

    // All should be zero
    for (int i = 0; i < 256; ++i) {
        Value v = rf.read(static_cast<std::uint8_t>(i));
        EXPECT_TRUE(v.is_float());
        EXPECT_EQ(v.as_float(), 0.0);
    }
}

// Raw view access
TEST_F(RegisterFileTest, RawView) {
    rf.write(50, Value::from_int(12345));

    auto view = rf.raw_view();
    EXPECT_EQ(view.size(), 256u);
    EXPECT_EQ(view[50].as_integer(), 12345);
}

// Span access to caller/callee saved
TEST_F(RegisterFileTest, SpanAccess) {
    rf.write(1, Value::from_int(111));
    rf.write(16, Value::from_int(1616));

    auto caller = rf.caller_saved_regs();
    auto callee = rf.callee_saved_regs();

    EXPECT_EQ(caller.size(), 15u);
    EXPECT_EQ(callee.size(), 16u);
    EXPECT_EQ(caller[0].as_integer(), 111);
    EXPECT_EQ(callee[0].as_integer(), 1616);
}

// Const access
TEST_F(RegisterFileTest, ConstAccess) {
    rf.write(5, Value::from_int(55));
    const RegisterFile& crf = rf;
    Value v = crf[5];
    EXPECT_EQ(v.as_integer(), 55);
}

// Constexpr test
TEST_F(RegisterFileTest, ConstexprOperations) {
    constexpr auto test_constexpr = []() {
        RegisterFile rf;
        rf.write(1, Value::from_int(42));
        return rf.read(1).as_integer();
    };

    static_assert(test_constexpr() == 42);
}
