#include <gtest/gtest.h>
#include <dotvm/core/bytecode.hpp>

#include <vector>
#include <cstring>

using namespace dotvm::core;

// ============================================================================
// Test Fixtures
// ============================================================================

class BytecodeHeaderTest : public ::testing::Test {
protected:
    // Helper to create valid header bytes
    static std::array<std::uint8_t, bytecode::HEADER_SIZE> make_valid_header_bytes() {
        BytecodeHeader header = make_header(
            Architecture::Arch64,
            bytecode::FLAG_NONE,
            0,      // entry_point
            48,     // const_pool_offset (right after header)
            100,    // const_pool_size
            148,    // code_offset (after const pool)
            200     // code_size
        );
        return write_header(header);
    }
};

class ConstantPoolTest : public ::testing::Test {
protected:
    // Helper to create a constant pool with an i64 entry
    static std::vector<std::uint8_t> make_pool_with_i64(std::int64_t value) {
        std::vector<std::uint8_t> pool;
        // Header: entry_count = 1
        pool.push_back(1);
        pool.push_back(0);
        pool.push_back(0);
        pool.push_back(0);
        // Entry: type tag + i64 value
        pool.push_back(bytecode::CONST_TYPE_I64);
        for (int i = 0; i < 8; ++i) {
            pool.push_back(static_cast<std::uint8_t>(
                (static_cast<std::uint64_t>(value) >> (i * 8)) & 0xFF));
        }
        return pool;
    }

    // Helper to create a constant pool with an f64 entry
    static std::vector<std::uint8_t> make_pool_with_f64(double value) {
        std::vector<std::uint8_t> pool;
        // Header: entry_count = 1
        pool.push_back(1);
        pool.push_back(0);
        pool.push_back(0);
        pool.push_back(0);
        // Entry: type tag + f64 value
        pool.push_back(bytecode::CONST_TYPE_F64);
        std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) {
            pool.push_back(static_cast<std::uint8_t>((bits >> (i * 8)) & 0xFF));
        }
        return pool;
    }

    // Helper to create a constant pool with a string entry
    static std::vector<std::uint8_t> make_pool_with_string(std::string_view str) {
        std::vector<std::uint8_t> pool;
        // Header: entry_count = 1
        pool.push_back(1);
        pool.push_back(0);
        pool.push_back(0);
        pool.push_back(0);
        // Entry: type tag + length + data
        pool.push_back(bytecode::CONST_TYPE_STRING);
        std::uint32_t len = static_cast<std::uint32_t>(str.size());
        pool.push_back(static_cast<std::uint8_t>(len & 0xFF));
        pool.push_back(static_cast<std::uint8_t>((len >> 8) & 0xFF));
        pool.push_back(static_cast<std::uint8_t>((len >> 16) & 0xFF));
        pool.push_back(static_cast<std::uint8_t>((len >> 24) & 0xFF));
        for (char c : str) {
            pool.push_back(static_cast<std::uint8_t>(c));
        }
        return pool;
    }
};

class EndianHelpersTest : public ::testing::Test {};

// ============================================================================
// Size and Layout Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, HeaderSizeIs48Bytes) {
    EXPECT_EQ(sizeof(BytecodeHeader), 48);
}

TEST_F(BytecodeHeaderTest, ConstantPoolHeaderSizeIs4Bytes) {
    EXPECT_EQ(sizeof(ConstantPoolHeader), 4);
}

