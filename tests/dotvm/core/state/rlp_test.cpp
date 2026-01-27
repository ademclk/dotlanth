/// @file rlp_test.cpp
/// @brief Unit tests for STATE-008 RLP encoding/decoding
///
/// TDD tests for RLP (Recursive Length Prefix) encoding used in MPT node
/// serialization. Written before implementation per TDD discipline.

#include <gtest/gtest.h>

#include "dotvm/core/state/rlp.hpp"

namespace dotvm::core::state {
namespace {

// ============================================================================
// RLP Encode Single Byte Tests
// ============================================================================

TEST(RlpEncodeTest, SingleByteUnder128IsEncodedAsItself) {
    // Values 0x00-0x7F are encoded directly
    const std::vector<std::byte> input = {std::byte{0x42}};
    const auto encoded = rlp::encode(input);

    ASSERT_EQ(encoded.size(), 1);
    EXPECT_EQ(encoded[0], std::byte{0x42});
}

TEST(RlpEncodeTest, ZeroByteIsEncodedAsItself) {
    const std::vector<std::byte> input = {std::byte{0x00}};
    const auto encoded = rlp::encode(input);

    ASSERT_EQ(encoded.size(), 1);
    EXPECT_EQ(encoded[0], std::byte{0x00});
}

TEST(RlpEncodeTest, ByteAt127IsEncodedAsItself) {
    const std::vector<std::byte> input = {std::byte{0x7F}};
    const auto encoded = rlp::encode(input);

    ASSERT_EQ(encoded.size(), 1);
    EXPECT_EQ(encoded[0], std::byte{0x7F});
}

TEST(RlpEncodeTest, ByteAt128NeedsPrefixByte) {
    // Values >= 0x80 need 0x81 prefix (string of length 1)
    const std::vector<std::byte> input = {std::byte{0x80}};
    const auto encoded = rlp::encode(input);

    ASSERT_EQ(encoded.size(), 2);
    EXPECT_EQ(encoded[0], std::byte{0x81});  // 0x80 + 1 = string of length 1
    EXPECT_EQ(encoded[1], std::byte{0x80});
}

// ============================================================================
// RLP Encode String Tests
// ============================================================================

TEST(RlpEncodeTest, EmptyStringIsEncodedAs0x80) {
    const std::vector<std::byte> empty;
    const auto encoded = rlp::encode(empty);

    ASSERT_EQ(encoded.size(), 1);
    EXPECT_EQ(encoded[0], std::byte{0x80});
}

TEST(RlpEncodeTest, ShortStringUnder55BytesHasSinglePrefix) {
    // "dog" = 0x646F67
    const std::vector<std::byte> dog = {std::byte{0x64}, std::byte{0x6F}, std::byte{0x67}};
    const auto encoded = rlp::encode(dog);

    ASSERT_EQ(encoded.size(), 4);
    EXPECT_EQ(encoded[0], std::byte{0x83});  // 0x80 + 3
    EXPECT_EQ(encoded[1], std::byte{0x64});
    EXPECT_EQ(encoded[2], std::byte{0x6F});
    EXPECT_EQ(encoded[3], std::byte{0x67});
}

TEST(RlpEncodeTest, String55BytesIsShortString) {
    // 55 bytes should still use short string encoding (0x80 + 55 = 0xB7)
    std::vector<std::byte> data(55, std::byte{0x42});
    const auto encoded = rlp::encode(data);

    ASSERT_EQ(encoded.size(), 56);
    EXPECT_EQ(encoded[0], std::byte{0xB7});  // 0x80 + 55
}

TEST(RlpEncodeTest, String56BytesIsLongString) {
    // 56 bytes needs long string encoding: 0xB8 0x38 (56 in big-endian)
    std::vector<std::byte> data(56, std::byte{0x42});
    const auto encoded = rlp::encode(data);

    ASSERT_EQ(encoded.size(), 58);           // 1 (prefix) + 1 (length) + 56 (data)
    EXPECT_EQ(encoded[0], std::byte{0xB8});  // 0xB7 + 1 (length takes 1 byte)
    EXPECT_EQ(encoded[1], std::byte{0x38});  // 56 = 0x38
}

TEST(RlpEncodeTest, String256BytesHasTwoLengthBytes) {
    // 256 bytes needs 2 length bytes: 0xB9 0x01 0x00
    std::vector<std::byte> data(256, std::byte{0x42});
    const auto encoded = rlp::encode(data);

    ASSERT_EQ(encoded.size(), 259);          // 1 + 2 + 256
    EXPECT_EQ(encoded[0], std::byte{0xB9});  // 0xB7 + 2
    EXPECT_EQ(encoded[1], std::byte{0x01});  // 256 = 0x0100
    EXPECT_EQ(encoded[2], std::byte{0x00});
}

// ============================================================================
// RLP Encode List Tests
// ============================================================================

TEST(RlpEncodeTest, EmptyListIsEncodedAs0xC0) {
    const std::vector<std::vector<std::byte>> empty_list;
    const auto encoded = rlp::encode_list(empty_list);

    ASSERT_EQ(encoded.size(), 1);
    EXPECT_EQ(encoded[0], std::byte{0xC0});
}

TEST(RlpEncodeTest, ListOfSingleElement) {
    // ["dog"] where dog = 0x83646F67 (encoded)
    const std::vector<std::byte> dog = {std::byte{0x64}, std::byte{0x6F}, std::byte{0x67}};
    const std::vector<std::vector<std::byte>> list = {dog};
    const auto encoded = rlp::encode_list(list);

    // List prefix: 0xC0 + 4 = 0xC4, then 0x83646F67
    ASSERT_EQ(encoded.size(), 5);
    EXPECT_EQ(encoded[0], std::byte{0xC4});  // 0xC0 + 4 (length of encoded content)
    EXPECT_EQ(encoded[1], std::byte{0x83});  // dog's prefix
}

TEST(RlpEncodeTest, ListOfTwoStrings) {
    // ["cat", "dog"]
    const std::vector<std::byte> cat = {std::byte{0x63}, std::byte{0x61}, std::byte{0x74}};
    const std::vector<std::byte> dog = {std::byte{0x64}, std::byte{0x6F}, std::byte{0x67}};
    const std::vector<std::vector<std::byte>> list = {cat, dog};
    const auto encoded = rlp::encode_list(list);

    // 0xC8 (list of 8 bytes) 0x83 "cat" 0x83 "dog"
    ASSERT_EQ(encoded.size(), 9);
    EXPECT_EQ(encoded[0], std::byte{0xC8});  // 0xC0 + 8
}

TEST(RlpEncodeTest, ListOver55BytesIsLongList) {
    // Create a list with enough content to exceed 55 bytes
    std::vector<std::byte> long_str(50, std::byte{0x42});
    const std::vector<std::vector<std::byte>> list = {long_str, long_str};
    const auto encoded = rlp::encode_list(list);

    // Two strings of 50 bytes each = 2 * (1 + 50) = 102 bytes content
    // Long list: 0xF8 0x66 (102) + content
    EXPECT_EQ(encoded[0], std::byte{0xF8});  // 0xF7 + 1 (length takes 1 byte)
    EXPECT_EQ(encoded[1], std::byte{0x66});  // 102
}

// ============================================================================
// RLP Decode Tests
// ============================================================================

TEST(RlpDecodeTest, DecodeEmptyString) {
    const std::vector<std::byte> encoded = {std::byte{0x80}};
    const auto result = rlp::decode(encoded);

    ASSERT_TRUE(result.is_ok()) << "Decode failed: " << to_string(result.error());
    EXPECT_TRUE(result.value().empty());
}

TEST(RlpDecodeTest, DecodeSingleByteUnder128) {
    const std::vector<std::byte> encoded = {std::byte{0x42}};
    const auto result = rlp::decode(encoded);

    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value()[0], std::byte{0x42});
}

TEST(RlpDecodeTest, DecodeSingleByteAt128) {
    const std::vector<std::byte> encoded = {std::byte{0x81}, std::byte{0x80}};
    const auto result = rlp::decode(encoded);

    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().size(), 1);
    EXPECT_EQ(result.value()[0], std::byte{0x80});
}

