#include <gtest/gtest.h>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/dsl/compiler/codegen.hpp"
#include "dotvm/core/dsl/compiler/ir_builder.hpp"
#include "dotvm/core/dsl/compiler/lowerer.hpp"
#include "dotvm/core/dsl/parser.hpp"

using namespace dotvm::core;
using namespace dotvm::core::dsl::compiler;
using namespace dotvm::core::dsl::ir;
using dotvm::core::dsl::DslParser;

class CodegenTest : public ::testing::Test {
protected:
    IRBuilder builder;
    Lowerer lowerer;
    CodeGenerator codegen{Architecture::Arch64};

    LinearIR build_and_lower(std::string_view source) {
        auto parse_result = DslParser::parse(source);
        EXPECT_TRUE(parse_result.is_ok());
        auto ir_result = builder.build(parse_result.value());
        EXPECT_TRUE(ir_result.has_value());
        EXPECT_FALSE(ir_result->dots.empty());
        return lowerer.lower(ir_result->dots[0]);
    }
};

TEST_F(CodegenTest, EmptyDot) {
    auto linear = build_and_lower(R"(
        dot empty:
            state:
                x: 0
    )");

    auto result = codegen.generate(linear);
    ASSERT_TRUE(result.has_value());

    // Should have some code
    EXPECT_FALSE(result->code.empty());
}

TEST_F(CodegenTest, BytecodeAssembly) {
    auto linear = build_and_lower(R"(
        dot asm_test:
            state:
                x: 0
    )");

    auto gen_result = codegen.generate(linear);
    ASSERT_TRUE(gen_result.has_value());

    auto bytecode = codegen.assemble(*gen_result);

    // Verify header
    EXPECT_GE(bytecode.size(), bytecode::HEADER_SIZE);

    // Check magic bytes
    EXPECT_EQ(bytecode[0], 'D');
    EXPECT_EQ(bytecode[1], 'O');
    EXPECT_EQ(bytecode[2], 'T');
    EXPECT_EQ(bytecode[3], 'M');

    // Check version
    EXPECT_EQ(bytecode[4], bytecode::CURRENT_VERSION);

    // Parse and validate header
    auto header_result = read_header(bytecode);
    ASSERT_TRUE(header_result.has_value());

    auto validation = validate_header(*header_result, bytecode.size());
    EXPECT_EQ(validation, BytecodeError::Success);
}

TEST_F(CodegenTest, InstructionAlignment) {
    auto linear = build_and_lower(R"(
        dot align_test:
            state:
                x: 0
            when true:
                do:
                    state.x = 1
    )");

    auto gen_result = codegen.generate(linear);
    ASSERT_TRUE(gen_result.has_value());

    // All instructions should be 4-byte aligned
    EXPECT_EQ(gen_result->code.size() % bytecode::INSTRUCTION_ALIGNMENT, 0);
}

TEST_F(CodegenTest, ConstantPoolGeneration) {
    auto linear = build_and_lower(R"(
        dot const_test:
            state:
                x: 12345
    )");

    auto gen_result = codegen.generate(linear);
    ASSERT_TRUE(gen_result.has_value());

    auto bytecode = codegen.assemble(*gen_result);
    auto header_result = read_header(bytecode);
    ASSERT_TRUE(header_result.has_value());

    // If there's a constant pool, validate it
    if (header_result->const_pool_size > 0) {
        auto pool_data = std::span<const std::uint8_t>(
            bytecode.data() + header_result->const_pool_offset, header_result->const_pool_size);

        auto pool_result = load_constant_pool(pool_data);
        EXPECT_TRUE(pool_result.has_value());
    }
}

TEST_F(CodegenTest, EntryPointAlignment) {
    auto linear = build_and_lower(R"(
        dot entry_test:
            state:
                y: 0
    )");

    auto gen_result = codegen.generate(linear);
    ASSERT_TRUE(gen_result.has_value());

    // Entry point should be aligned
    EXPECT_EQ(gen_result->entry_point % bytecode::INSTRUCTION_ALIGNMENT, 0);
}

TEST_F(CodegenTest, Architecture64Bit) {
    CodeGenerator gen64(Architecture::Arch64);

    auto linear = build_and_lower(R"(
        dot arch64:
            state:
                z: 0
    )");

    auto gen_result = gen64.generate(linear);
    ASSERT_TRUE(gen_result.has_value());
    EXPECT_EQ(gen_result->arch, Architecture::Arch64);

    auto bytecode = gen64.assemble(*gen_result);
    auto header = read_header(bytecode);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->arch, Architecture::Arch64);
}

TEST_F(CodegenTest, Architecture32Bit) {
    CodeGenerator gen32(Architecture::Arch32);

    auto linear = build_and_lower(R"(
        dot arch32:
            state:
                w: 0
    )");

    auto gen_result = gen32.generate(linear);
    ASSERT_TRUE(gen_result.has_value());
    EXPECT_EQ(gen_result->arch, Architecture::Arch32);

    auto bytecode = gen32.assemble(*gen_result);
    auto header = read_header(bytecode);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->arch, Architecture::Arch32);
}

TEST_F(CodegenTest, LowererRegisterAllocation) {
    auto parse_result = DslParser::parse(R"(
        dot regs:
            state:
                a: 1
                b: 2
                c: 3
            when true:
                do:
                    state.c = a + b
    )");
    ASSERT_TRUE(parse_result.is_ok());

    auto ir_result = builder.build(parse_result.value());
    ASSERT_TRUE(ir_result.has_value());

    auto linear = lowerer.lower(ir_result->dots[0]);

    // Should have allocated some registers
    EXPECT_GT(linear.max_register_used, 0);
}

TEST_F(CodegenTest, LinearBlockLabels) {
    auto parse_result = DslParser::parse(R"(
        dot labels:
            state:
                x: 0
            when x < 10:
                do:
                    state.x += 1
    )");
    ASSERT_TRUE(parse_result.is_ok());

    auto ir_result = builder.build(parse_result.value());
    ASSERT_TRUE(ir_result.has_value());

    auto linear = lowerer.lower(ir_result->dots[0]);

    // Should have multiple blocks with labels
    EXPECT_GT(linear.blocks.size(), 1);
    for (const auto& block : linear.blocks) {
        EXPECT_FALSE(block.label.empty());
    }
}
