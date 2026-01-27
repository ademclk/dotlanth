/// @file mpt_node_test.cpp
/// @brief Unit tests for STATE-008 MPT node types
///
/// TDD tests for BranchNode, ExtensionNode, LeafNode with Hex-Prefix encoding.

#include <gtest/gtest.h>

#include "dotvm/core/state/mpt_node.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// Hex-Prefix (HP) Encoding Tests
// ============================================================================

TEST(HexPrefixTest, EncodeEvenNibblesExtension) {
    // Extension path [1, 2, 3, 4] -> even length, no terminator
    // HP: 0x00 (flag) + packed nibbles
    const std::array<std::byte, 2> bytes = {std::byte{0x12}, std::byte{0x34}};
    const Nibbles path(bytes);

    const auto hp = hp_encode(path, /*is_leaf=*/false);

    ASSERT_EQ(hp.size(), 3);
    EXPECT_EQ(hp[0], std::byte{0x00});  // even extension flag
    EXPECT_EQ(hp[1], std::byte{0x12});
    EXPECT_EQ(hp[2], std::byte{0x34});
}

TEST(HexPrefixTest, EncodeOddNibblesExtension) {
    // Extension path [1, 2, 3] -> odd length, no terminator
    // HP: 0x1X (flag with first nibble) + remaining
    const std::array<std::byte, 2> bytes = {std::byte{0x12}, std::byte{0x34}};
    const Nibbles full_path(bytes);
    const auto path = full_path.slice(0, 3);  // [1, 2, 3]

    const auto hp = hp_encode(path, /*is_leaf=*/false);

    ASSERT_EQ(hp.size(), 2);
    EXPECT_EQ(hp[0], std::byte{0x11});  // odd extension (0x10) + first nibble (1)
    EXPECT_EQ(hp[1], std::byte{0x23});  // remaining nibbles
}

TEST(HexPrefixTest, EncodeEvenNibblesLeaf) {
    // Leaf path [1, 2, 3, 4] -> even length, with terminator
    // HP: 0x20 (flag) + packed nibbles
    const std::array<std::byte, 2> bytes = {std::byte{0x12}, std::byte{0x34}};
    const Nibbles path(bytes);

    const auto hp = hp_encode(path, /*is_leaf=*/true);

    ASSERT_EQ(hp.size(), 3);
    EXPECT_EQ(hp[0], std::byte{0x20});  // even leaf flag
    EXPECT_EQ(hp[1], std::byte{0x12});
    EXPECT_EQ(hp[2], std::byte{0x34});
}

TEST(HexPrefixTest, EncodeOddNibblesLeaf) {
    // Leaf path [A, B, C] -> odd length, with terminator
    // HP: 0x3X (flag with first nibble)
    const std::array<std::byte, 2> bytes = {std::byte{0xAB}, std::byte{0xC0}};
    const Nibbles full_path(bytes);
    const auto path = full_path.slice(0, 3);  // [A, B, C]

    const auto hp = hp_encode(path, /*is_leaf=*/true);

    ASSERT_EQ(hp.size(), 2);
    EXPECT_EQ(hp[0], std::byte{0x3A});  // odd leaf (0x30) + first nibble (A)
    EXPECT_EQ(hp[1], std::byte{0xBC});  // remaining nibbles
}

TEST(HexPrefixTest, EncodeEmptyPath) {
    // Empty path -> 0x00 for extension, 0x20 for leaf
    const Nibbles empty_path;

    const auto hp_ext = hp_encode(empty_path, /*is_leaf=*/false);
    ASSERT_EQ(hp_ext.size(), 1);
    EXPECT_EQ(hp_ext[0], std::byte{0x00});

    const auto hp_leaf = hp_encode(empty_path, /*is_leaf=*/true);
    ASSERT_EQ(hp_leaf.size(), 1);
    EXPECT_EQ(hp_leaf[0], std::byte{0x20});
}

TEST(HexPrefixTest, DecodeSingleNibbleExtension) {
    // HP 0x11 = odd extension with nibble 1
    const std::vector<std::byte> hp = {std::byte{0x11}};
    const auto result = hp_decode(hp);

    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(result.value().is_leaf);
    EXPECT_EQ(result.value().path.size(), 1);
    EXPECT_EQ(result.value().path[0], 1);
}