TEST(RlpDecodeTest, DecodeShortString) {
    // Encoded "dog": 0x83 0x64 0x6F 0x67
    const std::vector<std::byte> encoded = {std::byte{0x83}, std::byte{0x64}, std::byte{0x6F},
                                            std::byte{0x67}};
    const auto result = rlp::decode(encoded);

    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().size(), 3);
    EXPECT_EQ(result.value()[0], std::byte{0x64});
    EXPECT_EQ(result.value()[1], std::byte{0x6F});
    EXPECT_EQ(result.value()[2], std::byte{0x67});
}

TEST(RlpDecodeTest, DecodeLongString) {
    // Create a 56-byte string and its encoding
    std::vector<std::byte> encoded = {std::byte{0xB8}, std::byte{0x38}};  // prefix + length
    for (int i = 0; i < 56; ++i) {
        encoded.push_back(std::byte{0x42});
    }

    const auto result = rlp::decode(encoded);

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().size(), 56);
}

TEST(RlpDecodeTest, DecodeInvalidPrefixReturnsError) {
    // Invalid: prefix says string of length 10 but only 5 bytes follow
    const std::vector<std::byte> invalid = {std::byte{0x8A}, std::byte{0x01}, std::byte{0x02},
                                            std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};
    const auto result = rlp::decode(invalid);

    EXPECT_TRUE(result.is_err());
}