TEST_F(BytecodeHeaderTest, HeaderConstantsAreCorrect) {
    EXPECT_EQ(bytecode::HEADER_SIZE, 48);
    EXPECT_EQ(bytecode::CURRENT_VERSION, 26);
    EXPECT_EQ(bytecode::MAGIC, 0x4D54'4F44U);
}

TEST_F(BytecodeHeaderTest, MagicBytesAreCorrect) {
    EXPECT_EQ(bytecode::MAGIC_BYTES[0], 'D');
    EXPECT_EQ(bytecode::MAGIC_BYTES[1], 'O');
    EXPECT_EQ(bytecode::MAGIC_BYTES[2], 'T');
    EXPECT_EQ(bytecode::MAGIC_BYTES[3], 'M');
}

TEST_F(BytecodeHeaderTest, ConstantTypesAreCorrect) {
    EXPECT_EQ(bytecode::CONST_TYPE_I64, 0x01);
    EXPECT_EQ(bytecode::CONST_TYPE_F64, 0x02);
    EXPECT_EQ(bytecode::CONST_TYPE_STRING, 0x03);
}

TEST_F(BytecodeHeaderTest, FlagsAreCorrect) {
    EXPECT_EQ(bytecode::FLAG_NONE, 0x0000);
    EXPECT_EQ(bytecode::FLAG_DEBUG, 0x0001);
    EXPECT_EQ(bytecode::FLAG_OPTIMIZED, 0x0002);
}

// ============================================================================
// Little-Endian Helper Tests
// ============================================================================

TEST_F(EndianHelpersTest, ReadU16LE) {
    std::uint8_t data[] = {0x34, 0x12};
    EXPECT_EQ(endian::read_u16_le(data), 0x1234);
}

TEST_F(EndianHelpersTest, ReadU32LE) {
    std::uint8_t data[] = {0x78, 0x56, 0x34, 0x12};
    EXPECT_EQ(endian::read_u32_le(data), 0x12345678U);
}

TEST_F(EndianHelpersTest, ReadU64LE) {
    std::uint8_t data[] = {0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12};
    EXPECT_EQ(endian::read_u64_le(data), 0x1234567890ABCDEFULL);
}

TEST_F(EndianHelpersTest, ReadI64LE) {
    // Test negative value: -1
    std::uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(endian::read_i64_le(data), -1);
}

TEST_F(EndianHelpersTest, ReadF64LE) {
    double expected = 3.14159265358979;
    std::uint64_t bits = std::bit_cast<std::uint64_t>(expected);
    std::uint8_t data[8];
    for (int i = 0; i < 8; ++i) {
        data[i] = static_cast<std::uint8_t>((bits >> (i * 8)) & 0xFF);
    }
    EXPECT_DOUBLE_EQ(endian::read_f64_le(data), expected);
}

TEST_F(EndianHelpersTest, WriteU16LE) {
    std::uint8_t data[2] = {0};
    endian::write_u16_le(data, 0x1234);
    EXPECT_EQ(data[0], 0x34);
    EXPECT_EQ(data[1], 0x12);
}

TEST_F(EndianHelpersTest, WriteU32LE) {
    std::uint8_t data[4] = {0};
    endian::write_u32_le(data, 0x12345678U);
    EXPECT_EQ(data[0], 0x78);
    EXPECT_EQ(data[1], 0x56);
    EXPECT_EQ(data[2], 0x34);
    EXPECT_EQ(data[3], 0x12);
}

TEST_F(EndianHelpersTest, WriteU64LE) {
    std::uint8_t data[8] = {0};
    endian::write_u64_le(data, 0x1234567890ABCDEFULL);
    EXPECT_EQ(data[0], 0xEF);
    EXPECT_EQ(data[1], 0xCD);
    EXPECT_EQ(data[2], 0xAB);
    EXPECT_EQ(data[3], 0x90);
    EXPECT_EQ(data[4], 0x78);
    EXPECT_EQ(data[5], 0x56);
    EXPECT_EQ(data[6], 0x34);
    EXPECT_EQ(data[7], 0x12);
}

TEST_F(EndianHelpersTest, ReadWriteRoundTrip) {
    std::uint8_t buffer[8] = {0};

    // u16 round-trip
    endian::write_u16_le(buffer, 0xABCD);
    EXPECT_EQ(endian::read_u16_le(buffer), 0xABCD);

    // u32 round-trip
    endian::write_u32_le(buffer, 0xDEADBEEF);
    EXPECT_EQ(endian::read_u32_le(buffer), 0xDEADBEEF);

    // u64 round-trip
    endian::write_u64_le(buffer, 0xCAFEBABE12345678ULL);
    EXPECT_EQ(endian::read_u64_le(buffer), 0xCAFEBABE12345678ULL);
}

// ============================================================================
// Header Read/Write Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, ReadWriteRoundTrip) {
    BytecodeHeader original = make_header(
        Architecture::Arch64,
        bytecode::FLAG_DEBUG | bytecode::FLAG_OPTIMIZED,
        0x1000,     // entry_point
        48,         // const_pool_offset
        0x500,      // const_pool_size
        0x548,      // code_offset
        0x2000      // code_size
    );

    auto bytes = write_header(original);
    auto result = read_header(bytes);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, original);
}

TEST_F(BytecodeHeaderTest, ReadFromMinimalValidData) {
    auto bytes = make_valid_header_bytes();
    auto result = read_header(bytes);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->magic, bytecode::MAGIC_BYTES);
    EXPECT_EQ(result->version, bytecode::CURRENT_VERSION);
}

TEST_F(BytecodeHeaderTest, ReadFromTooSmallDataFails) {
    std::array<std::uint8_t, 47> small_data{};
    auto result = read_header(small_data);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::FileTooSmall);
}

TEST_F(BytecodeHeaderTest, WriteProducesExpectedBytes) {
    BytecodeHeader header = make_header(
        Architecture::Arch32,
        bytecode::FLAG_DEBUG,
        0,
        48, 0,
        48, 4
    );

    auto bytes = write_header(header);

    // Check magic
    EXPECT_EQ(bytes[0], 'D');
    EXPECT_EQ(bytes[1], 'O');
    EXPECT_EQ(bytes[2], 'T');
    EXPECT_EQ(bytes[3], 'M');

    // Check version
    EXPECT_EQ(bytes[4], 26);

    // Check arch
    EXPECT_EQ(bytes[5], 0);  // Arch32

    // Check flags (LE)
    EXPECT_EQ(bytes[6], 0x01);  // FLAG_DEBUG
    EXPECT_EQ(bytes[7], 0x00);
}

TEST_F(BytecodeHeaderTest, MakeHeaderSetsDefaults) {
    auto header = make_header(
        Architecture::Arch64, 0, 0, 48, 0, 48, 0);

    EXPECT_EQ(header.magic, bytecode::MAGIC_BYTES);
    EXPECT_EQ(header.version, bytecode::CURRENT_VERSION);
    EXPECT_EQ(header.arch, Architecture::Arch64);
}

// ============================================================================
// Magic Validation Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, ValidMagicAccepted) {
    std::array<std::uint8_t, 4> magic = {'D', 'O', 'T', 'M'};
    EXPECT_TRUE(validate_magic(magic));
}

TEST_F(BytecodeHeaderTest, InvalidMagicRejected) {
    std::array<std::uint8_t, 4> wrong = {'D', 'O', 'T', 'X'};
    EXPECT_FALSE(validate_magic(wrong));
}

