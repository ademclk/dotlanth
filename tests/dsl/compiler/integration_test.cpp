#include <gtest/gtest.h>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/dsl/compiler/dsl_compiler.hpp"
#include "dotvm/core/dsl/parser.hpp"

using namespace dotvm::core;
using namespace dotvm::core::dsl::compiler;

class IntegrationTest : public ::testing::Test {
protected:
    DslCompiler compiler;
};

TEST_F(IntegrationTest, CompileSimpleDot) {
    auto result = compiler.compile_source(R"(
        dot counter:
            state:
                count: 0
            when count < 10:
                do:
                    state.count += 1
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->bytecode.empty());

    // Verify bytecode is valid
    auto header = read_header(result->bytecode);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(validate_header(*header, result->bytecode.size()), BytecodeError::Success);
}

TEST_F(IntegrationTest, CompileMultipleDots) {
    auto result = compiler.compile_source(R"(
        dot producer:
            state:
                value: 0
            when true:
                do:
                    state.value += 1

        dot consumer:
            state:
                received: 0
            when true:
                do:
                    state.received = 1
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->ir.dots.size(), 2);
}

TEST_F(IntegrationTest, CompileWithLinks) {
    auto result = compiler.compile_source(R"(
        dot a:
            state:
                x: 0

        dot b:
            state:
                y: 0

        link a -> b
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->ir.dots.size(), 2);
    EXPECT_EQ(result->ir.links.size(), 1);
}

TEST_F(IntegrationTest, CompileWithOptimization) {
    CompileOptions opts;
    opts.opt_level = Optimizer::Level::Basic;
    DslCompiler opt_compiler(opts);

    auto result = opt_compiler.compile_source(R"(
        dot optimized:
            state:
                x: 0
            when 1 + 2 < 10:
                do:
                    state.x = 2 * 3
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_GT(result->opt_stats.constants_folded, 0);
}

TEST_F(IntegrationTest, CompileWithoutOptimization) {
    CompileOptions opts;
    opts.opt_level = Optimizer::Level::None;
    DslCompiler no_opt_compiler(opts);

    auto result = no_opt_compiler.compile_source(R"(
        dot unoptimized:
            state:
                x: 0
            when 1 + 2 < 10:
                do:
                    state.x = 2 * 3
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->opt_stats.constants_folded, 0);
}

TEST_F(IntegrationTest, CompileArch32) {
    CompileOptions opts;
    opts.arch = Architecture::Arch32;
    DslCompiler arch32_compiler(opts);

    auto result = arch32_compiler.compile_source(R"(
        dot arch32_test:
            state:
                val: 0
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;

    auto header = read_header(result->bytecode);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->arch, Architecture::Arch32);
}

TEST_F(IntegrationTest, ParseError) {
    auto result = compiler.compile_source(R"(
        dot invalid syntax here !!!
    )");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::Parse);
}

TEST_F(IntegrationTest, UnknownIdentifierError) {
    auto result = compiler.compile_source(R"(
        dot error_test:
            state:
                x: 0
            when unknown_var < 10:
                do:
                    state.x = 1
    )");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().stage, CompileError::Stage::IRBuild);
}

TEST_F(IntegrationTest, ComplexExpressions) {
    auto result = compiler.compile_source(R"(
        dot complex:
            state:
                a: 10
                b: 20
                c: 0
            when (a + b) * 2 > 50:
                do:
                    state.c = (a - b) / 2
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
}

TEST_F(IntegrationTest, BooleanLogic) {
    auto result = compiler.compile_source(R"(
        dot logic:
            state:
                flag: true
                count: 0
            when flag and count < 10:
                do:
                    state.count += 1
            when not flag or count == 10:
                do:
                    state.flag = false
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->ir.dots[0].triggers.size(), 2);
}

TEST_F(IntegrationTest, FloatState) {
    auto result = compiler.compile_source(R"(
        dot floating:
            state:
                pi: 3.14159
                radius: 2.0
            when true:
                do:
                    state.radius = pi * 2.0
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
}

TEST_F(IntegrationTest, PreservedIR) {
    auto result = compiler.compile_source(R"(
        dot preserved:
            state:
                x: 0
    )");

    ASSERT_TRUE(result.has_value());

    // IR should be preserved in result
    EXPECT_EQ(result->ir.dots.size(), 1);
    EXPECT_EQ(result->ir.dots[0].name, "preserved");
}

TEST_F(IntegrationTest, EmptySource) {
    auto result = compiler.compile_source("");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ir.dots.empty());
}

TEST_F(IntegrationTest, FunctionCalls) {
    auto result = compiler.compile_source(R"(
        dot caller:
            state:
                x: 0
            when true:
                do:
                    emit "event"
                    log "message"
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
}

TEST_F(IntegrationTest, AllComparisonOperators) {
    auto result = compiler.compile_source(R"(
        dot comparisons:
            state:
                x: 5
                y: 10
            when x == y:
                do:
                    state.x = 0
            when x != y:
                do:
                    state.x = 1
            when x < y:
                do:
                    state.x = 2
            when x <= y:
                do:
                    state.x = 3
            when x > y:
                do:
                    state.x = 4
            when x >= y:
                do:
                    state.x = 5
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->ir.dots[0].triggers.size(), 6);
}

TEST_F(IntegrationTest, AllAssignmentOperators) {
    auto result = compiler.compile_source(R"(
        dot assignments:
            state:
                v: 10
            when true:
                do:
                    state.v = 5
                    state.v += 1
                    state.v -= 1
                    state.v *= 2
                    state.v /= 2
    )");

    ASSERT_TRUE(result.has_value()) << result.error().message;
}
