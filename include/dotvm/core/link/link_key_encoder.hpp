#pragma once

/// @file link_key_encoder.hpp
/// @brief DEP-004 Link key encoding for StateBackend storage
///
/// Encodes forward, inverse, and cardinality keys for prefix-based scans.

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "dotvm/core/link/object_id.hpp"

namespace dotvm::core::link {

// ============================================================================
// LinkKeyEncoder
// ============================================================================

/// @brief Encode and decode relationship model storage keys
class LinkKeyEncoder {
public:
    // Forward link keys: "L|" + src(16B) + "|" + link_name + "|" + tgt(16B)
    [[nodiscard]] static std::vector<std::byte>
    encode_forward_key(ObjectId src, std::string_view link, ObjectId tgt);
    [[nodiscard]] static std::vector<std::byte> encode_forward_prefix(ObjectId src,
                                                                      std::string_view link);

    // Inverse link keys: "I|" + tgt(16B) + "|" + link_name + "|" + src(16B)
    [[nodiscard]] static std::vector<std::byte>
    encode_inverse_key(ObjectId tgt, std::string_view link, ObjectId src);
    [[nodiscard]] static std::vector<std::byte> encode_inverse_prefix(ObjectId tgt,
                                                                      std::string_view link);

    // Cardinality keys: "C|" + src(16B) + "|" + link_name
    [[nodiscard]] static std::vector<std::byte> encode_count_key(ObjectId src,
                                                                 std::string_view link);

    // Decode target ObjectId from a forward key
    [[nodiscard]] static ObjectId decode_target_from_forward(std::span<const std::byte> key);

    // Decode source ObjectId from an inverse key
    [[nodiscard]] static ObjectId decode_source_from_inverse(std::span<const std::byte> key);
};

}  // namespace dotvm::core::link
