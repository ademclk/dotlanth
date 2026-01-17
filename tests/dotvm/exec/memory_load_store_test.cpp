/// @file memory_load_store_test.cpp
/// @brief Unit tests for EXEC-006 Memory Load/Store Operations

#include <vector>

#include <dotvm/core/instruction.hpp>
#include <dotvm/core/memory.hpp>
#include <dotvm/core/value.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/exec/dispatch_macros.hpp>
#include <dotvm/exec/execution_context.hpp>
#include <dotvm/exec/execution_engine.hpp>

#include <gtest/gtest.h>

using namespace dotvm;
using namespace dotvm::exec;
using namespace dotvm::core;

// ============================================================================
// Test Fixture
// ============================================================================

class MemoryLoadStoreTest : public ::testing::Test {
protected:
    VmContext ctx_;
    ExecutionEngine engine_{ctx_};

    // Helper to create Type M instruction (EXEC-006)
    static std::uint32_t make_type_m(std::uint8_t op, std::uint8_t rd_rs2, std::uint8_t rs1,
                                     std::int8_t offset) {
        return encode_type_m(op, rd_rs2, rs1, offset);
    }

    // Helper to create Type C instruction (for HALT)
    static std::uint32_t make_type_c(std::uint8_t op, std::int32_t offset) {
        return encode_type_c(op, offset);
    }

    // Helper to run code
    ExecResult run(const std::vector<std::uint32_t>& code) {
        return engine_.execute(code.data(), code.size(), 0, {});
    }

    // Allocate memory and store handle in register
    Handle allocate_and_store(std::uint8_t reg, std::size_t size) {
        auto result = ctx_.memory().allocate(size);
        EXPECT_TRUE(result.has_value()) << "Memory allocation failed";
        ctx_.registers().write(reg, Value::from_handle(*result));
        return *result;
    }
};

// ============================================================================
// LOAD8 Tests
// ============================================================================

TEST_F(MemoryLoadStoreTest, Load8_BasicRead) {
    auto handle = allocate_and_store(1, 16);
    ASSERT_EQ(ctx_.memory().write<std::uint8_t>(handle, 0, 0xAB), MemoryError::Success);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD8, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 0xAB);
}

TEST_F(MemoryLoadStoreTest, Load8_WithOffset) {
    auto handle = allocate_and_store(1, 16);
    ASSERT_EQ(ctx_.memory().write<std::uint8_t>(handle, 5, 0xCD), MemoryError::Success);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD8, 2, 1, 5),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 0xCD);
}

TEST_F(MemoryLoadStoreTest, Load8_ZeroExtends) {
    auto handle = allocate_and_store(1, 16);
    ASSERT_EQ(ctx_.memory().write<std::uint8_t>(handle, 0, 0xFF), MemoryError::Success);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD8, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 0xFF);
}

// ============================================================================
// STORE8 Tests
// ============================================================================

TEST_F(MemoryLoadStoreTest, Store8_BasicWrite) {
    auto handle = allocate_and_store(1, 16);
    ctx_.registers().write(2, Value::from_int(0xEF));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE8, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    auto mem_result = ctx_.memory().read<std::uint8_t>(handle, 0);
    EXPECT_TRUE(mem_result.has_value());
    EXPECT_EQ(*mem_result, 0xEF);
}

TEST_F(MemoryLoadStoreTest, Store8_WithOffset) {
    auto handle = allocate_and_store(1, 16);
    ctx_.registers().write(2, Value::from_int(0x42));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE8, 2, 1, 7),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    auto mem_result = ctx_.memory().read<std::uint8_t>(handle, 7);
    EXPECT_TRUE(mem_result.has_value());
    EXPECT_EQ(*mem_result, 0x42);
}

TEST_F(MemoryLoadStoreTest, Store8_TruncatesValue) {
    auto handle = allocate_and_store(1, 16);
    ctx_.registers().write(2, Value::from_int(0x12345678ABCDLL));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE8, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    auto mem_result = ctx_.memory().read<std::uint8_t>(handle, 0);
    EXPECT_TRUE(mem_result.has_value());
    EXPECT_EQ(*mem_result, 0xCD);
}

// ============================================================================
// LOAD16 / STORE16 Tests
// ============================================================================

TEST_F(MemoryLoadStoreTest, Load16_AlignedAccess) {
    auto handle = allocate_and_store(1, 16);
    ASSERT_EQ(ctx_.memory().write<std::uint16_t>(handle, 0, 0x1234), MemoryError::Success);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD16, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 0x1234);
}

TEST_F(MemoryLoadStoreTest, Load16_UnalignedAccess_Error) {
    (void)allocate_and_store(1, 16);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD16, 2, 1, 1),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::UnalignedAccess);
}

