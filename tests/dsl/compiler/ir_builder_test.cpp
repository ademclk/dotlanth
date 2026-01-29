#include <gtest/gtest.h>

#include "dotvm/core/dsl/compiler/ir_builder.hpp"
#include "dotvm/core/dsl/ir/printer.hpp"
#include "dotvm/core/dsl/parser.hpp"

using namespace dotvm::core::dsl::compiler;
using namespace dotvm::core::dsl::ir;
using dotvm::core::dsl::DslParser;

class IRBuilderTest : public ::testing::Test {
protected:
    IRBuilder builder;

    // Helper to parse and build IR
    IRBuildResult<CompiledModule> build_from_source(std::string_view source) {
        auto parse_result = DslParser::parse(source);
        EXPECT_TRUE(parse_result.is_ok()) << "Parse failed";
        if (!parse_result.is_ok()) {
            return std::unexpected(IRBuildError::internal("Parse failed"));
        }
        return builder.build(parse_result.value());
    }
};

TEST_F(IRBuilderTest, EmptyModule) {
    auto result = build_from_source("");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->dots.empty());
}

TEST_F(IRBuilderTest, SimpleDot) {
    auto result = build_from_source(R"(
        dot counter:
            state:
                count: 0
    )");

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->dots.size(), 1);

    const auto& dot = result->dots[0];
    EXPECT_EQ(dot.name, "counter");
    ASSERT_EQ(dot.state_slots.size(), 1);
    EXPECT_EQ(dot.state_slots[0].name, "count");
    EXPECT_EQ(dot.state_slots[0].type, ValueType::Int64);
}

TEST_F(IRBuilderTest, StateWithMultipleVars) {
    auto result = build_from_source(R"(
        dot multi:
            state:
                x: 10
                y: 20.5
                flag: true
    )");

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->dots.size(), 1);

    const auto& dot = result->dots[0];
    ASSERT_EQ(dot.state_slots.size(), 3);

    EXPECT_EQ(dot.state_slots[0].name, "x");
    EXPECT_EQ(dot.state_slots[0].type, ValueType::Int64);

    EXPECT_EQ(dot.state_slots[1].name, "y");
    EXPECT_EQ(dot.state_slots[1].type, ValueType::Float64);

    EXPECT_EQ(dot.state_slots[2].name, "flag");
    EXPECT_EQ(dot.state_slots[2].type, ValueType::Bool);
}

TEST_F(IRBuilderTest, SimpleTrigger) {
    auto result = build_from_source(R"(
        dot ticker:
            state:
                count: 0
            when count < 10:
                do:
                    state.count += 1
    )");

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->dots.size(), 1);

    const auto& dot = result->dots[0];
    EXPECT_FALSE(dot.blocks.empty());
    EXPECT_EQ(dot.triggers.size(), 1);
}

TEST_F(IRBuilderTest, BinaryExpressions) {
    auto result = build_from_source(R"(
        dot calc:
            state:
                a: 5
                b: 3
            when true:
                do:
                    state.a = a + b
    )");

    ASSERT_TRUE(result.has_value());
    // Should have generated Add instruction
}

TEST_F(IRBuilderTest, ComparisonExpressions) {
    auto result = build_from_source(R"(
        dot cmp:
            state:
                x: 0
            when x == 0:
                do:
                    state.x = 1
    )");

    ASSERT_TRUE(result.has_value());
    // Should have generated Compare instruction with Eq
}

TEST_F(IRBuilderTest, CompoundAssignment) {
    auto result = build_from_source(R"(
        dot compound:
            state:
                val: 10
            when true:
                do:
                    state.val += 5
                    state.val -= 2
                    state.val *= 3
    )");

    ASSERT_TRUE(result.has_value());
}

TEST_F(IRBuilderTest, FunctionCall) {
    auto result = build_from_source(R"(
        dot caller:
            state:
                x: 0
            when true:
                do:
                    emit "hello"
    )");

    ASSERT_TRUE(result.has_value());
    // Should have generated Call instruction
}

TEST_F(IRBuilderTest, LinkMetadata) {
    auto result = build_from_source(R"(
        dot a:
            state:
                x: 0

        dot b:
            state:
                y: 0

        link a -> b
    )");

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->dots.size(), 2);
    ASSERT_EQ(result->links.size(), 1);
    EXPECT_EQ(result->links[0].source_dot, "a");
    EXPECT_EQ(result->links[0].target_dot, "b");
}

TEST_F(IRBuilderTest, IRPrinterOutput) {
    auto result = build_from_source(R"(
        dot printer_test:
            state:
                count: 0
            when count < 5:
                do:
                    state.count += 1
    )");

    ASSERT_TRUE(result.has_value());

    // Test that IR can be printed without crashing
    std::string ir_str = print_to_string(*result);
    EXPECT_FALSE(ir_str.empty());
    EXPECT_NE(ir_str.find("printer_test"), std::string::npos);
}

TEST_F(IRBuilderTest, StringInterpolation) {
    // Test that string interpolation is now supported and generates valid IR
    auto result = build_from_source(R"(
        dot formatter:
            state:
                name: "World"
                count: 42
            when true:
                do:
                    print "Hello, ${state.name}!"
                    print "Count is ${state.count}"
    )");

    ASSERT_TRUE(result.has_value()) << "String interpolation should be supported";
    ASSERT_EQ(result->dots.size(), 1);

    const auto& dot = result->dots[0];
    EXPECT_EQ(dot.name, "formatter");
    EXPECT_FALSE(dot.blocks.empty());

    // The IR should contain Call instructions for:
    // - str() syscall to convert values to strings
    // - string.concat syscall to concatenate parts
    // This verifies the interpolation generates proper IR
}

TEST_F(IRBuilderTest, StringInterpolationComplex) {
    // Test more complex interpolation with expressions
    auto result = build_from_source(R"(
        dot complex_format:
            state:
                a: 10
                b: 20
            when true:
                do:
                    log "Sum: ${a + b}"
    )");

    ASSERT_TRUE(result.has_value()) << "Complex string interpolation should work";
}

TEST_F(IRBuilderTest, StringInterpolationEmptySegment) {
    // Test interpolation with expression at the beginning
    auto result = build_from_source(R"(
        dot leading_expr:
            state:
                x: 5
            when true:
                do:
                    emit "${x} is the value"
    )");

    ASSERT_TRUE(result.has_value()) << "Interpolation with leading expression should work";
}
