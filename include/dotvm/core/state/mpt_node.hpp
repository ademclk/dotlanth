/// @file mpt_node.hpp
/// @brief STATE-008 MPT node types and serialization
///
/// Defines the three node types in a Merkle Patricia Trie:
/// - LeafNode: Stores key-value pair at path terminus
/// - ExtensionNode: Compresses shared path prefix
/// - BranchNode: 16-way branching point with optional value
///
/// Also provides Hex-Prefix (HP) encoding for compact path representation.

#pragma once

#include <array>
#include <variant>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/mpt_types.hpp"

namespace dotvm::core::state {

// ============================================================================
// Node Types
// ============================================================================

/// @brief Leaf node: terminal node storing a value
///
/// A leaf node marks the end of a key's path in the trie and stores
/// the associated value. The path contains the remaining nibbles after
/// traversing from the root.
struct LeafNode {
    Nibbles path;                  ///< Remaining key path (nibbles)
    std::vector<std::byte> value;  ///< Stored value
};

/// @brief Extension node: path compression
///
/// An extension node compresses a sequence of single-child branches into
/// a single node with a shared path. Points to exactly one child node.
struct ExtensionNode {
    Nibbles path;   ///< Shared prefix path (nibbles)
    Hash256 child;  ///< Hash of child node
};

/// @brief Branch node: 16-way branching point
///
/// A branch node has 16 slots (one per hex digit 0-F) for child hashes
/// and an optional value (for keys that terminate at this branch).
struct BranchNode {
    std::array<Hash256, 16> children{};  ///< Child hashes (zero = empty)
    std::vector<std::byte> value;        ///< Optional value at this branch
};

/// @brief Variant type holding any MPT node
using MptNode = std::variant<LeafNode, ExtensionNode, BranchNode>;

// ============================================================================
// Hex-Prefix (HP) Encoding
// ============================================================================

/// @brief Hex-Prefix encoding flags
///
/// HP uses the high nibble of the first byte as flags:
/// - Bit 1 (0x20): 1 = leaf, 0 = extension
/// - Bit 0 (0x10): 1 = odd path length (first nibble in low position)
///
/// For odd paths, the first nibble is stored in the low position of byte 0.
/// For even paths, byte 0 is just the flags.

/// @brief Encode a nibble path using Hex-Prefix encoding
///
/// @param path The nibble path to encode
/// @param is_leaf true for leaf nodes, false for extension nodes
/// @return HP-encoded bytes
[[nodiscard]] std::vector<std::byte> hp_encode(const Nibbles& path, bool is_leaf);

/// @brief Result of HP decoding
struct HpDecodeResult {
    Nibbles path;  ///< Decoded nibble path
    bool is_leaf;  ///< true if this was a leaf encoding
};

/// @brief Decode HP-encoded path
///
/// @param hp HP-encoded bytes
/// @return Decoded path and node type, or error
[[nodiscard]] Result<HpDecodeResult, MptError> hp_decode(std::span<const std::byte> hp);

// ============================================================================
// Node Serialization
// ============================================================================

/// @brief Serialize a node to RLP-encoded bytes
///
/// @param node The node to serialize
/// @return RLP-encoded node data
[[nodiscard]] std::vector<std::byte> serialize_node(const LeafNode& node);
[[nodiscard]] std::vector<std::byte> serialize_node(const ExtensionNode& node);
[[nodiscard]] std::vector<std::byte> serialize_node(const BranchNode& node);
[[nodiscard]] std::vector<std::byte> serialize_node(const MptNode& node);

/// @brief Deserialize RLP-encoded bytes to a node
///
/// @param data RLP-encoded node data
/// @return Deserialized node, or error
[[nodiscard]] Result<MptNode, MptError> deserialize_node(std::span<const std::byte> data);

// ============================================================================
// Node Hashing
// ============================================================================

/// @brief Compute the hash of a serialized node
///
/// For nodes with serialized length < 32 bytes, the hash is the serialized
/// data itself (identity hash). Otherwise, uses BLAKE3.
///
/// @param node The node to hash
/// @return 32-byte hash
[[nodiscard]] Hash256 hash_node(const LeafNode& node);
[[nodiscard]] Hash256 hash_node(const ExtensionNode& node);
[[nodiscard]] Hash256 hash_node(const BranchNode& node);
[[nodiscard]] Hash256 hash_node(const MptNode& node);

}  // namespace dotvm::core::state
