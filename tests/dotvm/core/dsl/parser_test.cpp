/// @file parser_test.cpp
/// @brief DSL-001 Parser unit tests

#include <gtest/gtest.h>

#include "dotvm/core/dsl/parser.hpp"

using namespace dotvm::core::dsl;

// ============================================================================
// Helper Functions
// ============================================================================

/// Parse source and expect success
DslModule parse_ok(std::string_view source) {
    auto result = DslParser::parse(source);
    EXPECT_TRUE(result.is_ok()) << "Parsing failed unexpectedly";
    return result.is_ok() ? std::move(result.value()) : DslModule{};
}

/// Parse source and expect failure
void parse_err(std::string_view source) {
    auto result = DslParser::parse(source);
    EXPECT_TRUE(result.is_err()) << "Parsing should have failed";
}

// ============================================================================
// Include Tests
// ============================================================================

TEST(ParserTest, SimpleInclude) {
    auto module = parse_ok("include: \"stdlib/common.dsl\"\n");
    ASSERT_EQ(module.includes.size(), 1);
    EXPECT_EQ(module.includes[0].path, "stdlib/common.dsl");
}

TEST(ParserTest, MultipleIncludes) {
    auto module = parse_ok(R"(
include: "lib1.dsl"
include: "lib2.dsl"
include: "../shared/utils.dsl"
)");
    ASSERT_EQ(module.includes.size(), 3);
    EXPECT_EQ(module.includes[0].path, "lib1.dsl");
    EXPECT_EQ(module.includes[1].path, "lib2.dsl");
    EXPECT_EQ(module.includes[2].path, "../shared/utils.dsl");
}

TEST(ParserTest, IncludeBeforeImport) {
    auto module = parse_ok(R"(
include: "stdlib/common.dsl"
import "mymodule"

dot Agent:
    state:
        x: 0
)");
    ASSERT_EQ(module.includes.size(), 1);
    ASSERT_EQ(module.imports.size(), 1);
    ASSERT_EQ(module.dots.size(), 1);
}

TEST(ParserTest, IncludeMissingColon) {
    parse_err("include \"path.dsl\"\n");
}

TEST(ParserTest, IncludeMissingString) {
    parse_err("include:\n");
}

// ============================================================================
// Import Tests
// ============================================================================

TEST(ParserTest, SimpleImport) {
    auto module = parse_ok("import \"path/to/module\"\n");
    ASSERT_EQ(module.imports.size(), 1);
    EXPECT_EQ(module.imports[0].path, "path/to/module");
}

TEST(ParserTest, MultipleImports) {
    auto module = parse_ok(R"(
import "module1"
import "module2"
import "module3"
)");
    ASSERT_EQ(module.imports.size(), 3);
    EXPECT_EQ(module.imports[0].path, "module1");
    EXPECT_EQ(module.imports[1].path, "module2");
    EXPECT_EQ(module.imports[2].path, "module3");
}

// ============================================================================
// Dot Definition Tests
// ============================================================================

TEST(ParserTest, EmptyDot) {
    auto module = parse_ok(R"(
dot agent:
    state:
        x: 0
)");
    ASSERT_EQ(module.dots.size(), 1);
    EXPECT_EQ(module.dots[0].name, "agent");
    EXPECT_TRUE(module.dots[0].state.has_value());
}

TEST(ParserTest, DotWithState) {
    auto module = parse_ok(R"(
dot counter:
    state:
        count: 0
        name: "default"
        flag: true
)");
    ASSERT_EQ(module.dots.size(), 1);
    EXPECT_EQ(module.dots[0].name, "counter");
    ASSERT_TRUE(module.dots[0].state.has_value());
    ASSERT_EQ(module.dots[0].state->variables.size(), 3);
    EXPECT_EQ(module.dots[0].state->variables[0].name, "count");
    EXPECT_EQ(module.dots[0].state->variables[1].name, "name");
    EXPECT_EQ(module.dots[0].state->variables[2].name, "flag");
}

TEST(ParserTest, DotWithTrigger) {
    auto module = parse_ok(R"(
dot agent:
    when true:
        do:
            action1 "arg"
)");
    ASSERT_EQ(module.dots.size(), 1);
    ASSERT_EQ(module.dots[0].triggers.size(), 1);
}

TEST(ParserTest, DotWithMultipleTriggers) {
    auto module = parse_ok(R"(
dot agent:
    when condition1:
        do:
            action1
    when condition2:
        do:
            action2
)");
    ASSERT_EQ(module.dots.size(), 1);
    ASSERT_EQ(module.dots[0].triggers.size(), 2);
}

// ============================================================================
// Link Tests
// ============================================================================

TEST(ParserTest, SimpleLink) {
    auto module = parse_ok("link agent1 -> agent2\n");
    ASSERT_EQ(module.links.size(), 1);
    EXPECT_EQ(module.links[0].source, "agent1");
    EXPECT_EQ(module.links[0].target, "agent2");
}

