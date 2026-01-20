/// @file integration_test.cpp
/// @brief DSL-001 Integration tests for full DSL parsing

#include <gtest/gtest.h>

#include "dotvm/core/dsl/parser.hpp"

using namespace dotvm::core::dsl;

// ============================================================================
// Full DSL Examples
// ============================================================================

TEST(DslIntegrationTest, SimpleAgent) {
    const char* source = R"(
dot greeter:
    state:
        greeting: "Hello"

    when true:
        do:
            say state.greeting
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    auto& module = result.value();
    ASSERT_EQ(module.dots.size(), 1);
    EXPECT_EQ(module.dots[0].name, "greeter");
    ASSERT_TRUE(module.dots[0].state.has_value());
    EXPECT_EQ(module.dots[0].state->variables.size(), 1);
    EXPECT_EQ(module.dots[0].triggers.size(), 1);
}

TEST(DslIntegrationTest, CounterAgent) {
    const char* source = R"(
dot counter:
    state:
        count: 0
        max: 10
        step: 1

    when count < max:
        do:
            state.count += step
            emit "incremented"

    when count >= max:
        do:
            state.count = 0
            emit "reset"
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    auto& module = result.value();
    ASSERT_EQ(module.dots.size(), 1);
    EXPECT_EQ(module.dots[0].name, "counter");
    ASSERT_TRUE(module.dots[0].state.has_value());
    EXPECT_EQ(module.dots[0].state->variables.size(), 3);
    EXPECT_EQ(module.dots[0].triggers.size(), 2);

    // Check first trigger actions
    const auto& trigger1 = module.dots[0].triggers[0];
    EXPECT_EQ(trigger1.do_block.actions.size(), 2);
    EXPECT_EQ(trigger1.do_block.actions[0].type, ActionStmt::Type::Assignment);
    EXPECT_EQ(trigger1.do_block.actions[1].type, ActionStmt::Type::Call);

    // Check second trigger actions
    const auto& trigger2 = module.dots[0].triggers[1];
    EXPECT_EQ(trigger2.do_block.actions.size(), 2);
}

TEST(DslIntegrationTest, LinkedAgents) {
    const char* source = R"(
import "stdlib/logging"

dot producer:
    state:
        value: 0

    when true:
        do:
            state.value += 1
            produce state.value

dot consumer:
    state:
        received: 0

    when message_available:
        do:
            consume
            state.received += 1

link producer -> consumer
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    auto& module = result.value();
    EXPECT_EQ(module.imports.size(), 1);
    EXPECT_EQ(module.dots.size(), 2);
    EXPECT_EQ(module.links.size(), 1);

    EXPECT_EQ(module.links[0].source, "producer");
    EXPECT_EQ(module.links[0].target, "consumer");
}

TEST(DslIntegrationTest, ConditionalLogic) {
    const char* source = R"(
dot validator:
    state:
        valid: false
        threshold: 50

    when input > threshold and not valid:
        do:
            state.valid = true
            notify "Validation passed"

    when input <= threshold or valid == false:
        do:
            state.valid = false
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    auto& module = result.value();
    ASSERT_EQ(module.dots.size(), 1);
    EXPECT_EQ(module.dots[0].triggers.size(), 2);

    // Check first trigger condition is 'and' expression
    const auto& cond1 = module.dots[0].triggers[0].condition;
    ASSERT_TRUE(std::holds_alternative<BinaryExpr>(cond1->value));
    EXPECT_EQ(std::get<BinaryExpr>(cond1->value).op, BinaryOp::And);

    // Check second trigger condition is 'or' expression
    const auto& cond2 = module.dots[0].triggers[1].condition;
    ASSERT_TRUE(std::holds_alternative<BinaryExpr>(cond2->value));
    EXPECT_EQ(std::get<BinaryExpr>(cond2->value).op, BinaryOp::Or);
}

TEST(DslIntegrationTest, StringInterpolation) {
    const char* source = R"(
dot formatter:
    state:
        name: "World"
        count: 42

    when true:
        do:
            print "Hello, ${state.name}!"
            print "Count is ${state.count}"
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    auto& module = result.value();
    ASSERT_EQ(module.dots.size(), 1);
    ASSERT_EQ(module.dots[0].triggers.size(), 1);

    const auto& actions = module.dots[0].triggers[0].do_block.actions;
    ASSERT_EQ(actions.size(), 2);

    // Both actions should have interpolated string arguments
    ASSERT_EQ(actions[0].arguments.size(), 1);
    EXPECT_TRUE(std::holds_alternative<InterpolatedString>(actions[0].arguments[0]->value));

    ASSERT_EQ(actions[1].arguments.size(), 1);
    EXPECT_TRUE(std::holds_alternative<InterpolatedString>(actions[1].arguments[0]->value));
}

