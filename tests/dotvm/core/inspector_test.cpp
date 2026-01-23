/// @file inspector_test.cpp
/// @brief TOOL-009 BytecodeInspector unit tests

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/inspector.hpp"
#include "dotvm/core/instruction.hpp"
#include "dotvm/core/opcode.hpp"

using namespace dotvm::core;

// ============================================================================
// Test Helpers - Create valid bytecode for testing
// ============================================================================

namespace {

/// Create minimal valid bytecode with empty code section
std::vector<std::uint8_t> make_minimal_bytecode() {
    BytecodeHeader header = make_header(
        Architecture::Arch64,
        bytecode::FLAG_NONE,
        0,   // entry_point
        48,  // const_pool_offset (right after header)
        0,   // const_pool_size (empty)
        48,  // code_offset
        0    // code_size (empty)
    );
    auto header_bytes = write_header(header);
    return std::vector<std::uint8_t>(header_bytes.begin(), header_bytes.end());
}

/// Create bytecode with specified flags
std::vector<std::uint8_t> make_bytecode_with_flags(std::uint16_t flags) {
    BytecodeHeader header = make_header(
        Architecture::Arch64,
        flags,
        0,   // entry_point
        48,  // const_pool_offset
        0,   // const_pool_size
        48,  // code_offset
        0    // code_size
    );
    auto header_bytes = write_header(header);
    return std::vector<std::uint8_t>(header_bytes.begin(), header_bytes.end());
}

/// Create bytecode with code section
std::vector<std::uint8_t> make_bytecode_with_code(std::span<const std::uint8_t> code) {
    const std::uint64_t const_pool_offset = bytecode::HEADER_SIZE;
    const std::uint64_t const_pool_size = 0;
    const std::uint64_t code_offset = bytecode::HEADER_SIZE;
    const std::uint64_t code_size = code.size();

    BytecodeHeader header = make_header(
        Architecture::Arch64,
        bytecode::FLAG_NONE,
        0,  // entry_point
        const_pool_offset,
        const_pool_size,
        code_offset,
        code_size
    );

    auto header_bytes = write_header(header);
    std::vector<std::uint8_t> result(header_bytes.begin(), header_bytes.end());
    result.insert(result.end(), code.begin(), code.end());
    return result;
}

/// Create bytecode with constant pool
std::vector<std::uint8_t> make_bytecode_with_const_pool(
    std::uint32_t int_count, std::uint32_t float_count) {
    // Build constant pool data
    std::vector<std::uint8_t> pool_data;

    // Entry count (4 bytes LE)
    std::uint32_t total_count = int_count + float_count;
    pool_data.push_back(static_cast<std::uint8_t>(total_count));
    pool_data.push_back(static_cast<std::uint8_t>(total_count >> 8));
    pool_data.push_back(static_cast<std::uint8_t>(total_count >> 16));
    pool_data.push_back(static_cast<std::uint8_t>(total_count >> 24));

    // Add int64 constants
    for (std::uint32_t i = 0; i < int_count; ++i) {
        pool_data.push_back(bytecode::CONST_TYPE_I64);
        // Value: i as int64 (LE)
        std::int64_t val = static_cast<std::int64_t>(i);
        for (int j = 0; j < 8; ++j) {
            pool_data.push_back(static_cast<std::uint8_t>(val >> (j * 8)));
        }
    }

    // Add float64 constants
    for (std::uint32_t i = 0; i < float_count; ++i) {
        pool_data.push_back(bytecode::CONST_TYPE_F64);
        // Value: i as double (LE)
        double val = static_cast<double>(i);
        auto bits = std::bit_cast<std::uint64_t>(val);
        for (int j = 0; j < 8; ++j) {
            pool_data.push_back(static_cast<std::uint8_t>(bits >> (j * 8)));
        }
    }

    const std::uint64_t const_pool_offset = bytecode::HEADER_SIZE;
    const std::uint64_t const_pool_size = pool_data.size();
    const std::uint64_t code_offset = const_pool_offset + const_pool_size;
    const std::uint64_t code_size = 0;

    BytecodeHeader header = make_header(
        Architecture::Arch64,
        bytecode::FLAG_NONE,
        0,  // entry_point
        const_pool_offset,
        const_pool_size,
        code_offset,
        code_size
    );

    auto header_bytes = write_header(header);
    std::vector<std::uint8_t> result(header_bytes.begin(), header_bytes.end());
    result.insert(result.end(), pool_data.begin(), pool_data.end());
    return result;
}

/// Create bytecode with invalid magic
std::vector<std::uint8_t> make_bytecode_invalid_magic() {
    auto data = make_minimal_bytecode();
    // Corrupt magic bytes
    data[0] = 'X';
    data[1] = 'X';
    data[2] = 'X';
    data[3] = 'X';
    return data;
}

/// Create bytecode with unsupported version
std::vector<std::uint8_t> make_bytecode_bad_version() {
    auto data = make_minimal_bytecode();
    // Set version to 0xFF (unsupported)
    data[4] = 0xFF;
    return data;
}

}  // namespace

