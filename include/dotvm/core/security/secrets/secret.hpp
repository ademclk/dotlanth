#pragma once

/// @file secret.hpp
/// @brief RAII wrapper for sensitive data with secure memory wipe
///
/// SEC-008 Phase 1: The core Secret type provides secure handling of
/// sensitive data such as API keys, passwords, and cryptographic keys.
/// It ensures that sensitive data is securely wiped from memory when
/// no longer needed.
///
/// Features:
/// - Secure memory wipe using volatile writes and memory fence
/// - Move-only semantics (copy operations are deleted)
/// - Consistent redaction ID for logging without exposing secret values
/// - Span-based view for safe, read-only access

#include <atomic>
#include <cstddef>
#include <cstring>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace dotvm::core::security::secrets {

/// @brief Securely zero memory to prevent secrets from lingering
///
/// Uses volatile writes to prevent compiler optimization and a
/// memory fence to ensure the writes are visible before returning.
///
/// @param data The span of memory to zero
void secure_zero(std::span<char> data) noexcept;

/// @brief RAII wrapper for sensitive data with secure memory wipe
///
/// Secret provides a move-only container for sensitive data that:
/// - Securely wipes memory on destruction
/// - Prevents accidental copying
/// - Provides a consistent redaction ID for logging
///
/// @par Thread Safety
/// Individual Secret instances are NOT thread-safe. External synchronization
/// is required for concurrent access.
///
/// @par Usage Example
/// @code
/// // Create a secret from sensitive data
/// Secret api_key("sk_live_xxxxxxxxxxxxx");
///
/// // Access the value when needed
/// auto view = api_key.view();
/// use_api_key(view);
///
/// // Get redaction ID for logging (safe to log)
/// log("Using API key: [REDACTED:{}]", api_key.redaction_id());
///
/// // Secret is automatically wiped when it goes out of scope
/// @endcode
class Secret {
public:
    // ========== Construction ==========

    /// @brief Default constructor creates an empty secret
    Secret() noexcept = default;

    /// @brief Construct from a string_view
    ///
    /// @param data The sensitive data to store (copied into internal storage)
    explicit Secret(std::string_view data);

    /// @brief Construct from a span of characters
    ///
    /// @param data The sensitive data to store (copied into internal storage)
    explicit Secret(std::span<const char> data);

    // ========== Destructor ==========

    /// @brief Destructor securely wipes memory before deallocation
    ~Secret();

    // ========== Copy Operations (Deleted) ==========

    /// @brief Copy constructor is deleted to prevent accidental duplication
    Secret(const Secret&) = delete;

    /// @brief Copy assignment is deleted to prevent accidental duplication
    Secret& operator=(const Secret&) = delete;

    // ========== Move Operations ==========

    /// @brief Move constructor transfers ownership
    ///
    /// @param other The secret to move from (will be empty after move)
    Secret(Secret&& other) noexcept;

    /// @brief Move assignment transfers ownership
    ///
    /// The current secret's data is securely wiped before taking ownership
    /// of the other secret's data.
    ///
    /// @param other The secret to move from (will be empty after move)
    /// @return Reference to this secret
    Secret& operator=(Secret&& other) noexcept;

    // ========== Accessors ==========

    /// @brief Get a read-only view of the secret data
    ///
    /// @return Span of const chars containing the secret value
    [[nodiscard]] std::span<const char> view() const noexcept;

    /// @brief Get the size of the secret in bytes
    ///
    /// @return Number of bytes in the secret
    [[nodiscard]] std::size_t size() const noexcept;

    /// @brief Check if the secret is empty
    ///
    /// @return true if the secret has no data
    [[nodiscard]] bool empty() const noexcept;

    /// @brief Get a consistent hash for logging/identification
    ///
    /// The redaction ID is computed from the secret value and can be
    /// safely logged without exposing the actual secret. Identical
    /// secrets will have the same redaction ID.
    ///
    /// @return Hash value suitable for logging
    [[nodiscard]] std::size_t redaction_id() const noexcept;

private:
    /// @brief Internal storage for secret data (mutable for secure wipe)
    mutable std::vector<char> data_;
};

}  // namespace dotvm::core::security::secrets
