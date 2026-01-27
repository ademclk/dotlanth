/// @file mpt_proof.hpp
/// @brief STATE-008 Merkle Patricia Trie proof types and verification
///
/// Provides proof generation and verification for MPT state commitment.
/// Supports both inclusion proofs (key exists with value) and exclusion
/// proofs (key does not exist).
///
/// @par Design Decisions
/// - Proofs are self-contained (include all nodes needed for verification)
/// - Verification is stateless (no trie access needed)
/// - BLAKE3 hashing matches trie implementation

#pragma once

#include <span>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/mpt_types.hpp"

namespace dotvm::core::state {

/// @brief Merkle proof for a key in the trie
///
/// Contains the path of serialized nodes from root to the target key.
/// Can be used to verify inclusion or exclusion of a key.
///
/// @par Structure
/// - key: The key this proof is for
/// - nodes: Serialized nodes along the path (root first, leaf last)
/// - value: For inclusion proofs, the value at the key (empty for exclusion)
struct MptProof {
    /// The key this proof is for
    std::vector<std::byte> key;

    /// Serialized nodes along the path from root to leaf
    /// Order: root node first, leaf node last
    std::vector<std::vector<std::byte>> nodes;

    /// The value at the key (for inclusion proofs)
    /// Empty for exclusion proofs
    std::vector<std::byte> value;
};

/// @brief Result type for proof operations
template <typename T>
using ProofResult = ::dotvm::core::Result<T, MptError>;

/// @brief Verify that a key-value pair exists in the trie
///
/// Reconstructs the root hash from the proof and compares with expected.
/// The proof must contain all nodes from root to the leaf containing the value.
///
/// @param root Expected root hash of the trie
/// @param key Key to verify
/// @param expected_value Value expected at the key
/// @param proof The inclusion proof
/// @return true if proof is valid and value matches, false otherwise
[[nodiscard]] ProofResult<bool> verify_inclusion(const Hash256& root,
                                                 std::span<const std::byte> key,
                                                 std::span<const std::byte> expected_value,
                                                 const MptProof& proof);

/// @brief Verify that a key does NOT exist in the trie
///
/// Verifies that the proof shows the key path terminates or diverges
/// before reaching a leaf with the key.
///
/// @param root Expected root hash of the trie
/// @param key Key to verify is absent
/// @param proof The exclusion proof
/// @return true if proof validly shows key is absent, false otherwise
[[nodiscard]] ProofResult<bool>
verify_exclusion(const Hash256& root, std::span<const std::byte> key, const MptProof& proof);

}  // namespace dotvm::core::state
