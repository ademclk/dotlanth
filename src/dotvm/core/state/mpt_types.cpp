/// @file mpt_types.cpp
/// @brief STATE-008 MPT foundation types implementation
///
/// Implementation of Hash256, Nibbles, and InMemoryNodeStore.

#include "dotvm/core/state/mpt_types.hpp"

#include <algorithm>
#include <stdexcept>

namespace dotvm::core::state {

// ============================================================================
// Hash256 Implementation
// ============================================================================

std::string Hash256::to_hex() const {
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);

    for (const auto byte : data) {
        result.push_back(hex_chars[(byte >> 4) & 0x0F]);
        result.push_back(hex_chars[byte & 0x0F]);
    }

    return result;
}

Hash256 Hash256::from_hex(std::string_view hex) {
    Hash256 result{};

    const auto hex_to_nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
        return 0;  // Invalid char treated as 0
    };

    const std::size_t byte_count = std::min(hex.size() / 2, result.data.size());
    for (std::size_t i = 0; i < byte_count; ++i) {
        const auto high = hex_to_nibble(hex[i * 2]);
        const auto low = hex_to_nibble(hex[i * 2 + 1]);
        result.data[i] = static_cast<std::uint8_t>((high << 4) | low);
    }

    return result;
}

// ============================================================================
// Nibbles Implementation
// ============================================================================

Nibbles::Nibbles(std::span<const std::byte> bytes) {
    nibbles_.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        const auto value = static_cast<std::uint8_t>(byte);
        nibbles_.push_back(static_cast<std::uint8_t>((value >> 4) & 0x0F));
        nibbles_.push_back(static_cast<std::uint8_t>(value & 0x0F));
    }
}

Nibbles Nibbles::slice(std::size_t start, std::size_t len) const {
    std::vector<std::uint8_t> sliced;
    sliced.reserve(len);

    const std::size_t end = std::min(start + len, nibbles_.size());
    for (std::size_t i = start; i < end; ++i) {
        sliced.push_back(nibbles_[i]);
    }

    return Nibbles(std::move(sliced));
}

std::size_t Nibbles::common_prefix_length(const Nibbles& other) const noexcept {
    const std::size_t max_len = std::min(nibbles_.size(), other.nibbles_.size());
    std::size_t i = 0;
    while (i < max_len && nibbles_[i] == other.nibbles_[i]) {
        ++i;
    }
    return i;
}

std::vector<std::byte> Nibbles::to_bytes() const {
    std::vector<std::byte> result;
    result.reserve((nibbles_.size() + 1) / 2);

    for (std::size_t i = 0; i < nibbles_.size(); i += 2) {
        const auto high = nibbles_[i];
        const auto low = (i + 1 < nibbles_.size()) ? nibbles_[i + 1] : 0;
        result.push_back(static_cast<std::byte>((high << 4) | low));
    }

    return result;
}

// ============================================================================
// InMemoryNodeStore Implementation
// ============================================================================

void InMemoryNodeStore::put(const Hash256& hash, std::span<const std::byte> data) {
    nodes_[hash] = std::vector<std::byte>(data.begin(), data.end());
}

std::optional<std::vector<std::byte>> InMemoryNodeStore::get(const Hash256& hash) const {
    const auto it = nodes_.find(hash);
    if (it == nodes_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool InMemoryNodeStore::contains(const Hash256& hash) const {
    return nodes_.contains(hash);
}

void InMemoryNodeStore::remove(const Hash256& hash) {
    nodes_.erase(hash);
}

}  // namespace dotvm::core::state
