/// @file mpt_types_test.cpp
/// @brief Unit tests for STATE-008 MPT foundation types
///
/// TDD tests for Hash256, Nibbles, MptError, and InMemoryNodeStore.
/// Written before implementation per TDD discipline.

#include <gtest/gtest.h>

#include "dotvm/core/state/mpt_types.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// Hash256 Tests
// ============================================================================

TEST(Hash256Test, ZeroHashIsAllZeroes) {
    constexpr auto zero = Hash256::zero();
    for (const auto byte : zero.data) {
        EXPECT_EQ(byte, 0);
    }
}

TEST(Hash256Test, IsZeroReturnsTrueForZeroHash) {
    constexpr auto zero = Hash256::zero();
    EXPECT_TRUE(zero.is_zero());
}

TEST(Hash256Test, IsZeroReturnsFalseForNonZeroHash) {
    Hash256 hash{};
    hash.data[0] = 0x42;
    EXPECT_FALSE(hash.is_zero());
}

TEST(Hash256Test, EqualityComparison) {
    Hash256 a{};
    Hash256 b{};
    EXPECT_EQ(a, b);

    a.data[15] = 0xFF;
    EXPECT_NE(a, b);

    b.data[15] = 0xFF;
    EXPECT_EQ(a, b);
}

TEST(Hash256Test, ToHexProducesCorrectString) {
    Hash256 hash{};
    // Fill with known pattern: 0x00, 0x01, 0x02, ..., 0x1F
    for (std::size_t i = 0; i < 32; ++i) {
        hash.data[i] = static_cast<std::uint8_t>(i);
    }

    const auto hex = hash.to_hex();
    EXPECT_EQ(hex.size(), 64);  // 32 bytes = 64 hex chars
    EXPECT_EQ(hex.substr(0, 8), "00010203");
    EXPECT_EQ(hex.substr(56, 8), "1c1d1e1f");
}

TEST(Hash256Test, FromHexRoundtrip) {
    Hash256 original{};
    for (std::size_t i = 0; i < 32; ++i) {
        original.data[i] = static_cast<std::uint8_t>(i * 8);
    }

    const auto hex = original.to_hex();
    const auto restored = Hash256::from_hex(hex);

    EXPECT_EQ(original, restored);
}