TEST_F(MemoryLoadStoreTest, Store16_AlignedAccess) {
    auto handle = allocate_and_store(1, 16);
    ctx_.registers().write(2, Value::from_int(0xABCD));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE16, 2, 1, 2),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    auto mem_result = ctx_.memory().read<std::uint16_t>(handle, 2);
    EXPECT_TRUE(mem_result.has_value());
    EXPECT_EQ(*mem_result, 0xABCD);
}

TEST_F(MemoryLoadStoreTest, Store16_UnalignedAccess_Error) {
    (void)allocate_and_store(1, 16);
    ctx_.registers().write(2, Value::from_int(0x1234));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE16, 2, 1, 3),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::UnalignedAccess);
}

// ============================================================================
// LOAD32 / STORE32 Tests
// ============================================================================

TEST_F(MemoryLoadStoreTest, Load32_AlignedAccess) {
    auto handle = allocate_and_store(1, 32);
    ASSERT_EQ(ctx_.memory().write<std::uint32_t>(handle, 0, 0x12345678), MemoryError::Success);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD32, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 0x12345678);
}

TEST_F(MemoryLoadStoreTest, Load32_UnalignedAccess_Error) {
    (void)allocate_and_store(1, 32);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD32, 2, 1, 2),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::UnalignedAccess);
}

TEST_F(MemoryLoadStoreTest, Store32_AlignedAccess) {
    auto handle = allocate_and_store(1, 32);
    ctx_.registers().write(2, Value::from_int(0xDEADBEEFLL));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE32, 2, 1, 4),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    auto mem_result = ctx_.memory().read<std::uint32_t>(handle, 4);
    EXPECT_TRUE(mem_result.has_value());
    EXPECT_EQ(*mem_result, 0xDEADBEEFU);
}

TEST_F(MemoryLoadStoreTest, Store32_UnalignedAccess_Error) {
    (void)allocate_and_store(1, 32);
    ctx_.registers().write(2, Value::from_int(0x12345678));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE32, 2, 1, 1),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::UnalignedAccess);
}

// ============================================================================
// LOAD64 / STORE64 Tests
// ============================================================================

TEST_F(MemoryLoadStoreTest, Load64_AlignedAccess) {
    auto handle = allocate_and_store(1, 64);
    // Use a 48-bit value (max signed 48-bit: 0x7FFFFFFFFFFF)
    // Value class uses NaN boxing which limits integers to 48 bits
    constexpr std::uint64_t test_val = 0x0000123456789ABCULL;  // 48-bit value
    ASSERT_EQ(ctx_.memory().write<std::uint64_t>(handle, 0, test_val), MemoryError::Success);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD64, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), static_cast<std::int64_t>(test_val));
}

TEST_F(MemoryLoadStoreTest, Load64_UnalignedAccess_Error) {
    (void)allocate_and_store(1, 64);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD64, 2, 1, 4),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::UnalignedAccess);
}

TEST_F(MemoryLoadStoreTest, Store64_AlignedAccess) {
    auto handle = allocate_and_store(1, 64);
    // Use a 48-bit value that fits in Value's integer representation
    constexpr std::int64_t test_val = 0x0000123456789ABCLL;  // 48-bit value
    ctx_.registers().write(2, Value::from_int(test_val));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE64, 2, 1, 8),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    auto mem_result = ctx_.memory().read<std::uint64_t>(handle, 8);
    EXPECT_TRUE(mem_result.has_value());
    EXPECT_EQ(*mem_result, static_cast<std::uint64_t>(test_val));
}

TEST_F(MemoryLoadStoreTest, Store64_UnalignedAccess_Error) {
    (void)allocate_and_store(1, 64);
    ctx_.registers().write(2, Value::from_int(0x1234567890ABCDEFLL));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE64, 2, 1, 2),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::UnalignedAccess);
}

// ============================================================================
// LEA Tests
// ============================================================================

TEST_F(MemoryLoadStoreTest, LEA_BasicComputation) {
    auto handle = allocate_and_store(1, 64);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LEA, 2, 1, 16),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    auto base_ptr = ctx_.memory().get_ptr(handle);
    EXPECT_TRUE(base_ptr.has_value());
    auto expected_addr = reinterpret_cast<std::uintptr_t>(*base_ptr) + 16;
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), static_cast<std::int64_t>(expected_addr));
}

TEST_F(MemoryLoadStoreTest, LEA_ZeroOffset) {
    auto handle = allocate_and_store(1, 64);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LEA, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    auto base_ptr = ctx_.memory().get_ptr(handle);
    EXPECT_TRUE(base_ptr.has_value());
    EXPECT_EQ(ctx_.registers().read(2).as_integer(),
              static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(*base_ptr)));
}

