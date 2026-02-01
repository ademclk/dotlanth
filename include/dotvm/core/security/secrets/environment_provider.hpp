#pragma once

/// @file environment_provider.hpp
/// @brief SEC-008 Phase 2: Environment variable based secret provider
///
/// This header defines the EnvironmentProvider class that reads secrets
/// from environment variables. It supports optional prefixing and validation
/// of required secrets at construction time.
///
/// @note Thread Safety: EnvironmentProvider is thread-safe for read operations
/// as it only reads from the process environment.

#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/core/security/secrets/secret.hpp"
#include "dotvm/core/security/secrets/secrets_vault.hpp"

namespace dotvm::core::security::secrets {

/// @brief Environment variable based secret provider
///
/// EnvironmentProvider reads secrets from the process environment. It supports:
/// - Optional prefix for all environment variable names (e.g., "DOTVM_")
/// - Validation of required secrets at construction time
/// - Listing of known/required secret names
///
/// @par Prefix Handling
/// When a prefix is configured (e.g., "DOTVM_"), requesting secret "API_KEY"
/// will look up the environment variable "DOTVM_API_KEY".
///
/// @par Required Secrets Validation
/// If required_names is provided at construction, the constructor validates
/// that all required secrets exist and throws std::runtime_error if any are
/// missing. This enables fail-fast behavior at application startup.
///
/// @par Thread Safety
/// Thread-safe for all operations (read-only after construction).
///
/// @par Usage Example
/// @code
/// // Simple usage with prefix
/// EnvironmentProvider provider("DOTVM_");
/// auto api_key = provider.get("API_KEY");  // Reads DOTVM_API_KEY
///
/// // With required secrets validation
/// std::vector<std::string> required = {"API_KEY", "DB_PASSWORD"};
/// EnvironmentProvider validated_provider("MYAPP_", required);
/// // Throws if MYAPP_API_KEY or MYAPP_DB_PASSWORD is missing
/// @endcode
class EnvironmentProvider final : public SecretsVault {
public:
    /// @brief Construct an environment provider
    ///
    /// @param prefix Prefix to prepend to all secret names (e.g., "DOTVM_")
    /// @param required_names Optional list of required secret names to validate
    /// @throws std::runtime_error if any required secret is missing
    ///
    /// @par Example
    /// @code
    /// // No validation
    /// EnvironmentProvider provider("APP_");
    ///
    /// // With validation (throws if missing)
    /// std::vector<std::string> required = {"API_KEY", "SECRET"};
    /// EnvironmentProvider validated("APP_", required);
    /// @endcode
    explicit EnvironmentProvider(std::string prefix = "",
                                 std::vector<std::string> required_names = {});

    /// @brief Retrieve a secret from environment
    ///
    /// Looks up the environment variable `prefix + name`.
    ///
    /// @param name The logical secret name (without prefix)
    /// @return The secret value on success, or an error code
    ///
    /// @par Error Conditions
    /// - SecretsError::InvalidName if name is empty
    /// - SecretsError::NotFound if the environment variable doesn't exist
    [[nodiscard]] std::expected<Secret, SecretsError> get(std::string_view name) const override;

    /// @brief Check if an environment variable exists
    ///
    /// @param name The logical secret name (without prefix)
    /// @return true if the environment variable exists
    [[nodiscard]] bool exists(std::string_view name) const override;

    /// @brief List known secret names
    ///
    /// Returns the logical names of secrets that were specified as required
    /// at construction time. If no required_names were specified, returns
    /// an empty vector.
    ///
    /// @return Vector of logical secret names (without prefix)
    [[nodiscard]] std::vector<std::string> list_names() const override;

    /// @brief Get provider identifier
    ///
    /// @return "environment"
    [[nodiscard]] std::string_view provider_id() const noexcept override;

private:
    /// @brief Build full environment variable name from logical name
    [[nodiscard]] std::string build_env_name(std::string_view name) const;

    std::string prefix_;
    std::vector<std::string> required_names_;
};

}  // namespace dotvm::core::security::secrets
