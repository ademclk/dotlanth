/// @file policy_engine_test.cpp
/// @brief Unit tests for SEC-009 Policy Engine

#include "dotvm/core/policy/policy_engine.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <thread>

#include "dotvm/core/opcode.hpp"
#include "dotvm/core/policy/evaluation_context.hpp"

namespace dotvm::core::policy {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class PolicyEngineTest : public ::testing::Test {
protected:
    PolicyEngine engine_;
};

// ============================================================================
// Basic Loading Tests
// ============================================================================

TEST_F(PolicyEngineTest, NoPolicyLoaded) {
    EXPECT_FALSE(engine_.has_policy());
    EXPECT_EQ(engine_.rule_count(), 0);
}

TEST_F(PolicyEngineTest, LoadEmptyPolicy) {
    auto result = engine_.load_policy(R"({"rules": [], "default_action": "Allow"})");
    ASSERT_TRUE(result.is_ok()) << result.error().message;

    EXPECT_TRUE(engine_.has_policy());
    EXPECT_EQ(engine_.rule_count(), 0);
}

TEST_F(PolicyEngineTest, LoadSingleRule) {
    auto result = engine_.load_policy(R"({
        "rules": [
            {
                "id": 1,
                "priority": 100,
                "if": {"opcode": "ADD"},
                "then": {"action": "Deny"}
            }
        ],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_ok()) << result.error().message;

    EXPECT_EQ(engine_.rule_count(), 1);
}

TEST_F(PolicyEngineTest, LoadWithMetadata) {
    auto result = engine_.load_policy(R"({
        "name": "Test Policy",
        "version": "1.0.0",
        "rules": [],
        "default_action": "Deny"
    })");
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(engine_.policy_name(), "Test Policy");
    EXPECT_EQ(engine_.policy_version(), "1.0.0");
}

// ============================================================================
// Parsing Error Tests
// ============================================================================

TEST_F(PolicyEngineTest, InvalidJson) {
    auto result = engine_.load_policy("{invalid json}");
    ASSERT_TRUE(result.is_err());
    // JsonSyntaxError because 'i' is unexpected at object key position (expects string or })
    EXPECT_EQ(result.error().error, PolicyError::JsonSyntaxError);
}

TEST_F(PolicyEngineTest, MissingRuleId) {
    auto result = engine_.load_policy(R"({
        "rules": [{"priority": 100, "then": {"action": "Deny"}}],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::MissingField);
}

TEST_F(PolicyEngineTest, InvalidRuleId) {
    auto result = engine_.load_policy(R"({
        "rules": [{"id": -1, "priority": 100, "then": {"action": "Deny"}}],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::InvalidRuleId);
}

TEST_F(PolicyEngineTest, UnknownOpcode) {
    auto result = engine_.load_policy(R"({
        "rules": [{"id": 1, "if": {"opcode": "INVALID_OPCODE"}, "then": {"action": "Deny"}}],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::UnknownOpcode);
}

TEST_F(PolicyEngineTest, InvalidAction) {
    auto result = engine_.load_policy(R"({
        "rules": [{"id": 1, "then": {"action": "Unknown"}}],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::InvalidAction);
}

// ============================================================================
// Evaluation Tests
// ============================================================================

TEST_F(PolicyEngineTest, EvaluateWithoutPolicyReturnsAllow) {
    EvaluationContext ctx = EvaluationContext::for_dot(1);
    PolicyDecision decision = engine_.evaluate(ctx, opcode::ADD);

    EXPECT_EQ(decision.decision, Decision::Allow);
}

TEST_F(PolicyEngineTest, EvaluateDefaultAction) {
    auto result = engine_.load_policy(R"({"rules": [], "default_action": "Deny"})");
    ASSERT_TRUE(result.is_ok());

    EvaluationContext ctx = EvaluationContext::for_dot(1);
    PolicyDecision decision = engine_.evaluate(ctx, opcode::ADD);

    EXPECT_EQ(decision.decision, Decision::Deny);
}

TEST_F(PolicyEngineTest, EvaluateOpcodeRule) {
    auto result = engine_.load_policy(R"({
        "rules": [
            {"id": 1, "priority": 100, "if": {"opcode": "ADD"}, "then": {"action": "Deny"}}
        ],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_ok());

    EvaluationContext ctx = EvaluationContext::for_dot(1);

    PolicyDecision d1 = engine_.evaluate(ctx, opcode::ADD);
    EXPECT_EQ(d1.decision, Decision::Deny);
    EXPECT_EQ(d1.matched_rule_id, 1);

    PolicyDecision d2 = engine_.evaluate(ctx, opcode::SUB);
    EXPECT_EQ(d2.decision, Decision::Allow);
}