// ============================================================================
// HeaderInfo Tests
// ============================================================================

TEST(InspectorTest, InspectValidHeaderExtractsAllFields) {
    auto bytecode = make_minimal_bytecode();
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.header.magic[0], 'D');
    EXPECT_EQ(result.header.magic[1], 'O');
    EXPECT_EQ(result.header.magic[2], 'T');
    EXPECT_EQ(result.header.magic[3], 'M');
    EXPECT_EQ(result.header.version, bytecode::CURRENT_VERSION);
    EXPECT_EQ(result.header.arch, Architecture::Arch64);
    EXPECT_EQ(result.header.flags, bytecode::FLAG_NONE);
    EXPECT_EQ(result.header.entry_point, 0u);
}

TEST(InspectorTest, InspectDetectsDebugFlag) {
    auto bytecode = make_bytecode_with_flags(bytecode::FLAG_DEBUG);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_TRUE(result.header.is_debug());
    EXPECT_FALSE(result.header.is_optimized());
}

TEST(InspectorTest, InspectDetectsOptimizedFlag) {
    auto bytecode = make_bytecode_with_flags(bytecode::FLAG_OPTIMIZED);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_FALSE(result.header.is_debug());
    EXPECT_TRUE(result.header.is_optimized());
}

TEST(InspectorTest, InspectDetectsBothFlags) {
    auto bytecode = make_bytecode_with_flags(bytecode::FLAG_DEBUG | bytecode::FLAG_OPTIMIZED);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_TRUE(result.header.is_debug());
    EXPECT_TRUE(result.header.is_optimized());
}

TEST(InspectorTest, ArchNameReturns64Bit) {
    auto bytecode = make_minimal_bytecode();
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.header.arch_name(), "64-bit");
}

TEST(InspectorTest, ArchNameReturns32Bit) {
    BytecodeHeader header = make_header(
        Architecture::Arch32,
        bytecode::FLAG_NONE,
        0, 48, 0, 48, 0
    );
    auto header_bytes = write_header(header);
    std::vector<std::uint8_t> bytecode(header_bytes.begin(), header_bytes.end());

    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.header.arch_name(), "32-bit");
}

TEST(InspectorTest, MagicStringReturnsCorrectValue) {
    auto bytecode = make_minimal_bytecode();
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.header.magic_string(), "DOTM");
}

// ============================================================================
// ConstPoolInfo Tests
// ============================================================================

TEST(InspectorTest, InspectEmptyConstPool) {
    auto bytecode = make_minimal_bytecode();
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.const_pool.entry_count, 0u);
    EXPECT_EQ(result.const_pool.int64_count, 0u);
    EXPECT_EQ(result.const_pool.float64_count, 0u);
    EXPECT_EQ(result.const_pool.string_count, 0u);
    EXPECT_TRUE(result.const_pool.loaded_successfully);
}