TEST_F(BytecodeHeaderTest, WrongMagicReturnsError) {
    auto bytes = make_valid_header_bytes();
    bytes[3] = 'X';  // Corrupt magic

    auto result = read_header(bytes);
    ASSERT_TRUE(result.has_value());  // read_header doesn't validate

    auto error = validate_header(*result, 1000);
    EXPECT_EQ(error, BytecodeError::InvalidMagic);
}

// ============================================================================
// Version Validation Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, Version26Accepted) {
    EXPECT_TRUE(validate_version(26));
}

TEST_F(BytecodeHeaderTest, Version0Rejected) {
    EXPECT_FALSE(validate_version(0));
}

TEST_F(BytecodeHeaderTest, Version25Rejected) {
    EXPECT_FALSE(validate_version(25));
}

TEST_F(BytecodeHeaderTest, Version27Rejected) {
    EXPECT_FALSE(validate_version(27));
}

TEST_F(BytecodeHeaderTest, Version255Rejected) {
    EXPECT_FALSE(validate_version(255));
}

TEST_F(BytecodeHeaderTest, WrongVersionReturnsError) {
    auto header = make_header(Architecture::Arch64, 0, 0, 48, 0, 48, 0);
    auto bytes = write_header(header);
    bytes[4] = 25;  // Wrong version

    auto result = read_header(bytes);
    ASSERT_TRUE(result.has_value());

    auto error = validate_header(*result, 1000);
    EXPECT_EQ(error, BytecodeError::UnsupportedVersion);
}

// ============================================================================
// Architecture Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, Arch32Accepted) {
    EXPECT_TRUE(validate_architecture(Architecture::Arch32));
}

TEST_F(BytecodeHeaderTest, Arch64Accepted) {
    EXPECT_TRUE(validate_architecture(Architecture::Arch64));
}

TEST_F(BytecodeHeaderTest, InvalidArchRejected) {
    // Now that Arch128=2, Arch256=3, Arch512=4 are valid, only 5+ should be invalid
    EXPECT_FALSE(validate_architecture(static_cast<Architecture>(5)));
    EXPECT_FALSE(validate_architecture(static_cast<Architecture>(255)));
}

TEST_F(BytecodeHeaderTest, InvalidArchReturnsError) {
    auto bytes = make_valid_header_bytes();
    bytes[5] = 99;  // Invalid arch

    auto result = read_header(bytes);
    ASSERT_TRUE(result.has_value());

    auto error = validate_header(*result, 1000);
    EXPECT_EQ(error, BytecodeError::InvalidArchitecture);
}

// ============================================================================
// Flags Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, DebugFlagDetected) {
    auto header = make_header(Architecture::Arch64, bytecode::FLAG_DEBUG,
                               0, 48, 0, 48, 0);
    EXPECT_TRUE(header.is_debug());
    EXPECT_FALSE(header.is_optimized());
}

TEST_F(BytecodeHeaderTest, OptimizedFlagDetected) {
    auto header = make_header(Architecture::Arch64, bytecode::FLAG_OPTIMIZED,
                               0, 48, 0, 48, 0);
    EXPECT_FALSE(header.is_debug());
    EXPECT_TRUE(header.is_optimized());
}

TEST_F(BytecodeHeaderTest, CombinedFlagsWork) {
    auto header = make_header(Architecture::Arch64,
                               bytecode::FLAG_DEBUG | bytecode::FLAG_OPTIMIZED,
                               0, 48, 0, 48, 0);
    EXPECT_TRUE(header.is_debug());
    EXPECT_TRUE(header.is_optimized());
}

TEST_F(BytecodeHeaderTest, NoFlagsIsValid) {
    auto header = make_header(Architecture::Arch64, bytecode::FLAG_NONE,
                               0, 48, 0, 48, 0);
    EXPECT_FALSE(header.is_debug());
    EXPECT_FALSE(header.is_optimized());
}

TEST_F(BytecodeHeaderTest, InvalidFlagsRejected) {
    auto header = make_header(Architecture::Arch64, 0x0004,  // Unknown flag
                               0, 48, 0, 48, 0);
    auto error = validate_header(header, 100);
    EXPECT_EQ(error, BytecodeError::InvalidFlags);
}

TEST_F(BytecodeHeaderTest, ReservedFlagBitsRejected) {
    auto header = make_header(Architecture::Arch64, 0xFF00,  // High bits set
                               0, 48, 0, 48, 0);
    auto error = validate_header(header, 100);
    EXPECT_EQ(error, BytecodeError::InvalidFlags);
}

// ============================================================================
// Section Bounds Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, ValidSectionBoundsPass) {
    EXPECT_TRUE(validate_section_bounds(0, 100, 100));
    EXPECT_TRUE(validate_section_bounds(50, 50, 100));
    EXPECT_TRUE(validate_section_bounds(0, 0, 100));
    EXPECT_TRUE(validate_section_bounds(100, 0, 100));
}

TEST_F(BytecodeHeaderTest, SectionOverflowDetected) {
    EXPECT_FALSE(validate_section_bounds(50, 100, 100));
    EXPECT_FALSE(validate_section_bounds(0, 101, 100));
    EXPECT_FALSE(validate_section_bounds(101, 0, 100));
}