TEST(Hash256Test, FromHexHandlesUppercase) {
    const auto hash = Hash256::from_hex(
        "DEADBEEF00000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(hash.data[0], 0xDE);
    EXPECT_EQ(hash.data[1], 0xAD);
    EXPECT_EQ(hash.data[2], 0xBE);
    EXPECT_EQ(hash.data[3], 0xEF);
}

TEST(Hash256Test, FromHexHandlesLowercase) {
    const auto hash = Hash256::from_hex(
        "deadbeef00000000000000000000000000000000000000000000000000000000");
    EXPECT_EQ(hash.data[0], 0xDE);
    EXPECT_EQ(hash.data[1], 0xAD);
    EXPECT_EQ(hash.data[2], 0xBE);
    EXPECT_EQ(hash.data[3], 0xEF);
}

// ============================================================================
// Nibbles Tests
// ============================================================================

TEST(NibblesTest, ConstructFromBytesExpandsToNibbles) {
    const std::array<std::byte, 2> bytes = {std::byte{0xAB}, std::byte{0xCD}};
    const Nibbles nibbles(bytes);

    EXPECT_EQ(nibbles.size(), 4);
    EXPECT_EQ(nibbles[0], 0x0A);
    EXPECT_EQ(nibbles[1], 0x0B);
    EXPECT_EQ(nibbles[2], 0x0C);
    EXPECT_EQ(nibbles[3], 0x0D);
}

TEST(NibblesTest, EmptyBytesProducesEmptyNibbles) {
    const std::span<const std::byte> empty;
    const Nibbles nibbles(empty);
    EXPECT_EQ(nibbles.size(), 0);
    EXPECT_TRUE(nibbles.empty());
}

TEST(NibblesTest, SliceReturnsSubrange) {
    const std::array<std::byte, 3> bytes = {std::byte{0x12}, std::byte{0x34},
                                            std::byte{0x56}};
    const Nibbles nibbles(bytes);  // [1, 2, 3, 4, 5, 6]

    const auto slice = nibbles.slice(1, 3);  // [2, 3, 4]
    EXPECT_EQ(slice.size(), 3);
    EXPECT_EQ(slice[0], 0x02);
    EXPECT_EQ(slice[1], 0x03);
    EXPECT_EQ(slice[2], 0x04);
}

TEST(NibblesTest, SliceFromStartReturnsPrefix) {
    const std::array<std::byte, 2> bytes = {std::byte{0xAB}, std::byte{0xCD}};
    const Nibbles nibbles(bytes);

    const auto prefix = nibbles.slice(0, 2);
    EXPECT_EQ(prefix.size(), 2);
    EXPECT_EQ(prefix[0], 0x0A);
    EXPECT_EQ(prefix[1], 0x0B);
}

TEST(NibblesTest, CommonPrefixLengthWithIdentical) {
    const std::array<std::byte, 2> bytes = {std::byte{0xAB}, std::byte{0xCD}};
    const Nibbles a(bytes);
    const Nibbles b(bytes);

    EXPECT_EQ(a.common_prefix_length(b), 4);
}

TEST(NibblesTest, CommonPrefixLengthWithPartialMatch) {
    const std::array<std::byte, 2> bytes_a = {std::byte{0xAB}, std::byte{0xCD}};
    const std::array<std::byte, 2> bytes_b = {std::byte{0xAB}, std::byte{0xEF}};
    const Nibbles a(bytes_a);  // [A, B, C, D]
    const Nibbles b(bytes_b);  // [A, B, E, F]

    EXPECT_EQ(a.common_prefix_length(b), 2);  // A, B match
}

TEST(NibblesTest, CommonPrefixLengthWithNoMatch) {
    const std::array<std::byte, 1> bytes_a = {std::byte{0xAB}};
    const std::array<std::byte, 1> bytes_b = {std::byte{0xCD}};
    const Nibbles a(bytes_a);
    const Nibbles b(bytes_b);

    EXPECT_EQ(a.common_prefix_length(b), 0);
}

TEST(NibblesTest, ToBytesRoundtrip) {
    const std::array<std::byte, 3> original = {std::byte{0x12}, std::byte{0x34},
                                               std::byte{0x56}};
    const Nibbles nibbles(original);
    const auto restored = nibbles.to_bytes();

    EXPECT_EQ(restored.size(), 3);
    EXPECT_EQ(restored[0], std::byte{0x12});
    EXPECT_EQ(restored[1], std::byte{0x34});
    EXPECT_EQ(restored[2], std::byte{0x56});
}

TEST(NibblesTest, ToBytesWithOddNibblesPadsWithZero) {
    const std::array<std::byte, 2> bytes = {std::byte{0x12}, std::byte{0x34}};
    const Nibbles nibbles(bytes);
    const auto slice = nibbles.slice(1, 3);  // [2, 3, 4] - odd count

    const auto restored = slice.to_bytes();
    EXPECT_EQ(restored.size(), 2);
    EXPECT_EQ(restored[0], std::byte{0x23});  // [2, 3]
    EXPECT_EQ(restored[1], std::byte{0x40});  // [4, 0] - padded
}

TEST(NibblesTest, EqualityComparison) {
    const std::array<std::byte, 2> bytes = {std::byte{0xAB}, std::byte{0xCD}};
    const Nibbles a(bytes);
    const Nibbles b(bytes);

    EXPECT_EQ(a, b);

    const std::array<std::byte, 2> different = {std::byte{0xAB}, std::byte{0xCE}};
    const Nibbles c(different);
    EXPECT_NE(a, c);
}

// ============================================================================
// MptError Tests
// ============================================================================

TEST(MptErrorTest, ErrorCodesInCorrectRange) {
    // Key/Value errors (96-103)
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::KeyNotFound), 96);
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::InvalidKey), 97);
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::ValueTooLarge), 98);

    // Node errors (104-111)
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::NodeNotFound), 104);
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::InvalidNode), 105);
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::NodeCorrupted), 106);

    // Proof errors (112-119)
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::InvalidProof), 112);
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::ProofMismatch), 113);
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::IncompleteProof), 114);

    // Storage errors (120-127)
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::StorageError), 120);
    EXPECT_EQ(static_cast<std::uint8_t>(MptError::HashMismatch), 121);
}

TEST(MptErrorTest, ToStringReturnsCorrectNames) {
    EXPECT_STREQ(to_string(MptError::KeyNotFound), "KeyNotFound");
    EXPECT_STREQ(to_string(MptError::InvalidKey), "InvalidKey");
    EXPECT_STREQ(to_string(MptError::ValueTooLarge), "ValueTooLarge");
    EXPECT_STREQ(to_string(MptError::NodeNotFound), "NodeNotFound");
    EXPECT_STREQ(to_string(MptError::InvalidNode), "InvalidNode");
    EXPECT_STREQ(to_string(MptError::NodeCorrupted), "NodeCorrupted");
    EXPECT_STREQ(to_string(MptError::InvalidProof), "InvalidProof");
    EXPECT_STREQ(to_string(MptError::ProofMismatch), "ProofMismatch");
    EXPECT_STREQ(to_string(MptError::IncompleteProof), "IncompleteProof");
    EXPECT_STREQ(to_string(MptError::StorageError), "StorageError");
    EXPECT_STREQ(to_string(MptError::HashMismatch), "HashMismatch");
}

