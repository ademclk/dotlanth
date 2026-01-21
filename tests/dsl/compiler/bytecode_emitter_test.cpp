#include <gtest/gtest.h>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/dsl/compiler/bytecode_emitter.hpp"
#include "dotvm/core/dsl/compiler/ir_builder.hpp"
#include "dotvm/core/dsl/parser.hpp"
#include "dotvm/core/opcode.hpp"

using namespace dotvm::core::dsl::compiler;
using dotvm::core::dsl::DslParser;
using dotvm::core::dsl::ir::DotIR;
using dotvm::core::dsl::SourceSpan;
using dotvm::core::dsl::SourceLocation;
using dotvm::core::Architecture;
using dotvm::core::BytecodeError;
using CoreValue = dotvm::core::Value;
namespace bytecode = dotvm::core::bytecode;
namespace opcode = dotvm::core::opcode;
using dotvm::core::read_header;
using dotvm::core::validate_header;
using dotvm::core::load_constant_pool;
using dotvm::core::sections_overlap;

// ============================================================================
// Primary API Tests
// ============================================================================

class BytecodeEmitterTest : public ::testing::Test {
protected:
    IRBuilder builder;

    DotIR build_ir(std::string_view source) {
        auto parse_result = DslParser::parse(source);
        EXPECT_TRUE(parse_result.is_ok());
        auto ir_result = builder.build(parse_result.value());
        EXPECT_TRUE(ir_result.has_value());
        EXPECT_FALSE(ir_result->dots.empty());
        return std::move(ir_result->dots[0]);
    }
};

TEST_F(BytecodeEmitterTest, EmitSimpleDot) {
    auto dot = build_ir(R"(
        dot simple:
            state:
                x: 0
    )");

    BytecodeEmitter emitter;
    auto result = emitter.emit(dot);

    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->size(), bytecode::HEADER_SIZE);

    // Verify magic bytes
    EXPECT_EQ((*result)[0], 'D');
    EXPECT_EQ((*result)[1], 'O');
    EXPECT_EQ((*result)[2], 'T');
    EXPECT_EQ((*result)[3], 'M');
}

TEST_F(BytecodeEmitterTest, EmitWithTrigger) {
    auto dot = build_ir(R"(
        dot trigger_test:
            state:
                counter: 0
            when counter < 10:
                do:
                    state.counter += 1
    )");

    BytecodeEmitter emitter;
    auto result = emitter.emit(dot);

    ASSERT_TRUE(result.has_value());

    // Parse and validate header
    auto header_result = read_header(*result);
    ASSERT_TRUE(header_result.has_value());

    auto validation = validate_header(*header_result, result->size());
    EXPECT_EQ(validation, BytecodeError::Success);
}

TEST_F(BytecodeEmitterTest, EmitWithArithmetic) {
    auto dot = build_ir(R"(
        dot arithmetic:
            state:
                a: 10
                b: 20
                sum: 0
            when true:
                do:
                    state.sum = a + b
    )");

    BytecodeEmitter emitter;
    auto result = emitter.emit(dot);

    ASSERT_TRUE(result.has_value());

    // Verify bytecode validates
    auto header_result = read_header(*result);
    ASSERT_TRUE(header_result.has_value());
    EXPECT_EQ(validate_header(*header_result, result->size()), BytecodeError::Success);
}

// ============================================================================
// Config Tests
// ============================================================================

TEST_F(BytecodeEmitterTest, ConfigArch32) {
    auto dot = build_ir(R"(
        dot arch32_test:
            state:
                x: 0
    )");

    EmitterConfig config;
    config.arch = Architecture::Arch32;

    BytecodeEmitter emitter(config);
    auto result = emitter.emit(dot);

    ASSERT_TRUE(result.has_value());

    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->arch, Architecture::Arch32);
}

TEST_F(BytecodeEmitterTest, ConfigArch64) {
    auto dot = build_ir(R"(
        dot arch64_test:
            state:
                y: 0
    )");

    EmitterConfig config;
    config.arch = Architecture::Arch64;

    BytecodeEmitter emitter(config);
    auto result = emitter.emit(dot);

    ASSERT_TRUE(result.has_value());

    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->arch, Architecture::Arch64);
}

TEST_F(BytecodeEmitterTest, ConfigDebugFlag) {
    auto dot = build_ir(R"(
        dot debug_test:
            state:
                z: 0
    )");

    EmitterConfig config;
    config.flags = bytecode::FLAG_DEBUG;

    BytecodeEmitter emitter(config);
    auto result = emitter.emit(dot);

    ASSERT_TRUE(result.has_value());

    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());
    // Note: CodeGenerator doesn't currently pass through flags,
    // but the emitter accepts them for future use
}

