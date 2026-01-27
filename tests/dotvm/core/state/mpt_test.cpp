/// @file mpt_test.cpp
/// @brief Unit tests for STATE-008 MerklePatriciaTrie core operations
///
/// TDD tests for the main trie class: insert, get, remove, root hash.

#include <gtest/gtest.h>

#include "dotvm/core/state/merkle_patricia_trie.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

[[nodiscard]] std::vector<std::byte> to_bytes(std::string_view str) {
    std::vector<std::byte> result;
    result.reserve(str.size());
    for (const char c : str) {
        result.push_back(static_cast<std::byte>(c));
    }
    return result;
}

[[nodiscard]] std::string to_string(std::span<const std::byte> bytes) {
    std::string result;
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<char>(byte));
    }
    return result;
}

// ============================================================================
// Empty Trie Tests
// ============================================================================

TEST(MerklePatriciaTrieTest, EmptyTrieHasZeroRootHash) {
    MerklePatriciaTrie trie;
    EXPECT_TRUE(trie.root_hash().is_zero());
}

TEST(MerklePatriciaTrieTest, EmptyTrieIsEmpty) {
    MerklePatriciaTrie trie;
    EXPECT_TRUE(trie.empty());
    EXPECT_EQ(trie.size(), 0);
}

TEST(MerklePatriciaTrieTest, GetFromEmptyTrieReturnsKeyNotFound) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");
    const auto result = trie.get(key);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), MptError::KeyNotFound);
}

// ============================================================================
// Single Key Tests
// ============================================================================

TEST(MerklePatriciaTrieTest, InsertSingleKey) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");
    const auto value = to_bytes("world");

    const auto insert_result = trie.insert(key, value);
    ASSERT_TRUE(insert_result.is_ok()) << "Insert failed: " << to_string(insert_result.error());

    EXPECT_FALSE(trie.empty());
    EXPECT_EQ(trie.size(), 1);
}

TEST(MerklePatriciaTrieTest, InsertAndRetrieveSingleKey) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");
    const auto value = to_bytes("world");

    auto insert_result = trie.insert(key, value);
    ASSERT_TRUE(insert_result.is_ok());

    auto get_result = trie.get(key);
    ASSERT_TRUE(get_result.is_ok()) << "Get failed: " << to_string(get_result.error());
    EXPECT_EQ(to_string(get_result.value()), "world");
}

TEST(MerklePatriciaTrieTest, InsertChangesRootHash) {
    MerklePatriciaTrie trie;
    const auto initial_hash = trie.root_hash();

    const auto key = to_bytes("key");
    const auto value = to_bytes("value");
    auto result = trie.insert(key, value);
    ASSERT_TRUE(result.is_ok());

    const auto new_hash = trie.root_hash();
    EXPECT_NE(initial_hash, new_hash);
    EXPECT_FALSE(new_hash.is_zero());
}

// ============================================================================
// Multiple Keys Tests
// ============================================================================

TEST(MerklePatriciaTrieTest, InsertMultipleDistinctKeys) {
    MerklePatriciaTrie trie;

    const auto keys = std::array{"apple", "banana", "cherry"};
    const auto values = std::array{"red", "yellow", "red"};

    for (std::size_t i = 0; i < keys.size(); ++i) {
        auto result = trie.insert(to_bytes(keys[i]), to_bytes(values[i]));
        ASSERT_TRUE(result.is_ok()) << "Insert " << keys[i] << " failed";
    }

    EXPECT_EQ(trie.size(), 3);

    // Verify all keys are retrievable
    for (std::size_t i = 0; i < keys.size(); ++i) {
        auto result = trie.get(to_bytes(keys[i]));
        ASSERT_TRUE(result.is_ok()) << "Get " << keys[i] << " failed";
        EXPECT_EQ(to_string(result.value()), values[i]);
    }
}

TEST(MerklePatriciaTrieTest, InsertKeysWithCommonPrefix) {
    MerklePatriciaTrie trie;

    // Keys sharing common prefix "test"
    auto r1 = trie.insert(to_bytes("test"), to_bytes("v0"));
    auto r2 = trie.insert(to_bytes("testing"), to_bytes("v1"));
    auto r3 = trie.insert(to_bytes("tested"), to_bytes("v2"));

    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    ASSERT_TRUE(r3.is_ok());

    EXPECT_EQ(trie.size(), 3);

    EXPECT_EQ(to_string(trie.get(to_bytes("test")).value()), "v0");
    EXPECT_EQ(to_string(trie.get(to_bytes("testing")).value()), "v1");
    EXPECT_EQ(to_string(trie.get(to_bytes("tested")).value()), "v2");
}

