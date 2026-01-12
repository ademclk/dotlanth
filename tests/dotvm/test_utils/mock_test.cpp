/// @file mock_test.cpp
/// @brief Tests for mock implementations
///
/// Verifies that the mock implementations work correctly and
/// demonstrates their usage patterns for other test files.

#include "mock_register_file.hpp"
#include "mock_memory_manager.hpp"
#include "test_fixtures.hpp"

#include <gtest/gtest.h>

namespace dotvm::test {
namespace {

// ============================================================================
// MockRegisterFile Tests
// ============================================================================

class MockRegisterFileTest : public ::testing::Test {
protected:
    MockRegisterFile regs;
};

TEST_F(MockRegisterFileTest, DefaultInitialization) {
    EXPECT_EQ(regs.size(), 256);
    // All registers should be nil by default
    for (std::size_t i = 0; i < regs.size(); ++i) {
        EXPECT_TRUE(regs.read(static_cast<std::uint8_t>(i)).is_nil());
    }
}

TEST_F(MockRegisterFileTest, ReadWrite) {
    regs.write(1, core::Value::from_int(42));
    EXPECT_EQ(regs.read(1).as_integer(), 42);

    regs.write(2, core::Value::from_float(3.14));
    EXPECT_DOUBLE_EQ(regs.read(2).as_float(), 3.14);

    regs.write(3, core::Value::from_bool(true));
    EXPECT_TRUE(regs.read(3).as_bool());
}

TEST_F(MockRegisterFileTest, AccessLogging) {
    regs.write(1, core::Value::from_int(10));
    auto val = regs.read(1);
    (void)val;

    EXPECT_EQ(regs.total_accesses(), 2);
    EXPECT_EQ(regs.write_count(1), 1);
    EXPECT_EQ(regs.read_count(1), 1);
}

TEST_F(MockRegisterFileTest, ClearLog) {
    regs.write(1, core::Value::from_int(10));
    [[maybe_unused]] auto val = regs.read(1);

    regs.clear_log();

    EXPECT_EQ(regs.total_accesses(), 0);
}

TEST_F(MockRegisterFileTest, Reset) {
    regs.write(1, core::Value::from_int(42));
    regs.reset();

    // Read to check value (this will add to access log)
    EXPECT_TRUE(regs.read(1).is_nil());
    // Access log should only have the post-reset read
    EXPECT_EQ(regs.total_accesses(), 1);  // Just the read above
}

TEST_F(MockRegisterFileTest, OutOfBoundsRead) {
    // Out of bounds should return nil
    auto val = regs.read(255);
    EXPECT_TRUE(val.is_nil() || val.type() == core::ValueType::Nil);
}

// ============================================================================
// MockArchRegisterFile Tests
// ============================================================================

class MockArchRegisterFileTest : public ::testing::Test {};

TEST_F(MockArchRegisterFileTest, Arch64NoMasking) {
    MockArchRegisterFile regs(core::Architecture::Arch64);

    std::int64_t large_val = 0x1'0000'0000LL;  // Exceeds 32 bits
    regs.write(1, core::Value::from_int(large_val));

    EXPECT_EQ(regs.read(1).as_integer(), large_val);
}

TEST_F(MockArchRegisterFileTest, Arch32Masking) {
    MockArchRegisterFile regs(core::Architecture::Arch32);

    std::int64_t large_val = 0x1'0000'0000LL;  // Exceeds 32 bits
    regs.write(1, core::Value::from_int(large_val));

    // Should be masked to 32 bits (wrapped to 0)
    EXPECT_EQ(regs.read(1).as_integer(), 0);
}

TEST_F(MockArchRegisterFileTest, ArchQuery) {
    MockArchRegisterFile regs32(core::Architecture::Arch32);
    MockArchRegisterFile regs64(core::Architecture::Arch64);

    EXPECT_EQ(regs32.arch(), core::Architecture::Arch32);
    EXPECT_EQ(regs64.arch(), core::Architecture::Arch64);
}

// ============================================================================
// MockMemoryManager Tests
// ============================================================================

class MockMemoryManagerTest : public ::testing::Test {
protected:
    MockMemoryManager mem;
};

TEST_F(MockMemoryManagerTest, AllocateAndDeallocate) {
    auto result = mem.allocate(4096);
    ASSERT_TRUE(result.has_value());

    auto handle = *result;
    EXPECT_TRUE(mem.is_valid(handle));
    EXPECT_EQ(mem.active_allocations(), 1);

    auto err = mem.deallocate(handle);
    EXPECT_EQ(err, core::MemoryError::Success);
    EXPECT_FALSE(mem.is_valid(handle));
    EXPECT_EQ(mem.active_allocations(), 0);
}

TEST_F(MockMemoryManagerTest, ReadWrite) {
    auto result = mem.allocate(4096);
    ASSERT_TRUE(result.has_value());
    auto handle = *result;

    // Write and read back
    auto write_err = mem.write<int>(handle, 0, 42);
    EXPECT_EQ(write_err, core::MemoryError::Success);

    auto read_result = mem.read<int>(handle, 0);
    ASSERT_TRUE(read_result.has_value());
    EXPECT_EQ(*read_result, 42);
}

TEST_F(MockMemoryManagerTest, BoundsViolation) {
    auto result = mem.allocate(4096);
    ASSERT_TRUE(result.has_value());
    auto handle = *result;

    // Write past end
    auto err = mem.write<int>(handle, 4096, 42);
    EXPECT_EQ(err, core::MemoryError::BoundsViolation);
}

TEST_F(MockMemoryManagerTest, InvalidHandle) {
    core::Handle invalid{9999, 1};

    auto read_result = mem.read<int>(invalid, 0);
    EXPECT_FALSE(read_result.has_value());
    EXPECT_EQ(read_result.error(), core::MemoryError::InvalidHandle);
}

TEST_F(MockMemoryManagerTest, UseAfterFree) {
    auto result = mem.allocate(4096);
    ASSERT_TRUE(result.has_value());
    auto handle = *result;

    [[maybe_unused]] auto dealloc_err = mem.deallocate(handle);

    // Handle should now be invalid
    auto read_result = mem.read<int>(handle, 0);
    EXPECT_FALSE(read_result.has_value());
    EXPECT_EQ(read_result.error(), core::MemoryError::InvalidHandle);
}

TEST_F(MockMemoryManagerTest, FailureInjection) {
    mem.fail_next_allocate(core::MemoryError::AllocationFailed);

    auto result = mem.allocate(4096);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), core::MemoryError::AllocationFailed);