TEST_F(PolicyEngineTest, EvaluateKeyPrefixRule) {
    auto result = engine_.load_policy(R"({
        "rules": [
            {
                "id": 1,
                "priority": 100,
                "if": {"opcode": "STORE64", "key_prefix": "/admin"},
                "then": {"action": "Deny", "reason": "Admin access restricted"}
            }
        ],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_ok());

    EvaluationContext ctx = EvaluationContext::for_dot(1);

    PolicyDecision d1 = engine_.evaluate(ctx, opcode::STORE64, "/admin/users");
    EXPECT_EQ(d1.decision, Decision::Deny);
    EXPECT_EQ(d1.audit_reason, "Admin access restricted");

    PolicyDecision d2 = engine_.evaluate(ctx, opcode::STORE64, "/public/data");
    EXPECT_EQ(d2.decision, Decision::Allow);
}

TEST_F(PolicyEngineTest, EvaluateRequireCapability) {
    auto result = engine_.load_policy(R"({
        "rules": [
            {
                "id": 1,
                "priority": 100,
                "if": {"opcode": "ENCRYPT_AES256"},
                "then": {"action": "RequireCapability", "capability": "Crypto"}
            }
        ],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_ok());

    EvaluationContext ctx = EvaluationContext::for_dot(1);
    PolicyDecision decision = engine_.evaluate(ctx, opcode::ENCRYPT_AES256);

    EXPECT_EQ(decision.decision, Decision::RequireCapability);
    EXPECT_EQ(decision.required_capability, "Crypto");
}

TEST_F(PolicyEngineTest, EvaluateCapabilityCondition) {
    auto result = engine_.load_policy(R"({
        "rules": [
            {
                "id": 1,
                "priority": 100,
                "if": {"opcode": "ADD", "capability_has": "Admin"},
                "then": {"action": "Allow"}
            },
            {
                "id": 2,
                "priority": 50,
                "if": {"opcode": "ADD"},
                "then": {"action": "Deny"}
            }
        ],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_ok());

    // With Admin capability - matches rule 1
    EvaluationContext ctx1;
    ctx1.capabilities.insert("Admin");
    EXPECT_EQ(engine_.evaluate(ctx1, opcode::ADD).decision, Decision::Allow);

    // Without Admin - matches rule 2
    EvaluationContext ctx2;
    EXPECT_EQ(engine_.evaluate(ctx2, opcode::ADD).decision, Decision::Deny);
}

// ============================================================================
// Complex Condition Tests
// ============================================================================

TEST_F(PolicyEngineTest, MemoryRegionCondition) {
    auto result = engine_.load_policy(R"({
        "rules": [
            {
                "id": 1,
                "priority": 100,
                "if": {
                    "opcode": "STORE64",
                    "memory_region": {"start": "0x1000", "end": "0x2000"}
                },
                "then": {"action": "Deny"}
            }
        ],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_ok());

    EvaluationContext ctx1;
    ctx1.memory_address = 0x1500;
    EXPECT_EQ(engine_.evaluate(ctx1, opcode::STORE64).decision, Decision::Deny);

    EvaluationContext ctx2;
    ctx2.memory_address = 0x3000;
    EXPECT_EQ(engine_.evaluate(ctx2, opcode::STORE64).decision, Decision::Allow);
}

TEST_F(PolicyEngineTest, TimeWindowCondition) {
    // This test is time-dependent - we just verify parsing works
    auto result = engine_.load_policy(R"({
        "rules": [
            {
                "id": 1,
                "priority": 100,
                "if": {
                    "opcode": "ADD",
                    "time_window": {"after": "09:00", "before": "17:00"}
                },
                "then": {"action": "Allow"}
            }
        ],
        "default_action": "Deny"
    })");
    ASSERT_TRUE(result.is_ok()) << result.error().message;
}

TEST_F(PolicyEngineTest, CallerChainCondition) {
    auto result = engine_.load_policy(R"({
        "rules": [
            {
                "id": 1,
                "priority": 100,
                "if": {
                    "opcode": "CALL",
                    "caller_chain": {"min_depth": 5, "max_depth": 10}
                },
                "then": {"action": "Deny", "reason": "Call depth limit"}
            }
        ],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_ok());

    EvaluationContext ctx1;
    ctx1.call_depth = 7;
    EXPECT_EQ(engine_.evaluate(ctx1, opcode::CALL).decision, Decision::Deny);

    EvaluationContext ctx2;
    ctx2.call_depth = 3;
    EXPECT_EQ(engine_.evaluate(ctx2, opcode::CALL).decision, Decision::Allow);
}

TEST_F(PolicyEngineTest, ResourceUsageCondition) {
    auto result = engine_.load_policy(R"({
        "rules": [
            {
                "id": 1,
                "priority": 100,
                "if": {
                    "opcode": "ADD",
                    "resource_usage": {"memory_above_percent": 90}
                },
                "then": {"action": "Deny", "reason": "Memory pressure"}
            }
        ],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_ok());

    EvaluationContext ctx1;
    ctx1.memory_used = 95;
    ctx1.memory_limit = 100;
    EXPECT_EQ(engine_.evaluate(ctx1, opcode::ADD).decision, Decision::Deny);

    EvaluationContext ctx2;
    ctx2.memory_used = 50;
    ctx2.memory_limit = 100;
    EXPECT_EQ(engine_.evaluate(ctx2, opcode::ADD).decision, Decision::Allow);
}