TEST(MerklePatriciaTrieTest, InsertKeyThatIsPrefixOfExisting) {
    MerklePatriciaTrie trie;

    // Insert longer key first, then prefix
    auto r1 = trie.insert(to_bytes("testing"), to_bytes("long"));
    auto r2 = trie.insert(to_bytes("test"), to_bytes("short"));

    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());

    EXPECT_EQ(trie.size(), 2);
    EXPECT_EQ(to_string(trie.get(to_bytes("testing")).value()), "long");
    EXPECT_EQ(to_string(trie.get(to_bytes("test")).value()), "short");
}

// ============================================================================
// Update Tests
// ============================================================================

TEST(MerklePatriciaTrieTest, UpdateExistingKey) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");

    auto r1 = trie.insert(key, to_bytes("world"));
    ASSERT_TRUE(r1.is_ok());

    auto r2 = trie.insert(key, to_bytes("universe"));
    ASSERT_TRUE(r2.is_ok());

    EXPECT_EQ(trie.size(), 1);  // Still just one key

    auto get_result = trie.get(key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "universe");
}

TEST(MerklePatriciaTrieTest, UpdateChangesRootHash) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("key");

    ASSERT_TRUE(trie.insert(key, to_bytes("value1")).is_ok());
    const auto hash1 = trie.root_hash();

    ASSERT_TRUE(trie.insert(key, to_bytes("value2")).is_ok());
    const auto hash2 = trie.root_hash();

    EXPECT_NE(hash1, hash2);
}

// ============================================================================
// Remove Tests
// ============================================================================

TEST(MerklePatriciaTrieTest, RemoveOnlyKey) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");

    ASSERT_TRUE(trie.insert(key, to_bytes("world")).is_ok());
    ASSERT_EQ(trie.size(), 1);

    auto remove_result = trie.remove(key);
    ASSERT_TRUE(remove_result.is_ok()) << "Remove failed: " << to_string(remove_result.error());

    EXPECT_TRUE(trie.empty());
    EXPECT_TRUE(trie.root_hash().is_zero());

    // Key should no longer exist
    auto get_result = trie.get(key);
    EXPECT_TRUE(get_result.is_err());
    EXPECT_EQ(get_result.error(), MptError::KeyNotFound);
}

TEST(MerklePatriciaTrieTest, RemoveOneOfMultipleKeys) {
    MerklePatriciaTrie trie;

    ASSERT_TRUE(trie.insert(to_bytes("apple"), to_bytes("red")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("banana"), to_bytes("yellow")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("cherry"), to_bytes("red")).is_ok());

    auto result = trie.remove(to_bytes("banana"));
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(trie.size(), 2);

    // Removed key should not exist
    EXPECT_TRUE(trie.get(to_bytes("banana")).is_err());

    // Other keys should still exist
    EXPECT_EQ(to_string(trie.get(to_bytes("apple")).value()), "red");
    EXPECT_EQ(to_string(trie.get(to_bytes("cherry")).value()), "red");
}

TEST(MerklePatriciaTrieTest, RemoveNonexistentKeyReturnsError) {
    MerklePatriciaTrie trie;
    ASSERT_TRUE(trie.insert(to_bytes("hello"), to_bytes("world")).is_ok());

    auto result = trie.remove(to_bytes("nonexistent"));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), MptError::KeyNotFound);
}

TEST(MerklePatriciaTrieTest, RemoveFromEmptyTrieReturnsError) {
    MerklePatriciaTrie trie;

    auto result = trie.remove(to_bytes("any"));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), MptError::KeyNotFound);
}

// ============================================================================
// Root Hash Determinism Tests
// ============================================================================

TEST(MerklePatriciaTrieTest, SameKeysSameOrderSameHash) {
    MerklePatriciaTrie trie1;
    MerklePatriciaTrie trie2;

    const auto keys = std::array{"a", "b", "c"};
    const auto values = std::array{"1", "2", "3"};

    for (std::size_t i = 0; i < keys.size(); ++i) {
        ASSERT_TRUE(trie1.insert(to_bytes(keys[i]), to_bytes(values[i])).is_ok());
        ASSERT_TRUE(trie2.insert(to_bytes(keys[i]), to_bytes(values[i])).is_ok());
    }

    EXPECT_EQ(trie1.root_hash(), trie2.root_hash());
}