TEST(MptErrorTest, KeyNotFoundIsRecoverable) {
    // Key not found is a normal condition, not a corruption
    EXPECT_TRUE(is_recoverable(MptError::KeyNotFound));
}

TEST(MptErrorTest, CorruptionErrorsAreNotRecoverable) {
    EXPECT_FALSE(is_recoverable(MptError::NodeCorrupted));
    EXPECT_FALSE(is_recoverable(MptError::HashMismatch));
}

TEST(MptErrorTest, ProofErrorsAreNotRecoverable) {
    EXPECT_FALSE(is_recoverable(MptError::InvalidProof));
    EXPECT_FALSE(is_recoverable(MptError::ProofMismatch));
    EXPECT_FALSE(is_recoverable(MptError::IncompleteProof));
}

// ============================================================================
// InMemoryNodeStore Tests
// ============================================================================

TEST(InMemoryNodeStoreTest, PutAndGetNode) {
    InMemoryNodeStore store;

    Hash256 hash{};
    hash.data[0] = 0x42;

    const std::vector<std::byte> node_data = {std::byte{0x01}, std::byte{0x02},
                                              std::byte{0x03}};

    store.put(hash, node_data);
    const auto retrieved = store.get(hash);

    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, node_data);
}

TEST(InMemoryNodeStoreTest, GetNonexistentReturnsNullopt) {
    InMemoryNodeStore store;

    Hash256 hash{};
    hash.data[0] = 0xFF;

    const auto result = store.get(hash);
    EXPECT_FALSE(result.has_value());
}

TEST(InMemoryNodeStoreTest, ContainsReturnsTrueForExistingNode) {
    InMemoryNodeStore store;

    Hash256 hash{};
    hash.data[0] = 0x42;

    const std::vector<std::byte> node_data = {std::byte{0x01}};
    store.put(hash, node_data);

    EXPECT_TRUE(store.contains(hash));
}

TEST(InMemoryNodeStoreTest, ContainsReturnsFalseForMissingNode) {
    InMemoryNodeStore store;

    Hash256 hash{};
    hash.data[0] = 0x42;

    EXPECT_FALSE(store.contains(hash));
}

TEST(InMemoryNodeStoreTest, RemoveDeletesNode) {
    InMemoryNodeStore store;

    Hash256 hash{};
    hash.data[0] = 0x42;

    const std::vector<std::byte> node_data = {std::byte{0x01}};
    store.put(hash, node_data);

    EXPECT_TRUE(store.contains(hash));

    store.remove(hash);

    EXPECT_FALSE(store.contains(hash));
    EXPECT_FALSE(store.get(hash).has_value());
}

TEST(InMemoryNodeStoreTest, RemoveNonexistentIsNoOp) {
    InMemoryNodeStore store;

    Hash256 hash{};
    hash.data[0] = 0x42;

    // Should not throw
    store.remove(hash);
    EXPECT_FALSE(store.contains(hash));
}

TEST(InMemoryNodeStoreTest, SizeReflectsNodeCount) {
    InMemoryNodeStore store;
    EXPECT_EQ(store.size(), 0);

    Hash256 hash1{};
    hash1.data[0] = 0x01;
    const std::vector<std::byte> data1 = {std::byte{0x01}};
    store.put(hash1, data1);
    EXPECT_EQ(store.size(), 1);

    Hash256 hash2{};
    hash2.data[0] = 0x02;
    const std::vector<std::byte> data2 = {std::byte{0x02}};
    store.put(hash2, data2);
    EXPECT_EQ(store.size(), 2);

    store.remove(hash1);
    EXPECT_EQ(store.size(), 1);
}

TEST(InMemoryNodeStoreTest, ClearRemovesAllNodes) {
    InMemoryNodeStore store;

    Hash256 hash1{};
    hash1.data[0] = 0x01;
    Hash256 hash2{};
    hash2.data[0] = 0x02;

    const std::vector<std::byte> data1 = {std::byte{0x01}};
    const std::vector<std::byte> data2 = {std::byte{0x02}};
    store.put(hash1, data1);
    store.put(hash2, data2);
    EXPECT_EQ(store.size(), 2);

    store.clear();

    EXPECT_EQ(store.size(), 0);
    EXPECT_FALSE(store.contains(hash1));
    EXPECT_FALSE(store.contains(hash2));
}

TEST(InMemoryNodeStoreTest, PutOverwritesExistingNode) {
    InMemoryNodeStore store;

    Hash256 hash{};
    hash.data[0] = 0x42;

    const std::vector<std::byte> original = {std::byte{0x01}};
    const std::vector<std::byte> updated = {std::byte{0x02}, std::byte{0x03}};

    store.put(hash, original);
    store.put(hash, updated);

    const auto retrieved = store.get(hash);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, updated);
    EXPECT_EQ(store.size(), 1);  // Still only one node
}

}  // namespace
}  // namespace dotvm::core::state
