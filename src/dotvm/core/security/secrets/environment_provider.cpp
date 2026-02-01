/// @file environment_provider.cpp
/// @brief SEC-008 Phase 2: Implementation of environment variable secret provider

#include "dotvm/core/security/secrets/environment_provider.hpp"

#include <cstdlib>
#include <format>

namespace dotvm::core::security::secrets {

EnvironmentProvider::EnvironmentProvider(std::string prefix,
                                         std::vector<std::string> required_names)
    : prefix_(std::move(prefix)), required_names_(std::move(required_names)) {
    // Validate that all required secrets exist
    for (const auto& name : required_names_) {
        if (!exists(name)) {
            throw std::runtime_error(std::format(
                "Required environment variable '{}{}' is not set", prefix_, name));
        }
    }
}

std::expected<Secret, SecretsError> EnvironmentProvider::get(std::string_view name) const {
    // Validate name
    if (name.empty()) {
        return std::unexpected(SecretsError::InvalidName);
    }

    // Build full environment variable name
    std::string env_name = build_env_name(name);

    // Look up the environment variable
    // NOLINTNEXTLINE(concurrency-mt-unsafe): getenv is MT-safe for reads
    const char* value = std::getenv(env_name.c_str());

    if (value == nullptr) {
        return std::unexpected(SecretsError::NotFound);
    }

    // Return the secret (even if empty string)
    return Secret(std::string_view(value));
}

bool EnvironmentProvider::exists(std::string_view name) const {
    if (name.empty()) {
        return false;
    }

    std::string env_name = build_env_name(name);

    // NOLINTNEXTLINE(concurrency-mt-unsafe): getenv is MT-safe for reads
    return std::getenv(env_name.c_str()) != nullptr;
}

std::vector<std::string> EnvironmentProvider::list_names() const {
    return required_names_;
}

std::string_view EnvironmentProvider::provider_id() const noexcept {
    return "environment";
}

std::string EnvironmentProvider::build_env_name(std::string_view name) const {
    std::string result;
    result.reserve(prefix_.size() + name.size());
    result.append(prefix_);
    result.append(name);
    return result;
}

}  // namespace dotvm::core::security::secrets
