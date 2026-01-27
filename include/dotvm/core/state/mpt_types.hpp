/// @file mpt_types.hpp
/// @brief STATE-008 MPT foundation types
///
/// Core types for Merkle Patricia Trie implementation:
/// - Hash256: 32-byte hash digest
/// - Nibbles: 4-bit path segments for trie traversal
/// - MptError: Error codes for MPT operations
/// - InMemoryNodeStore: In-memory node storage implementation

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dotvm::core::state {

// ============================================================================
// Hash256 - 32-byte hash digest
// ============================================================================

/// @brief 32-byte (256-bit) hash digest
///
/// Used as node identifiers in the Merkle Patricia Trie. Compatible with
/// BLAKE3 and other 256-bit hash functions.
///
/// @par Thread Safety
/// This is a value type with no shared state. Instances are thread-safe
/// for concurrent read access.
struct Hash256 {
    /// @brief Raw hash bytes (32 bytes / 256 bits)
    std::array<std::uint8_t, 32> data{};

    /// @brief Create a zero-initialized hash
    /// @return Hash with all bytes set to zero
    [[nodiscard]] static constexpr Hash256 zero() noexcept { return Hash256{}; }

    /// @brief Check if this hash is all zeroes
    /// @return true if all 32 bytes are zero
    [[nodiscard]] constexpr bool is_zero() const noexcept {
        for (const auto byte : data) {
            if (byte != 0) {
                return false;
            }
        }
        return true;
    }

    /// @brief Convert hash to lowercase hex string
    /// @return 64-character hex string representation
    [[nodiscard]] std::string to_hex() const;

    /// @brief Parse hash from hex string
    /// @param hex 64-character hex string (case-insensitive)
    /// @return Parsed Hash256
    /// @pre hex.size() == 64
    [[nodiscard]] static Hash256 from_hex(std::string_view hex);

    /// @brief Equality comparison
    [[nodiscard]] constexpr bool operator==(const Hash256& other) const noexcept = default;
};

}  // namespace dotvm::core::state

// Hash specialization for std::unordered_map
template <>
struct std::hash<dotvm::core::state::Hash256> {
    std::size_t operator()(const dotvm::core::state::Hash256& h) const noexcept {
        // Use first 8 bytes as hash (sufficient for hash table distribution)
        std::size_t result = 0;
        for (std::size_t i = 0; i < sizeof(std::size_t) && i < h.data.size(); ++i) {
            result |= static_cast<std::size_t>(h.data[i]) << (i * 8);
        }
        return result;
    }
};

namespace dotvm::core::state {

// ============================================================================
// Nibbles - 4-bit path segments
// ============================================================================

/// @brief Sequence of 4-bit values (nibbles) for trie path representation
///
/// In a Merkle Patricia Trie, keys are split into nibbles (4-bit segments)
/// for traversal. Each nibble corresponds to one of 16 possible branches.
///
/// @par Example
/// The key 0xABCD becomes nibbles [0xA, 0xB, 0xC, 0xD].
///
/// @par Thread Safety
/// This is a value type. Instances are thread-safe for concurrent read access.
class Nibbles {
public:
    /// @brief Construct empty nibbles
    Nibbles() = default;

    /// @brief Construct from raw bytes, expanding each byte to two nibbles
    /// @param bytes Raw key bytes
    explicit Nibbles(std::span<const std::byte> bytes);

    /// @brief Get number of nibbles
    [[nodiscard]] std::size_t size() const noexcept { return nibbles_.size(); }

    /// @brief Check if empty
    [[nodiscard]] bool empty() const noexcept { return nibbles_.empty(); }

    /// @brief Access nibble at index
    /// @param index Nibble index
    /// @return Nibble value (0-15)
    /// @pre index < size()
    [[nodiscard]] std::uint8_t operator[](std::size_t index) const { return nibbles_[index]; }

    /// @brief Extract a slice of nibbles
    /// @param start Starting index
    /// @param len Number of nibbles to extract
    /// @return New Nibbles containing the slice
    /// @pre start + len <= size()
    [[nodiscard]] Nibbles slice(std::size_t start, std::size_t len) const;

    /// @brief Calculate length of common prefix with another nibbles sequence
    /// @param other Nibbles to compare against
    /// @return Number of matching nibbles from the start
    [[nodiscard]] std::size_t common_prefix_length(const Nibbles& other) const noexcept;

    /// @brief Convert nibbles back to bytes
    ///
    /// If there's an odd number of nibbles, the last byte is padded with 0
    /// in the low nibble position.
    ///
    /// @return Packed byte representation
    [[nodiscard]] std::vector<std::byte> to_bytes() const;