TEST(ParserTest, MultipleLinks) {
    auto module = parse_ok(R"(
link a -> b
link b -> c
link c -> a
)");
    ASSERT_EQ(module.links.size(), 3);
}

// ============================================================================
// Expression Tests
// ============================================================================

TEST(ParserTest, IntegerLiteral) {
    auto module = parse_ok(R"(
dot test:
    state:
        x: 42
)");
    ASSERT_TRUE(module.dots[0].state.has_value());
    ASSERT_EQ(module.dots[0].state->variables.size(), 1);
    const auto& value = module.dots[0].state->variables[0].value;
    ASSERT_TRUE(std::holds_alternative<IntegerExpr>(value->value));
    EXPECT_EQ(std::get<IntegerExpr>(value->value).value, 42);
}

TEST(ParserTest, FloatLiteral) {
    auto module = parse_ok(R"(
dot test:
    state:
        x: 3.14
)");
    ASSERT_TRUE(module.dots[0].state.has_value());
    const auto& value = module.dots[0].state->variables[0].value;
    ASSERT_TRUE(std::holds_alternative<FloatExpr>(value->value));
    EXPECT_DOUBLE_EQ(std::get<FloatExpr>(value->value).value, 3.14);
}

TEST(ParserTest, StringLiteral) {
    auto module = parse_ok(R"(
dot test:
    state:
        x: "hello"
)");
    ASSERT_TRUE(module.dots[0].state.has_value());
    const auto& value = module.dots[0].state->variables[0].value;
    ASSERT_TRUE(std::holds_alternative<StringExpr>(value->value));
    EXPECT_EQ(std::get<StringExpr>(value->value).value, "hello");
}

TEST(ParserTest, BooleanLiterals) {
    auto module = parse_ok(R"(
dot test:
    state:
        a: true
        b: false
)");
    ASSERT_TRUE(module.dots[0].state.has_value());
    ASSERT_EQ(module.dots[0].state->variables.size(), 2);

    const auto& val_a = module.dots[0].state->variables[0].value;
    ASSERT_TRUE(std::holds_alternative<BoolExpr>(val_a->value));
    EXPECT_TRUE(std::get<BoolExpr>(val_a->value).value);

    const auto& val_b = module.dots[0].state->variables[1].value;
    ASSERT_TRUE(std::holds_alternative<BoolExpr>(val_b->value));
    EXPECT_FALSE(std::get<BoolExpr>(val_b->value).value);
}

TEST(ParserTest, IdentifierExpression) {
    auto module = parse_ok(R"(
dot test:
    when active:
        do:
            noop
)");
    ASSERT_EQ(module.dots[0].triggers.size(), 1);
    const auto& cond = module.dots[0].triggers[0].condition;
    ASSERT_TRUE(std::holds_alternative<IdentifierExpr>(cond->value));
    EXPECT_EQ(std::get<IdentifierExpr>(cond->value).name, "active");
}

TEST(ParserTest, MemberExpression) {
    auto module = parse_ok(R"(
dot test:
    when state.active:
        do:
            noop
)");
    ASSERT_EQ(module.dots[0].triggers.size(), 1);
    const auto& cond = module.dots[0].triggers[0].condition;
    ASSERT_TRUE(std::holds_alternative<MemberExpr>(cond->value));
    const auto& member = std::get<MemberExpr>(cond->value);
    EXPECT_EQ(member.member, "active");
}

// ============================================================================
// Binary Expression Tests
// ============================================================================

TEST(ParserTest, ArithmeticExpression) {
    auto module = parse_ok(R"(
dot test:
    state:
        x: 1 + 2
)");
    const auto& value = module.dots[0].state->variables[0].value;
    ASSERT_TRUE(std::holds_alternative<BinaryExpr>(value->value));
    const auto& bin = std::get<BinaryExpr>(value->value);
    EXPECT_EQ(bin.op, BinaryOp::Add);
}

TEST(ParserTest, ComparisonExpression) {
    auto module = parse_ok(R"(
dot test:
    when x > 0:
        do:
            noop
)");
    const auto& cond = module.dots[0].triggers[0].condition;
    ASSERT_TRUE(std::holds_alternative<BinaryExpr>(cond->value));
    const auto& bin = std::get<BinaryExpr>(cond->value);
    EXPECT_EQ(bin.op, BinaryOp::Gt);
}

TEST(ParserTest, LogicalExpression) {
    auto module = parse_ok(R"(
dot test:
    when a and b:
        do:
            noop
)");
    const auto& cond = module.dots[0].triggers[0].condition;
    ASSERT_TRUE(std::holds_alternative<BinaryExpr>(cond->value));
    const auto& bin = std::get<BinaryExpr>(cond->value);
    EXPECT_EQ(bin.op, BinaryOp::And);
}

