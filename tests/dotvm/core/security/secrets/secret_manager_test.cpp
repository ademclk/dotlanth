/// @file secret_manager_test.cpp
/// @brief Unit tests for SEC-008 Phase 3: SecretManager with multi-vault support

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/security/secrets/secret_manager.hpp"
#include "dotvm/core/security/secrets/secrets_vault.hpp"
#include "dotvm/core/security/security_context.hpp"

namespace dotvm::core::security::secrets {
namespace {

// ============================================================================
// MockVault for Testing
// ============================================================================

/// @brief Mock vault implementation for testing SecretManager
class MockVault : public SecretsVault {
public:
    explicit MockVault(std::string id) : id_(std::move(id)) {}

    void add_secret(std::string name, std::string value) {
        secrets_[std::move(name)] = std::move(value);
    }

    [[nodiscard]] std::expected<Secret, SecretsError> get(std::string_view name) const override {
        if (name.empty()) {
            return std::unexpected(SecretsError::InvalidName);
        }
        auto it = secrets_.find(std::string(name));
        if (it == secrets_.end()) {
            return std::unexpected(SecretsError::NotFound);
        }
        return Secret(std::string_view{it->second});
    }

    [[nodiscard]] bool exists(std::string_view name) const override {
        return secrets_.contains(std::string(name));
    }

    [[nodiscard]] std::vector<std::string> list_names() const override {
        std::vector<std::string> names;
        names.reserve(secrets_.size());
        for (const auto& [key, value] : secrets_) {
            names.push_back(key);
        }
        return names;
    }

