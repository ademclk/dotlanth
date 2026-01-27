/// @file rlp.cpp
/// @brief STATE-008 RLP encoding/decoding implementation
///
/// Implements RLP (Recursive Length Prefix) encoding as specified in
/// the Ethereum Yellow Paper, Appendix B.

#include "dotvm/core/state/rlp.hpp"

#include <algorithm>

namespace dotvm::core::state {
namespace rlp {

namespace {

/// @brief Encode a length value as big-endian bytes
[[nodiscard]] std::vector<std::byte> encode_length(std::size_t len) {
    std::vector<std::byte> result;

    // Count bytes needed
    std::size_t temp = len;
    std::size_t byte_count = 0;
    do {
        ++byte_count;
        temp >>= 8;
    } while (temp > 0);

    // Encode big-endian
    result.reserve(byte_count);
    for (std::size_t i = byte_count; i > 0; --i) {
        result.push_back(static_cast<std::byte>((len >> ((i - 1) * 8)) & 0xFF));
    }

    return result;
}

/// @brief Decode a big-endian length from bytes
[[nodiscard]] std::size_t decode_length(std::span<const std::byte> data) {
    std::size_t result = 0;
    for (const auto byte : data) {
        result = (result << 8) | static_cast<std::uint8_t>(byte);
    }
    return result;
}

}  // namespace

// ============================================================================
// Encoding Implementation
// ============================================================================

std::vector<std::byte> encode(std::span<const std::byte> data) {
    std::vector<std::byte> result;

    // Single byte in range [0x00, 0x7F] - encode as itself
    if (data.size() == 1 && static_cast<std::uint8_t>(data[0]) < 0x80) {
        result.push_back(data[0]);
        return result;
    }

    // Empty or short string (0-55 bytes)
    if (data.size() <= 55) {
        result.reserve(1 + data.size());
        result.push_back(static_cast<std::byte>(0x80 + data.size()));
        result.insert(result.end(), data.begin(), data.end());
        return result;
    }

    // Long string (56+ bytes)
    const auto length_bytes = encode_length(data.size());
    result.reserve(1 + length_bytes.size() + data.size());
    result.push_back(static_cast<std::byte>(0xB7 + length_bytes.size()));
    result.insert(result.end(), length_bytes.begin(), length_bytes.end());
    result.insert(result.end(), data.begin(), data.end());
    return result;
}

std::vector<std::byte> encode_list(const std::vector<std::vector<std::byte>>& items) {
    // First, encode all items and concatenate
    std::vector<std::byte> payload;
    for (const auto& item : items) {
        const auto encoded_item = encode(item);
        payload.insert(payload.end(), encoded_item.begin(), encoded_item.end());
    }

    std::vector<std::byte> result;

    // Short list (0-55 bytes payload)
    if (payload.size() <= 55) {
        result.reserve(1 + payload.size());
        result.push_back(static_cast<std::byte>(0xC0 + payload.size()));
        result.insert(result.end(), payload.begin(), payload.end());
        return result;
    }

    // Long list (56+ bytes payload)
    const auto length_bytes = encode_length(payload.size());
    result.reserve(1 + length_bytes.size() + payload.size());
    result.push_back(static_cast<std::byte>(0xF7 + length_bytes.size()));
    result.insert(result.end(), length_bytes.begin(), length_bytes.end());
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::vector<std::byte> encode_list_raw(std::span<const std::span<const std::byte>> encoded_items) {
    // Concatenate already-encoded items
    std::vector<std::byte> payload;
    for (const auto& item : encoded_items) {
        payload.insert(payload.end(), item.begin(), item.end());
    }

    std::vector<std::byte> result;

    // Short list
    if (payload.size() <= 55) {
        result.reserve(1 + payload.size());
        result.push_back(static_cast<std::byte>(0xC0 + payload.size()));
        result.insert(result.end(), payload.begin(), payload.end());
        return result;
    }

    // Long list
    const auto length_bytes = encode_length(payload.size());
    result.reserve(1 + length_bytes.size() + payload.size());
    result.push_back(static_cast<std::byte>(0xF7 + length_bytes.size()));
    result.insert(result.end(), length_bytes.begin(), length_bytes.end());
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

// ============================================================================
// Decoding Implementation
// ============================================================================

Result<DecodeResult, MptError> decode_item(std::span<const std::byte> data) {
    if (data.empty()) {
        return MptError::InvalidNode;
    }

    const auto prefix = static_cast<std::uint8_t>(data[0]);

    // Single byte [0x00, 0x7F]
    if (prefix < 0x80) {
        return DecodeResult{{data[0]}, 1};
    }

    // Short string [0x80, 0xB7]: length = prefix - 0x80
    if (prefix <= 0xB7) {
        const std::size_t len = prefix - 0x80;
        if (data.size() < 1 + len) {
            return MptError::InvalidNode;
        }
        return DecodeResult{
            std::vector<std::byte>(data.begin() + 1, data.begin() + 1 + static_cast<long>(len)),
            1 + len};
    }

    // Long string [0xB8, 0xBF]: length-of-length = prefix - 0xB7
    if (prefix <= 0xBF) {
        const std::size_t len_of_len = prefix - 0xB7;
        if (data.size() < 1 + len_of_len) {
            return MptError::InvalidNode;
        }

        const std::size_t len = decode_length(data.subspan(1, len_of_len));
        const std::size_t total = 1 + len_of_len + len;

        if (data.size() < total) {
            return MptError::InvalidNode;
        }

        return DecodeResult{std::vector<std::byte>(data.begin() + 1 + static_cast<long>(len_of_len),
                                                   data.begin() + static_cast<long>(total)),
                           total};
    }

    // Short list [0xC0, 0xF7]: length = prefix - 0xC0
    if (prefix <= 0xF7) {
        const std::size_t len = prefix - 0xC0;
        if (data.size() < 1 + len) {
            return MptError::InvalidNode;
        }
        // For lists, return the raw list content (not decoded items)
        return DecodeResult{
            std::vector<std::byte>(data.begin() + 1, data.begin() + 1 + static_cast<long>(len)),
            1 + len};
    }

    // Long list [0xF8, 0xFF]: length-of-length = prefix - 0xF7
    const std::size_t len_of_len = prefix - 0xF7;
    if (data.size() < 1 + len_of_len) {
        return MptError::InvalidNode;
    }

    const std::size_t len = decode_length(data.subspan(1, len_of_len));
    const std::size_t total = 1 + len_of_len + len;

    if (data.size() < total) {
        return MptError::InvalidNode;
    }

    return DecodeResult{std::vector<std::byte>(data.begin() + 1 + static_cast<long>(len_of_len),
                                               data.begin() + static_cast<long>(total)),
                       total};
}

Result<std::vector<std::byte>, MptError> decode(std::span<const std::byte> data) {
    if (data.empty()) {
        return MptError::InvalidNode;
    }

    const auto prefix = static_cast<std::uint8_t>(data[0]);

    // Reject list prefixes when decoding a string
    if (prefix >= 0xC0) {
        return MptError::InvalidNode;
    }

    auto result = decode_item(data);
    if (result.is_err()) {
        return result.error();
    }

    return std::move(result.value().data);
}

Result<std::vector<std::vector<std::byte>>, MptError> decode_list(std::span<const std::byte> data) {
    if (data.empty()) {
        return MptError::InvalidNode;
    }

    const auto prefix = static_cast<std::uint8_t>(data[0]);

    // Must be a list prefix
    if (prefix < 0xC0) {
        return MptError::InvalidNode;
    }

    // Decode the list header to get payload
    auto list_result = decode_item(data);
    if (list_result.is_err()) {
        return list_result.error();
    }

    // Now decode each item in the payload
    std::vector<std::vector<std::byte>> items;
    std::span<const std::byte> payload = list_result.value().data;

    while (!payload.empty()) {
        auto item_result = decode_item(payload);
        if (item_result.is_err()) {
            return item_result.error();
        }

        items.push_back(std::move(item_result.value().data));
        payload = payload.subspan(item_result.value().bytes_consumed);
    }

    return items;
}

}  // namespace rlp
}  // namespace dotvm::core::state
