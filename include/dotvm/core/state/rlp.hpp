/// @file rlp.hpp
/// @brief STATE-008 RLP (Recursive Length Prefix) encoding
///
/// Minimal RLP implementation for Merkle Patricia Trie node serialization.
/// Supports encoding/decoding byte strings and lists.
///
/// @par RLP Encoding Rules
/// - Single byte 0x00-0x7F: Encoded as itself
/// - String 0-55 bytes: Prefix 0x80 + length, then data
/// - String 56+ bytes: Prefix 0xB7 + length-of-length, length bytes, then data
/// - List 0-55 bytes payload: Prefix 0xC0 + length, then items
/// - List 56+ bytes payload: Prefix 0xF7 + length-of-length, length bytes, then items

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/mpt_types.hpp"

namespace dotvm::core::state {

/// @brief RLP encoding/decoding utilities
namespace rlp {

// ============================================================================
// Encoding Functions
// ============================================================================

/// @brief Encode a byte string using RLP
///
/// @param data The byte string to encode
/// @return RLP-encoded bytes
[[nodiscard]] std::vector<std::byte> encode(std::span<const std::byte> data);

/// @brief Encode a list of byte strings using RLP
///
/// Each item is RLP-encoded first, then wrapped in a list encoding.
///
/// @param items List of byte strings to encode
/// @return RLP-encoded list
[[nodiscard]] std::vector<std::byte> encode_list(const std::vector<std::vector<std::byte>>& items);

/// @brief Encode a list of pre-encoded items
///
/// Use this when items are already RLP-encoded to avoid double-encoding.
///
/// @param encoded_items List of already-encoded RLP items
/// @return RLP list wrapper around the items
[[nodiscard]] std::vector<std::byte>
encode_list_raw(std::span<const std::span<const std::byte>> encoded_items);

// ============================================================================
// Decoding Functions
// ============================================================================

/// @brief Decode an RLP-encoded byte string
///
/// @param data RLP-encoded data
/// @return Decoded byte string, or error if malformed
[[nodiscard]] Result<std::vector<std::byte>, MptError> decode(std::span<const std::byte> data);

/// @brief Decode an RLP-encoded list
///
/// @param data RLP-encoded list data
/// @return Vector of decoded items, or error if malformed
[[nodiscard]] Result<std::vector<std::vector<std::byte>>, MptError>
decode_list(std::span<const std::byte> data);

// ============================================================================
// Helper Types
// ============================================================================

/// @brief Result of decoding a single RLP item with position tracking
struct DecodeResult {
    std::vector<std::byte> data;  ///< Decoded data
    std::size_t bytes_consumed;   ///< Number of bytes consumed from input
};

/// @brief Decode a single RLP item from a stream
///
/// @param data Input data
/// @return Decoded item and bytes consumed, or error
[[nodiscard]] Result<DecodeResult, MptError> decode_item(std::span<const std::byte> data);

}  // namespace rlp

}  // namespace dotvm::core::state