TEST(DslIntegrationTest, MathExpressions) {
    const char* source = R"(
dot calculator:
    state:
        a: 10
        b: 5
        result: 0

    when operation == 1:
        do:
            state.result = a + b

    when operation == 2:
        do:
            state.result = a - b

    when operation == 3:
        do:
            state.result = a * b

    when operation == 4:
        do:
            state.result = a / b
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    auto& module = result.value();
    EXPECT_EQ(module.dots[0].triggers.size(), 4);
}

TEST(DslIntegrationTest, NestedMemberAccess) {
    const char* source = R"(
dot nested:
    state:
        x: 0

    when outer.inner.value > 0:
        do:
            state.x = outer.inner.value
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    auto& module = result.value();
    const auto& cond = module.dots[0].triggers[0].condition;

    // Should be a comparison with nested member access on left
    ASSERT_TRUE(std::holds_alternative<BinaryExpr>(cond->value));
    const auto& bin = std::get<BinaryExpr>(cond->value);
    EXPECT_EQ(bin.op, BinaryOp::Gt);

    // Left side should be member expression
    ASSERT_TRUE(std::holds_alternative<MemberExpr>(bin.left->value));
}

TEST(DslIntegrationTest, ComplexWorkflow) {
    const char* source = R"(
import "core/events"
import "core/state"
import "utils/logging"

dot workflow_manager:
    state:
        phase: 0
        completed: false
        retry_count: 0
        max_retries: 3

    when phase == 0 and not completed:
        do:
            initialize
            state.phase = 1
            log "Workflow started"

    when phase == 1 and input_ready:
        do:
            process_input
            state.phase = 2
            log "Input processed"

    when phase == 2 and validation_passed:
        do:
            finalize
            state.completed = true
            state.phase = 3
            log "Workflow completed"

    when phase == 2 and not validation_passed and retry_count < max_retries:
        do:
            state.retry_count += 1
            state.phase = 1
            log "Retrying validation"

    when retry_count >= max_retries:
        do:
            abort
            log "Max retries exceeded"

dot notifier:
    when event_occurred:
        do:
            send_notification "Event: ${event.type}"
            log "Notification sent for ${event.type}"

link workflow_manager -> notifier
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    auto& module = result.value();
    EXPECT_EQ(module.imports.size(), 3);
    EXPECT_EQ(module.dots.size(), 2);
    EXPECT_EQ(module.links.size(), 1);

    // workflow_manager has 5 triggers
    EXPECT_EQ(module.dots[0].triggers.size(), 5);

    // notifier has 1 trigger
    EXPECT_EQ(module.dots[1].triggers.size(), 1);
}

// ============================================================================
// Error Recovery Tests
// ============================================================================

TEST(DslIntegrationTest, RecoveryAfterError) {
    // Parser should recover and continue parsing after error
    const char* source = R"(
dot agent1:
    state:
        x:
    when true:
        do:
            action

dot agent2:
    state:
        x: 1
)";

    auto result = DslParser::parse(source);
    // Should have errors (missing value after 'x:') but also parse agent2
    EXPECT_TRUE(result.is_err());
    if (result.is_err()) {
        EXPECT_GE(result.error().size(), 1);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(DslIntegrationTest, EmptyModule) {
    auto result = DslParser::parse("");
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().imports.size(), 0);
    EXPECT_EQ(result.value().dots.size(), 0);
    EXPECT_EQ(result.value().links.size(), 0);
}

TEST(DslIntegrationTest, OnlyComments) {
    const char* source = R"(
# This is a comment
# Another comment
# More comments
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().imports.size(), 0);
    EXPECT_EQ(result.value().dots.size(), 0);
}

TEST(DslIntegrationTest, MixedCommentsAndCode) {
    const char* source = R"(
# Header comment
import "module"  # inline comment

# Agent definition
dot test:  # agent name
    state:
        # state variables
        x: 0  # initial value
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().imports.size(), 1);
    EXPECT_EQ(result.value().dots.size(), 1);
}

TEST(DslIntegrationTest, GroupedExpressions) {
    const char* source = R"(
dot test:
    state:
        x: (1 + 2) * 3
)";

    auto result = DslParser::parse(source);
    ASSERT_TRUE(result.is_ok());

    const auto& value = result.value().dots[0].state->variables[0].value;
    // Top level should be multiplication
    ASSERT_TRUE(std::holds_alternative<BinaryExpr>(value->value));
    const auto& mul = std::get<BinaryExpr>(value->value);
    EXPECT_EQ(mul.op, BinaryOp::Mul);

    // Left side should be grouped addition
    ASSERT_TRUE(std::holds_alternative<GroupExpr>(mul.left->value));
}