// ============================================================================
// Bounds Checking Tests
// ============================================================================

TEST_F(MemoryLoadStoreTest, Load8_InvalidHandle) {
    // Test with an invalid handle (index=0, generation=0 is invalid)
    ctx_.registers().write(1, Value::from_handle(Handle{.index = 0, .generation = 0}));

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD8, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::MemoryError);
}

TEST_F(MemoryLoadStoreTest, Store8_InvalidHandle) {
    // Test with an invalid handle (index=0, generation=0 is invalid)
    ctx_.registers().write(1, Value::from_handle(Handle{.index = 0, .generation = 0}));
    ctx_.registers().write(2, Value::from_int(0xFF));

    std::vector<std::uint32_t> code = {make_type_m(opcode::STORE8, 2, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::MemoryError);
}

// ============================================================================
// Roundtrip Tests (Store then Load)
// ============================================================================

TEST_F(MemoryLoadStoreTest, Roundtrip_AllSizes) {
    (void)allocate_and_store(1, 64);

    // Use a 48-bit value for 64-bit test (Value uses NaN boxing, max 48-bit integers)
    constexpr std::int64_t val64 = 0x0000123456789ABCLL;  // 48-bit value

    ctx_.registers().write(2, Value::from_int(0xAB));
    ctx_.registers().write(3, Value::from_int(0xCDEF));
    ctx_.registers().write(4, Value::from_int(0x12345678));
    ctx_.registers().write(5, Value::from_int(val64));

    std::vector<std::uint32_t> code = {
        make_type_m(opcode::STORE8, 2, 1, 0),  make_type_m(opcode::STORE16, 3, 1, 2),
        make_type_m(opcode::STORE32, 4, 1, 8), make_type_m(opcode::STORE64, 5, 1, 16),
        make_type_m(opcode::LOAD8, 6, 1, 0),   make_type_m(opcode::LOAD16, 7, 1, 2),
        make_type_m(opcode::LOAD32, 8, 1, 8),  make_type_m(opcode::LOAD64, 9, 1, 16),
        make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);

    EXPECT_EQ(ctx_.registers().read(6).as_integer(), 0xAB);
    EXPECT_EQ(ctx_.registers().read(7).as_integer(), 0xCDEF);
    EXPECT_EQ(ctx_.registers().read(8).as_integer(), 0x12345678);
    EXPECT_EQ(ctx_.registers().read(9).as_integer(), val64);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(MemoryLoadStoreTest, MaxPositiveOffset) {
    auto handle = allocate_and_store(1, 256);
    ASSERT_EQ(ctx_.memory().write<std::uint8_t>(handle, 127, 0x99), MemoryError::Success);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD8, 2, 1, 127),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(2).as_integer(), 0x99);
}

TEST_F(MemoryLoadStoreTest, WriteToR0_Ignored) {
    auto handle = allocate_and_store(1, 16);
    ASSERT_EQ(ctx_.memory().write<std::uint8_t>(handle, 0, 0xFF), MemoryError::Success);

    std::vector<std::uint32_t> code = {make_type_m(opcode::LOAD8, 0, 1, 0),
                                       make_type_c(opcode::HALT, 0)};

    auto result = run(code);
    EXPECT_EQ(result, ExecResult::Success);
    EXPECT_EQ(ctx_.registers().read(0).as_integer(), 0);
}

// ============================================================================
// Type M Instruction Encoding Tests
// ============================================================================

TEST(TypeMEncodingTest, EncodeDecodeRoundtrip) {
    auto instr = encode_type_m(opcode::LOAD8, 5, 10, -16);
    auto decoded = decode_type_m(instr);

    EXPECT_EQ(decoded.opcode, opcode::LOAD8);
    EXPECT_EQ(decoded.rd_rs2, 5);
    EXPECT_EQ(decoded.rs1, 10);
    EXPECT_EQ(decoded.offset8, -16);
}

TEST(TypeMEncodingTest, AllOpcodes) {
    std::vector<std::uint8_t> opcodes = {opcode::LOAD8,   opcode::LOAD16,  opcode::LOAD32,
                                         opcode::LOAD64,  opcode::STORE8,  opcode::STORE16,
                                         opcode::STORE32, opcode::STORE64, opcode::LEA};

    for (auto op : opcodes) {
        auto instr = encode_type_m(op, 15, 20, 50);
        auto decoded = decode_type_m(instr);
        EXPECT_EQ(decoded.opcode, op);
        EXPECT_EQ(decoded.rd_rs2, 15);
        EXPECT_EQ(decoded.rs1, 20);
        EXPECT_EQ(decoded.offset8, 50);
    }
}