TEST(InspectorTest, InspectInt64Constants) {
    auto bytecode = make_bytecode_with_const_pool(3, 0);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.const_pool.entry_count, 3u);
    EXPECT_EQ(result.const_pool.int64_count, 3u);
    EXPECT_EQ(result.const_pool.float64_count, 0u);
    EXPECT_TRUE(result.const_pool.loaded_successfully);
}

TEST(InspectorTest, InspectFloat64Constants) {
    auto bytecode = make_bytecode_with_const_pool(0, 2);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.const_pool.entry_count, 2u);
    EXPECT_EQ(result.const_pool.int64_count, 0u);
    EXPECT_EQ(result.const_pool.float64_count, 2u);
    EXPECT_TRUE(result.const_pool.loaded_successfully);
}

TEST(InspectorTest, InspectMixedConstants) {
    auto bytecode = make_bytecode_with_const_pool(2, 3);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.const_pool.entry_count, 5u);
    EXPECT_EQ(result.const_pool.int64_count, 2u);
    EXPECT_EQ(result.const_pool.float64_count, 3u);
    EXPECT_TRUE(result.const_pool.loaded_successfully);
}

TEST(InspectorTest, InspectCorruptedPoolReportsError) {
    // Create bytecode with truncated constant pool
    BytecodeHeader header = make_header(
        Architecture::Arch64,
        bytecode::FLAG_NONE,
        0, 48, 10, 58, 0  // const_pool_size=10 but we won't provide enough data
    );
    auto header_bytes = write_header(header);
    std::vector<std::uint8_t> bytecode(header_bytes.begin(), header_bytes.end());
    // Add incomplete pool data (only entry count, no entries)
    bytecode.push_back(1);  // entry_count = 1
    bytecode.push_back(0);
    bytecode.push_back(0);
    bytecode.push_back(0);
    // Missing actual entry data

    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_FALSE(result.const_pool.loaded_successfully);
    EXPECT_FALSE(result.const_pool.load_error.empty());
}

// ============================================================================
// CodeInfo Tests
// ============================================================================

TEST(InspectorTest, InspectEmptyCodeSection) {
    auto bytecode = make_minimal_bytecode();
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.instruction_count, 0u);
    EXPECT_TRUE(result.code.opcode_histogram.empty());
    EXPECT_EQ(result.code.branch_count, 0u);
    EXPECT_EQ(result.code.jump_count, 0u);
    EXPECT_EQ(result.code.memory_op_count, 0u);
    EXPECT_EQ(result.code.arithmetic_count, 0u);
}

TEST(InspectorTest, InspectSingleInstruction) {
    // HALT instruction: 0x5F000000
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.instruction_count, 1u);
}

TEST(InspectorTest, OpcodeHistogramCountsCorrectly) {
    // Two ADD instructions and one HALT
    std::array<std::uint8_t, 12> code = {
        0x03, 0x02, 0x01, 0x00,  // ADD R1, R2, R3
        0x06, 0x05, 0x04, 0x00,  // ADD R4, R5, R6
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.instruction_count, 3u);
    EXPECT_EQ(result.code.opcode_histogram[opcode::ADD], 2u);
    EXPECT_EQ(result.code.opcode_histogram[opcode::HALT], 1u);
}

TEST(InspectorTest, CategoryHistogramGroupsCorrectly) {
    // ADD (arithmetic), AND (bitwise), JMP (control flow), HALT (control flow)
    std::array<std::uint8_t, 16> code = {
        0x03, 0x02, 0x01, 0x00,  // ADD R1, R2, R3 (arithmetic)
        0x00, 0x00, 0x00, 0x20,  // AND R0, R0, R0 (bitwise)
        0x04, 0x00, 0x00, 0x40,  // JMP +4 (control flow)
        0x00, 0x00, 0x00, 0x5F   // HALT (control flow)
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.category_histogram[OpcodeCategory::Arithmetic], 1u);
    EXPECT_EQ(result.code.category_histogram[OpcodeCategory::Bitwise], 1u);
    EXPECT_EQ(result.code.category_histogram[OpcodeCategory::ControlFlow], 2u);
}

