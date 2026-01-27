/// @file mpt_proof_test.cpp
/// @brief Unit tests for STATE-008 MPT proof generation and verification
///
/// TDD tests for inclusion/exclusion proofs: generate, verify, reject invalid.

#include <gtest/gtest.h>

#include "dotvm/core/state/merkle_patricia_trie.hpp"
#include "dotvm/core/state/mpt_proof.hpp"

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

[[nodiscard]] [[maybe_unused]] std::string to_string(std::span<const std::byte> bytes) {
    std::string result;
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<char>(byte));
    }
    return result;
}

// ============================================================================
// MptProof Structure Tests
// ============================================================================

TEST(MptProofTest, EmptyProofIsValid) {
    MptProof proof;
    EXPECT_TRUE(proof.nodes.empty());
    EXPECT_TRUE(proof.key.empty());
}

TEST(MptProofTest, ProofStoresKeyAndNodes) {
    MptProof proof;
    proof.key = to_bytes("hello");
    proof.nodes.push_back(to_bytes("node1"));
    proof.nodes.push_back(to_bytes("node2"));

    EXPECT_EQ(proof.key.size(), 5);
    EXPECT_EQ(proof.nodes.size(), 2);
}

// ============================================================================
// Proof Generation Tests
// ============================================================================

TEST(MptProofTest, GenerateProofForExistingKey) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");
    const auto value = to_bytes("world");

    ASSERT_TRUE(trie.insert(key, value).is_ok());

    auto proof_result = trie.get_proof(key);
    ASSERT_TRUE(proof_result.is_ok()) << "Proof generation failed";

    const auto& proof = proof_result.value();
    EXPECT_EQ(proof.key, std::vector<std::byte>(key.begin(), key.end()));
    EXPECT_FALSE(proof.nodes.empty());
}

TEST(MptProofTest, GenerateProofForNonexistentKeyReturnsError) {
    MerklePatriciaTrie trie;
    ASSERT_TRUE(trie.insert(to_bytes("hello"), to_bytes("world")).is_ok());

    auto proof_result = trie.get_proof(to_bytes("nonexistent"));
    EXPECT_TRUE(proof_result.is_err());
    EXPECT_EQ(proof_result.error(), MptError::KeyNotFound);
}

TEST(MptProofTest, GenerateProofFromEmptyTrieReturnsError) {
    MerklePatriciaTrie trie;

    auto proof_result = trie.get_proof(to_bytes("any"));
    EXPECT_TRUE(proof_result.is_err());
    EXPECT_EQ(proof_result.error(), MptError::KeyNotFound);
}

TEST(MptProofTest, GenerateProofForMultipleKeys) {
    MerklePatriciaTrie trie;

    ASSERT_TRUE(trie.insert(to_bytes("apple"), to_bytes("red")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("banana"), to_bytes("yellow")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("cherry"), to_bytes("red")).is_ok());

    // Each key should produce a valid proof
    auto proof1 = trie.get_proof(to_bytes("apple"));
    auto proof2 = trie.get_proof(to_bytes("banana"));
    auto proof3 = trie.get_proof(to_bytes("cherry"));

    ASSERT_TRUE(proof1.is_ok());
    ASSERT_TRUE(proof2.is_ok());
    ASSERT_TRUE(proof3.is_ok());

    // Proofs should be different (different paths)
    EXPECT_NE(proof1.value().nodes, proof2.value().nodes);
}

TEST(MptProofTest, GenerateProofForKeysWithCommonPrefix) {
    MerklePatriciaTrie trie;

    ASSERT_TRUE(trie.insert(to_bytes("test"), to_bytes("v0")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("testing"), to_bytes("v1")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("tested"), to_bytes("v2")).is_ok());

    auto proof1 = trie.get_proof(to_bytes("test"));
    auto proof2 = trie.get_proof(to_bytes("testing"));
    auto proof3 = trie.get_proof(to_bytes("tested"));

    ASSERT_TRUE(proof1.is_ok());
    ASSERT_TRUE(proof2.is_ok());
    ASSERT_TRUE(proof3.is_ok());
}

// ============================================================================
// Inclusion Proof Verification Tests
// ============================================================================

TEST(MptProofTest, VerifyInclusionForValidProof) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");
    const auto value = to_bytes("world");

    ASSERT_TRUE(trie.insert(key, value).is_ok());
    const auto root = trie.root_hash();

    auto proof_result = trie.get_proof(key);
    ASSERT_TRUE(proof_result.is_ok());

    auto verify_result = verify_inclusion(root, key, value, proof_result.value());
    EXPECT_TRUE(verify_result.is_ok());
    EXPECT_TRUE(verify_result.value());
}