TEST_F(BytecodeHeaderTest, U64OverflowDetected) {
    // Test overflow in offset + size
    EXPECT_FALSE(validate_section_bounds(0xFFFF'FFFF'FFFF'FFFFULL, 1, 100));
}

TEST_F(BytecodeHeaderTest, ConstPoolOverflowReturnsError) {
    auto header = make_header(Architecture::Arch64, 0,
                               0,    // entry_point
                               48,   // const_pool_offset
                               1000, // const_pool_size - too big
                               1048, // code_offset
                               0);   // code_size

    auto error = validate_header(header, 100);  // File is only 100 bytes
    EXPECT_EQ(error, BytecodeError::ConstPoolOutOfBounds);
}

TEST_F(BytecodeHeaderTest, CodeOverflowReturnsError) {
    auto header = make_header(Architecture::Arch64, 0,
                               0,    // entry_point
                               48,   // const_pool_offset
                               0,    // const_pool_size
                               48,   // code_offset
                               1000);// code_size - too big

    auto error = validate_header(header, 100);
    EXPECT_EQ(error, BytecodeError::CodeSectionOutOfBounds);
}

TEST_F(BytecodeHeaderTest, EntryPointBeyondCodeRejected) {
    auto header = make_header(Architecture::Arch64, 0,
                               100,  // entry_point - beyond code_size
                               48,   // const_pool_offset
                               0,    // const_pool_size
                               48,   // code_offset
                               50);  // code_size

    auto error = validate_header(header, 200);
    EXPECT_EQ(error, BytecodeError::EntryPointOutOfBounds);
}

TEST_F(BytecodeHeaderTest, ZeroSizeCodeWithZeroEntryPointValid) {
    auto header = make_header(Architecture::Arch64, 0,
                               0,   // entry_point
                               48,  // const_pool_offset
                               0,   // const_pool_size
                               48,  // code_offset
                               0);  // code_size - empty is valid

    auto error = validate_header(header, 48);
    EXPECT_EQ(error, BytecodeError::Success);
}

TEST_F(BytecodeHeaderTest, ZeroSizeCodeWithNonZeroEntryPointRejected) {
    auto header = make_header(Architecture::Arch64, 0,
                               100, // entry_point - non-zero with zero code
                               48,  // const_pool_offset
                               0,   // const_pool_size
                               48,  // code_offset
                               0);  // code_size

    auto error = validate_header(header, 48);
    EXPECT_EQ(error, BytecodeError::EntryPointOutOfBounds);
}

TEST_F(BytecodeHeaderTest, ConstPoolOverlapsHeaderRejected) {
    auto header = make_header(Architecture::Arch64, 0,
                               0,   // entry_point
                               0,   // const_pool_offset - overlaps header
                               100, // const_pool_size
                               100, // code_offset
                               0);  // code_size

    auto error = validate_header(header, 200);
    EXPECT_EQ(error, BytecodeError::ConstPoolOutOfBounds);
}

TEST_F(BytecodeHeaderTest, CodeSectionOverlapsHeaderRejected) {
    auto header = make_header(Architecture::Arch64, 0,
                               0,   // entry_point
                               48,  // const_pool_offset
                               0,   // const_pool_size
                               16,  // code_offset - overlaps header
                               100);// code_size

    auto error = validate_header(header, 200);
    EXPECT_EQ(error, BytecodeError::CodeSectionOutOfBounds);
}

// ============================================================================
// Section Overlap Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, SectionOverlapDetected) {
    // [0, 100) overlaps with [50, 150)
    EXPECT_TRUE(sections_overlap(0, 100, 50, 100));
    EXPECT_TRUE(sections_overlap(50, 100, 0, 100));

    // Same range overlaps
    EXPECT_TRUE(sections_overlap(0, 100, 0, 100));

    // Contained range overlaps
    EXPECT_TRUE(sections_overlap(0, 100, 25, 50));
}

TEST_F(BytecodeHeaderTest, AdjacentSectionsAllowed) {
    // [0, 50) and [50, 100) don't overlap
    EXPECT_FALSE(sections_overlap(0, 50, 50, 50));
    EXPECT_FALSE(sections_overlap(50, 50, 0, 50));
}

TEST_F(BytecodeHeaderTest, EmptySectionsNeverOverlap) {
    EXPECT_FALSE(sections_overlap(0, 0, 0, 100));
    EXPECT_FALSE(sections_overlap(0, 100, 0, 0));
    EXPECT_FALSE(sections_overlap(0, 0, 0, 0));
}

TEST_F(BytecodeHeaderTest, OverlappingSectionsReturnError) {
    auto header = make_header(Architecture::Arch64, 0,
                               0,    // entry_point
                               48,   // const_pool_offset
                               100,  // const_pool_size: [48, 148)
                               100,  // code_offset: overlaps at [100, 148)
                               100); // code_size

    auto error = validate_header(header, 300);
    EXPECT_EQ(error, BytecodeError::SectionsOverlap);
}

// ============================================================================
// Full Header Validation Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, FullValidHeaderPasses) {
    auto header = make_header(Architecture::Arch64,
                               bytecode::FLAG_DEBUG,
                               0,     // entry_point
                               48,    // const_pool_offset
                               100,   // const_pool_size
                               148,   // code_offset
                               200);  // code_size

    auto error = validate_header(header, 500);
    EXPECT_EQ(error, BytecodeError::Success);
}

// ============================================================================
// Constant Pool Tests
// ============================================================================