    [[nodiscard]] std::string_view provider_id() const noexcept override { return id_; }

private:
    std::string id_;
    std::unordered_map<std::string, std::string> secrets_;
};

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Helper to convert Secret view to string for testing
[[nodiscard]] std::string view_to_string(const Secret& secret) {
    auto view = secret.view();
    return std::string(view.data(), view.size());
}

// ============================================================================
// SecretManager Single Vault Tests
// ============================================================================

TEST(SecretManager, GetSecretFromSingleVault) {
    SecretManager manager;

    auto vault = std::make_unique<MockVault>("test-vault");
    vault->add_secret("API_KEY", "sk-test-12345");
    manager.add_vault(std::move(vault));

    auto result = manager.get_secret("API_KEY");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(view_to_string(*result), "sk-test-12345");
}

// ============================================================================
// SecretManager Multi-Vault Tests
// ============================================================================

TEST(SecretManager, MultiVaultPriorityOrder) {
    SecretManager manager;

    // First vault (highest priority) has API_KEY
    auto vault1 = std::make_unique<MockVault>("primary");
    vault1->add_secret("API_KEY", "primary-key");
    manager.add_vault(std::move(vault1));

    // Second vault also has API_KEY but with different value
    auto vault2 = std::make_unique<MockVault>("secondary");
    vault2->add_secret("API_KEY", "secondary-key");
    manager.add_vault(std::move(vault2));

    // Should return from first vault (highest priority)
    auto result = manager.get_secret("API_KEY");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(view_to_string(*result), "primary-key");
}

TEST(SecretManager, SecretExistsAcrossVaults) {
    SecretManager manager;

    auto vault1 = std::make_unique<MockVault>("vault1");
    vault1->add_secret("KEY_A", "value_a");
    manager.add_vault(std::move(vault1));

    auto vault2 = std::make_unique<MockVault>("vault2");
    vault2->add_secret("KEY_B", "value_b");
    manager.add_vault(std::move(vault2));

    // Should find secrets in both vaults
    EXPECT_TRUE(manager.secret_exists("KEY_A"));
    EXPECT_TRUE(manager.secret_exists("KEY_B"));
    EXPECT_FALSE(manager.secret_exists("NONEXISTENT"));
}

// ============================================================================
// SecretManager Redaction Tests
// ============================================================================

TEST(SecretManager, RedactionRegistration) {
    SecretManager manager;

    manager.register_for_redaction("my-secret-value");

    // After registration, redact should work
    std::string message = "The secret is my-secret-value here";
    std::string redacted = manager.redact(message);

    EXPECT_EQ(redacted, "The secret is [REDACTED] here");
}

TEST(SecretManager, RedactReplacesSecretValues) {
    SecretManager manager;

    auto vault = std::make_unique<MockVault>("test");
    vault->add_secret("PASSWORD", "super-secret-123");
    manager.add_vault(std::move(vault));

    // Getting the secret should auto-register it for redaction
    auto result = manager.get_secret("PASSWORD");
    ASSERT_TRUE(result.has_value());

    // Now redact a message containing the secret
    std::string message = "Connection using password: super-secret-123";
    std::string redacted = manager.redact(message);

    EXPECT_EQ(redacted, "Connection using password: [REDACTED]");
}

TEST(SecretManager, RedactMultipleOccurrences) {
    SecretManager manager;

    manager.register_for_redaction("secret");

    std::string message = "First secret and another secret in the text";
    std::string redacted = manager.redact(message);

    EXPECT_EQ(redacted, "First [REDACTED] and another [REDACTED] in the text");
}

TEST(SecretManager, RedactPreservesNonSecretText) {
    SecretManager manager;

    manager.register_for_redaction("hidden");

    std::string message = "This message has no secrets to hide";
    std::string redacted = manager.redact(message);

    // Message should be unchanged when no patterns match
    EXPECT_EQ(redacted, "This message has no secrets to hide");
}

// ============================================================================
// SecretManager Thread Safety Tests
// ============================================================================

TEST(SecretManager, ThreadSafeRedaction) {
    SecretManager manager;

    // Register multiple secrets
    manager.register_for_redaction("secret1");
    manager.register_for_redaction("secret2");
    manager.register_for_redaction("secret3");

    constexpr int num_threads = 8;
    constexpr int iterations_per_thread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    // Launch threads that concurrently redact messages
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&manager, &success_count, t]() {
            for (int i = 0; i < iterations_per_thread; ++i) {
                std::string message;
                if (t % 3 == 0) {
                    message = "Contains secret1 here";
                } else if (t % 3 == 1) {
                    message = "Contains secret2 here";
                } else {
                    message = "Contains secret3 here";
                }

                std::string redacted = manager.redact(message);

                // Verify redaction worked
                if (redacted.find("[REDACTED]") != std::string::npos) {
                    ++success_count;
                }
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // All redactions should have succeeded
    EXPECT_EQ(success_count.load(), num_threads * iterations_per_thread);
}

// ============================================================================
// SecretManager Error Handling Tests
// ============================================================================

TEST(SecretManager, GetSecretNotFoundInAnyVault) {
    SecretManager manager;

    auto vault1 = std::make_unique<MockVault>("vault1");
    vault1->add_secret("KEY_A", "value_a");
    manager.add_vault(std::move(vault1));

    auto vault2 = std::make_unique<MockVault>("vault2");
    vault2->add_secret("KEY_B", "value_b");
    manager.add_vault(std::move(vault2));

    // Secret not in any vault
    auto result = manager.get_secret("NONEXISTENT");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), SecretsError::NotFound);
}

// ============================================================================
// SecretManager Vault Count Tests
// ============================================================================

TEST(SecretManager, AddVaultIncreasesCount) {
    SecretManager manager;

    EXPECT_EQ(manager.vault_count(), 0);

    manager.add_vault(std::make_unique<MockVault>("vault1"));
    EXPECT_EQ(manager.vault_count(), 1);

    manager.add_vault(std::make_unique<MockVault>("vault2"));
    EXPECT_EQ(manager.vault_count(), 2);

    manager.add_vault(std::make_unique<MockVault>("vault3"));
    EXPECT_EQ(manager.vault_count(), 3);
}

// ============================================================================
// SecretManager Edge Cases
// ============================================================================

TEST(SecretManager, GetSecretFromEmptyManager) {
    SecretManager manager;

    auto result = manager.get_secret("ANY_KEY");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), SecretsError::NotFound);
}