TEST(InspectorTest, BranchCountIsAccurate) {
    // JZ and JNZ are branches
    std::array<std::uint8_t, 12> code = {
        0x04, 0x00, 0x01, 0x41,  // JZ R1, +4
        0x04, 0x00, 0x02, 0x42,  // JNZ R2, +4
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.branch_count, 2u);
}

TEST(InspectorTest, JumpCountIsAccurate) {
    // JMP and CALL are jumps
    std::array<std::uint8_t, 12> code = {
        0x08, 0x00, 0x00, 0x40,  // JMP +8
        0x04, 0x00, 0x00, 0x50,  // CALL +4
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.jump_count, 2u);
}

TEST(InspectorTest, MemoryOpCountIsAccurate) {
    // LOAD64 and STORE64 are memory ops
    std::array<std::uint8_t, 12> code = {
        0x00, 0x02, 0x01, 0x63,  // LOAD64 R1, [R2+0]
        0x00, 0x04, 0x03, 0x67,  // STORE64 R3, [R4+0]
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.memory_op_count, 2u);
}

TEST(InspectorTest, ArithmeticCountIsAccurate) {
    // ADD, SUB, MUL are arithmetic
    std::array<std::uint8_t, 16> code = {
        0x03, 0x02, 0x01, 0x00,  // ADD R1, R2, R3
        0x03, 0x02, 0x01, 0x01,  // SUB R1, R2, R3
        0x03, 0x02, 0x01, 0x02,  // MUL R1, R2, R3
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.arithmetic_count, 3u);
}

TEST(InspectorTest, CryptoCountIsAccurate) {
    // Crypto opcodes are in range 0xB0-0xBF
    std::array<std::uint8_t, 8> code = {
        0x00, 0x00, 0x00, 0xB0,  // First crypto opcode
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.crypto_count, 1u);
}

// ============================================================================
// ValidationInfo Tests
// ============================================================================

TEST(InspectorTest, ValidBytecodePassesAllChecks) {
    auto bytecode = make_minimal_bytecode();
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_TRUE(result.validation.all_passed);
    EXPECT_TRUE(result.is_valid());

    // All individual checks should pass
    for (const auto& check : result.validation.checks) {
        EXPECT_TRUE(check.passed) << "Check failed: " << check.name;
    }
}

TEST(InspectorTest, InvalidMagicDetected) {
    auto bytecode = make_bytecode_invalid_magic();
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_FALSE(result.validation.all_passed);
    EXPECT_FALSE(result.is_valid());

    // Find the magic check
    bool found_magic_failure = false;
    for (const auto& check : result.validation.checks) {
        if (check.name == "magic" && !check.passed) {
            found_magic_failure = true;
            EXPECT_EQ(check.error_code, BytecodeError::InvalidMagic);
        }
    }
    EXPECT_TRUE(found_magic_failure);
}

TEST(InspectorTest, UnsupportedVersionDetected) {
    auto bytecode = make_bytecode_bad_version();
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_FALSE(result.validation.all_passed);

    bool found_version_failure = false;
    for (const auto& check : result.validation.checks) {
        if (check.name == "version" && !check.passed) {
            found_version_failure = true;
            EXPECT_EQ(check.error_code, BytecodeError::UnsupportedVersion);
        }
    }
    EXPECT_TRUE(found_version_failure);
}

TEST(InspectorTest, SectionOverlapDetected) {
    // Create bytecode where const_pool and code overlap
    BytecodeHeader header = make_header(
        Architecture::Arch64,
        bytecode::FLAG_NONE,
        0,   // entry_point
        48,  // const_pool_offset
        20,  // const_pool_size
        50,  // code_offset (overlaps with const_pool!)
        10   // code_size
    );
    auto header_bytes = write_header(header);
    std::vector<std::uint8_t> bytecode(header_bytes.begin(), header_bytes.end());
    // Pad to make it valid size
    bytecode.resize(70, 0);

    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_FALSE(result.validation.all_passed);

    bool found_overlap_failure = false;
    for (const auto& check : result.validation.checks) {
        if (check.name == "sections" && !check.passed) {
            found_overlap_failure = true;
            EXPECT_EQ(check.error_code, BytecodeError::SectionsOverlap);
        }
    }
    EXPECT_TRUE(found_overlap_failure);
}