TEST(HexPrefixTest, DecodeMultipleNibblesLeaf) {
    // HP 0x20 0x12 0x34 = even leaf [1,2,3,4]
    const std::vector<std::byte> hp = {std::byte{0x20}, std::byte{0x12}, std::byte{0x34}};
    const auto result = hp_decode(hp);

    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().is_leaf);
    EXPECT_EQ(result.value().path.size(), 4);
    EXPECT_EQ(result.value().path[0], 1);
    EXPECT_EQ(result.value().path[1], 2);
    EXPECT_EQ(result.value().path[2], 3);
    EXPECT_EQ(result.value().path[3], 4);
}

TEST(HexPrefixTest, RoundtripEvenExtension) {
    const std::array<std::byte, 3> bytes = {std::byte{0x12}, std::byte{0x34}, std::byte{0x56}};
    const Nibbles original(bytes);

    const auto hp = hp_encode(original, /*is_leaf=*/false);
    const auto decoded = hp_decode(hp);

    ASSERT_TRUE(decoded.is_ok());
    EXPECT_FALSE(decoded.value().is_leaf);
    EXPECT_EQ(decoded.value().path, original);
}

TEST(HexPrefixTest, RoundtripOddLeaf) {
    const std::array<std::byte, 2> bytes = {std::byte{0xAB}, std::byte{0xCD}};
    const Nibbles full_path(bytes);
    const auto original = full_path.slice(0, 3);  // [A, B, C] - odd

    const auto hp = hp_encode(original, /*is_leaf=*/true);
    const auto decoded = hp_decode(hp);

    ASSERT_TRUE(decoded.is_ok());
    EXPECT_TRUE(decoded.value().is_leaf);
    EXPECT_EQ(decoded.value().path, original);
}

// ============================================================================
// LeafNode Tests
// ============================================================================

TEST(LeafNodeTest, CreateWithPathAndValue) {
    const std::array<std::byte, 2> key_bytes = {std::byte{0xAB}, std::byte{0xCD}};
    const Nibbles path(key_bytes);
    const std::vector<std::byte> value = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    LeafNode leaf{path, value};

    EXPECT_EQ(leaf.path, path);
    EXPECT_EQ(leaf.value, value);
}

TEST(LeafNodeTest, SerializeAndDeserialize) {
    const std::array<std::byte, 2> key_bytes = {std::byte{0x12}, std::byte{0x34}};
    const Nibbles path(key_bytes);
    const std::vector<std::byte> value = {std::byte{0xDE}, std::byte{0xAD}};

    LeafNode original{path, value};
    const auto serialized = serialize_node(original);
    const auto deserialized = deserialize_node(serialized);

    ASSERT_TRUE(deserialized.is_ok());
    ASSERT_TRUE(std::holds_alternative<LeafNode>(deserialized.value()));

    const auto& restored = std::get<LeafNode>(deserialized.value());
    EXPECT_EQ(restored.path, path);
    EXPECT_EQ(restored.value, value);
}

// ============================================================================
// ExtensionNode Tests
// ============================================================================

TEST(ExtensionNodeTest, CreateWithPathAndChild) {
    const std::array<std::byte, 2> key_bytes = {std::byte{0x12}, std::byte{0x34}};
    const Nibbles path(key_bytes);
    Hash256 child_hash{};
    child_hash.data[0] = 0xFF;

    ExtensionNode ext{path, child_hash};

    EXPECT_EQ(ext.path, path);
    EXPECT_EQ(ext.child, child_hash);
}

TEST(ExtensionNodeTest, SerializeAndDeserialize) {
    const std::array<std::byte, 1> key_bytes = {std::byte{0xAB}};
    const Nibbles path(key_bytes);
    Hash256 child_hash{};
    for (std::size_t i = 0; i < 32; ++i) {
        child_hash.data[i] = static_cast<std::uint8_t>(i);
    }

    ExtensionNode original{path, child_hash};
    const auto serialized = serialize_node(original);
    const auto deserialized = deserialize_node(serialized);

    ASSERT_TRUE(deserialized.is_ok());
    ASSERT_TRUE(std::holds_alternative<ExtensionNode>(deserialized.value()));

    const auto& restored = std::get<ExtensionNode>(deserialized.value());
    EXPECT_EQ(restored.path, path);
    EXPECT_EQ(restored.child, child_hash);
}

// ============================================================================
// BranchNode Tests
// ============================================================================

TEST(BranchNodeTest, CreateEmpty) {
    BranchNode branch{};

    for (const auto& child : branch.children) {
        EXPECT_TRUE(child.is_zero());
    }
    EXPECT_TRUE(branch.value.empty());
}