TEST(SecretManager, SecretExistsInEmptyManagerReturnsFalse) {
    SecretManager manager;

    EXPECT_FALSE(manager.secret_exists("ANY_KEY"));
}

TEST(SecretManager, RedactEmptyMessage) {
    SecretManager manager;
    manager.register_for_redaction("secret");

    std::string empty;
    std::string redacted = manager.redact(empty);

    EXPECT_TRUE(redacted.empty());
}

TEST(SecretManager, RedactWithNoPatterns) {
    SecretManager manager;

    std::string message = "Nothing to redact here";
    std::string redacted = manager.redact(message);

    EXPECT_EQ(redacted, message);
}

TEST(SecretManager, MultipleSecretsAutoRegistered) {
    SecretManager manager;

    auto vault = std::make_unique<MockVault>("test");
    vault->add_secret("SECRET_A", "value-a");
    vault->add_secret("SECRET_B", "value-b");
    manager.add_vault(std::move(vault));

    // Get both secrets - should auto-register for redaction
    auto result_a = manager.get_secret("SECRET_A");
    auto result_b = manager.get_secret("SECRET_B");
    ASSERT_TRUE(result_a.has_value());
    ASSERT_TRUE(result_b.has_value());

    // Message containing both secrets should have both redacted
    std::string message = "First: value-a, Second: value-b";
    std::string redacted = manager.redact(message);

    EXPECT_EQ(redacted, "First: [REDACTED], Second: [REDACTED]");
}

// ============================================================================
// SecretManager Move Semantics
// ============================================================================

TEST(SecretManager, MoveConstruction) {
    SecretManager manager1;

    auto vault = std::make_unique<MockVault>("test");
    vault->add_secret("KEY", "value");
    manager1.add_vault(std::move(vault));
    manager1.register_for_redaction("pattern");

    // Move construct
    SecretManager manager2(std::move(manager1));

    // manager2 should have the vault and redaction patterns
    EXPECT_EQ(manager2.vault_count(), 1);
    auto result = manager2.get_secret("KEY");
    ASSERT_TRUE(result.has_value());

    std::string redacted = manager2.redact("test pattern here");
    EXPECT_EQ(redacted, "test [REDACTED] here");
}

TEST(SecretManager, MoveAssignment) {
    SecretManager manager1;
    SecretManager manager2;

    auto vault = std::make_unique<MockVault>("test");
    vault->add_secret("KEY", "value");
    manager1.add_vault(std::move(vault));

    // Move assign
    manager2 = std::move(manager1);

    EXPECT_EQ(manager2.vault_count(), 1);
    auto result = manager2.get_secret("KEY");
    ASSERT_TRUE(result.has_value());
}

// ============================================================================
// SecretManager Audit Integration Tests (SEC-008 Phase 4)
// ============================================================================

TEST(SecretManager, LogsSecretAccessedOnSuccess) {
    SecretManager manager;

    // Set up a vault with a secret
    auto vault = std::make_unique<MockVault>("test-vault");
    vault->add_secret("API_KEY", "sk-test-12345");
    manager.add_vault(std::move(vault));

    // Set up audit logger (legacy BufferedAuditLogger)
    security::BufferedAuditLogger logger(100);
    manager.set_audit_logger(&logger);

    // Get the secret - should log SecretAccessed
    auto result = manager.get_secret("API_KEY");
    ASSERT_TRUE(result.has_value());

    // Verify audit log
    auto events = logger.events();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, security::AuditEventType::SecretAccessed);
    // Legacy AuditEvent uses context field for secret name
    EXPECT_EQ(events[0].context, "API_KEY");
}