TEST_F(ConstantPoolTest, EmptyPoolValid) {
    std::vector<std::uint8_t> empty_pool;
    auto result = load_constant_pool(empty_pool);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(ConstantPoolTest, SingleInt64Entry) {
    auto pool = make_pool_with_i64(42);
    auto result = load_constant_pool(pool);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_TRUE((*result)[0].is_integer());
    EXPECT_EQ((*result)[0].as_integer(), 42);
}

TEST_F(ConstantPoolTest, NegativeInt64Entry) {
    auto pool = make_pool_with_i64(-12345678901234LL);
    auto result = load_constant_pool(pool);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_TRUE((*result)[0].is_integer());
    EXPECT_EQ((*result)[0].as_integer(), -12345678901234LL);
}

TEST_F(ConstantPoolTest, SingleFloat64Entry) {
    auto pool = make_pool_with_f64(3.14159265358979);
    auto result = load_constant_pool(pool);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1);
    EXPECT_TRUE((*result)[0].is_float());
    EXPECT_DOUBLE_EQ((*result)[0].as_float(), 3.14159265358979);
}

TEST_F(ConstantPoolTest, SingleStringEntry) {
    auto pool = make_pool_with_string("Hello, World!");
    auto result = load_constant_pool(pool);

    // Strings now return an explicit error instead of silent nil
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::StringNotSupported);
}

TEST_F(ConstantPoolTest, MixedEntries) {
    // Test mixed entries without strings (strings now return error)
    std::vector<std::uint8_t> pool;

    // Header: 2 entries (i64 and f64 only)
    pool.push_back(2); pool.push_back(0); pool.push_back(0); pool.push_back(0);

    // Entry 1: i64 = 100
    pool.push_back(bytecode::CONST_TYPE_I64);
    std::int64_t i64_val = 100;
    for (int i = 0; i < 8; ++i) {
        pool.push_back(static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(i64_val) >> (i * 8)) & 0xFF));
    }

    // Entry 2: f64 = 2.5
    pool.push_back(bytecode::CONST_TYPE_F64);
    double f64_val = 2.5;
    std::uint64_t f64_bits = std::bit_cast<std::uint64_t>(f64_val);
    for (int i = 0; i < 8; ++i) {
        pool.push_back(static_cast<std::uint8_t>((f64_bits >> (i * 8)) & 0xFF));
    }

    auto result = load_constant_pool(pool);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2);
    EXPECT_EQ((*result)[0].as_integer(), 100);
    EXPECT_DOUBLE_EQ((*result)[1].as_float(), 2.5);
}

TEST_F(ConstantPoolTest, InvalidTypeTagRejected) {
    std::vector<std::uint8_t> pool = {
        1, 0, 0, 0,     // entry_count = 1
        0xFF,           // Invalid type tag
        0, 0, 0, 0, 0, 0, 0, 0  // Padding
    };

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::InvalidConstantType);
}

TEST_F(ConstantPoolTest, TruncatedInt64Rejected) {
    std::vector<std::uint8_t> pool = {
        1, 0, 0, 0,     // entry_count = 1
        bytecode::CONST_TYPE_I64,
        0, 0, 0, 0      // Only 4 bytes instead of 8
    };

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::ConstPoolTruncated);
}

TEST_F(ConstantPoolTest, TruncatedFloat64Rejected) {
    std::vector<std::uint8_t> pool = {
        1, 0, 0, 0,     // entry_count = 1
        bytecode::CONST_TYPE_F64,
        0, 0            // Only 2 bytes instead of 8
    };

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::ConstPoolTruncated);
}

TEST_F(ConstantPoolTest, TruncatedStringRejected) {
    // String constants now return StringNotSupported error before checking truncation
    std::vector<std::uint8_t> pool = {
        1, 0, 0, 0,     // entry_count = 1
        bytecode::CONST_TYPE_STRING,
        10, 0, 0, 0,    // length = 10
        'A', 'B', 'C'   // Only 3 bytes instead of 10
    };

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    // Now returns StringNotSupported since strings aren't implemented
    EXPECT_EQ(result.error(), BytecodeError::StringNotSupported);
}

TEST_F(ConstantPoolTest, StringTooLongRejected) {
    // String constants now return StringNotSupported error before checking length
    std::vector<std::uint8_t> pool = {
        1, 0, 0, 0,     // entry_count = 1
        bytecode::CONST_TYPE_STRING,
        0xFF, 0xFF, 0xFF, 0x7F  // length > MAX_STRING_LENGTH
    };

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    // Now returns StringNotSupported since strings aren't implemented
    EXPECT_EQ(result.error(), BytecodeError::StringNotSupported);
}

TEST_F(ConstantPoolTest, TruncatedHeaderRejected) {
    std::vector<std::uint8_t> pool = {1, 0, 0};  // Only 3 bytes
    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::ConstPoolTruncated);
}

TEST_F(ConstantPoolTest, TooManyEntriesRejected) {
    std::vector<std::uint8_t> pool = {
        5, 0, 0, 0,     // entry_count = 5, but only 1 entry follows
        bytecode::CONST_TYPE_I64,
        0, 0, 0, 0, 0, 0, 0, 0
    };

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::ConstPoolTruncated);
}

