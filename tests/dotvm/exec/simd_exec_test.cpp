/// @file simd_exec_test.cpp
/// @brief Integration tests for SIMD opcode execution in the VM
///
/// Tests cover:
/// - SIMD opcodes return InvalidOpcode when SIMD is disabled
/// - VADD, VSUB, VMUL execute correctly with SIMD enabled
/// - Element size validation (only i32 supported)
/// - V0 hardwired zero behavior

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include <dotvm/core/instruction.hpp>
#include <dotvm/core/opcode.hpp>
#include <dotvm/core/simd/simd_opcodes.hpp>
#include <dotvm/core/simd/vector_types.hpp>
#include <dotvm/core/vm_context.hpp>
#include <dotvm/exec/execution_engine.hpp>

namespace dotvm::exec {
namespace {

using namespace dotvm::core;
using namespace dotvm::core::simd;

// ============================================================================
// Test Fixture
// ============================================================================

class SimdExecTest : public ::testing::Test {
protected:
    /// Create a SIMD instruction (32-bit encoded)
    ///
    /// @param op SIMD opcode (VADD, VSUB, VMUL)
    /// @param vd Destination vector register (0-31)
    /// @param vs1 Source vector register 1 (0-31)
    /// @param vs2 Source vector register 2 (0-31)
    /// @param elem_size Element size (default Int32)
    /// @param vec_width Vector width (default Width128)
    static std::uint32_t make_simd_instr(std::uint8_t op, std::uint8_t vd, std::uint8_t vs1,
                                          std::uint8_t vs2,
                                          ElementSize elem_size = ElementSize::Int32,
                                          VectorWidth vec_width = VectorWidth::Width128) {
        SimdInstruction inst{op, elem_size, vec_width, vd, vs1, vs2};
        return inst.encode();
    }

    /// Helper to run code with a given VM configuration
    ExecResult run(VmContext& ctx, const std::vector<std::uint32_t>& code) {
        ExecutionEngine engine{ctx};
        return engine.execute(code.data(), code.size(), 0, {});
    }
};

// ============================================================================
// SIMD Disabled Tests
// ============================================================================

TEST_F(SimdExecTest, VADD_ReturnsInvalidOpcode_WhenSimdDisabled) {
    // Default config has SIMD disabled
    VmContext ctx{VmConfig::arch64()};
    EXPECT_FALSE(ctx.simd_enabled());

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VADD, 1, 2, 3),  // V1 = V2 + V3
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::InvalidOpcode);
}

TEST_F(SimdExecTest, VSUB_ReturnsInvalidOpcode_WhenSimdDisabled) {
    VmContext ctx{VmConfig::arch64()};
    EXPECT_FALSE(ctx.simd_enabled());

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VSUB, 1, 2, 3),
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::InvalidOpcode);
}

TEST_F(SimdExecTest, VMUL_ReturnsInvalidOpcode_WhenSimdDisabled) {
    VmContext ctx{VmConfig::arch64()};
    EXPECT_FALSE(ctx.simd_enabled());

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VMUL, 1, 2, 3),
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::InvalidOpcode);
}

// ============================================================================
// SIMD Enabled Tests - VADD
// ============================================================================

TEST_F(SimdExecTest, VADD_ExecutesCorrectly_WithSimdEnabled) {
    VmContext ctx{VmConfig::simd128()};
    EXPECT_TRUE(ctx.simd_enabled());

    // Set up vector registers with test data
    // V1 = {1, 2, 3, 4}
    // V2 = {10, 20, 30, 40}
    Vector128i32 v1, v2;
    v1[0] = 1;
    v1[1] = 2;
    v1[2] = 3;
    v1[3] = 4;
    v2[0] = 10;
    v2[1] = 20;
    v2[2] = 30;
    v2[3] = 40;

    ctx.vec_registers().write_v128i32(1, v1);
    ctx.vec_registers().write_v128i32(2, v2);

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VADD, 3, 1, 2),  // V3 = V1 + V2
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::Success);

    // Verify result: V3 should be {11, 22, 33, 44}
    auto v3 = ctx.vec_registers().read_v128i32(3);
    EXPECT_EQ(v3[0], 11);
    EXPECT_EQ(v3[1], 22);
    EXPECT_EQ(v3[2], 33);
    EXPECT_EQ(v3[3], 44);
}

// ============================================================================
// SIMD Enabled Tests - VSUB
// ============================================================================

TEST_F(SimdExecTest, VSUB_ExecutesCorrectly_WithSimdEnabled) {
    VmContext ctx{VmConfig::simd128()};

    Vector128i32 v1, v2;
    v1[0] = 100;
    v1[1] = 200;
    v1[2] = 300;
    v1[3] = 400;
    v2[0] = 10;
    v2[1] = 20;
    v2[2] = 30;
    v2[3] = 40;

    ctx.vec_registers().write_v128i32(1, v1);
    ctx.vec_registers().write_v128i32(2, v2);

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VSUB, 3, 1, 2),  // V3 = V1 - V2
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::Success);

    auto v3 = ctx.vec_registers().read_v128i32(3);
    EXPECT_EQ(v3[0], 90);
    EXPECT_EQ(v3[1], 180);
    EXPECT_EQ(v3[2], 270);
    EXPECT_EQ(v3[3], 360);
}

// ============================================================================
// SIMD Enabled Tests - VMUL
// ============================================================================

