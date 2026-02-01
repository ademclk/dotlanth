/// @file secret_manager.cpp
/// @brief SEC-008 Phase 3: SecretManager implementation

#include "dotvm/core/security/secrets/secret_manager.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>

#include "dotvm/core/security/security_context.hpp"

namespace dotvm::core::security::secrets {

// ============================================================================
// Move Operations
// ============================================================================

SecretManager::SecretManager(SecretManager&& other) noexcept
    : vaults_(std::move(other.vaults_)) {
    // Lock other's mutex to safely move redaction_patterns_
    std::lock_guard lock(other.redaction_mutex_);
    redaction_patterns_ = std::move(other.redaction_patterns_);
}

SecretManager& SecretManager::operator=(SecretManager&& other) noexcept {
    if (this != &other) {
        vaults_ = std::move(other.vaults_);

        // Lock both mutexes to safely move redaction_patterns_
        std::scoped_lock lock(redaction_mutex_, other.redaction_mutex_);
        redaction_patterns_ = std::move(other.redaction_patterns_);
    }
    return *this;
}

// ============================================================================
// Vault Management
// ============================================================================

void SecretManager::add_vault(std::unique_ptr<SecretsVault> vault) {
    if (vault) {
        vaults_.push_back(std::move(vault));
    }
}

std::size_t SecretManager::vault_count() const noexcept {
    return vaults_.size();
}

// ============================================================================
// Secret Access
// ============================================================================

std::expected<Secret, SecretsError> SecretManager::get_secret(
    std::string_view name) const {
    // Search vaults in priority order
    for (const auto& vault : vaults_) {
        auto result = vault->get(name);
        if (result.has_value()) {
            // Auto-register the secret value for redaction
            auto view = result->view();
            if (!view.empty()) {
                std::lock_guard lock(redaction_mutex_);
                redaction_patterns_.emplace(view.data(), view.size());
            }

            // Log successful access using legacy AuditEvent
            if (audit_logger_ != nullptr && audit_logger_->is_enabled()) {
                audit_logger_->log(security::AuditEvent::now(
                    security::AuditEventType::SecretAccessed,
                    security::Permission::None,
                    0,
                    name));
            }

            return result;
        }
        // If not found, continue to next vault
        // For other errors (AccessDenied, etc.), we still try next vault
    }

    // Log not found using legacy AuditEvent
    if (audit_logger_ != nullptr && audit_logger_->is_enabled()) {
        audit_logger_->log(security::AuditEvent::now(
            security::AuditEventType::SecretNotFound,
            security::Permission::None,
            0,
            name));
    }

    // Not found in any vault
    return std::unexpected(SecretsError::NotFound);
}

bool SecretManager::secret_exists(std::string_view name) const {
    return std::any_of(vaults_.begin(), vaults_.end(),
                       [name](const auto& vault) { return vault->exists(name); });
}

// ============================================================================
// Redaction
// ============================================================================

void SecretManager::register_for_redaction(std::string_view value) {
    if (!value.empty()) {
        std::lock_guard lock(redaction_mutex_);
        redaction_patterns_.emplace(value);
    }
}

std::string SecretManager::redact(std::string_view message) const {
    std::string result(message);

    std::lock_guard lock(redaction_mutex_);

    for (const auto& pattern : redaction_patterns_) {
        std::size_t pos = 0;
        while ((pos = result.find(pattern, pos)) != std::string::npos) {
            result.replace(pos, pattern.length(), "[REDACTED]");
            pos += 10;  // length of "[REDACTED]"
        }
    }

    return result;
}

// ============================================================================
// Audit Logging
// ============================================================================

void SecretManager::set_audit_logger(security::AuditLogger* logger) noexcept {
    audit_logger_ = logger;
}

security::AuditLogger* SecretManager::audit_logger() const noexcept {
    return audit_logger_;
}

}  // namespace dotvm::core::security::secrets