// ============================================================================
// Error Message Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, ErrorToStringCompleteness) {
    // Ensure all error codes have non-empty messages (except Success)
    EXPECT_TRUE(to_string(BytecodeError::Success).empty());
    EXPECT_FALSE(to_string(BytecodeError::InvalidMagic).empty());
    EXPECT_FALSE(to_string(BytecodeError::UnsupportedVersion).empty());
    EXPECT_FALSE(to_string(BytecodeError::InvalidArchitecture).empty());
    EXPECT_FALSE(to_string(BytecodeError::InvalidFlags).empty());
    EXPECT_FALSE(to_string(BytecodeError::EntryPointOutOfBounds).empty());
    EXPECT_FALSE(to_string(BytecodeError::ConstPoolOutOfBounds).empty());
    EXPECT_FALSE(to_string(BytecodeError::CodeSectionOutOfBounds).empty());
    EXPECT_FALSE(to_string(BytecodeError::SectionsOverlap).empty());
    EXPECT_FALSE(to_string(BytecodeError::InvalidConstantType).empty());
    EXPECT_FALSE(to_string(BytecodeError::StringTooLong).empty());
    EXPECT_FALSE(to_string(BytecodeError::ConstPoolCorrupted).empty());
    EXPECT_FALSE(to_string(BytecodeError::ConstPoolTruncated).empty());
    EXPECT_FALSE(to_string(BytecodeError::FileTooSmall).empty());
    EXPECT_FALSE(to_string(BytecodeError::UnexpectedEof).empty());
}

// ============================================================================
// Constexpr Verification Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, ConstexprMagicValidation) {
    constexpr std::array<std::uint8_t, 4> valid_magic = {'D', 'O', 'T', 'M'};
    constexpr bool is_valid = validate_magic(valid_magic);
    static_assert(is_valid);

    constexpr std::array<std::uint8_t, 4> invalid_magic = {'X', 'Y', 'Z', 'W'};
    constexpr bool is_invalid = validate_magic(invalid_magic);
    static_assert(!is_invalid);

    EXPECT_TRUE(is_valid);
    EXPECT_FALSE(is_invalid);
}

TEST_F(BytecodeHeaderTest, ConstexprVersionValidation) {
    static_assert(validate_version(26));
    static_assert(!validate_version(0));
    static_assert(!validate_version(25));
    static_assert(!validate_version(27));

    EXPECT_TRUE(validate_version(26));
}

TEST_F(BytecodeHeaderTest, ConstexprSectionBoundsValidation) {
    static_assert(validate_section_bounds(0, 100, 100));
    static_assert(!validate_section_bounds(50, 100, 100));

    EXPECT_TRUE(validate_section_bounds(0, 100, 100));
}

TEST_F(BytecodeHeaderTest, ConstexprSectionsOverlap) {
    static_assert(sections_overlap(0, 100, 50, 100));
    static_assert(!sections_overlap(0, 50, 50, 50));
    static_assert(!sections_overlap(0, 0, 0, 100));

    EXPECT_TRUE(sections_overlap(0, 100, 50, 100));
}

TEST_F(BytecodeHeaderTest, ConstexprMakeHeader) {
    constexpr auto header = make_header(
        Architecture::Arch64, bytecode::FLAG_DEBUG,
        0, 48, 100, 148, 200);

    static_assert(header.magic == bytecode::MAGIC_BYTES);
    static_assert(header.version == 26);
    static_assert(header.arch == Architecture::Arch64);
    static_assert(header.flags == bytecode::FLAG_DEBUG);

    EXPECT_EQ(header.version, 26);
}

TEST_F(BytecodeHeaderTest, ConstexprWriteHeader) {
    constexpr auto header = make_header(
        Architecture::Arch64, 0, 0, 48, 0, 48, 0);
    constexpr auto bytes = write_header(header);

    static_assert(bytes[0] == 'D');
    static_assert(bytes[1] == 'O');
    static_assert(bytes[2] == 'T');
    static_assert(bytes[3] == 'M');
    static_assert(bytes[4] == 26);

    EXPECT_EQ(bytes[0], 'D');
}

TEST_F(EndianHelpersTest, ConstexprLittleEndianHelpers) {
    constexpr std::uint8_t data16[] = {0x34, 0x12};
    static_assert(endian::read_u16_le(data16) == 0x1234);

    constexpr std::uint8_t data32[] = {0x78, 0x56, 0x34, 0x12};
    static_assert(endian::read_u32_le(data32) == 0x12345678U);

    constexpr std::uint8_t data64[] = {0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12};
    static_assert(endian::read_u64_le(data64) == 0x1234567890ABCDEFULL);

    EXPECT_EQ(endian::read_u16_le(data16), 0x1234);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(BytecodeHeaderTest, FullBytecodeFileSimulation) {
    // Simulate a complete bytecode file
    std::vector<std::uint8_t> bytecode_file;

    // Create header
    auto header = make_header(
        Architecture::Arch64,
        bytecode::FLAG_OPTIMIZED,
        0,      // entry_point
        48,     // const_pool_offset
        13,     // const_pool_size (4 header + 9 for one i64)
        64,     // code_offset (aligned to 16)
        8       // code_size (2 instructions)
    );
    auto header_bytes = write_header(header);
    bytecode_file.insert(bytecode_file.end(),
                         header_bytes.begin(), header_bytes.end());

    // Add constant pool at offset 48
    bytecode_file.resize(48);
    // Pool header: 1 entry
    bytecode_file.push_back(1);
    bytecode_file.push_back(0);
    bytecode_file.push_back(0);
    bytecode_file.push_back(0);
    // Entry: i64 = 42
    bytecode_file.push_back(bytecode::CONST_TYPE_I64);
    std::int64_t val = 42;
    for (int i = 0; i < 8; ++i) {
        bytecode_file.push_back(static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(val) >> (i * 8)) & 0xFF));
    }

    // Add code section at offset 64
    bytecode_file.resize(64);
    // Two NOP instructions (0x00000000)
    for (int i = 0; i < 8; ++i) {
        bytecode_file.push_back(0);
    }

    // Parse and validate
    auto parsed_header = read_header(bytecode_file);
    ASSERT_TRUE(parsed_header.has_value());

    auto validation = validate_header(*parsed_header, bytecode_file.size());
    EXPECT_EQ(validation, BytecodeError::Success);

    // Load constant pool
    auto pool_span = std::span<const std::uint8_t>(
        bytecode_file.data() + parsed_header->const_pool_offset,
        parsed_header->const_pool_size);
    auto constants = load_constant_pool(pool_span);

    ASSERT_TRUE(constants.has_value());
    ASSERT_EQ(constants->size(), 1);
    EXPECT_EQ((*constants)[0].as_integer(), 42);
}