    // Next allocation should succeed
    auto result2 = mem.allocate(4096);
    EXPECT_TRUE(result2.has_value());
}

TEST_F(MockMemoryManagerTest, ZeroSizeRejected) {
    auto result = mem.allocate(0);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), core::MemoryError::InvalidSize);
}

TEST_F(MockMemoryManagerTest, GetSize) {
    auto result = mem.allocate(100);  // Will be rounded to page size
    ASSERT_TRUE(result.has_value());

    auto size_result = mem.get_size(*result);
    ASSERT_TRUE(size_result.has_value());
    EXPECT_GE(*size_result, 100);  // At least requested
    EXPECT_EQ(*size_result % 4096, 0);  // Page aligned
}

TEST_F(MockMemoryManagerTest, Reset) {
    auto result = mem.allocate(4096);
    ASSERT_TRUE(result.has_value());

    mem.reset();

    EXPECT_EQ(mem.active_allocations(), 0);
    EXPECT_EQ(mem.total_allocated_bytes(), 0);
    EXPECT_TRUE(mem.access_log().empty());
}

// ============================================================================
// Test Fixtures Tests
// ============================================================================

class TestFixturesTest : public ::testing::Test {};

TEST_F(TestFixturesTest, GenerateTestValues) {
    auto values = generate_test_values();

    EXPECT_GT(values.size(), 10);

    // Should have variety of types
    bool has_int = false, has_float = false, has_bool = false, has_nil = false;
    for (const auto& v : values) {
        if (v.is_integer()) has_int = true;
        if (v.is_float()) has_float = true;
        if (v.is_bool()) has_bool = true;
        if (v.is_nil()) has_nil = true;
    }

    EXPECT_TRUE(has_int);
    EXPECT_TRUE(has_float);
    EXPECT_TRUE(has_bool);
    EXPECT_TRUE(has_nil);
}

TEST_F(TestFixturesTest, GenerateRandomIntegers) {
    auto values = generate_random_integers(100, -1000, 1000);

    EXPECT_EQ(values.size(), 100);

    for (const auto& v : values) {
        EXPECT_TRUE(v.is_integer());
        auto i = v.as_integer();
        EXPECT_GE(i, -1000);
        EXPECT_LE(i, 1000);
    }
}

TEST_F(TestFixturesTest, InstructionEncoding) {
    auto instr = encode_type_a(0x00, 1, 2, 3);  // ADD R1, R2, R3

    auto decoded = core::decode_type_a(instr);
    EXPECT_EQ(decoded.opcode, 0x00);
    EXPECT_EQ(decoded.rd, 1);
    EXPECT_EQ(decoded.rs1, 2);
    EXPECT_EQ(decoded.rs2, 3);
}

TEST_F(TestFixturesTest, MakeProgram) {
    auto program = make_program({
        encode_type_a(0x00, 1, 2, 3),  // ADD
        encode_type_a(0x01, 4, 5, 6),  // SUB
    });

    EXPECT_EQ(program.size(), 3);  // 2 instructions + HALT

    auto last = core::decode_type_a(program.back());
    EXPECT_EQ(last.opcode, exec::opcode::HALT);
}

TEST_F(TestFixturesTest, ScopedAllocation) {
    core::MemoryManager mm;

    {
        ScopedAllocation alloc(mm, 4096);
        EXPECT_TRUE(alloc.valid());
        EXPECT_TRUE(mm.is_valid(alloc.handle()));
        EXPECT_EQ(mm.active_allocations(), 1);
    }

    // After scope, allocation should be freed
    EXPECT_EQ(mm.active_allocations(), 0);
}

} // namespace
} // namespace dotvm::test
