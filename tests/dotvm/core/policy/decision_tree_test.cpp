/// @file decision_tree_test.cpp
/// @brief Unit tests for SEC-009 Decision Tree

#include "dotvm/core/policy/decision_tree.hpp"

#include <gtest/gtest.h>

#include "dotvm/core/opcode.hpp"
#include "dotvm/core/policy/evaluation_context.hpp"

namespace dotvm::core::policy {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class DecisionTreeTest : public ::testing::Test {
protected:
    DecisionTree tree_;

    Rule make_rule(std::uint64_t id, std::int32_t priority, Decision decision) {
        Rule rule;
        rule.id = id;
        rule.priority = priority;
        rule.action.decision = decision;
        return rule;
    }

    Rule make_opcode_rule(std::uint64_t id, std::int32_t priority, std::uint8_t opcode,
                          Decision decision) {
        Rule rule = make_rule(id, priority, decision);
        OpcodeCondition cond;
        cond.opcode = opcode;
        rule.conditions.push_back(cond);
        return rule;
    }

    Rule make_prefix_rule(std::uint64_t id, std::int32_t priority, std::uint8_t opcode,
                          const std::string& prefix, Decision decision) {
        Rule rule = make_opcode_rule(id, priority, opcode, decision);
        KeyPrefixCondition cond;
        cond.prefix = prefix;
        rule.conditions.push_back(cond);
        return rule;
    }
};

// ============================================================================
// Basic Compilation Tests
// ============================================================================

TEST_F(DecisionTreeTest, CompileEmpty) {
    std::vector<Rule> rules;
    tree_.compile(rules, Decision::Allow);

    EXPECT_EQ(tree_.default_action(), Decision::Allow);
    EXPECT_EQ(tree_.rule_count(), 0);
}

TEST_F(DecisionTreeTest, CompileSingleRule) {
    std::vector<Rule> rules;
    rules.push_back(make_opcode_rule(1, 100, opcode::ADD, Decision::Deny));
    tree_.compile(rules, Decision::Allow);

    EXPECT_EQ(tree_.rule_count(), 1);
}

TEST_F(DecisionTreeTest, CompileMultipleRules) {
    std::vector<Rule> rules;
    rules.push_back(make_opcode_rule(1, 100, opcode::ADD, Decision::Deny));
    rules.push_back(make_opcode_rule(2, 200, opcode::SUB, Decision::Audit));
    rules.push_back(make_opcode_rule(3, 50, opcode::MUL, Decision::Allow));
    tree_.compile(rules, Decision::Deny);

    EXPECT_EQ(tree_.rule_count(), 3);
    EXPECT_EQ(tree_.default_action(), Decision::Deny);
}

// ============================================================================
// Basic Evaluation Tests
// ============================================================================

TEST_F(DecisionTreeTest, EvaluateNoRulesReturnsDefault) {
    tree_.compile({}, Decision::Allow);

    EvaluationContext ctx = EvaluationContext::for_dot(1);
    PolicyDecision decision = tree_.evaluate(ctx, opcode::ADD);

    EXPECT_EQ(decision.decision, Decision::Allow);
    EXPECT_EQ(decision.matched_rule_id, 0);
}

TEST_F(DecisionTreeTest, EvaluateMatchesOpcodeRule) {
    std::vector<Rule> rules;
    rules.push_back(make_opcode_rule(1, 100, opcode::ADD, Decision::Deny));
    tree_.compile(rules, Decision::Allow);

    EvaluationContext ctx = EvaluationContext::for_dot(1);

    // Should match
    PolicyDecision d1 = tree_.evaluate(ctx, opcode::ADD);
    EXPECT_EQ(d1.decision, Decision::Deny);
    EXPECT_EQ(d1.matched_rule_id, 1);

    // Should not match
    PolicyDecision d2 = tree_.evaluate(ctx, opcode::SUB);
    EXPECT_EQ(d2.decision, Decision::Allow);
    EXPECT_EQ(d2.matched_rule_id, 0);
}

// ============================================================================
// Priority Tests
// ============================================================================

TEST_F(DecisionTreeTest, HigherPriorityWins) {
    std::vector<Rule> rules;
    rules.push_back(make_opcode_rule(1, 100, opcode::ADD, Decision::Deny));
    rules.push_back(make_opcode_rule(2, 200, opcode::ADD, Decision::Allow));
    tree_.compile(rules, Decision::Deny);

    EvaluationContext ctx = EvaluationContext::for_dot(1);
    PolicyDecision decision = tree_.evaluate(ctx, opcode::ADD);

    EXPECT_EQ(decision.decision, Decision::Allow);
    EXPECT_EQ(decision.matched_rule_id, 2);
}

TEST_F(DecisionTreeTest, PriorityOrderIndependentOfInsertOrder) {
    // Insert in reverse priority order
    std::vector<Rule> rules;
    rules.push_back(make_opcode_rule(3, 50, opcode::ADD, Decision::Audit));
    rules.push_back(make_opcode_rule(1, 200, opcode::ADD, Decision::Deny));
    rules.push_back(make_opcode_rule(2, 100, opcode::ADD, Decision::Allow));
    tree_.compile(rules, Decision::Allow);

    EvaluationContext ctx = EvaluationContext::for_dot(1);
    PolicyDecision decision = tree_.evaluate(ctx, opcode::ADD);

    EXPECT_EQ(decision.decision, Decision::Deny);
    EXPECT_EQ(decision.matched_rule_id, 1);
}

// ============================================================================
// Key Prefix Tests
// ============================================================================

TEST_F(DecisionTreeTest, KeyPrefixMatching) {
    std::vector<Rule> rules;
    rules.push_back(make_prefix_rule(1, 100, opcode::STORE64, "/admin", Decision::Deny));
    tree_.compile(rules, Decision::Allow);

    EvaluationContext ctx = EvaluationContext::for_dot(1);

    // Should match
    PolicyDecision d1 = tree_.evaluate(ctx, opcode::STORE64, "/admin/users");
    EXPECT_EQ(d1.decision, Decision::Deny);
    EXPECT_EQ(d1.matched_rule_id, 1);

    // Should match exact prefix
    PolicyDecision d2 = tree_.evaluate(ctx, opcode::STORE64, "/admin");
    EXPECT_EQ(d2.decision, Decision::Deny);

    // Should not match
    PolicyDecision d3 = tree_.evaluate(ctx, opcode::STORE64, "/user/data");
    EXPECT_EQ(d3.decision, Decision::Allow);

    // Wrong opcode
    PolicyDecision d4 = tree_.evaluate(ctx, opcode::LOAD64, "/admin/users");
    EXPECT_EQ(d4.decision, Decision::Allow);
}

TEST_F(DecisionTreeTest, LongerPrefixMoreSpecific) {
    std::vector<Rule> rules;
    rules.push_back(make_prefix_rule(1, 100, opcode::STORE64, "/data", Decision::Audit));
    rules.push_back(make_prefix_rule(2, 100, opcode::STORE64, "/data/sensitive", Decision::Deny));
    tree_.compile(rules, Decision::Allow);

    EvaluationContext ctx = EvaluationContext::for_dot(1);

    // More specific prefix should still match
    PolicyDecision d1 = tree_.evaluate(ctx, opcode::STORE64, "/data/sensitive/secret");
    EXPECT_EQ(d1.decision, Decision::Deny);

    // General prefix
    PolicyDecision d2 = tree_.evaluate(ctx, opcode::STORE64, "/data/public");
    EXPECT_EQ(d2.decision, Decision::Audit);
}

// ============================================================================
// Condition Matching Tests
// ============================================================================

TEST_F(DecisionTreeTest, DotIdCondition) {
    Rule rule = make_opcode_rule(1, 100, opcode::ADD, Decision::Deny);
    DotIdCondition cond;
    cond.dot_id = 42;
    rule.conditions.push_back(cond);

    tree_.compile({rule}, Decision::Allow);

    EvaluationContext ctx1 = EvaluationContext::for_dot(42);
    EXPECT_EQ(tree_.evaluate(ctx1, opcode::ADD).decision, Decision::Deny);

    EvaluationContext ctx2 = EvaluationContext::for_dot(99);
    EXPECT_EQ(tree_.evaluate(ctx2, opcode::ADD).decision, Decision::Allow);
}

TEST_F(DecisionTreeTest, CapabilityCondition) {
    Rule rule = make_opcode_rule(1, 100, opcode::ADD, Decision::Deny);
    CapabilityCondition cond;
    cond.capability_name = "Admin";
    rule.conditions.push_back(cond);

    tree_.compile({rule}, Decision::Allow);

    EvaluationContext ctx1;
    ctx1.capabilities.insert("Admin");
    EXPECT_EQ(tree_.evaluate(ctx1, opcode::ADD).decision, Decision::Deny);

    EvaluationContext ctx2;
    ctx2.capabilities.insert("User");
    EXPECT_EQ(tree_.evaluate(ctx2, opcode::ADD).decision, Decision::Allow);
}

TEST_F(DecisionTreeTest, MemoryRegionCondition) {
    Rule rule = make_opcode_rule(1, 100, opcode::STORE64, Decision::Deny);
    MemoryRegionCondition cond;
    cond.start = 0x1000;
    cond.end = 0x2000;
    rule.conditions.push_back(cond);

    tree_.compile({rule}, Decision::Allow);

    EvaluationContext ctx1;
    ctx1.memory_address = 0x1500;
    EXPECT_EQ(tree_.evaluate(ctx1, opcode::STORE64).decision, Decision::Deny);

    EvaluationContext ctx2;
    ctx2.memory_address = 0x2500;
    EXPECT_EQ(tree_.evaluate(ctx2, opcode::STORE64).decision, Decision::Allow);

    // Boundary: start is inclusive
    EvaluationContext ctx3;
    ctx3.memory_address = 0x1000;
    EXPECT_EQ(tree_.evaluate(ctx3, opcode::STORE64).decision, Decision::Deny);

    // Boundary: end is exclusive
    EvaluationContext ctx4;
    ctx4.memory_address = 0x2000;
    EXPECT_EQ(tree_.evaluate(ctx4, opcode::STORE64).decision, Decision::Allow);
}

TEST_F(DecisionTreeTest, CallerChainCondition) {
    Rule rule = make_opcode_rule(1, 100, opcode::CALL, Decision::Deny);
    CallerChainCondition cond;
    cond.min_depth = 2;
    cond.max_depth = 5;
    rule.conditions.push_back(cond);

    tree_.compile({rule}, Decision::Allow);

    EvaluationContext ctx1;
    ctx1.call_depth = 3;
    EXPECT_EQ(tree_.evaluate(ctx1, opcode::CALL).decision, Decision::Deny);

    EvaluationContext ctx2;
    ctx2.call_depth = 1;
    EXPECT_EQ(tree_.evaluate(ctx2, opcode::CALL).decision, Decision::Allow);

    EvaluationContext ctx3;
    ctx3.call_depth = 6;
    EXPECT_EQ(tree_.evaluate(ctx3, opcode::CALL).decision, Decision::Allow);
}

TEST_F(DecisionTreeTest, ResourceUsageCondition) {
    Rule rule = make_opcode_rule(1, 100, opcode::ADD, Decision::Deny);
    ResourceUsageCondition cond;
    cond.memory_above_percent = 80;
    rule.conditions.push_back(cond);

    tree_.compile({rule}, Decision::Allow);

    EvaluationContext ctx1;
    ctx1.memory_used = 90;
    ctx1.memory_limit = 100;
    EXPECT_EQ(tree_.evaluate(ctx1, opcode::ADD).decision, Decision::Deny);

    EvaluationContext ctx2;
    ctx2.memory_used = 70;
    ctx2.memory_limit = 100;
    EXPECT_EQ(tree_.evaluate(ctx2, opcode::ADD).decision, Decision::Allow);
}

// ============================================================================
// Multiple Condition Tests
// ============================================================================

TEST_F(DecisionTreeTest, AllConditionsMustMatch) {
    Rule rule;
    rule.id = 1;
    rule.priority = 100;
    rule.action.decision = Decision::Deny;

    OpcodeCondition oc;
    oc.opcode = opcode::ADD;
    rule.conditions.push_back(oc);

    DotIdCondition dc;
    dc.dot_id = 42;
    rule.conditions.push_back(dc);

    tree_.compile({rule}, Decision::Allow);

    // Both conditions match
    EvaluationContext ctx1 = EvaluationContext::for_dot(42);
    EXPECT_EQ(tree_.evaluate(ctx1, opcode::ADD).decision, Decision::Deny);

    // Opcode matches, dot_id doesn't
    EvaluationContext ctx2 = EvaluationContext::for_dot(99);
    EXPECT_EQ(tree_.evaluate(ctx2, opcode::ADD).decision, Decision::Allow);

    // Dot_id matches, opcode doesn't
    EvaluationContext ctx3 = EvaluationContext::for_dot(42);
    EXPECT_EQ(tree_.evaluate(ctx3, opcode::SUB).decision, Decision::Allow);
}

// ============================================================================
// Global Rules Tests
// ============================================================================

TEST_F(DecisionTreeTest, GlobalRulesMatchAnyOpcode) {
    Rule rule;
    rule.id = 1;
    rule.priority = 100;
    rule.action.decision = Decision::Audit;
    // No opcode condition - global rule

    DotIdCondition cond;
    cond.dot_id = 42;
    rule.conditions.push_back(cond);

    tree_.compile({rule}, Decision::Allow);

    EvaluationContext ctx = EvaluationContext::for_dot(42);

    EXPECT_EQ(tree_.evaluate(ctx, opcode::ADD).decision, Decision::Audit);
    EXPECT_EQ(tree_.evaluate(ctx, opcode::SUB).decision, Decision::Audit);
    EXPECT_EQ(tree_.evaluate(ctx, opcode::CALL).decision, Decision::Audit);
}

}  // namespace
}  // namespace dotvm::core::policy