// ============================================================================
// Reload Tests
// ============================================================================

TEST_F(PolicyEngineTest, ReloadFromString) {
    auto load_result = engine_.load_policy(R"({"rules": [], "default_action": "Allow"})");
    ASSERT_TRUE(load_result.is_ok());

    EvaluationContext ctx = EvaluationContext::for_dot(1);
    EXPECT_EQ(engine_.evaluate(ctx, opcode::ADD).decision, Decision::Allow);

    // Reload same policy
    auto result = engine_.reload_policy();
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(engine_.evaluate(ctx, opcode::ADD).decision, Decision::Allow);
}

TEST_F(PolicyEngineTest, ReloadWithoutSourceFails) {
    // No policy loaded - reload should fail
    auto result = engine_.reload_policy();
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::NoPolicyLoaded);
}

// ============================================================================
// File Loading Tests
// ============================================================================

TEST_F(PolicyEngineTest, LoadNonExistentFileFails) {
    auto result = engine_.load_policy_file("/nonexistent/path/policy.json");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().error, PolicyError::FileNotFound);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(PolicyEngineTest, ConcurrentEvaluation) {
    auto result = engine_.load_policy(R"({
        "rules": [
            {"id": 1, "priority": 100, "if": {"opcode": "ADD"}, "then": {"action": "Deny"}}
        ],
        "default_action": "Allow"
    })");
    ASSERT_TRUE(result.is_ok()) << result.error().message;

    std::atomic<int> deny_count{0};
    std::atomic<int> allow_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([this, &deny_count, &allow_count]() {
            for (int j = 0; j < 1000; ++j) {
                EvaluationContext ctx = EvaluationContext::for_dot(1);
                auto d1 = engine_.evaluate(ctx, opcode::ADD);
                if (d1.decision == Decision::Deny) {
                    ++deny_count;
                }

                auto d2 = engine_.evaluate(ctx, opcode::SUB);
                if (d2.decision == Decision::Allow) {
                    ++allow_count;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(deny_count.load(), 4000);
    EXPECT_EQ(allow_count.load(), 4000);
}

TEST_F(PolicyEngineTest, ConcurrentReloadAndEvaluate) {
    auto load_result = engine_.load_policy(R"({"rules": [], "default_action": "Allow"})");
    ASSERT_TRUE(load_result.is_ok()) << load_result.error().message;

    std::atomic<bool> stop{false};
    std::atomic<int> eval_count{0};

    // Evaluator threads
    std::vector<std::thread> evaluators;
    for (int i = 0; i < 2; ++i) {
        evaluators.emplace_back([this, &stop, &eval_count]() {
            while (!stop.load()) {
                EvaluationContext ctx = EvaluationContext::for_dot(1);
                auto decision = engine_.evaluate(ctx, opcode::ADD);
                // Decision should always be valid (Allow or Deny)
                EXPECT_TRUE(decision.decision == Decision::Allow ||
                            decision.decision == Decision::Deny);
                ++eval_count;
            }
        });
    }

    // Reloader thread
    std::thread reloader([this, &stop]() {
        for (int i = 0; i < 10 && !stop.load(); ++i) {
            auto result = engine_.reload_policy();
            EXPECT_TRUE(result.is_ok());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    reloader.join();
    stop.store(true);

    for (auto& t : evaluators) {
        t.join();
    }

    EXPECT_GT(eval_count.load(), 0);
}

// ============================================================================
// Full Policy Example
// ============================================================================

TEST_F(PolicyEngineTest, FullPolicyExample) {
    auto result = engine_.load_policy(R"({
        "name": "DotVM Security Policy",
        "version": "1.0.0",
        "rules": [
            {
                "id": 1,
                "priority": 1000,
                "if": {
                    "opcode": "STORE64",
                    "key_prefix": "/system"
                },
                "then": {
                    "action": "RequireCapability",
                    "capability": "SystemWrite"
                }
            },
            {
                "id": 2,
                "priority": 900,
                "if": {
                    "opcode": "ENCRYPT_AES256"
                },
                "then": {
                    "action": "RequireCapability",
                    "capability": "Crypto"
                }
            },
            {
                "id": 3,
                "priority": 800,
                "if": {
                    "opcode": "CALL",
                    "caller_chain": {"max_depth": 100}
                },
                "then": {
                    "action": "Deny",
                    "reason": "Call depth exceeded"
                }
            },
            {
                "id": 4,
                "priority": 100,
                "if": {
                    "resource_usage": {"memory_above_percent": 95}
                },
                "then": {
                    "action": "Deny",
                    "reason": "Memory pressure"
                }
            }
        ],
        "default_action": "Allow"
    })");

    ASSERT_TRUE(result.is_ok()) << result.error().message;
    EXPECT_EQ(engine_.policy_name(), "DotVM Security Policy");
    EXPECT_EQ(engine_.policy_version(), "1.0.0");
    EXPECT_EQ(engine_.rule_count(), 4);
}

}  // namespace
}  // namespace dotvm::core::policy