TEST_F(ConstantPoolTest, LoadPoolIntoValueVector) {
    auto pool = make_pool_with_f64(2.71828);
    auto result = load_constant_pool(pool);

    ASSERT_TRUE(result.has_value());
    std::vector<Value>& constants = *result;

    EXPECT_EQ(constants.size(), 1);
    EXPECT_TRUE(constants[0].is_float());
    EXPECT_DOUBLE_EQ(constants[0].as_float(), 2.71828);
}

TEST_F(ConstantPoolTest, IntegrationWithValueType) {
    // Test that loaded constants work correctly with Value type
    // Note: Value uses 48-bit NaN-boxed integers, range is [-2^47, 2^47-1]
    constexpr std::int64_t val = -(1LL << 47);  // -140737488355328 (min 48-bit)
    auto pool = make_pool_with_i64(val);
    auto result = load_constant_pool(pool);

    ASSERT_TRUE(result.has_value());
    Value v = (*result)[0];

    EXPECT_TRUE(v.is_integer());
    EXPECT_EQ(v.type(), ValueType::Integer);
    EXPECT_EQ(v.as_integer(), val);
    EXPECT_TRUE(v.is_truthy());  // Non-zero is truthy
}

// ============================================================================
// Security Tests: New Error Conditions
// ============================================================================

class BytecodeSecurityTest : public ConstantPoolTest {
    // Inherit from ConstantPoolTest to access helper methods
};

// Test: Integer out of 48-bit range is rejected
TEST_F(BytecodeSecurityTest, IntegerOutOfRangePositiveRejected) {
    // MAX_VALUE_INT = (1LL << 47) - 1 = 140737488355327
    // Values > MAX_VALUE_INT should be rejected
    std::int64_t too_large = (1LL << 47);  // One past max

    std::vector<std::uint8_t> pool;
    pool.push_back(1); pool.push_back(0); pool.push_back(0); pool.push_back(0);
    pool.push_back(bytecode::CONST_TYPE_I64);
    for (int i = 0; i < 8; ++i) {
        pool.push_back(static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(too_large) >> (i * 8)) & 0xFF));
    }

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::IntegerOutOfRange);
}

TEST_F(BytecodeSecurityTest, IntegerOutOfRangeNegativeRejected) {
    // MIN_VALUE_INT = -(1LL << 47) = -140737488355328
    // Values < MIN_VALUE_INT should be rejected
    std::int64_t too_small = -(1LL << 47) - 1;  // One past min

    std::vector<std::uint8_t> pool;
    pool.push_back(1); pool.push_back(0); pool.push_back(0); pool.push_back(0);
    pool.push_back(bytecode::CONST_TYPE_I64);
    for (int i = 0; i < 8; ++i) {
        pool.push_back(static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(too_small) >> (i * 8)) & 0xFF));
    }

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::IntegerOutOfRange);
}

TEST_F(BytecodeSecurityTest, IntegerAtBoundaryAccepted) {
    // Test min boundary: -(1LL << 47)
    std::int64_t min_val = -(1LL << 47);
    auto pool_min = ConstantPoolTest::make_pool_with_i64(min_val);
    auto result_min = load_constant_pool(pool_min);
    ASSERT_TRUE(result_min.has_value());
    EXPECT_EQ((*result_min)[0].as_integer(), min_val);

    // Test max boundary: (1LL << 47) - 1
    std::int64_t max_val = (1LL << 47) - 1;
    auto pool_max = ConstantPoolTest::make_pool_with_i64(max_val);
    auto result_max = load_constant_pool(pool_max);
    ASSERT_TRUE(result_max.has_value());
    EXPECT_EQ((*result_max)[0].as_integer(), max_val);
}

// Test: Entry point alignment validation
TEST_F(BytecodeSecurityTest, EntryPointAlignedAccepted) {
    auto header = make_header(Architecture::Arch64, 0,
                               0,    // entry_point - aligned (0 % 4 == 0)
                               48, 0, 48, 100);
    EXPECT_EQ(validate_header(header, 200), BytecodeError::Success);

    auto header2 = make_header(Architecture::Arch64, 0,
                                4,    // entry_point - aligned (4 % 4 == 0)
                                48, 0, 48, 100);
    EXPECT_EQ(validate_header(header2, 200), BytecodeError::Success);

    auto header3 = make_header(Architecture::Arch64, 0,
                                96,   // entry_point - aligned (96 % 4 == 0)
                                48, 0, 48, 100);
    EXPECT_EQ(validate_header(header3, 200), BytecodeError::Success);
}

