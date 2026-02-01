#pragma once

/// @file secrets_vault.hpp
/// @brief SEC-008 Phase 2: Abstract interface for secret providers
///
/// This header defines the SecretsVault abstract interface that secret
/// providers must implement. Different implementations can read secrets
/// from various sources: environment variables, files, external vaults
/// (AWS Secrets Manager, HashiCorp Vault, etc.).
///
/// The interface uses std::expected for error handling, avoiding exceptions
/// in the hot path while providing detailed error information.

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/core/security/secrets/secret.hpp"

namespace dotvm::core::security::secrets {

// ============================================================================
// SecretsError Enum
// ============================================================================

/// @brief Error codes for secret retrieval operations
///
/// These error codes cover the common failure modes when accessing secrets
/// from any provider.
enum class SecretsError : std::uint8_t {
    /// Operation succeeded (typically not returned via expected::error)
    Success = 0,

    /// Secret with the given name was not found
    NotFound,

    /// Access to the secret was denied (permissions issue)
    AccessDenied,

    /// Failed to connect to the secret provider
    ConnectionFailed,

    /// The secret name is invalid (empty, malformed, etc.)
    InvalidName,

    /// The secret has expired (for time-limited secrets)
    Expired
};

/// @brief Convert SecretsError to human-readable string
///
/// @param error The error code to convert
/// @return String representation of the error
[[nodiscard]] constexpr std::string_view to_string(SecretsError error) noexcept {
    switch (error) {
        case SecretsError::Success:
            return "Success";
        case SecretsError::NotFound:
            return "NotFound";
        case SecretsError::AccessDenied:
            return "AccessDenied";
        case SecretsError::ConnectionFailed:
            return "ConnectionFailed";
        case SecretsError::InvalidName:
            return "InvalidName";
        case SecretsError::Expired:
            return "Expired";
    }
    return "Unknown";
}

// ============================================================================
// SecretsVault Interface
// ============================================================================

/// @brief Abstract interface for secret providers
///
/// SecretsVault defines the contract that all secret providers must implement.
/// This allows the application to switch between different secret sources
/// (environment variables, files, cloud secret managers) without changing
/// the consuming code.
///
/// @par Thread Safety
/// Implementations should document their thread safety guarantees.
/// The interface itself makes no thread safety promises.
///
/// @par Error Handling
/// All methods that can fail return std::expected<T, SecretsError>.
/// This provides both the success value and detailed error information
/// without using exceptions.
///
/// @par Usage Example
/// @code
/// void process_request(SecretsVault& vault) {
///     auto api_key = vault.get("API_KEY");
///     if (!api_key) {
///         handle_error(api_key.error());
///         return;
///     }
///     call_api(api_key->view());
/// }
/// @endcode
class SecretsVault {
public:
    /// @brief Virtual destructor for proper cleanup through base pointer
    virtual ~SecretsVault() = default;

    // Default copy/move operations
    SecretsVault() = default;
    SecretsVault(const SecretsVault&) = default;
    SecretsVault& operator=(const SecretsVault&) = default;
    SecretsVault(SecretsVault&&) = default;
    SecretsVault& operator=(SecretsVault&&) = default;

    /// @brief Retrieve a secret by name
    ///
    /// @param name The logical name of the secret (without any prefix)
    /// @return The secret value on success, or an error code on failure
    ///
    /// @par Error Conditions
    /// - SecretsError::NotFound if the secret doesn't exist
    /// - SecretsError::InvalidName if the name is empty or malformed
    /// - SecretsError::AccessDenied if access is denied
    /// - SecretsError::ConnectionFailed for network/connection errors
    /// - SecretsError::Expired if the secret has expired
    [[nodiscard]] virtual std::expected<Secret, SecretsError> get(
        std::string_view name) const = 0;

    /// @brief Check if a secret exists without retrieving it
    ///
    /// This is useful for validation or to avoid error handling when
    /// you only need to know if a secret is available.
    ///
    /// @param name The logical name of the secret
    /// @return true if the secret exists and is accessible
    [[nodiscard]] virtual bool exists(std::string_view name) const = 0;

    /// @brief List the names of available secrets
    ///
    /// Returns the logical names (not including any provider-specific prefix)
    /// of secrets that this provider knows about. For some providers (like
    /// environment variables without required_names), this may return empty.
    ///
    /// @return Vector of secret names available through this provider
    [[nodiscard]] virtual std::vector<std::string> list_names() const = 0;

    /// @brief Get the provider identifier
    ///
    /// Returns a string identifying the type of provider. This is useful
    /// for logging and debugging.
    ///
    /// @return Provider identifier (e.g., "environment", "aws_secrets_manager")
    [[nodiscard]] virtual std::string_view provider_id() const noexcept = 0;
};

}  // namespace dotvm::core::security::secrets