TEST(SecretManager, LogsSecretNotFoundOnFailure) {
    SecretManager manager;

    // Set up an empty vault
    auto vault = std::make_unique<MockVault>("test-vault");
    manager.add_vault(std::move(vault));

    // Set up audit logger (legacy BufferedAuditLogger)
    security::BufferedAuditLogger logger(100);
    manager.set_audit_logger(&logger);

    // Try to get non-existent secret - should log SecretNotFound
    auto result = manager.get_secret("NONEXISTENT");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), SecretsError::NotFound);

    // Verify audit log
    auto events = logger.events();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].type, security::AuditEventType::SecretNotFound);
    // Legacy AuditEvent uses context field for secret name
    EXPECT_EQ(events[0].context, "NONEXISTENT");
}

TEST(SecretManager, NoLoggingWhenAuditLoggerNull) {
    SecretManager manager;

    // Set up a vault with a secret
    auto vault = std::make_unique<MockVault>("test-vault");
    vault->add_secret("API_KEY", "sk-test-12345");
    manager.add_vault(std::move(vault));

    // No audit logger set (nullptr by default)
    EXPECT_EQ(manager.audit_logger(), nullptr);

    // Get the secret - should NOT crash without logger
    auto result = manager.get_secret("API_KEY");
    ASSERT_TRUE(result.has_value());

    // Get non-existent secret - should also NOT crash
    auto not_found = manager.get_secret("NONEXISTENT");
    EXPECT_FALSE(not_found.has_value());
}

TEST(SecretManager, SetAndGetAuditLogger) {
    SecretManager manager;

    // Initially null
    EXPECT_EQ(manager.audit_logger(), nullptr);

    // Set logger
    security::BufferedAuditLogger logger(100);
    manager.set_audit_logger(&logger);
    EXPECT_EQ(manager.audit_logger(), &logger);

    // Can reset to null
    manager.set_audit_logger(nullptr);
    EXPECT_EQ(manager.audit_logger(), nullptr);
}

TEST(SecretManager, AuditLogsMultipleAccesses) {
    SecretManager manager;

    // Set up vault with multiple secrets
    auto vault = std::make_unique<MockVault>("test-vault");
    vault->add_secret("KEY_A", "value-a");
    vault->add_secret("KEY_B", "value-b");
    manager.add_vault(std::move(vault));

    // Set up audit logger (legacy BufferedAuditLogger)
    security::BufferedAuditLogger logger(100);
    manager.set_audit_logger(&logger);

    // Access multiple secrets
    auto result_a = manager.get_secret("KEY_A");
    auto result_b = manager.get_secret("KEY_B");
    auto result_c = manager.get_secret("KEY_C");  // Not found

    EXPECT_TRUE(result_a.has_value());
    EXPECT_TRUE(result_b.has_value());
    EXPECT_FALSE(result_c.has_value());

    // Verify audit log contains all events
    auto events = logger.events();
    ASSERT_EQ(events.size(), 3);

    // First two should be SecretAccessed
    EXPECT_EQ(events[0].type, security::AuditEventType::SecretAccessed);
    EXPECT_EQ(events[1].type, security::AuditEventType::SecretAccessed);

    // Last should be SecretNotFound
    EXPECT_EQ(events[2].type, security::AuditEventType::SecretNotFound);
}

// ============================================================================
// Type Traits
// ============================================================================

static_assert(!std::is_copy_constructible_v<SecretManager>,
              "SecretManager should not be copy constructible");
static_assert(!std::is_copy_assignable_v<SecretManager>,
              "SecretManager should not be copy assignable");
static_assert(std::is_move_constructible_v<SecretManager>,
              "SecretManager should be move constructible");
static_assert(std::is_move_assignable_v<SecretManager>, "SecretManager should be move assignable");

}  // namespace
}  // namespace dotvm::core::security::secrets