TEST_F(BytecodeSecurityTest, EntryPointMisalignedRejected) {
    // Entry point 1 is not 4-byte aligned
    auto header1 = make_header(Architecture::Arch64, 0,
                                1,    // entry_point - NOT aligned
                                48, 0, 48, 100);
    EXPECT_EQ(validate_header(header1, 200), BytecodeError::EntryPointNotAligned);

    // Entry point 2 is not 4-byte aligned
    auto header2 = make_header(Architecture::Arch64, 0,
                                2,    // entry_point - NOT aligned
                                48, 0, 48, 100);
    EXPECT_EQ(validate_header(header2, 200), BytecodeError::EntryPointNotAligned);

    // Entry point 3 is not 4-byte aligned
    auto header3 = make_header(Architecture::Arch64, 0,
                                3,    // entry_point - NOT aligned
                                48, 0, 48, 100);
    EXPECT_EQ(validate_header(header3, 200), BytecodeError::EntryPointNotAligned);

    // Entry point 5 is not 4-byte aligned
    auto header5 = make_header(Architecture::Arch64, 0,
                                5,    // entry_point - NOT aligned
                                48, 0, 48, 100);
    EXPECT_EQ(validate_header(header5, 200), BytecodeError::EntryPointNotAligned);
}

// Test: File size limit
TEST_F(BytecodeSecurityTest, FileTooLargeRejected) {
    auto header = make_header(Architecture::Arch64, 0, 0, 48, 0, 48, 0);

    // bytecode::MAX_FILE_SIZE is 2GB
    std::size_t too_large = bytecode::MAX_FILE_SIZE + 1;
    EXPECT_EQ(validate_header(header, too_large), BytecodeError::FileTooLarge);
}

TEST_F(BytecodeSecurityTest, FileSizeAtLimitAccepted) {
    auto header = make_header(Architecture::Arch64, 0, 0, 48, 0, 48, 0);

    // Exactly at limit should be accepted
    EXPECT_EQ(validate_header(header, bytecode::MAX_FILE_SIZE), BytecodeError::Success);
}

// Test: Too many constant pool entries
TEST_F(BytecodeSecurityTest, TooManyConstantsRejected) {
    std::vector<std::uint8_t> pool;

    // entry_count > MAX_CONST_POOL_ENTRIES
    std::uint32_t too_many = bytecode::MAX_CONST_POOL_ENTRIES + 1;
    pool.push_back(static_cast<std::uint8_t>(too_many & 0xFF));
    pool.push_back(static_cast<std::uint8_t>((too_many >> 8) & 0xFF));
    pool.push_back(static_cast<std::uint8_t>((too_many >> 16) & 0xFF));
    pool.push_back(static_cast<std::uint8_t>((too_many >> 24) & 0xFF));

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::TooManyConstants);
}

TEST_F(BytecodeSecurityTest, EntryCountExceedsDataSizeRejected) {
    // Claim 1000 entries but only provide space for ~10
    std::vector<std::uint8_t> pool;
    pool.push_back(0xE8); pool.push_back(0x03); pool.push_back(0); pool.push_back(0); // 1000 entries

    // Only add data for one entry
    pool.push_back(bytecode::CONST_TYPE_I64);
    for (int i = 0; i < 8; ++i) pool.push_back(0);

    auto result = load_constant_pool(pool);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::ConstPoolCorrupted);
}

// Test: String constants return explicit error
TEST_F(BytecodeSecurityTest, StringConstantReturnsNotSupportedError) {
    auto pool = ConstantPoolTest::make_pool_with_string("test string");
    auto result = load_constant_pool(pool);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BytecodeError::StringNotSupported);
}

// Test: New error messages are present
TEST_F(BytecodeSecurityTest, NewErrorMessagesExist) {
    EXPECT_FALSE(to_string(BytecodeError::IntegerOutOfRange).empty());
    EXPECT_FALSE(to_string(BytecodeError::EntryPointNotAligned).empty());
    EXPECT_FALSE(to_string(BytecodeError::TooManyConstants).empty());
    EXPECT_FALSE(to_string(BytecodeError::FileTooLarge).empty());
    EXPECT_FALSE(to_string(BytecodeError::StringNotSupported).empty());
}

// Test: New security constants are defined
TEST_F(BytecodeSecurityTest, SecurityConstantsAreDefined) {
    EXPECT_GT(bytecode::MAX_CONST_POOL_ENTRIES, 0u);
    EXPECT_GT(bytecode::MAX_FILE_SIZE, 0u);
    EXPECT_EQ(bytecode::INSTRUCTION_ALIGNMENT, 4u);

    // MAX_CONST_POOL_ENTRIES should be 1M
    EXPECT_EQ(bytecode::MAX_CONST_POOL_ENTRIES, 0x00'10'00'00U);

    // MAX_FILE_SIZE should be 2GB
    EXPECT_EQ(bytecode::MAX_FILE_SIZE, 0x80'00'00'00ULL);
}

// Test: Integer validation helper function
TEST_F(BytecodeSecurityTest, IsValidValueIntFunction) {
    // Within range
    EXPECT_TRUE(is_valid_value_int(0));
    EXPECT_TRUE(is_valid_value_int(1));
    EXPECT_TRUE(is_valid_value_int(-1));
    EXPECT_TRUE(is_valid_value_int(MIN_VALUE_INT));
    EXPECT_TRUE(is_valid_value_int(MAX_VALUE_INT));

    // Out of range
    EXPECT_FALSE(is_valid_value_int(MIN_VALUE_INT - 1));
    EXPECT_FALSE(is_valid_value_int(MAX_VALUE_INT + 1));
    EXPECT_FALSE(is_valid_value_int(INT64_MAX));
    EXPECT_FALSE(is_valid_value_int(INT64_MIN));
}
