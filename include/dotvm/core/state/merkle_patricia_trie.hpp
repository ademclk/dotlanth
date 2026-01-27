/// @file merkle_patricia_trie.hpp
/// @brief STATE-008 Merkle Patricia Trie state commitment structure
///
/// Provides a cryptographic state commitment with proof generation/verification.
/// The trie supports key-value storage with deterministic root hashing.
///
/// @par Design Decisions
/// - Standalone data structure (not coupled to storage backend)
/// - BLAKE3 hashing for performance (3x faster than Keccak)
/// - Incremental hashing for <1ms root hash updates
/// - Pluggable node storage via template parameter

#pragma once

#include <memory>
#include <span>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/mpt_node.hpp"
#include "dotvm/core/state/mpt_types.hpp"

namespace dotvm::core::state {

// Forward declaration for proof types
struct MptProof;

/// @brief Merkle Patricia Trie for state commitment
///
/// A Modified Merkle Patricia Trie providing cryptographic commitment to
/// key-value state. Supports efficient inclusion/exclusion proofs.
///
/// @par Thread Safety
/// NOT thread-safe. External synchronization required for concurrent access.
///
/// @par Performance
/// Target: <1ms root hash update for tries with up to 100k keys.
/// Achieved through incremental hashing (only dirty path rehashed).
///
/// @example
/// ```cpp
/// MerklePatriciaTrie trie;
/// trie.insert(key, value);
/// auto root = trie.root_hash();
/// auto proof = trie.get_proof(key);
/// ```
class MerklePatriciaTrie {
public:
    /// Key type: span of bytes
    using Key = std::span<const std::byte>;

    /// Value type: span of bytes
    using Value = std::span<const std::byte>;

    /// Result type with MPT error
    template <typename T>
    using Result = ::dotvm::core::Result<T, MptError>;

    // =========================================================================
    // Constructors
    // =========================================================================

    /// @brief Create an empty trie
    MerklePatriciaTrie();

    /// @brief Destructor
    ~MerklePatriciaTrie();

    /// @brief Move constructor
    MerklePatriciaTrie(MerklePatriciaTrie&& other) noexcept;

    /// @brief Move assignment
    MerklePatriciaTrie& operator=(MerklePatriciaTrie&& other) noexcept;

    // Non-copyable (internal pointers)
    MerklePatriciaTrie(const MerklePatriciaTrie&) = delete;
    MerklePatriciaTrie& operator=(const MerklePatriciaTrie&) = delete;

    // =========================================================================
    // State Queries
    // =========================================================================

    /// @brief Get the root hash of the trie
    ///
    /// For an empty trie, returns zero hash. The root hash uniquely identifies
    /// the entire state of the trie.
    ///
    /// @return 32-byte root hash
    [[nodiscard]] Hash256 root_hash() const noexcept;

    /// @brief Check if the trie is empty
    [[nodiscard]] bool empty() const noexcept;

    /// @brief Get number of key-value pairs
    [[nodiscard]] std::size_t size() const noexcept;

    // =========================================================================
    // Key-Value Operations
    // =========================================================================

    /// @brief Insert or update a key-value pair
    ///
    /// @param key The key to insert
    /// @param value The value to associate with the key
    /// @return Ok on success, or error
    [[nodiscard]] Result<void> insert(Key key, Value value);

    /// @brief Retrieve a value by key
    ///
    /// @param key The key to look up
    /// @return The value if found, or KeyNotFound error
    [[nodiscard]] Result<std::vector<std::byte>> get(Key key) const;

    /// @brief Remove a key-value pair
    ///
    /// @param key The key to remove
    /// @return Ok if removed, or KeyNotFound if not present
    [[nodiscard]] Result<void> remove(Key key);

    /// @brief Check if a key exists
    ///
    /// @param key The key to check
    /// @return true if key exists
    [[nodiscard]] bool contains(Key key) const;

    /// @brief Remove all key-value pairs
    void clear();

    // =========================================================================
    // Proof Operations
    // =========================================================================

    /// @brief Generate an inclusion proof for a key
    ///
    /// Creates a proof that can be used to verify the key exists with its
    /// value, without access to the full trie.
    ///
    /// @param key The key to generate a proof for
    /// @return The proof if key exists, or KeyNotFound error
    [[nodiscard]] Result<MptProof> get_proof(Key key) const;

    /// @brief Generate an exclusion proof for a key
    ///
    /// Creates a proof that can be used to verify the key does NOT exist
    /// in the trie, without access to the full trie.
    ///
    /// @param key The key to generate an exclusion proof for
    /// @return The proof showing key is absent, or InvalidProof if key exists
    [[nodiscard]] Result<MptProof> get_exclusion_proof(Key key) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dotvm::core::state