TEST(InspectorTest, EntryPointOutOfBoundsDetected) {
    // Create bytecode with entry point beyond code section
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};  // HALT
    const std::uint64_t code_offset = bytecode::HEADER_SIZE;
    const std::uint64_t code_size = code.size();

    BytecodeHeader header = make_header(
        Architecture::Arch64,
        bytecode::FLAG_NONE,
        100,  // entry_point (way beyond code_size of 4!)
        code_offset,
        0,
        code_offset,
        code_size
    );

    auto header_bytes = write_header(header);
    std::vector<std::uint8_t> bytecode(header_bytes.begin(), header_bytes.end());
    bytecode.insert(bytecode.end(), code.begin(), code.end());

    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_FALSE(result.validation.all_passed);

    bool found_entry_failure = false;
    for (const auto& check : result.validation.checks) {
        if (check.name == "entry_point" && !check.passed) {
            found_entry_failure = true;
            EXPECT_EQ(check.error_code, BytecodeError::EntryPointOutOfBounds);
        }
    }
    EXPECT_TRUE(found_entry_failure);
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST(InspectorTest, FileSizeCalculatedCorrectly) {
    auto bytecode = make_minimal_bytecode();
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.stats.file_size, bytecode.size());
}

TEST(InspectorTest, EstimatedCostBaseIsInstructionCount) {
    // Single HALT instruction should have base cost of 1
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    // Base cost = instruction_count (1)
    // HALT is control flow, not memory/crypto, so no penalty
    EXPECT_GE(result.stats.estimated_cost, 1u);
}

TEST(InspectorTest, EstimatedCostAddsMemoryPenalty) {
    // LOAD64 is a memory op, should add penalty
    std::array<std::uint8_t, 8> code = {
        0x00, 0x02, 0x01, 0x63,  // LOAD64 R1, [R2+0]
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    // Base cost = 2 (instructions)
    // Memory penalty = +2 per memory op (so LOAD64 costs 3 total)
    // Expected: 2 + 2 = 4 minimum
    EXPECT_GE(result.stats.estimated_cost, 4u);
}

TEST(InspectorTest, EstimatedCostAddsCryptoPenalty) {
    // Crypto opcode 0xB0 should add penalty
    std::array<std::uint8_t, 8> code = {
        0x00, 0x00, 0x00, 0xB0,  // Crypto opcode
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    // Base cost = 2 (instructions)
    // Crypto penalty = +10 per crypto op (so crypto costs 11 total)
    // Expected: 2 + 10 = 12 minimum
    EXPECT_GE(result.stats.estimated_cost, 12u);
}

TEST(InspectorTest, CodeDensityCalculatedCorrectly) {
    std::array<std::uint8_t, 8> code = {
        0x00, 0x00, 0x00, 0x00,  // ADD
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    // code_density = code_size / file_size
    double expected_density =
        static_cast<double>(code.size()) / static_cast<double>(bytecode.size());
    EXPECT_NEAR(result.stats.code_density, expected_density, 0.001);
}

// ============================================================================
// inspect_file Tests
// ============================================================================

TEST(InspectorTest, InspectFileNonexistent) {
    auto result = BytecodeInspector::inspect_file("/nonexistent/path/file.dot");

    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(InspectorTest, InspectTooSmallFile) {
    // File smaller than header
    std::vector<std::uint8_t> tiny = {'D', 'O', 'T', 'M'};
    auto result = BytecodeInspector::inspect(tiny);

    EXPECT_FALSE(result.validation.all_passed);
}

TEST(InspectorTest, InspectUnknownOpcodes) {
    // Reserved opcode 0x99
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x99};
    auto bytecode = make_bytecode_with_code(code);
    auto result = BytecodeInspector::inspect(bytecode);

    EXPECT_EQ(result.code.instruction_count, 1u);
    EXPECT_GE(result.code.unknown_count, 1u);
}