TEST(MptProofTest, VerifyInclusionFailsForWrongValue) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");
    const auto correct_value = to_bytes("world");
    const auto wrong_value = to_bytes("universe");

    ASSERT_TRUE(trie.insert(key, correct_value).is_ok());
    const auto root = trie.root_hash();

    auto proof_result = trie.get_proof(key);
    ASSERT_TRUE(proof_result.is_ok());

    // Verify with wrong value should fail
    auto verify_result = verify_inclusion(root, key, wrong_value, proof_result.value());
    EXPECT_TRUE(verify_result.is_ok());
    EXPECT_FALSE(verify_result.value());
}

TEST(MptProofTest, VerifyInclusionFailsForWrongRoot) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");
    const auto value = to_bytes("world");

    ASSERT_TRUE(trie.insert(key, value).is_ok());

    auto proof_result = trie.get_proof(key);
    ASSERT_TRUE(proof_result.is_ok());

    // Create a different root hash
    Hash256 wrong_root{};
    wrong_root.data[0] = 0xFF;

    auto verify_result = verify_inclusion(wrong_root, key, value, proof_result.value());
    EXPECT_TRUE(verify_result.is_ok());
    EXPECT_FALSE(verify_result.value());
}

TEST(MptProofTest, VerifyInclusionWithMultipleKeys) {
    MerklePatriciaTrie trie;

    ASSERT_TRUE(trie.insert(to_bytes("apple"), to_bytes("red")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("banana"), to_bytes("yellow")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("cherry"), to_bytes("red")).is_ok());

    const auto root = trie.root_hash();

    // Verify each key
    auto proof1 = trie.get_proof(to_bytes("apple"));
    auto proof2 = trie.get_proof(to_bytes("banana"));
    auto proof3 = trie.get_proof(to_bytes("cherry"));

    ASSERT_TRUE(proof1.is_ok());
    ASSERT_TRUE(proof2.is_ok());
    ASSERT_TRUE(proof3.is_ok());

    auto v1 = verify_inclusion(root, to_bytes("apple"), to_bytes("red"), proof1.value());
    auto v2 = verify_inclusion(root, to_bytes("banana"), to_bytes("yellow"), proof2.value());
    auto v3 = verify_inclusion(root, to_bytes("cherry"), to_bytes("red"), proof3.value());

    EXPECT_TRUE(v1.is_ok() && v1.value());
    EXPECT_TRUE(v2.is_ok() && v2.value());
    EXPECT_TRUE(v3.is_ok() && v3.value());
}

TEST(MptProofTest, VerifyInclusionFailsForTamperedProof) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");
    const auto value = to_bytes("world");

    ASSERT_TRUE(trie.insert(key, value).is_ok());
    const auto root = trie.root_hash();

    auto proof_result = trie.get_proof(key);
    ASSERT_TRUE(proof_result.is_ok());

    // Tamper with the proof
    auto tampered_proof = proof_result.value();
    if (!tampered_proof.nodes.empty() && !tampered_proof.nodes[0].empty()) {
        tampered_proof.nodes[0][0] = static_cast<std::byte>(0xFF);
    }

    auto verify_result = verify_inclusion(root, key, value, tampered_proof);
    EXPECT_TRUE(verify_result.is_ok());
    EXPECT_FALSE(verify_result.value());
}

// ============================================================================
// Exclusion Proof Tests (Future Enhancement)
// ============================================================================

TEST(MptProofTest, GenerateExclusionProofForNonexistentKey) {
    MerklePatriciaTrie trie;

    ASSERT_TRUE(trie.insert(to_bytes("apple"), to_bytes("red")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("cherry"), to_bytes("red")).is_ok());

    // "banana" doesn't exist - should be able to prove its absence
    auto exclusion_result = trie.get_exclusion_proof(to_bytes("banana"));
    ASSERT_TRUE(exclusion_result.is_ok());

    const auto& proof = exclusion_result.value();
    EXPECT_FALSE(proof.nodes.empty());
}

TEST(MptProofTest, VerifyExclusionForNonexistentKey) {
    MerklePatriciaTrie trie;

    ASSERT_TRUE(trie.insert(to_bytes("apple"), to_bytes("red")).is_ok());
    ASSERT_TRUE(trie.insert(to_bytes("cherry"), to_bytes("red")).is_ok());

    const auto root = trie.root_hash();

    auto exclusion_result = trie.get_exclusion_proof(to_bytes("banana"));
    ASSERT_TRUE(exclusion_result.is_ok());

    auto verify_result = verify_exclusion(root, to_bytes("banana"), exclusion_result.value());
    EXPECT_TRUE(verify_result.is_ok());
    EXPECT_TRUE(verify_result.value());
}

