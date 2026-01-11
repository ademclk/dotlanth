#include <gtest/gtest.h>

// GCC's -Warray-bounds warning has false positives with constexpr code
// that has early size checks. Suppress for this fuzz test file.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#include <dotvm/core/bytecode.hpp>
#include <dotvm/core/memory.hpp>
#include <dotvm/core/value.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace dotvm::core;

// ============================================================================
// Bytecode Malformation Tests
// ============================================================================

class BytecodeFuzzTest : public ::testing::Test {
protected:
    // Create a minimal valid bytecode header
    std::vector<std::uint8_t> make_valid_header() {
        std::vector<std::uint8_t> data(bytecode::HEADER_SIZE, 0);

        // Magic
        data[0] = 'D'; data[1] = 'O'; data[2] = 'T'; data[3] = 'M';

        // Version
        data[4] = bytecode::CURRENT_VERSION;

        // Arch (Arch64)
        data[5] = static_cast<std::uint8_t>(Architecture::Arch64);

        // Flags (none)
        data[6] = 0; data[7] = 0;

        // Entry point = 0
        // const_pool_offset = 48, const_pool_size = 0
        endian::write_u64_le(data.data() + 16, bytecode::HEADER_SIZE);

        // code_offset = 48, code_size = 0
        endian::write_u64_le(data.data() + 32, bytecode::HEADER_SIZE);

        return data;
    }
};

TEST_F(BytecodeFuzzTest, CorruptMagicRejected) {
    auto data = make_valid_header();

    // Corrupt each magic byte
    for (std::size_t i = 0; i < 4; ++i) {
        auto corrupted = data;
        corrupted[i] ^= 0xFF;

        auto result = read_header(std::span{corrupted});
        ASSERT_TRUE(result.has_value()) << "Header parsing failed";

        auto err = validate_header(*result, corrupted.size());
        EXPECT_EQ(err, BytecodeError::InvalidMagic)
            << "Magic corruption at byte " << i << " should be rejected";
    }
}

TEST_F(BytecodeFuzzTest, InvalidVersionRejected) {
    auto data = make_valid_header();

    // Test versions outside supported range
    std::array<std::uint8_t, 4> bad_versions = {0, 1, 25, 27};

    for (auto ver : bad_versions) {
        auto corrupted = data;
        corrupted[4] = ver;

        auto result = read_header(std::span{corrupted});
        ASSERT_TRUE(result.has_value());

        auto err = validate_header(*result, corrupted.size());
        EXPECT_EQ(err, BytecodeError::UnsupportedVersion)
            << "Version " << static_cast<int>(ver) << " should be rejected";
    }
}

TEST_F(BytecodeFuzzTest, InvalidArchitectureRejected) {
    auto data = make_valid_header();

    // Test invalid architecture values
    // Note: Arch128=2, Arch256=3, Arch512=4 are now valid, so only 5+ is invalid
    std::array<std::uint8_t, 4> bad_archs = {5, 6, 128, 255};

    for (auto arch : bad_archs) {
        auto corrupted = data;
        corrupted[5] = arch;

        auto result = read_header(std::span{corrupted});
        ASSERT_TRUE(result.has_value());

        auto err = validate_header(*result, corrupted.size());
        EXPECT_EQ(err, BytecodeError::InvalidArchitecture)
            << "Architecture " << static_cast<int>(arch) << " should be rejected";
    }
}

TEST_F(BytecodeFuzzTest, InvalidFlagsRejected) {
    auto data = make_valid_header();

    // Test reserved flag bits
    std::array<std::uint16_t, 4> bad_flags = {0x0004, 0x0008, 0x8000, 0xFFFC};

    for (auto flags : bad_flags) {
        auto corrupted = data;
        endian::write_u16_le(corrupted.data() + 6, flags);

        auto result = read_header(std::span{corrupted});
        ASSERT_TRUE(result.has_value());

        auto err = validate_header(*result, corrupted.size());
        EXPECT_EQ(err, BytecodeError::InvalidFlags)
            << "Flags 0x" << std::hex << flags << " should be rejected";
    }
}

TEST_F(BytecodeFuzzTest, TruncatedHeaderRejected) {
    // Try reading headers of various truncated sizes
    for (std::size_t len = 0; len < bytecode::HEADER_SIZE; ++len) {
        std::vector<std::uint8_t> truncated(len, 0);
        if (len > 0) truncated[0] = 'D';
        if (len > 1) truncated[1] = 'O';
        if (len > 2) truncated[2] = 'T';
        if (len > 3) truncated[3] = 'M';

        auto result = read_header(std::span{truncated});
        EXPECT_FALSE(result.has_value())
            << "Header of size " << len << " should be rejected";
        EXPECT_EQ(result.error(), BytecodeError::FileTooSmall);
    }
}