TEST_F(BytecodeEmitterTest, ConfigValidationDisabled) {
    auto dot = build_ir(R"(
        dot no_validate:
            state:
                x: 0
    )");

    EmitterConfig config;
    config.validate_output = false;

    BytecodeEmitter emitter(config);
    auto result = emitter.emit(dot);

    ASSERT_TRUE(result.has_value());
    // Output should still be valid even without validation
    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());
}

// ============================================================================
// Low-level API Tests
// ============================================================================

TEST_F(BytecodeEmitterTest, IncrementalBuildSimple) {
    BytecodeEmitter emitter;
    emitter.begin();

    // Emit a simple HALT instruction
    std::array<std::uint8_t, 3> args = {0, 0, 0};
    emitter.emit_instruction(opcode::HALT, args);

    auto result = emitter.finalize();
    ASSERT_TRUE(result.has_value());

    // Verify header
    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->code_size, 4);  // One 4-byte instruction
}

TEST_F(BytecodeEmitterTest, IncrementalBuildWithConstants) {
    BytecodeEmitter emitter;
    emitter.begin();

    // Add constants
    auto idx1 = emitter.add_constant(CoreValue::from_int(42));
    auto idx2 = emitter.add_constant(CoreValue::from_float(3.14));

    EXPECT_EQ(idx1, 0);
    EXPECT_EQ(idx2, 1);
    EXPECT_EQ(emitter.constant_pool_size(), 2);

    // Emit HALT
    std::array<std::uint8_t, 3> args = {0, 0, 0};
    emitter.emit_instruction(opcode::HALT, args);

    auto result = emitter.finalize();
    ASSERT_TRUE(result.has_value());

    // Verify constant pool
    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());
    EXPECT_GT(header->const_pool_size, 0);

    // Load and verify constants
    auto pool_data = std::span<const std::uint8_t>(
        result->data() + header->const_pool_offset, header->const_pool_size);
    auto pool = load_constant_pool(pool_data);
    ASSERT_TRUE(pool.has_value());
    ASSERT_EQ(pool->size(), 2);
    EXPECT_EQ((*pool)[0].as_integer(), 42);
    EXPECT_DOUBLE_EQ((*pool)[1].as_float(), 3.14);
}

TEST_F(BytecodeEmitterTest, ConstantDeduplication) {
    BytecodeEmitter emitter;
    emitter.begin();

    // Add same constant multiple times
    auto idx1 = emitter.add_constant(CoreValue::from_int(100));
    auto idx2 = emitter.add_constant(CoreValue::from_int(100));
    auto idx3 = emitter.add_constant(CoreValue::from_int(200));
    auto idx4 = emitter.add_constant(CoreValue::from_int(100));

    // Same value should return same index
    EXPECT_EQ(idx1, idx2);
    EXPECT_EQ(idx1, idx4);
    EXPECT_NE(idx1, idx3);

    // Pool should only have 2 entries
    EXPECT_EQ(emitter.constant_pool_size(), 2);
}

TEST_F(BytecodeEmitterTest, LabelDefinitionAndReference) {
    BytecodeEmitter emitter;
    emitter.begin();

    // Define a label
    emitter.define_label("start");

    // Emit some instructions
    std::array<std::uint8_t, 3> nop_args = {0, 0, 0};
    emitter.emit_instruction(opcode::NOP, nop_args);
    emitter.emit_instruction(opcode::NOP, nop_args);

    // Define another label
    emitter.define_label("end");

    // Add a jump back to start (reference before we emit it)
    emitter.add_label_reference("start", true);
    emitter.emit_instruction(opcode::JMP, nop_args);

    // Emit HALT
    emitter.emit_instruction(opcode::HALT, nop_args);

    auto result = emitter.finalize();
    ASSERT_TRUE(result.has_value());

    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(validate_header(*header, result->size()), BytecodeError::Success);
}

TEST_F(BytecodeEmitterTest, StateQueries) {
    BytecodeEmitter emitter;

    // Default config
    EXPECT_EQ(emitter.config().arch, Architecture::Arch64);
    EXPECT_EQ(emitter.config().flags, bytecode::FLAG_NONE);
    EXPECT_TRUE(emitter.config().validate_output);

    // Initial state
    EXPECT_EQ(emitter.current_code_size(), 0);
    EXPECT_EQ(emitter.constant_pool_size(), 0);

    emitter.begin();

    // Add some content
    [[maybe_unused]] auto idx = emitter.add_constant(CoreValue::from_int(1));
    std::array<std::uint8_t, 3> args = {0, 0, 0};
    emitter.emit_instruction(opcode::NOP, args);

    EXPECT_EQ(emitter.current_code_size(), 4);
    EXPECT_EQ(emitter.constant_pool_size(), 1);
}

