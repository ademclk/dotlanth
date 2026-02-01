/// @file secret.cpp
/// @brief Implementation of SEC-008 Secret RAII type

#include "dotvm/core/security/secrets/secret.hpp"

#include <atomic>
#include <cstring>
#include <functional>

namespace dotvm::core::security::secrets {

// ============================================================================
// secure_zero Implementation
// ============================================================================

void secure_zero(std::span<char> data) noexcept {
    if (data.empty()) {
        return;
    }

    // Use volatile pointer to prevent compiler from optimizing away the writes
    volatile char* volatile ptr = data.data();
    for (std::size_t i = 0; i < data.size(); ++i) {
        ptr[i] = '\0';
    }

    // Memory fence to ensure writes are visible before function returns
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

// ============================================================================
// Secret Implementation
// ============================================================================

Secret::Secret(std::string_view data)
    : data_(data.begin(), data.end()) {
}

Secret::Secret(std::span<const char> data)
    : data_(data.begin(), data.end()) {
}

Secret::~Secret() {
    if (!data_.empty()) {
        secure_zero(std::span<char>(data_.data(), data_.size()));
    }
}

Secret::Secret(Secret&& other) noexcept
    : data_(std::move(other.data_)) {
    // other.data_ is now in a valid but unspecified state
    // Clear it to ensure it's empty
    other.data_.clear();
}

Secret& Secret::operator=(Secret&& other) noexcept {
    if (this != &other) {
        // Securely wipe current data before taking ownership
        if (!data_.empty()) {
            secure_zero(std::span<char>(data_.data(), data_.size()));
        }

        data_ = std::move(other.data_);
        other.data_.clear();
    }
    return *this;
}

std::span<const char> Secret::view() const noexcept {
    if (data_.empty()) {
        return {};
    }
    return std::span<const char>(data_.data(), data_.size());
}

std::size_t Secret::size() const noexcept {
    return data_.size();
}

bool Secret::empty() const noexcept {
    return data_.empty();
}

std::size_t Secret::redaction_id() const noexcept {
    return std::hash<std::string_view>{}(
        std::string_view(data_.data(), data_.size()));
}

}  // namespace dotvm::core::security::secrets