TEST_F(BytecodeFuzzTest, SectionOverlapRejected) {
    auto data = make_valid_header();
    data.resize(256);  // Extend to have room for sections

    // Set up overlapping sections
    // const_pool at offset 48, size 32
    endian::write_u64_le(data.data() + 16, 48);
    endian::write_u64_le(data.data() + 24, 32);
    // code at offset 64, size 32 (overlaps with const_pool ending at 80)
    endian::write_u64_le(data.data() + 32, 64);
    endian::write_u64_le(data.data() + 40, 32);

    auto result = read_header(std::span{data});
    ASSERT_TRUE(result.has_value());

    auto err = validate_header(*result, data.size());
    EXPECT_EQ(err, BytecodeError::SectionsOverlap);
}

TEST_F(BytecodeFuzzTest, SectionOutOfBoundsRejected) {
    auto data = make_valid_header();

    // const_pool extends beyond file
    endian::write_u64_le(data.data() + 16, 48);
    endian::write_u64_le(data.data() + 24, 100);  // File is only 48 bytes

    auto result = read_header(std::span{data});
    ASSERT_TRUE(result.has_value());

    auto err = validate_header(*result, data.size());
    EXPECT_EQ(err, BytecodeError::ConstPoolOutOfBounds);
}

TEST_F(BytecodeFuzzTest, EntryPointMisalignedRejected) {
    auto data = make_valid_header();
    data.resize(256);

    // Set up valid code section
    endian::write_u64_le(data.data() + 32, 64);   // code_offset
    endian::write_u64_le(data.data() + 40, 128);  // code_size

    // Set misaligned entry point
    endian::write_u64_le(data.data() + 8, 1);  // entry_point = 1 (not 4-byte aligned)

    auto result = read_header(std::span{data});
    ASSERT_TRUE(result.has_value());

    auto err = validate_header(*result, data.size());
    EXPECT_EQ(err, BytecodeError::EntryPointNotAligned);
}

// ============================================================================
// Constant Pool Edge Cases
// ============================================================================

class ConstantPoolFuzzTest : public ::testing::Test {
protected:
    // Create a minimal constant pool header
    std::vector<std::uint8_t> make_pool_header(std::uint32_t count) {
        std::vector<std::uint8_t> data(4);
        endian::write_u32_le(data.data(), count);
        return data;
    }
};

TEST_F(ConstantPoolFuzzTest, EmptyPoolAccepted) {
    std::vector<std::uint8_t> empty_pool;
    auto result = load_constant_pool(std::span{empty_pool});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(ConstantPoolFuzzTest, ZeroEntryPoolAccepted) {
    auto data = make_pool_header(0);
    auto result = load_constant_pool(std::span{data});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(ConstantPoolFuzzTest, TruncatedPoolHeaderRejected) {
    std::vector<std::uint8_t> data = {1, 2};  // Only 2 bytes, need 4
    auto result = load_constant_pool(std::span{data});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::ConstPoolTruncated);
}

TEST_F(ConstantPoolFuzzTest, EntryCountExceedsDataRejected) {
    auto data = make_pool_header(1000);  // Claims 1000 entries
    // But only has header, no actual entries
    auto result = load_constant_pool(std::span{data});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::ConstPoolCorrupted);
}

TEST_F(ConstantPoolFuzzTest, InvalidConstantTypeRejected) {
    auto data = make_pool_header(1);
    // Add entry with invalid type tag
    data.push_back(0xFF);  // Invalid type
    data.resize(data.size() + 8, 0);  // Pad with zeros

    auto result = load_constant_pool(std::span{data});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::InvalidConstantType);
}

TEST_F(ConstantPoolFuzzTest, IntegerOutOfRangeRejected) {
    auto data = make_pool_header(1);
    data.push_back(bytecode::CONST_TYPE_I64);

    // Add integer that exceeds 48-bit range
    std::int64_t too_large = MAX_VALUE_INT + 1;
    std::array<std::uint8_t, 8> int_bytes{};
    endian::write_i64_le(int_bytes.data(), too_large);
    data.insert(data.end(), int_bytes.begin(), int_bytes.end());

    auto result = load_constant_pool(std::span{data});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::IntegerOutOfRange);
}

TEST_F(ConstantPoolFuzzTest, StringConstantRejected) {
    auto data = make_pool_header(1);
    data.push_back(bytecode::CONST_TYPE_STRING);
    // String constants are not supported
    data.resize(data.size() + 12, 0);

    auto result = load_constant_pool(std::span{data});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::StringNotSupported);
}