TEST(BranchNodeTest, SetChildAtIndex) {
    BranchNode branch{};
    Hash256 child_hash{};
    child_hash.data[0] = 0x42;

    branch.children[5] = child_hash;

    EXPECT_EQ(branch.children[5], child_hash);
    EXPECT_TRUE(branch.children[0].is_zero());
}

TEST(BranchNodeTest, SetValue) {
    BranchNode branch{};
    branch.value = {std::byte{0x01}, std::byte{0x02}};

    EXPECT_EQ(branch.value.size(), 2);
    EXPECT_EQ(branch.value[0], std::byte{0x01});
}

TEST(BranchNodeTest, SerializeAndDeserialize) {
    BranchNode original{};
    original.children[0].data[0] = 0x11;
    original.children[15].data[0] = 0xFF;
    original.value = {std::byte{0xDE}, std::byte{0xAD}};

    const auto serialized = serialize_node(original);
    const auto deserialized = deserialize_node(serialized);

    ASSERT_TRUE(deserialized.is_ok());
    ASSERT_TRUE(std::holds_alternative<BranchNode>(deserialized.value()));

    const auto& restored = std::get<BranchNode>(deserialized.value());
    EXPECT_EQ(restored.children[0], original.children[0]);
    EXPECT_EQ(restored.children[15], original.children[15]);
    EXPECT_EQ(restored.value, original.value);
}

TEST(BranchNodeTest, SerializeEmptyBranch) {
    BranchNode empty{};

    const auto serialized = serialize_node(empty);
    const auto deserialized = deserialize_node(serialized);

    ASSERT_TRUE(deserialized.is_ok());
    ASSERT_TRUE(std::holds_alternative<BranchNode>(deserialized.value()));

    const auto& restored = std::get<BranchNode>(deserialized.value());
    for (const auto& child : restored.children) {
        EXPECT_TRUE(child.is_zero());
    }
    EXPECT_TRUE(restored.value.empty());
}

// ============================================================================
// Node Hashing Tests
// ============================================================================

TEST(NodeHashTest, HashLeafNode) {
    const std::array<std::byte, 2> key_bytes = {std::byte{0x12}, std::byte{0x34}};
    const Nibbles path(key_bytes);
    const std::vector<std::byte> value = {std::byte{0xDE}, std::byte{0xAD}};

    LeafNode leaf{path, value};
    const auto hash = hash_node(leaf);

    // Hash should be non-zero
    EXPECT_FALSE(hash.is_zero());
}

TEST(NodeHashTest, DifferentNodesHaveDifferentHashes) {
    const std::array<std::byte, 2> key_bytes = {std::byte{0x12}, std::byte{0x34}};
    const Nibbles path(key_bytes);

    LeafNode leaf1{path, {std::byte{0x01}}};
    LeafNode leaf2{path, {std::byte{0x02}}};

    const auto hash1 = hash_node(leaf1);
    const auto hash2 = hash_node(leaf2);

    EXPECT_NE(hash1, hash2);
}

TEST(NodeHashTest, SameNodeProducesSameHash) {
    const std::array<std::byte, 2> key_bytes = {std::byte{0x12}, std::byte{0x34}};
    const Nibbles path(key_bytes);
    const std::vector<std::byte> value = {std::byte{0x42}};

    LeafNode leaf1{path, value};
    LeafNode leaf2{path, value};

    const auto hash1 = hash_node(leaf1);
    const auto hash2 = hash_node(leaf2);

    EXPECT_EQ(hash1, hash2);
}

// ============================================================================
// MptNode Variant Tests
// ============================================================================

TEST(MptNodeTest, VariantHoldsCorrectType) {
    const Nibbles path;
    const std::vector<std::byte> value = {std::byte{0x42}};

    MptNode leaf_node = LeafNode{path, value};
    EXPECT_TRUE(std::holds_alternative<LeafNode>(leaf_node));

    MptNode ext_node = ExtensionNode{path, Hash256::zero()};
    EXPECT_TRUE(std::holds_alternative<ExtensionNode>(ext_node));

    MptNode branch_node = BranchNode{};
    EXPECT_TRUE(std::holds_alternative<BranchNode>(branch_node));
}

TEST(MptNodeTest, HashVariant) {
    const Nibbles path;
    const std::vector<std::byte> value = {std::byte{0x42}};

    MptNode leaf_node = LeafNode{path, value};
    const auto hash = hash_node(leaf_node);

    EXPECT_FALSE(hash.is_zero());
}

}  // namespace
}  // namespace dotvm::core::state