    /// @brief Equality comparison
    [[nodiscard]] bool operator==(const Nibbles& other) const noexcept = default;

    /// @brief Get raw nibble data for iteration
    [[nodiscard]] const std::vector<std::uint8_t>& raw() const noexcept { return nibbles_; }

private:
    /// @brief Internal constructor from raw nibble vector
    explicit Nibbles(std::vector<std::uint8_t> nibbles) : nibbles_(std::move(nibbles)) {}

    std::vector<std::uint8_t> nibbles_;
};

// ============================================================================
// MptError - Error codes for MPT operations
// ============================================================================

/// @brief Error codes for Merkle Patricia Trie operations
///
/// Error codes are grouped by category in the 96-127 range:
/// - 96-103: Key/Value errors
/// - 104-111: Node errors
/// - 112-119: Proof errors
/// - 120-127: Storage errors
enum class MptError : std::uint8_t {
    // Key/Value errors (96-103)
    KeyNotFound = 96,    ///< Key does not exist in trie
    InvalidKey = 97,     ///< Key is malformed or empty
    ValueTooLarge = 98,  ///< Value exceeds maximum allowed size

    // Node errors (104-111)
    NodeNotFound = 104,   ///< Referenced node not in store
    InvalidNode = 105,    ///< Node data is malformed
    NodeCorrupted = 106,  ///< Node data fails integrity check

    // Proof errors (112-119)
    InvalidProof = 112,     ///< Proof structure is invalid
    ProofMismatch = 113,    ///< Proof does not match expected root
    IncompleteProof = 114,  ///< Proof is missing required nodes

    // Storage errors (120-127)
    StorageError = 120,  ///< Generic storage backend error
    HashMismatch = 121,  ///< Computed hash doesn't match expected
};

/// @brief Convert MPT error to human-readable string
/// @param error The error code
/// @return String name of the error
[[nodiscard]] constexpr const char* to_string(MptError error) noexcept {
    switch (error) {
        case MptError::KeyNotFound:
            return "KeyNotFound";
        case MptError::InvalidKey:
            return "InvalidKey";
        case MptError::ValueTooLarge:
            return "ValueTooLarge";
        case MptError::NodeNotFound:
            return "NodeNotFound";
        case MptError::InvalidNode:
            return "InvalidNode";
        case MptError::NodeCorrupted:
            return "NodeCorrupted";
        case MptError::InvalidProof:
            return "InvalidProof";
        case MptError::ProofMismatch:
            return "ProofMismatch";
        case MptError::IncompleteProof:
            return "IncompleteProof";
        case MptError::StorageError:
            return "StorageError";
        case MptError::HashMismatch:
            return "HashMismatch";
    }
    return "Unknown";
}

/// @brief Check if an MPT error is recoverable
///
/// Recoverable errors indicate a normal condition (like key not found) rather
/// than corruption or system failure.
///
/// @param error The error to check
/// @return true if the error represents a recoverable condition
[[nodiscard]] constexpr bool is_recoverable(MptError error) noexcept {
    switch (error) {
        case MptError::KeyNotFound:
            // Key not found is a normal query result
            return true;
        default:
            // All other errors indicate corruption or invalid state
            return false;
    }
}

// ============================================================================
// InMemoryNodeStore - In-memory node storage
// ============================================================================

/// @brief In-memory storage for MPT nodes
///
/// Provides hash-addressed storage for serialized node data. Used for testing
/// and as a reference implementation. Production systems would use persistent
/// storage.
///
/// @par Thread Safety
/// NOT thread-safe. External synchronization required for concurrent access.
class InMemoryNodeStore {
public:
    /// @brief Store a node
    /// @param hash Node's content hash (key)
    /// @param data Serialized node data (value)
    void put(const Hash256& hash, std::span<const std::byte> data);

    /// @brief Retrieve a node by hash
    /// @param hash Node's content hash
    /// @return Node data if found, nullopt otherwise
    [[nodiscard]] std::optional<std::vector<std::byte>> get(const Hash256& hash) const;

    /// @brief Check if a node exists
    /// @param hash Node's content hash
    /// @return true if node is in store
    [[nodiscard]] bool contains(const Hash256& hash) const;

    /// @brief Remove a node
    /// @param hash Node's content hash
    void remove(const Hash256& hash);

    /// @brief Get number of stored nodes
    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }

    /// @brief Remove all nodes
    void clear() { nodes_.clear(); }

private:
    std::unordered_map<Hash256, std::vector<std::byte>> nodes_;
};

}  // namespace dotvm::core::state