TEST_F(ConstantPoolFuzzTest, ValidI64Accepted) {
    auto data = make_pool_header(1);
    data.push_back(bytecode::CONST_TYPE_I64);

    std::array<std::uint8_t, 8> int_bytes{};
    endian::write_i64_le(int_bytes.data(), 12345);
    data.insert(data.end(), int_bytes.begin(), int_bytes.end());

    auto result = load_constant_pool(std::span{data});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_TRUE(result->at(0).is_integer());
    EXPECT_EQ(result->at(0).as_integer(), 12345);
}

TEST_F(ConstantPoolFuzzTest, ValidF64Accepted) {
    auto data = make_pool_header(1);
    data.push_back(bytecode::CONST_TYPE_F64);

    std::array<std::uint8_t, 8> float_bytes{};
    endian::write_f64_le(float_bytes.data(), 3.14159);
    data.insert(data.end(), float_bytes.begin(), float_bytes.end());

    auto result = load_constant_pool(std::span{data});
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_TRUE(result->at(0).is_float());
    EXPECT_DOUBLE_EQ(result->at(0).as_float(), 3.14159);
}

// ============================================================================
// Memory Operation Edge Cases
// ============================================================================

class MemoryFuzzTest : public ::testing::Test {
protected:
    MemoryManager mem;
};

TEST_F(MemoryFuzzTest, AllocationAtExactLimit) {
    auto [handle, err] = mem.allocate(mem_config::MAX_ALLOCATION_SIZE);
    EXPECT_EQ(err, MemoryError::Success);
    if (err == MemoryError::Success) {
        [[maybe_unused]] auto dealloc_err = mem.deallocate(handle);
    }
}

TEST_F(MemoryFuzzTest, AllocationOneByteOverLimit) {
    // Note: PAGE_SIZE rounding might affect this
    auto [handle, err] = mem.allocate(mem_config::MAX_ALLOCATION_SIZE + 1);
    EXPECT_EQ(err, MemoryError::InvalidSize);
}

TEST_F(MemoryFuzzTest, ZeroSizeAllocationRejected) {
    auto [handle, err] = mem.allocate(0);
    EXPECT_EQ(err, MemoryError::InvalidSize);
}

TEST_F(MemoryFuzzTest, RapidAllocDeallocCycles) {
    // Stress test: rapid allocation/deallocation
    for (int cycle = 0; cycle < 100; ++cycle) {
        auto [handle, err] = mem.allocate(mem_config::PAGE_SIZE);
        ASSERT_EQ(err, MemoryError::Success) << "Failed at cycle " << cycle;

        // Write some data
        ASSERT_EQ(mem.write<std::uint64_t>(handle, 0, static_cast<std::uint64_t>(cycle)),
                  MemoryError::Success);

        // Read it back
        auto [val, read_err] = mem.read<std::uint64_t>(handle, 0);
        ASSERT_EQ(read_err, MemoryError::Success);
        EXPECT_EQ(val, static_cast<std::uint64_t>(cycle));

        // Deallocate
        ASSERT_EQ(mem.deallocate(handle), MemoryError::Success);
    }
}

TEST_F(MemoryFuzzTest, AlternatingHandleReuse) {
    Handle handles[10];
    std::uint32_t generations[10];

    // Allocate all
    for (int i = 0; i < 10; ++i) {
        auto [h, err] = mem.allocate(mem_config::PAGE_SIZE);
        ASSERT_EQ(err, MemoryError::Success);
        handles[i] = h;
        generations[i] = h.generation;
    }

    // Deallocate and reallocate alternating
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 10; i += 2) {
            [[maybe_unused]] auto err = mem.deallocate(handles[i]);
        }

        for (int i = 0; i < 10; i += 2) {
            auto [h, err] = mem.allocate(mem_config::PAGE_SIZE);
            ASSERT_EQ(err, MemoryError::Success);
            // Generation should have increased
            EXPECT_GT(h.generation, generations[i]) << "Generation should increase after realloc";
            handles[i] = h;
            generations[i] = h.generation;
        }
    }

    // Clean up
    for (int i = 0; i < 10; ++i) {
        [[maybe_unused]] auto err = mem.deallocate(handles[i]);
    }
}

// ============================================================================
// Value System Edge Cases
// ============================================================================

class ValueFuzzTest : public ::testing::Test {
protected:
};