// ============================================================================
// RLP Decode List Tests
// ============================================================================

TEST(RlpDecodeTest, DecodeEmptyList) {
    const std::vector<std::byte> encoded = {std::byte{0xC0}};
    const auto result = rlp::decode_list(encoded);

    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.value().empty());
}

TEST(RlpDecodeTest, DecodeListOfOneElement) {
    // List ["A"] = 0xC1 0x41
    const std::vector<std::byte> encoded = {std::byte{0xC1}, std::byte{0x41}};
    const auto result = rlp::decode_list(encoded);

    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().size(), 1);
    ASSERT_EQ(result.value()[0].size(), 1);
    EXPECT_EQ(result.value()[0][0], std::byte{0x41});
}

TEST(RlpDecodeTest, DecodeListOfMultipleElements) {
    // ["cat", "dog"] = 0xC8 0x83 "cat" 0x83 "dog"
    const std::vector<std::byte> encoded = {std::byte{0xC8}, std::byte{0x83}, std::byte{0x63},
                                            std::byte{0x61}, std::byte{0x74}, std::byte{0x83},
                                            std::byte{0x64}, std::byte{0x6F}, std::byte{0x67}};
    const auto result = rlp::decode_list(encoded);

    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().size(), 2);

    // First element: "cat"
    ASSERT_EQ(result.value()[0].size(), 3);
    EXPECT_EQ(result.value()[0][0], std::byte{0x63});

    // Second element: "dog"
    ASSERT_EQ(result.value()[1].size(), 3);
    EXPECT_EQ(result.value()[1][0], std::byte{0x64});
}

// ============================================================================
// Roundtrip Tests
// ============================================================================

TEST(RlpRoundtripTest, EncodeDecodeEmptyString) {
    const std::vector<std::byte> original;
    const auto encoded = rlp::encode(original);
    const auto decoded = rlp::decode(encoded);

    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded.value(), original);
}

TEST(RlpRoundtripTest, EncodeDecodeShortString) {
    const std::vector<std::byte> original = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                                             std::byte{0xEF}};
    const auto encoded = rlp::encode(original);
    const auto decoded = rlp::decode(encoded);

    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded.value(), original);
}

TEST(RlpRoundtripTest, EncodeDecodeLongString) {
    std::vector<std::byte> original(100, std::byte{0x42});
    const auto encoded = rlp::encode(original);
    const auto decoded = rlp::decode(encoded);

    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded.value(), original);
}

TEST(RlpRoundtripTest, EncodeDecodeList) {
    const std::vector<std::byte> elem1 = {std::byte{0x01}, std::byte{0x02}};
    const std::vector<std::byte> elem2 = {std::byte{0x03}};
    const std::vector<std::vector<std::byte>> original = {elem1, elem2};

    const auto encoded = rlp::encode_list(original);
    const auto decoded = rlp::decode_list(encoded);

    ASSERT_TRUE(decoded.is_ok());
    ASSERT_EQ(decoded.value().size(), 2);
    EXPECT_EQ(decoded.value()[0], elem1);
    EXPECT_EQ(decoded.value()[1], elem2);
}

// ============================================================================
// Pre-Encoded List Tests
// ============================================================================

TEST(RlpEncodeTest, EncodeListOfPreEncodedItems) {
    // When items are already RLP-encoded, use encode_list_raw
    const std::vector<std::byte> dog = {std::byte{0x64}, std::byte{0x6F}, std::byte{0x67}};
    const std::vector<std::byte> cat = {std::byte{0x63}, std::byte{0x61}, std::byte{0x74}};
    const auto dog_encoded = rlp::encode(dog);
    const auto cat_encoded = rlp::encode(cat);

    // Create spans for the encode_list_raw call
    const std::array<std::span<const std::byte>, 2> items = {
        std::span<const std::byte>(dog_encoded), std::span<const std::byte>(cat_encoded)};
    const auto list_encoded = rlp::encode_list_raw(items);

    // Decode and verify
    const auto decoded = rlp::decode_list(list_encoded);
    ASSERT_TRUE(decoded.is_ok());
    ASSERT_EQ(decoded.value().size(), 2);
}

}  // namespace
}  // namespace dotvm::core::state