TEST_F(SimdExecTest, VMUL_ExecutesCorrectly_WithSimdEnabled) {
    VmContext ctx{VmConfig::simd128()};

    Vector128i32 v1, v2;
    v1[0] = 2;
    v1[1] = 3;
    v1[2] = 4;
    v1[3] = 5;
    v2[0] = 10;
    v2[1] = 10;
    v2[2] = 10;
    v2[3] = 10;

    ctx.vec_registers().write_v128i32(1, v1);
    ctx.vec_registers().write_v128i32(2, v2);

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VMUL, 3, 1, 2),  // V3 = V1 * V2
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::Success);

    auto v3 = ctx.vec_registers().read_v128i32(3);
    EXPECT_EQ(v3[0], 20);
    EXPECT_EQ(v3[1], 30);
    EXPECT_EQ(v3[2], 40);
    EXPECT_EQ(v3[3], 50);
}

// ============================================================================
// Element Size Validation Tests
// ============================================================================

TEST_F(SimdExecTest, VADD_ReturnsInvalidOpcode_ForNonInt32ElementSize) {
    VmContext ctx{VmConfig::simd128()};
    EXPECT_TRUE(ctx.simd_enabled());

    // Try with Int16 element size (not supported)
    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VADD, 3, 1, 2, ElementSize::Int16),
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::InvalidOpcode);
}

TEST_F(SimdExecTest, VSUB_ReturnsInvalidOpcode_ForNonInt32ElementSize) {
    VmContext ctx{VmConfig::simd128()};

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VSUB, 3, 1, 2, ElementSize::Float32),
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::InvalidOpcode);
}

TEST_F(SimdExecTest, VMUL_ReturnsInvalidOpcode_ForNonInt32ElementSize) {
    VmContext ctx{VmConfig::simd128()};

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VMUL, 3, 1, 2, ElementSize::Int64),
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::InvalidOpcode);
}

// ============================================================================
// V0 Hardwired Zero Tests
// ============================================================================

TEST_F(SimdExecTest, V0_RemainsZero_AfterWriteAttempt) {
    VmContext ctx{VmConfig::simd128()};

    // Set up non-zero values
    Vector128i32 v1, v2;
    v1[0] = 100;
    v1[1] = 200;
    v1[2] = 300;
    v1[3] = 400;
    v2[0] = 1;
    v2[1] = 2;
    v2[2] = 3;
    v2[3] = 4;

    ctx.vec_registers().write_v128i32(1, v1);
    ctx.vec_registers().write_v128i32(2, v2);

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VADD, 0, 1, 2),  // Try to write to V0
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::Success);

    // V0 should remain zero (hardwired)
    auto v0 = ctx.vec_registers().read_v128i32(0);
    EXPECT_EQ(v0[0], 0);
    EXPECT_EQ(v0[1], 0);
    EXPECT_EQ(v0[2], 0);
    EXPECT_EQ(v0[3], 0);
}

// ============================================================================
// Multi-Operation Tests
// ============================================================================

TEST_F(SimdExecTest, MultipleSimdOps_ExecuteSequentially) {
    VmContext ctx{VmConfig::simd128()};

    // V1 = {1, 1, 1, 1}
    // V2 = {2, 2, 2, 2}
    Vector128i32 v1, v2;
    for (std::size_t i = 0; i < 4; ++i) {
        v1[i] = 1;
        v2[i] = 2;
    }

    ctx.vec_registers().write_v128i32(1, v1);
    ctx.vec_registers().write_v128i32(2, v2);

    std::vector<std::uint32_t> code = {
        make_simd_instr(opcode::VADD, 3, 1, 2),  // V3 = V1 + V2 = {3, 3, 3, 3}
        make_simd_instr(opcode::VMUL, 4, 3, 2),  // V4 = V3 * V2 = {6, 6, 6, 6}
        make_simd_instr(opcode::VSUB, 5, 4, 1),  // V5 = V4 - V1 = {5, 5, 5, 5}
        encode_type_c(opcode::HALT, 0)};

    auto result = run(ctx, code);
    EXPECT_EQ(result, ExecResult::Success);

    auto v3 = ctx.vec_registers().read_v128i32(3);
    auto v4 = ctx.vec_registers().read_v128i32(4);
    auto v5 = ctx.vec_registers().read_v128i32(5);

    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(v3[i], 3) << "V3[" << i << "] mismatch";
        EXPECT_EQ(v4[i], 6) << "V4[" << i << "] mismatch";
        EXPECT_EQ(v5[i], 5) << "V5[" << i << "] mismatch";
    }
}

// ============================================================================
// Opcode Range Tests
// ============================================================================

TEST_F(SimdExecTest, IsSimdOp_IdentifiesSimdOpcodes) {
    // SIMD opcodes (0xC0-0xCF)
    EXPECT_TRUE(is_simd_op(opcode::VADD));
    EXPECT_TRUE(is_simd_op(opcode::VSUB));
    EXPECT_TRUE(is_simd_op(opcode::VMUL));
    EXPECT_TRUE(is_simd_op(0xC3));  // Future VDIV
    EXPECT_TRUE(is_simd_op(0xCF));  // Last SIMD opcode

    // Non-SIMD opcodes
    EXPECT_FALSE(is_simd_op(opcode::ADD));
    EXPECT_FALSE(is_simd_op(opcode::SUB));
    EXPECT_FALSE(is_simd_op(opcode::HALT));
    EXPECT_FALSE(is_simd_op(opcode::NOP));
    EXPECT_FALSE(is_simd_op(0xBF));  // Crypto range end
    EXPECT_FALSE(is_simd_op(0xD0));  // Reserved range start
}

}  // namespace
}  // namespace dotvm::exec