TEST_F(ValueFuzzTest, NaNBitPatterns) {
    // Test various NaN bit patterns don't cause issues
    std::array<std::uint64_t, 8> nan_patterns = {
        0x7FF0000000000001ULL,  // Signaling NaN
        0x7FF8000000000000ULL,  // Quiet NaN
        0xFFF0000000000001ULL,  // Negative signaling NaN
        0xFFF8000000000000ULL,  // Negative quiet NaN
        0x7FFFFFFFFFFFFFFFULL,  // All bits except sign
        0xFFFFFFFFFFFFFFFFULL,  // All bits set
        0x7FF0000000000000ULL,  // Positive infinity
        0xFFF0000000000000ULL,  // Negative infinity
    };

    for (auto pattern : nan_patterns) {
        double d = std::bit_cast<double>(pattern);
        Value v = Value::from_float(d);

        // Should be a valid float value
        EXPECT_TRUE(v.is_float()) << "Pattern 0x" << std::hex << pattern;

        // Round-trip should preserve the value (or canonicalize NaN)
        double recovered = v.as_float();
        if (std::isnan(d)) {
            EXPECT_TRUE(std::isnan(recovered));
        } else if (std::isinf(d)) {
            EXPECT_TRUE(std::isinf(recovered));
            EXPECT_EQ(std::signbit(d), std::signbit(recovered));
        }
    }
}

TEST_F(ValueFuzzTest, SignedIntegerBoundaries) {
    // Test boundary values
    std::array<std::int64_t, 8> boundary_values = {
        0,
        1,
        -1,
        MAX_VALUE_INT,
        MIN_VALUE_INT,
        MAX_VALUE_INT - 1,
        MIN_VALUE_INT + 1,
        static_cast<std::int64_t>(1) << 31,  // 32-bit boundary
    };

    for (auto val : boundary_values) {
        Value v = Value::from_int(val);
        EXPECT_TRUE(v.is_integer()) << "Value " << val << " should be integer";
        EXPECT_EQ(v.as_integer(), val) << "Round-trip failed for " << val;
    }
}

TEST_F(ValueFuzzTest, HandleGenerationBoundaries) {
    Handle h1{.index = 0, .generation = 1};
    Handle h2{.index = 0, .generation = mem_config::MAX_GENERATION};
    Handle h3{.index = mem_config::INVALID_INDEX - 1, .generation = 1};

    // Convert to Values and back
    Value v1 = Value::from_handle(h1);
    Value v2 = Value::from_handle(h2);
    Value v3 = Value::from_handle(h3);

    EXPECT_TRUE(v1.is_handle());
    EXPECT_TRUE(v2.is_handle());
    EXPECT_TRUE(v3.is_handle());

    Handle r1 = v1.as_handle();
    Handle r2 = v2.as_handle();
    Handle r3 = v3.as_handle();

    EXPECT_EQ(r1.index, h1.index);
    EXPECT_EQ(r1.generation, h1.generation);
    EXPECT_EQ(r2.index, h2.index);
    EXPECT_EQ(r2.generation, h2.generation);
    EXPECT_EQ(r3.index, h3.index);
    EXPECT_EQ(r3.generation, h3.generation);
}

// ============================================================================
// Execution Constraint Edge Cases
// ============================================================================

class ExecutionConstraintFuzzTest : public ::testing::Test {
protected:
};

TEST_F(ExecutionConstraintFuzzTest, PCAtCodeSizeBoundary) {
    constexpr std::uint64_t CODE_SIZE = 1024;

    // PC at last valid position
    EXPECT_EQ(validate_pc(CODE_SIZE - 4, CODE_SIZE), BytecodeError::Success);

    // PC exactly at code_size (invalid)
    EXPECT_EQ(validate_pc(CODE_SIZE, CODE_SIZE), BytecodeError::EntryPointOutOfBounds);

    // PC one past code_size
    EXPECT_EQ(validate_pc(CODE_SIZE + 1, CODE_SIZE), BytecodeError::EntryPointOutOfBounds);
}

TEST_F(ExecutionConstraintFuzzTest, JumpTargetAtBoundaries) {
    constexpr std::uint64_t CODE_SIZE = 1024;

    // Jump to first instruction
    EXPECT_EQ(validate_jump_target(512, -512, CODE_SIZE), BytecodeError::Success);

    // Jump to last instruction
    EXPECT_EQ(validate_jump_target(0, static_cast<std::int32_t>(CODE_SIZE - 4), CODE_SIZE),
              BytecodeError::Success);

    // Jump would go negative
    EXPECT_EQ(validate_jump_target(4, -8, CODE_SIZE), BytecodeError::EntryPointOutOfBounds);

    // Jump would exceed code size
    EXPECT_EQ(validate_jump_target(CODE_SIZE - 4, 4, CODE_SIZE),
              BytecodeError::EntryPointOutOfBounds);
}

TEST_F(ExecutionConstraintFuzzTest, LargeOffsetJumps) {
    constexpr std::uint64_t LARGE_CODE_SIZE = 1ULL << 30;  // 1GB code section

    // Large forward jump
    EXPECT_EQ(validate_jump_target(0, static_cast<std::int32_t>(1 << 20), LARGE_CODE_SIZE),
              BytecodeError::Success);

    // Large backward jump
    EXPECT_EQ(validate_jump_target(1 << 24, -(1 << 20), LARGE_CODE_SIZE),
              BytecodeError::Success);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