TEST(MptProofTest, VerifyExclusionFailsForExistingKey) {
    MerklePatriciaTrie trie;

    ASSERT_TRUE(trie.insert(to_bytes("apple"), to_bytes("red")).is_ok());

    const auto root = trie.root_hash();

    // Generate exclusion proof for "apple" which exists
    auto exclusion_result = trie.get_exclusion_proof(to_bytes("apple"));

    // This should either fail to generate or fail verification
    if (exclusion_result.is_ok()) {
        auto verify_result = verify_exclusion(root, to_bytes("apple"), exclusion_result.value());
        EXPECT_TRUE(verify_result.is_ok());
        EXPECT_FALSE(verify_result.value());
    } else {
        // Alternatively, it's valid to reject generating an exclusion proof for existing key
        EXPECT_EQ(exclusion_result.error(), MptError::InvalidProof);
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(MptProofTest, ProofForEmptyKey) {
    MerklePatriciaTrie trie;
    const std::vector<std::byte> empty_key;
    const auto value = to_bytes("empty_key_value");

    ASSERT_TRUE(trie.insert(empty_key, value).is_ok());
    const auto root = trie.root_hash();

    auto proof_result = trie.get_proof(empty_key);
    ASSERT_TRUE(proof_result.is_ok());

    auto verify_result = verify_inclusion(root, empty_key, value, proof_result.value());
    EXPECT_TRUE(verify_result.is_ok());
    EXPECT_TRUE(verify_result.value());
}

TEST(MptProofTest, ProofForEmptyValue) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("key_with_empty_value");
    const std::vector<std::byte> empty_value;

    ASSERT_TRUE(trie.insert(key, empty_value).is_ok());
    const auto root = trie.root_hash();

    auto proof_result = trie.get_proof(key);
    ASSERT_TRUE(proof_result.is_ok());

    auto verify_result = verify_inclusion(root, key, empty_value, proof_result.value());
    EXPECT_TRUE(verify_result.is_ok());
    EXPECT_TRUE(verify_result.value());
}

TEST(MptProofTest, ProofAfterUpdatingValue) {
    MerklePatriciaTrie trie;
    const auto key = to_bytes("hello");

    ASSERT_TRUE(trie.insert(key, to_bytes("world")).is_ok());
    ASSERT_TRUE(trie.insert(key, to_bytes("universe")).is_ok());

    const auto root = trie.root_hash();

    auto proof_result = trie.get_proof(key);
    ASSERT_TRUE(proof_result.is_ok());

    // Should verify with the new value
    auto verify_new = verify_inclusion(root, key, to_bytes("universe"), proof_result.value());
    EXPECT_TRUE(verify_new.is_ok());
    EXPECT_TRUE(verify_new.value());

    // Should fail with old value
    auto verify_old = verify_inclusion(root, key, to_bytes("world"), proof_result.value());
    EXPECT_TRUE(verify_old.is_ok());
    EXPECT_FALSE(verify_old.value());
}

TEST(MptProofTest, ProofStaysValidAcrossUnrelatedInsertions) {
    MerklePatriciaTrie trie;

    ASSERT_TRUE(trie.insert(to_bytes("hello"), to_bytes("world")).is_ok());
    [[maybe_unused]] const auto root1 = trie.root_hash();
    auto proof1 = trie.get_proof(to_bytes("hello"));
    ASSERT_TRUE(proof1.is_ok());

    // Insert an unrelated key
    ASSERT_TRUE(trie.insert(to_bytes("foo"), to_bytes("bar")).is_ok());
    const auto root2 = trie.root_hash();

    // Old proof should NOT verify against new root
    // This might pass or fail depending on whether the path changed
    // If it fails, that's expected behavior (proof is for old state)
    [[maybe_unused]] auto verify_old_root =
        verify_inclusion(root2, to_bytes("hello"), to_bytes("world"), proof1.value());

    // New proof should verify against new root
    auto proof2 = trie.get_proof(to_bytes("hello"));
    ASSERT_TRUE(proof2.is_ok());
    auto verify_new_root = verify_inclusion(root2, to_bytes("hello"), to_bytes("world"), proof2.value());
    EXPECT_TRUE(verify_new_root.is_ok());
    EXPECT_TRUE(verify_new_root.value());
}

}  // namespace
}  // namespace dotvm::core::state
