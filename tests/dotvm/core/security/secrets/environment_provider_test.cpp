/// @file environment_provider_test.cpp
/// @brief Unit tests for SEC-008 EnvironmentProvider

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/security/secrets/environment_provider.hpp"
#include "dotvm/core/security/secrets/secrets_vault.hpp"

namespace dotvm::core::security::secrets {
namespace {

// ============================================================================
// Test Fixture with Environment Variable Setup/Teardown
// ============================================================================

class EnvironmentProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test environment variables
        setenv("DOTVM_TEST_API_KEY", "sk-test-12345", 1);
        setenv("DOTVM_TEST_SECRET", "super-secret-value", 1);
        setenv("DOTVM_TEST_EMPTY", "", 1);
        setenv("TEST_NO_PREFIX", "value-without-prefix", 1);
    }

    void TearDown() override {
        // Clean up test environment variables
        unsetenv("DOTVM_TEST_API_KEY");
        unsetenv("DOTVM_TEST_SECRET");
        unsetenv("DOTVM_TEST_EMPTY");
        unsetenv("TEST_NO_PREFIX");
    }
};

// ============================================================================
// SecretsError Tests
// ============================================================================

TEST(SecretsErrorTest, ToStringAllValues) {
    EXPECT_EQ(to_string(SecretsError::Success), "Success");
    EXPECT_EQ(to_string(SecretsError::NotFound), "NotFound");
    EXPECT_EQ(to_string(SecretsError::AccessDenied), "AccessDenied");
    EXPECT_EQ(to_string(SecretsError::ConnectionFailed), "ConnectionFailed");
    EXPECT_EQ(to_string(SecretsError::InvalidName), "InvalidName");
    EXPECT_EQ(to_string(SecretsError::Expired), "Expired");
}

// ============================================================================
// EnvironmentProvider Basic Tests
// ============================================================================

/// @brief Helper to convert Secret view to string for testing
[[nodiscard]] std::string view_to_string(const Secret& secret) {
    auto view = secret.view();
    return std::string(view.data(), view.size());
}

TEST_F(EnvironmentProviderTest, GetExistingSecret) {
    EnvironmentProvider provider("DOTVM_TEST_");

    auto result = provider.get("API_KEY");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(view_to_string(*result), "sk-test-12345");
}

TEST_F(EnvironmentProviderTest, GetMissingSecretReturnsNotFound) {
    EnvironmentProvider provider("DOTVM_TEST_");

    auto result = provider.get("NONEXISTENT");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), SecretsError::NotFound);
}

TEST_F(EnvironmentProviderTest, PrefixIsApplied) {
    EnvironmentProvider provider("DOTVM_TEST_");

    // Should find DOTVM_TEST_SECRET via logical name "SECRET"
    auto result = provider.get("SECRET");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(view_to_string(*result), "super-secret-value");
}

TEST_F(EnvironmentProviderTest, NoPrefixWorks) {
    EnvironmentProvider provider("");

    auto result = provider.get("TEST_NO_PREFIX");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(view_to_string(*result), "value-without-prefix");
}

// ============================================================================
// EnvironmentProvider Exists Tests
// ============================================================================

TEST_F(EnvironmentProviderTest, ExistsCheckWorks) {
    EnvironmentProvider provider("DOTVM_TEST_");

    EXPECT_TRUE(provider.exists("API_KEY"));
    EXPECT_TRUE(provider.exists("SECRET"));
    EXPECT_FALSE(provider.exists("NONEXISTENT"));
}

TEST_F(EnvironmentProviderTest, ExistsReturnsTrueForEmptyValue) {
    EnvironmentProvider provider("DOTVM_TEST_");

    // DOTVM_TEST_EMPTY exists but has empty value
    EXPECT_TRUE(provider.exists("EMPTY"));
}

// ============================================================================
// EnvironmentProvider ListNames Tests
// ============================================================================

TEST_F(EnvironmentProviderTest, ListNamesReturnsLogicalNames) {
    std::vector<std::string> required_names = {"API_KEY", "SECRET"};
    EnvironmentProvider provider("DOTVM_TEST_", required_names);

    auto names = provider.list_names();
    EXPECT_EQ(names.size(), 2);

    // Should contain the logical names (without prefix)
    EXPECT_NE(std::find(names.begin(), names.end(), "API_KEY"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "SECRET"), names.end());
}

TEST_F(EnvironmentProviderTest, ListNamesEmptyWhenNoRequiredNames) {
    EnvironmentProvider provider("DOTVM_TEST_");

    auto names = provider.list_names();
    EXPECT_TRUE(names.empty());
}

// ============================================================================
// EnvironmentProvider Required Names Validation Tests
// ============================================================================

TEST_F(EnvironmentProviderTest, RequiredNamesValidatedAtConstruction) {
    // Missing required secret should throw
    std::vector<std::string> required = {"API_KEY", "MISSING_SECRET"};

    EXPECT_THROW({ EnvironmentProvider provider("DOTVM_TEST_", required); }, std::runtime_error);
}

TEST_F(EnvironmentProviderTest, AllRequiredNamesPresentSucceeds) {
    std::vector<std::string> required = {"API_KEY", "SECRET"};

    // Should not throw
    EXPECT_NO_THROW({ EnvironmentProvider provider("DOTVM_TEST_", required); });
}

// ============================================================================
// EnvironmentProvider ProviderId Tests
// ============================================================================

TEST_F(EnvironmentProviderTest, ProviderIdReturnsEnvironment) {
    EnvironmentProvider provider("DOTVM_");

    EXPECT_EQ(provider.provider_id(), "environment");
}

// ============================================================================
// EnvironmentProvider Edge Cases
// ============================================================================

TEST_F(EnvironmentProviderTest, GetEmptyValueReturnsEmptySecret) {
    EnvironmentProvider provider("DOTVM_TEST_");

    auto result = provider.get("EMPTY");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST_F(EnvironmentProviderTest, InvalidNameReturnsError) {
    EnvironmentProvider provider("DOTVM_TEST_");

    // Empty name should be invalid
    auto result = provider.get("");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), SecretsError::InvalidName);
}

// ============================================================================
// SecretsVault Interface Compliance
// ============================================================================

TEST_F(EnvironmentProviderTest, ImplementsSecretsVaultInterface) {
    EnvironmentProvider provider("DOTVM_TEST_");

    // Should be usable through base interface
    SecretsVault& vault = provider;

    auto result = vault.get("API_KEY");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->empty());

    EXPECT_TRUE(vault.exists("API_KEY"));
    EXPECT_EQ(vault.provider_id(), "environment");
}

}  // namespace
}  // namespace dotvm::core::security::secrets