TEST(MerklePatriciaTrieTest, SameKeysDifferentOrderSameHash) {
    MerklePatriciaTrie trie1;
    MerklePatriciaTrie trie2;

    // Insert in different orders
    ASSERT_TRUE(trie1.insert(to_bytes("a"), to_bytes("1")).is_ok());
    ASSERT_TRUE(trie1.insert(to_bytes("b"), to_bytes("2")).is_ok());
    ASSERT_TRUE(trie1.insert(to_bytes("c"), to_bytes("3")).is_ok());

    ASSERT_TRUE(trie2.insert(to_bytes("c"), to_bytes("3")).is_ok());
    ASSERT_TRUE(trie2.insert(to_bytes("a"), to_bytes("1")).is_ok());
    ASSERT_TRUE(trie2.insert(to_bytes("b"), to_bytes("2")).is_ok());

    // Same final state should yield same hash
    EXPECT_EQ(trie1.root_hash(), trie2.root_hash());
}

TEST(MerklePatriciaTrieTest, DifferentKeysDifferentHash) {
    MerklePatriciaTrie trie1;
    MerklePatriciaTrie trie2;

    ASSERT_TRUE(trie1.insert(to_bytes("hello"), to_bytes("world")).is_ok());
    ASSERT_TRUE(trie2.insert(to_bytes("hello"), to_bytes("universe")).is_ok());

    EXPECT_NE(trie1.root_hash(), trie2.root_hash());
}

// ============================================================================
// Contains Tests
// ============================================================================

TEST(MerklePatriciaTrieTest, ContainsReturnsTrueForExistingKey) {
    MerklePatriciaTrie trie;
    ASSERT_TRUE(trie.insert(to_bytes("hello"), to_bytes("world")).is_ok());

    EXPECT_TRUE(trie.contains(to_bytes("hello")));
}

TEST(MerklePatriciaTrieTest, ContainsReturnsFalseForMissingKey) {
    MerklePatriciaTrie trie;
    ASSERT_TRUE(trie.insert(to_bytes("hello"), to_bytes("world")).is_ok());

    EXPECT_FALSE(trie.contains(to_bytes("world")));
}

TEST(MerklePatriciaTrieTest, ContainsReturnsFalseForEmptyTrie) {
    MerklePatriciaTrie trie;
    EXPECT_FALSE(trie.contains(to_bytes("anything")));
}

// ============================================================================
// Clear Tests
// ============================================================================

TEST(MerklePatriciaTrieTest, ClearRemovesAllKeys) {
    MerklePatriciaTrie trie;

    ASSERT_TRUE(trie.insert(to_bytes("a"), to_bytes("1")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("b"), to_bytes("2")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("c"), to_bytes("3")).is_ok());

    EXPECT_EQ(trie.size(), 3);

    trie.clear();

    EXPECT_TRUE(trie.empty());
    EXPECT_EQ(trie.size(), 0);
    EXPECT_TRUE(trie.root_hash().is_zero());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(MerklePatriciaTrieTest, EmptyKeyIsValid) {
    MerklePatriciaTrie trie;
    const std::vector<std::byte> empty_key;
    const auto value = to_bytes("empty");

    auto result = trie.insert(empty_key, value);
    ASSERT_TRUE(result.is_ok());

    auto get_result = trie.get(empty_key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_EQ(to_string(get_result.value()), "empty");
}

TEST(MerklePatriciaTrieTest, EmptyValueIsValid) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("key");
    const std::vector<std::byte> empty_value;

    auto result = trie.insert(key, empty_value);
    ASSERT_TRUE(result.is_ok());

    auto get_result = trie.get(key);
    ASSERT_TRUE(get_result.is_ok());
    EXPECT_TRUE(get_result.value().empty());
}

TEST(MerklePatriciaTrieTest, SingleByteKeys) {
    MerklePatriciaTrie trie;

    // All single-byte keys 0x00 through 0x0F
    for (int i = 0; i < 16; ++i) {
        const std::vector<std::byte> key = {static_cast<std::byte>(i)};
        const std::vector<std::byte> value = {static_cast<std::byte>(i * 2)};
        auto result = trie.insert(key, value);
        ASSERT_TRUE(result.is_ok()) << "Insert of key " << i << " failed";
    }

    EXPECT_EQ(trie.size(), 16);

    // Verify all
    for (int i = 0; i < 16; ++i) {
        const std::vector<std::byte> key = {static_cast<std::byte>(i)};
        auto result = trie.get(key);
        ASSERT_TRUE(result.is_ok()) << "Get of key " << i << " failed";
        EXPECT_EQ(result.value()[0], static_cast<std::byte>(i * 2));
    }
}

}  // namespace
}  // namespace dotvm::core::state