TEST_F(BytecodeEmitterTest, BeginClearsState) {
    BytecodeEmitter emitter;
    emitter.begin();

    // Add some content
    [[maybe_unused]] auto idx = emitter.add_constant(CoreValue::from_int(42));
    std::array<std::uint8_t, 3> args = {0, 0, 0};
    emitter.emit_instruction(opcode::NOP, args);

    EXPECT_EQ(emitter.constant_pool_size(), 1);
    EXPECT_EQ(emitter.current_code_size(), 4);

    // Begin again should clear
    emitter.begin();

    EXPECT_EQ(emitter.constant_pool_size(), 0);
    EXPECT_EQ(emitter.current_code_size(), 0);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(BytecodeEmitterTest, ErrorFactoryMethods) {
    // Test that factory methods create correct error kinds
    auto err1 = EmitterError::register_spill("test");
    EXPECT_EQ(err1.kind, EmitterError::Kind::RegisterSpill);
    EXPECT_EQ(err1.message, "test");
    EXPECT_FALSE(err1.span.has_value());

    auto span = SourceSpan::at(SourceLocation::at(1, 1, 0));
    auto err2 = EmitterError::unsupported_instruction("unsupported", span);
    EXPECT_EQ(err2.kind, EmitterError::Kind::UnsupportedInstruction);
    EXPECT_TRUE(err2.span.has_value());

    auto err3 = EmitterError::constant_pool_overflow("overflow");
    EXPECT_EQ(err3.kind, EmitterError::Kind::ConstantPoolOverflow);

    auto err4 = EmitterError::jump_out_of_range("range");
    EXPECT_EQ(err4.kind, EmitterError::Kind::JumpOutOfRange);

    auto err5 = EmitterError::header_validation_failed("validation");
    EXPECT_EQ(err5.kind, EmitterError::Kind::HeaderValidationFailed);

    auto err6 = EmitterError::bytecode_size_mismatch("size");
    EXPECT_EQ(err6.kind, EmitterError::Kind::BytecodeSizeMismatch);

    auto err7 = EmitterError::entry_point_invalid("entry");
    EXPECT_EQ(err7.kind, EmitterError::Kind::EntryPointInvalid);

    auto err8 = EmitterError::internal("internal");
    EXPECT_EQ(err8.kind, EmitterError::Kind::InternalError);
}

TEST_F(BytecodeEmitterTest, UnresolvedLabelError) {
    BytecodeEmitter emitter;
    emitter.begin();

    // Reference a label that doesn't exist
    emitter.add_label_reference("nonexistent", true);
    std::array<std::uint8_t, 3> args = {0, 0, 0};
    emitter.emit_instruction(opcode::JMP, args);

    auto result = emitter.finalize();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().kind, EmitterError::Kind::InternalError);
    EXPECT_TRUE(result.error().message.find("nonexistent") != std::string::npos);
}

// ============================================================================
// Round-trip Validation Tests
// ============================================================================

TEST_F(BytecodeEmitterTest, RoundTripConstantPool) {
    auto dot = build_ir(R"(
        dot roundtrip:
            state:
                big_num: 99999
    )");

    BytecodeEmitter emitter;
    auto result = emitter.emit(dot);
    ASSERT_TRUE(result.has_value());

    // Read header and load constant pool
    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());

    if (header->const_pool_size > 0) {
        auto pool_data = std::span<const std::uint8_t>(
            result->data() + header->const_pool_offset, header->const_pool_size);
        auto pool = load_constant_pool(pool_data);
        EXPECT_TRUE(pool.has_value());
    }
}

TEST_F(BytecodeEmitterTest, BytecodeAlignment) {
    auto dot = build_ir(R"(
        dot alignment:
            state:
                x: 0
            when x < 5:
                do:
                    state.x += 1
    )");

    BytecodeEmitter emitter;
    auto result = emitter.emit(dot);
    ASSERT_TRUE(result.has_value());

    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());

    // Code offset should be aligned
    EXPECT_EQ(header->code_offset % bytecode::INSTRUCTION_ALIGNMENT, 0);

    // Entry point should be aligned
    EXPECT_EQ(header->entry_point % bytecode::INSTRUCTION_ALIGNMENT, 0);
}

// ============================================================================
// Integration with Full Pipeline
// ============================================================================

TEST_F(BytecodeEmitterTest, ComplexDotCompilation) {
    auto dot = build_ir(R"(
        dot complex:
            state:
                count: 0
                done: false
            when count < 5:
                do:
                    state.count += 1
                    state.done = true
    )");

    BytecodeEmitter emitter;
    auto result = emitter.emit(dot);

    ASSERT_TRUE(result.has_value());

    // Full validation
    auto header = read_header(*result);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(validate_header(*header, result->size()), BytecodeError::Success);

    // Verify sections don't overlap
    if (header->const_pool_size > 0 && header->code_size > 0) {
        EXPECT_FALSE(sections_overlap(
            header->const_pool_offset, header->const_pool_size,
            header->code_offset, header->code_size));
    }
}
