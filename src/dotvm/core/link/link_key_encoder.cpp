#include "dotvm/core/link/link_key_encoder.hpp"

#include <cstddef>

namespace dotvm::core::link {
namespace {

constexpr std::byte kDelimiter{static_cast<std::byte>('|')};
constexpr std::byte kForwardTag{static_cast<std::byte>('L')};
constexpr std::byte kInverseTag{static_cast<std::byte>('I')};
constexpr std::byte kCountTag{static_cast<std::byte>('C')};
constexpr std::size_t kObjectIdSize = 16;

void append_string(std::vector<std::byte>& out, std::string_view text) {
    for (char c : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
}

void append_object_id(std::vector<std::byte>& out, ObjectId id) {
    const auto bytes = id.to_bytes();
    out.insert(out.end(), bytes.begin(), bytes.end());
}

[[nodiscard]] bool has_minimum_layout(std::span<const std::byte> key, std::byte tag) {
    const std::size_t min_size = 2U + kObjectIdSize + 1U + 1U + kObjectIdSize;
    if (key.size() < min_size) {
        return false;
    }
    if (key[0] != tag || key[1] != kDelimiter) {
        return false;
    }
    if (key[2 + kObjectIdSize] != kDelimiter) {
        return false;
    }
    const std::size_t tail_delim = key.size() - kObjectIdSize - 1U;
    if (key[tail_delim] != kDelimiter) {
        return false;
    }
    return true;
}

}  // namespace

std::vector<std::byte> LinkKeyEncoder::encode_forward_key(ObjectId src, std::string_view link,
                                                          ObjectId tgt) {
    std::vector<std::byte> key;
    key.reserve(2U + kObjectIdSize + 1U + link.size() + 1U + kObjectIdSize);

    key.push_back(kForwardTag);
    key.push_back(kDelimiter);
    append_object_id(key, src);
    key.push_back(kDelimiter);
    append_string(key, link);
    key.push_back(kDelimiter);
    append_object_id(key, tgt);

    return key;
}

std::vector<std::byte> LinkKeyEncoder::encode_forward_prefix(ObjectId src, std::string_view link) {
    std::vector<std::byte> key;
    key.reserve(2U + kObjectIdSize + 1U + link.size() + 1U);

    key.push_back(kForwardTag);
    key.push_back(kDelimiter);
    append_object_id(key, src);
    key.push_back(kDelimiter);
    append_string(key, link);
    key.push_back(kDelimiter);

    return key;
}

std::vector<std::byte> LinkKeyEncoder::encode_inverse_key(ObjectId tgt, std::string_view link,
                                                          ObjectId src) {
    std::vector<std::byte> key;
    key.reserve(2U + kObjectIdSize + 1U + link.size() + 1U + kObjectIdSize);

    key.push_back(kInverseTag);
    key.push_back(kDelimiter);
    append_object_id(key, tgt);
    key.push_back(kDelimiter);
    append_string(key, link);
    key.push_back(kDelimiter);
    append_object_id(key, src);

    return key;
}

std::vector<std::byte> LinkKeyEncoder::encode_inverse_prefix(ObjectId tgt, std::string_view link) {
    std::vector<std::byte> key;
    key.reserve(2U + kObjectIdSize + 1U + link.size() + 1U);

    key.push_back(kInverseTag);
    key.push_back(kDelimiter);
    append_object_id(key, tgt);
    key.push_back(kDelimiter);
    append_string(key, link);
    key.push_back(kDelimiter);

    return key;
}

std::vector<std::byte> LinkKeyEncoder::encode_count_key(ObjectId src, std::string_view link) {
    std::vector<std::byte> key;
    key.reserve(2U + kObjectIdSize + 1U + link.size());

    key.push_back(kCountTag);
    key.push_back(kDelimiter);
    append_object_id(key, src);
    key.push_back(kDelimiter);
    append_string(key, link);

    return key;
}

ObjectId LinkKeyEncoder::decode_target_from_forward(std::span<const std::byte> key) {
    if (!has_minimum_layout(key, kForwardTag)) {
        return ObjectId::invalid();
    }

    const std::size_t target_offset = key.size() - kObjectIdSize;
    std::span<const std::byte, kObjectIdSize> target_bytes{key.data() + target_offset,
                                                           kObjectIdSize};
    return ObjectId::from_bytes(target_bytes);
}

ObjectId LinkKeyEncoder::decode_source_from_inverse(std::span<const std::byte> key) {
    if (!has_minimum_layout(key, kInverseTag)) {
        return ObjectId::invalid();
    }

    const std::size_t source_offset = key.size() - kObjectIdSize;
    std::span<const std::byte, kObjectIdSize> source_bytes{key.data() + source_offset,
                                                           kObjectIdSize};
    return ObjectId::from_bytes(source_bytes);
}

}  // namespace dotvm::core::link
