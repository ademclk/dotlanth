#pragma once

/// @file secret_manager.hpp
/// @brief SEC-008 Phase 3: High-level SecretManager with multi-vault support
///
/// This header defines the SecretManager class that provides:
/// - Multi-vault support with priority ordering (first added = highest priority)
/// - Automatic registration of retrieved secrets for redaction
/// - Thread-safe redaction of secret values in log messages
///
/// @par Thread Safety
/// SecretManager is thread-safe for redaction operations. The redaction_mutex_
/// protects the redaction_patterns_ set during concurrent access.
///
/// @par Usage Example
/// @code
/// SecretManager manager;
///
/// // Add vaults in priority order (first = highest priority)
/// manager.add_vault(std::make_unique<EnvironmentProvider>("PROD_"));
/// manager.add_vault(std::make_unique<EnvironmentProvider>("DEFAULT_"));
///
/// // Get secrets - automatically registered for redaction
/// auto api_key = manager.get_secret("API_KEY");
/// if (api_key) {
///     use_api_key(api_key->view());
/// }
///
/// // Safely log messages with secrets redacted
/// std::string safe_log = manager.redact("Using key: " + raw_key);
/// logger.info(safe_log);  // "Using key: [REDACTED]"
/// @endcode

#include <cstddef>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "dotvm/core/security/secrets/secret.hpp"
#include "dotvm/core/security/secrets/secrets_vault.hpp"

namespace dotvm::core::security {
// Forward declaration of legacy AuditLogger from security_context.hpp
class AuditLogger;
}  // namespace dotvm::core::security

namespace dotvm::core::security::secrets {

/// @brief High-level secret manager with multi-vault support and redaction
///
/// SecretManager aggregates multiple SecretsVault implementations and provides:
/// - Priority-based secret lookup (first vault that has the secret wins)
/// - Automatic registration of secret values for redaction
/// - Thread-safe redaction of secrets from strings
///
/// @par Multi-Vault Priority
/// Vaults are searched in the order they were added. The first vault to
/// successfully return a secret "wins". This allows layering configurations:
/// - Production secrets (highest priority)
/// - Environment-specific overrides
/// - Default/fallback values (lowest priority)
///
/// @par Automatic Redaction
/// When a secret is retrieved via get_secret(), its value is automatically
/// registered for redaction. Call redact() on log messages to replace all
/// registered secret values with "[REDACTED]".
///
/// @par Thread Safety
/// - add_vault() is NOT thread-safe (call during initialization)
/// - get_secret() is thread-safe with respect to redaction registration
/// - secret_exists() is thread-safe
/// - redact() is thread-safe
/// - register_for_redaction() is thread-safe
class SecretManager {
public:
    /// @brief Construct an empty SecretManager
    SecretManager() = default;

    /// @brief Destructor
    ~SecretManager() = default;

    // ========== Non-copyable, movable ==========

    /// @brief Copy constructor is deleted
    SecretManager(const SecretManager&) = delete;

    /// @brief Copy assignment is deleted
    SecretManager& operator=(const SecretManager&) = delete;

    /// @brief Move constructor
    SecretManager(SecretManager&& other) noexcept;

    /// @brief Move assignment operator
    SecretManager& operator=(SecretManager&& other) noexcept;

    // ========== Vault Management ==========

    /// @brief Add a vault to the search order
    ///
    /// Vaults are searched in the order they are added. The first vault
    /// added has the highest priority (searched first).
    ///
    /// @param vault The vault to add (ownership transferred)
    ///
    /// @par Thread Safety
    /// NOT thread-safe. Call during initialization only.
    void add_vault(std::unique_ptr<SecretsVault> vault);

    /// @brief Get the number of registered vaults
    ///
    /// @return Number of vaults
    [[nodiscard]] std::size_t vault_count() const noexcept;

    // ========== Secret Access ==========

    /// @brief Get a secret by name, searching vaults in priority order
    ///
    /// Searches vaults in the order they were added. Returns the first
    /// successful result. The secret's value is automatically registered
    /// for redaction.
    ///
    /// @param name The logical name of the secret
    /// @return The secret on success, or SecretsError::NotFound if not in any vault
    ///
    /// @par Thread Safety
    /// Thread-safe with respect to redaction registration.
    [[nodiscard]] std::expected<Secret, SecretsError> get_secret(
        std::string_view name) const;

    /// @brief Check if a secret exists in any vault
    ///
    /// @param name The logical name of the secret
    /// @return true if the secret exists in at least one vault
    [[nodiscard]] bool secret_exists(std::string_view name) const;

    // ========== Redaction ==========

    /// @brief Register a value for redaction
    ///
    /// After registration, calls to redact() will replace occurrences of
    /// this value with "[REDACTED]".
    ///
    /// @param value The value to redact (typically a secret's raw value)
    ///
    /// @par Thread Safety
    /// Thread-safe.
    void register_for_redaction(std::string_view value);

    /// @brief Redact all registered secret values from a message
    ///
    /// Replaces all occurrences of registered patterns with "[REDACTED]".
    ///
    /// @param message The message to redact
    /// @return A new string with secrets replaced by "[REDACTED]"
    ///
    /// @par Thread Safety
    /// Thread-safe.
    [[nodiscard]] std::string redact(std::string_view message) const;

    // ========== Audit Logging ==========

    /// @brief Set the audit logger for secret access logging
    ///
    /// When set, the SecretManager will log audit events for secret access:
    /// - SecretAccessed: When a secret is successfully retrieved
    /// - SecretNotFound: When a secret lookup returns not found
    ///
    /// @param logger Pointer to the audit logger (nullptr to disable logging)
    ///
    /// @par Thread Safety
    /// NOT thread-safe. Call during initialization only.
    void set_audit_logger(security::AuditLogger* logger) noexcept;

    /// @brief Get the current audit logger
    ///
    /// @return Pointer to the audit logger, or nullptr if not set
    [[nodiscard]] security::AuditLogger* audit_logger() const noexcept;

private:
    /// @brief Ordered list of vaults (first = highest priority)
    std::vector<std::unique_ptr<SecretsVault>> vaults_;

    /// @brief Mutex protecting redaction_patterns_
    mutable std::mutex redaction_mutex_;

    /// @brief Set of values to redact (mutable for const get_secret auto-registration)
    mutable std::unordered_set<std::string> redaction_patterns_;

    /// @brief Optional audit logger for secret access logging
    security::AuditLogger* audit_logger_ = nullptr;
};

}  // namespace dotvm::core::security::secrets
