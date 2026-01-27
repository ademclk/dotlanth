/// @file mpt_node.cpp
/// @brief STATE-008 MPT node types implementation
///
/// Implements Hex-Prefix encoding, node serialization, and hashing.

#include "dotvm/core/state/mpt_node.hpp"

#include <algorithm>

#include "dotvm/core/crypto/blake3.hpp"
#include "dotvm/core/state/rlp.hpp"

namespace dotvm::core::state {

// ============================================================================
// Hex-Prefix Encoding
// ============================================================================

std::vector<std::byte> hp_encode(const Nibbles& path, bool is_leaf) {
    std::vector<std::byte> result;

    // Flag byte: bit 5 (0x20) = leaf, bit 4 (0x10) = odd length
    const std::uint8_t leaf_flag = is_leaf ? 0x20 : 0x00;
    const bool odd = (path.size() % 2) != 0;

    if (odd) {
        // Odd: first byte is flag | 0x10 | first_nibble
        result.push_back(static_cast<std::byte>(leaf_flag | 0x10 | path[0]));

        // Pack remaining nibbles
        for (std::size_t i = 1; i < path.size(); i += 2) {
            const auto high = path[i];
            const auto low = (i + 1 < path.size()) ? path[i + 1] : 0;
            result.push_back(static_cast<std::byte>((high << 4) | low));
        }
    } else {
        // Even: first byte is just the flag
        result.push_back(static_cast<std::byte>(leaf_flag));

        // Pack all nibbles
        for (std::size_t i = 0; i < path.size(); i += 2) {
            const auto high = path[i];
            const auto low = (i + 1 < path.size()) ? path[i + 1] : 0;
            result.push_back(static_cast<std::byte>((high << 4) | low));
        }
    }

    return result;
}

Result<HpDecodeResult, MptError> hp_decode(std::span<const std::byte> hp) {
    if (hp.empty()) {
        return MptError::InvalidNode;
    }

    const auto first_byte = static_cast<std::uint8_t>(hp[0]);
    const bool is_leaf = (first_byte & 0x20) != 0;
    const bool is_odd = (first_byte & 0x10) != 0;

    std::vector<std::uint8_t> nibbles;

    if (is_odd) {
        // First nibble is in low position of byte 0
        nibbles.push_back(first_byte & 0x0F);

        // Expand remaining bytes
        for (std::size_t i = 1; i < hp.size(); ++i) {
            const auto byte = static_cast<std::uint8_t>(hp[i]);
            nibbles.push_back((byte >> 4) & 0x0F);
            nibbles.push_back(byte & 0x0F);
        }
    } else {
        // Expand all bytes after the flag byte
        for (std::size_t i = 1; i < hp.size(); ++i) {
            const auto byte = static_cast<std::uint8_t>(hp[i]);
            nibbles.push_back((byte >> 4) & 0x0F);
            nibbles.push_back(byte & 0x0F);
        }
    }

    // Convert to Nibbles using helper constructor
    // Note: Nibbles stores the raw vector internally
    const auto path_bytes = [&]() {
        std::vector<std::byte> bytes;
        for (std::size_t i = 0; i + 1 < nibbles.size(); i += 2) {
            bytes.push_back(static_cast<std::byte>((nibbles[i] << 4) | nibbles[i + 1]));
        }
        // Handle odd nibble count by padding
        if (nibbles.size() % 2 != 0) {
            bytes.push_back(static_cast<std::byte>(nibbles.back() << 4));
        }
        return bytes;
    }();

    // Create Nibbles from the reconstructed bytes, then slice to exact size
    Nibbles full_path(path_bytes);
    const auto path = full_path.slice(0, nibbles.size());

    return HpDecodeResult{path, is_leaf};
}

// ============================================================================
// Node Serialization
// ============================================================================

std::vector<std::byte> serialize_node(const LeafNode& node) {
    // Leaf: [hp_encoded_path, value]
    const auto hp_path = hp_encode(node.path, /*is_leaf=*/true);

    std::vector<std::vector<std::byte>> items;
    items.push_back(hp_path);
    items.push_back(node.value);

    return rlp::encode_list(items);
}

std::vector<std::byte> serialize_node(const ExtensionNode& node) {
    // Extension: [hp_encoded_path, child_hash]
    const auto hp_path = hp_encode(node.path, /*is_leaf=*/false);

    std::vector<std::byte> child_bytes;
    child_bytes.reserve(32);
    for (const auto byte : node.child.data) {
        child_bytes.push_back(static_cast<std::byte>(byte));
    }

    std::vector<std::vector<std::byte>> items;
    items.push_back(hp_path);
    items.push_back(child_bytes);

    return rlp::encode_list(items);
}

std::vector<std::byte> serialize_node(const BranchNode& node) {
    // Branch: [child0, child1, ..., child15, value]
    std::vector<std::vector<std::byte>> items;
    items.reserve(17);

    for (const auto& child : node.children) {
        if (child.is_zero()) {
            items.emplace_back();  // Empty for missing children
        } else {
            std::vector<std::byte> child_bytes;
            child_bytes.reserve(32);
            for (const auto byte : child.data) {
                child_bytes.push_back(static_cast<std::byte>(byte));
            }
            items.push_back(std::move(child_bytes));
        }
    }
    items.push_back(node.value);

    return rlp::encode_list(items);
}

std::vector<std::byte> serialize_node(const MptNode& node) {
    return std::visit([](const auto& n) { return serialize_node(n); }, node);
}

Result<MptNode, MptError> deserialize_node(std::span<const std::byte> data) {
    // Decode the RLP list
    auto list_result = rlp::decode_list(data);
    if (list_result.is_err()) {
        return list_result.error();
    }

    const auto& items = list_result.value();

    // Branch node: 17 items (16 children + 1 value)
    if (items.size() == 17) {
        BranchNode branch{};
        for (std::size_t i = 0; i < 16; ++i) {
            if (!items[i].empty()) {
                if (items[i].size() != 32) {
                    return MptError::InvalidNode;
                }
                std::transform(items[i].begin(), items[i].end(), branch.children[i].data.begin(),
                               [](std::byte b) { return static_cast<std::uint8_t>(b); });
            }
        }
        branch.value = items[16];
        return MptNode{branch};
    }

    // Leaf or Extension: 2 items (hp_path + value/child)
    if (items.size() == 2) {
        const auto hp_result = hp_decode(items[0]);
        if (hp_result.is_err()) {
            return hp_result.error();
        }

        if (hp_result.value().is_leaf) {
            return MptNode{LeafNode{hp_result.value().path, items[1]}};
        }

        // Extension: second item is child hash (32 bytes)
        if (items[1].size() != 32) {
            return MptError::InvalidNode;
        }

        Hash256 child_hash{};
        std::transform(items[1].begin(), items[1].end(), child_hash.data.begin(),
                       [](std::byte b) { return static_cast<std::uint8_t>(b); });
        return MptNode{ExtensionNode{hp_result.value().path, child_hash}};
    }

    return MptError::InvalidNode;
}

// ============================================================================
// Node Hashing
// ============================================================================

namespace {

[[nodiscard]] Hash256 hash_serialized(std::span<const std::byte> serialized) {
    // Compute BLAKE3 hash
    std::vector<std::uint8_t> input(serialized.size());
    std::transform(serialized.begin(), serialized.end(), input.begin(),
                   [](std::byte b) { return static_cast<std::uint8_t>(b); });

    const auto digest = crypto::Blake3::hash(input);

    Hash256 result{};
    std::copy(digest.begin(), digest.end(), result.data.begin());
    return result;
}

}  // namespace

Hash256 hash_node(const LeafNode& node) {
    const auto serialized = serialize_node(node);
    return hash_serialized(serialized);
}

Hash256 hash_node(const ExtensionNode& node) {
    const auto serialized = serialize_node(node);
    return hash_serialized(serialized);
}

Hash256 hash_node(const BranchNode& node) {
    const auto serialized = serialize_node(node);
    return hash_serialized(serialized);
}

Hash256 hash_node(const MptNode& node) {
    return std::visit([](const auto& n) { return hash_node(n); }, node);
}

}  // namespace dotvm::core::state