TEST(ParserTest, OperatorPrecedence) {
    // Test that * has higher precedence than +
    auto module = parse_ok(R"(
dot test:
    state:
        x: 1 + 2 * 3
)");
    const auto& value = module.dots[0].state->variables[0].value;
    ASSERT_TRUE(std::holds_alternative<BinaryExpr>(value->value));
    const auto& add = std::get<BinaryExpr>(value->value);
    EXPECT_EQ(add.op, BinaryOp::Add);  // Top level is addition

    // Right side should be multiplication
    ASSERT_TRUE(std::holds_alternative<BinaryExpr>(add.right->value));
    const auto& mul = std::get<BinaryExpr>(add.right->value);
    EXPECT_EQ(mul.op, BinaryOp::Mul);
}

// ============================================================================
// Unary Expression Tests
// ============================================================================

TEST(ParserTest, NegationExpression) {
    auto module = parse_ok(R"(
dot test:
    state:
        x: -42
)");
    const auto& value = module.dots[0].state->variables[0].value;
    ASSERT_TRUE(std::holds_alternative<UnaryExpr>(value->value));
    const auto& un = std::get<UnaryExpr>(value->value);
    EXPECT_EQ(un.op, UnaryOp::Neg);
}

TEST(ParserTest, NotExpression) {
    auto module = parse_ok(R"(
dot test:
    when not active:
        do:
            noop
)");
    const auto& cond = module.dots[0].triggers[0].condition;
    ASSERT_TRUE(std::holds_alternative<UnaryExpr>(cond->value));
    const auto& un = std::get<UnaryExpr>(cond->value);
    EXPECT_EQ(un.op, UnaryOp::Not);
}

// ============================================================================
// Action Tests
// ============================================================================

TEST(ParserTest, CallAction) {
    auto module = parse_ok(R"(
dot test:
    when true:
        do:
            print "hello"
)");
    ASSERT_EQ(module.dots[0].triggers.size(), 1);
    const auto& actions = module.dots[0].triggers[0].do_block.actions;
    ASSERT_EQ(actions.size(), 1);
    EXPECT_EQ(actions[0].type, ActionStmt::Type::Call);
    EXPECT_EQ(actions[0].callee, "print");
    ASSERT_EQ(actions[0].arguments.size(), 1);
}

TEST(ParserTest, AssignmentAction) {
    auto module = parse_ok(R"(
dot test:
    when true:
        do:
            state.counter = 0
)");
    const auto& actions = module.dots[0].triggers[0].do_block.actions;
    ASSERT_EQ(actions.size(), 1);
    EXPECT_EQ(actions[0].type, ActionStmt::Type::Assignment);
    EXPECT_EQ(actions[0].assign_op, AssignOp::Assign);
}

TEST(ParserTest, CompoundAssignment) {
    auto module = parse_ok(R"(
dot test:
    when true:
        do:
            state.counter += 1
)");
    const auto& actions = module.dots[0].triggers[0].do_block.actions;
    ASSERT_EQ(actions.size(), 1);
    EXPECT_EQ(actions[0].type, ActionStmt::Type::Assignment);
    EXPECT_EQ(actions[0].assign_op, AssignOp::AddAssign);
}

TEST(ParserTest, MultipleActions) {
    auto module = parse_ok(R"(
dot test:
    when true:
        do:
            action1
            state.x = 1
            action2 "arg"
)");
    const auto& actions = module.dots[0].triggers[0].do_block.actions;
    ASSERT_EQ(actions.size(), 3);
}

// ============================================================================
// Complex Module Tests
// ============================================================================

TEST(ParserTest, CompleteModule) {
    auto module = parse_ok(R"(
import "utils"

dot counter:
    state:
        count: 0
        max: 100

    when count < max:
        do:
            increment
            state.count += 1

    when count >= max:
        do:
            reset
            state.count = 0

dot logger:
    when true:
        do:
            log "message"

link counter -> logger
)");
    EXPECT_EQ(module.imports.size(), 1);
    EXPECT_EQ(module.dots.size(), 2);
    EXPECT_EQ(module.links.size(), 1);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(ParserTest, MissingColon) {
    parse_err("dot agent\n    state:\n        x: 0\n");
}

TEST(ParserTest, MissingIndent) {
    parse_err("dot agent:\nstate:\n    x: 0\n");
}

TEST(ParserTest, InvalidExpression) {
    parse_err(R"(
dot test:
    state:
        x:
)");
}

TEST(ParserTest, MissingArrow) {
    parse_err("link agent1 agent2\n");
}

// ============================================================================
// Interpolated String Tests
// ============================================================================

TEST(ParserTest, InterpolatedStringInAction) {
    auto module = parse_ok(R"(
dot test:
    when true:
        do:
            print "Value: ${state.counter}"
)");
    const auto& actions = module.dots[0].triggers[0].do_block.actions;
    ASSERT_EQ(actions.size(), 1);
    ASSERT_EQ(actions[0].arguments.size(), 1);
    EXPECT_TRUE(std::holds_alternative<InterpolatedString>(actions[0].arguments[0]->value));
}
