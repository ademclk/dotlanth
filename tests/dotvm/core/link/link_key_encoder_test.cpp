/// @file link_key_encoder_test.cpp
/// @brief Unit tests for LinkKeyEncoder

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/link/link_key_encoder.hpp"
#include "dotvm/core/link/object_id.hpp"

namespace dotvm::core::link {
namespace {

constexpr std::size_t kObjectIdSize = 16;
constexpr std::size_t kPrefixSize = 2;

void expect_string_bytes(std::span<const std::byte> bytes, std::string_view text) {
    ASSERT_EQ(bytes.size(), text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        EXPECT_EQ(bytes[i], std::byte{static_cast<unsigned char>(text[i])});
    }
}

// ============================================================================
// Encoding Tests
// ============================================================================

TEST(LinkKeyEncoderTest, ForwardKeyContainsPrefixAndComponents) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};
    constexpr std::string_view link = "owns";

    const auto key = LinkKeyEncoder::encode_forward_key(src, link, tgt);
    const auto src_bytes = src.to_bytes();
    const auto tgt_bytes = tgt.to_bytes();

    ASSERT_GE(key.size(), 2U + kObjectIdSize + 1U + link.size() + 1U + kObjectIdSize);
    EXPECT_EQ(key[0], std::byte{'L'});
    EXPECT_EQ(key[1], std::byte{'|'});

    EXPECT_TRUE(std::equal(src_bytes.begin(), src_bytes.end(),
                           key.begin() + static_cast<std::ptrdiff_t>(kPrefixSize)));
    EXPECT_EQ(key[kPrefixSize + kObjectIdSize], std::byte{'|'});

    const std::size_t link_offset = kPrefixSize + kObjectIdSize + 1U;
    expect_string_bytes(std::span<const std::byte>(key.data() + link_offset, link.size()), link);

    const std::size_t target_delim = link_offset + link.size();
    EXPECT_EQ(key[target_delim], std::byte{'|'});

    EXPECT_TRUE(std::equal(tgt_bytes.begin(), tgt_bytes.end(),
                           key.begin() + static_cast<std::ptrdiff_t>(key.size() - kObjectIdSize)));
}

TEST(LinkKeyEncoderTest, ForwardKeyDecodesTargetObjectId) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};

    const auto key = LinkKeyEncoder::encode_forward_key(src, "owns", tgt);

    EXPECT_EQ(LinkKeyEncoder::decode_target_from_forward(key), tgt);
}

TEST(LinkKeyEncoderTest, ForwardKeySupportsEmptyLinkName) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};

    const auto key = LinkKeyEncoder::encode_forward_key(src, "", tgt);
    const auto prefix = LinkKeyEncoder::encode_forward_prefix(src, "");

    EXPECT_EQ(LinkKeyEncoder::decode_target_from_forward(key), tgt);
    EXPECT_TRUE(std::equal(prefix.begin(), prefix.end(), key.begin()));
    EXPECT_EQ(prefix.back(), std::byte{'|'});
}

TEST(LinkKeyEncoderTest, InverseKeyContainsPrefixAndComponents) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};
    constexpr std::string_view link = "member_of";

    const auto key = LinkKeyEncoder::encode_inverse_key(tgt, link, src);
    const auto src_bytes = src.to_bytes();
    const auto tgt_bytes = tgt.to_bytes();

    ASSERT_GE(key.size(), 2U + kObjectIdSize + 1U + link.size() + 1U + kObjectIdSize);
    EXPECT_EQ(key[0], std::byte{'I'});
    EXPECT_EQ(key[1], std::byte{'|'});

    EXPECT_TRUE(std::equal(tgt_bytes.begin(), tgt_bytes.end(),
                           key.begin() + static_cast<std::ptrdiff_t>(kPrefixSize)));
    EXPECT_EQ(key[kPrefixSize + kObjectIdSize], std::byte{'|'});

    const std::size_t link_offset = kPrefixSize + kObjectIdSize + 1U;
    expect_string_bytes(std::span<const std::byte>(key.data() + link_offset, link.size()), link);

    const std::size_t source_delim = link_offset + link.size();
    EXPECT_EQ(key[source_delim], std::byte{'|'});

    EXPECT_TRUE(std::equal(src_bytes.begin(), src_bytes.end(),
                           key.begin() + static_cast<std::ptrdiff_t>(key.size() - kObjectIdSize)));
}

TEST(LinkKeyEncoderTest, InverseKeyDecodesSourceObjectId) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};

    const auto key = LinkKeyEncoder::encode_inverse_key(tgt, "member_of", src);

    EXPECT_EQ(LinkKeyEncoder::decode_source_from_inverse(key), src);
}

TEST(LinkKeyEncoderTest, InverseKeySupportsEmptyLinkName) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};

    const auto key = LinkKeyEncoder::encode_inverse_key(tgt, "", src);
    const auto prefix = LinkKeyEncoder::encode_inverse_prefix(tgt, "");

    EXPECT_EQ(LinkKeyEncoder::decode_source_from_inverse(key), src);
    EXPECT_TRUE(std::equal(prefix.begin(), prefix.end(), key.begin()));
    EXPECT_EQ(prefix.back(), std::byte{'|'});
}

TEST(LinkKeyEncoderTest, CountKeyContainsPrefixAndComponents) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    constexpr std::string_view link = "children";

    const auto key = LinkKeyEncoder::encode_count_key(src, link);
    const auto src_bytes = src.to_bytes();

    ASSERT_EQ(key.size(), 2U + kObjectIdSize + 1U + link.size());
    EXPECT_EQ(key[0], std::byte{'C'});
    EXPECT_EQ(key[1], std::byte{'|'});

    EXPECT_TRUE(std::equal(src_bytes.begin(), src_bytes.end(),
                           key.begin() + static_cast<std::ptrdiff_t>(kPrefixSize)));
    EXPECT_EQ(key[kPrefixSize + kObjectIdSize], std::byte{'|'});

    const std::size_t link_offset = kPrefixSize + kObjectIdSize + 1U;
    expect_string_bytes(std::span<const std::byte>(key.data() + link_offset, link.size()), link);
    EXPECT_EQ(key.back(), std::byte{static_cast<unsigned char>(link.back())});
}

TEST(LinkKeyEncoderTest, PrefixIsProperPrefixOfFullKey) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};
    constexpr std::string_view link = "parent";

    const auto prefix = LinkKeyEncoder::encode_forward_prefix(src, link);
    const auto key = LinkKeyEncoder::encode_forward_key(src, link, tgt);

    ASSERT_LT(prefix.size(), key.size());
    EXPECT_TRUE(std::equal(prefix.begin(), prefix.end(), key.begin()));
    EXPECT_EQ(prefix.back(), std::byte{'|'});
}

TEST(LinkKeyEncoderTest, InversePrefixIsProperPrefixOfFullKey) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};
    constexpr std::string_view link = "parent";

    const auto prefix = LinkKeyEncoder::encode_inverse_prefix(tgt, link);
    const auto key = LinkKeyEncoder::encode_inverse_key(tgt, link, src);

    ASSERT_LT(prefix.size(), key.size());
    EXPECT_TRUE(std::equal(prefix.begin(), prefix.end(), key.begin()));
    EXPECT_EQ(prefix.back(), std::byte{'|'});
}

TEST(LinkKeyEncoderTest, DecodeForwardRejectsMalformedKeys) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};

    const auto valid = LinkKeyEncoder::encode_forward_key(src, "owns", tgt);
    const auto prefix = LinkKeyEncoder::encode_forward_prefix(src, "owns");

    std::vector<std::byte> key = valid;
    key[0] = std::byte{'X'};
    EXPECT_EQ(LinkKeyEncoder::decode_target_from_forward(key), ObjectId::invalid());

    key = valid;
    key[1] = std::byte{'X'};
    EXPECT_EQ(LinkKeyEncoder::decode_target_from_forward(key), ObjectId::invalid());

    key = valid;
    key[kPrefixSize + kObjectIdSize] = std::byte{'X'};
    EXPECT_EQ(LinkKeyEncoder::decode_target_from_forward(key), ObjectId::invalid());

    key = valid;
    key[key.size() - kObjectIdSize - 1U] = std::byte{'X'};
    EXPECT_EQ(LinkKeyEncoder::decode_target_from_forward(key), ObjectId::invalid());

    key.assign(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(valid.size() - 1U));
    EXPECT_EQ(LinkKeyEncoder::decode_target_from_forward(key), ObjectId::invalid());

    EXPECT_EQ(LinkKeyEncoder::decode_target_from_forward(prefix), ObjectId::invalid());
}

TEST(LinkKeyEncoderTest, DecodeInverseRejectsMalformedKeys) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};

    const auto valid = LinkKeyEncoder::encode_inverse_key(tgt, "member_of", src);
    const auto prefix = LinkKeyEncoder::encode_inverse_prefix(tgt, "member_of");

    std::vector<std::byte> key = valid;
    key[0] = std::byte{'X'};
    EXPECT_EQ(LinkKeyEncoder::decode_source_from_inverse(key), ObjectId::invalid());

    key = valid;
    key[1] = std::byte{'X'};
    EXPECT_EQ(LinkKeyEncoder::decode_source_from_inverse(key), ObjectId::invalid());

    key = valid;
    key[kPrefixSize + kObjectIdSize] = std::byte{'X'};
    EXPECT_EQ(LinkKeyEncoder::decode_source_from_inverse(key), ObjectId::invalid());

    key = valid;
    key[key.size() - kObjectIdSize - 1U] = std::byte{'X'};
    EXPECT_EQ(LinkKeyEncoder::decode_source_from_inverse(key), ObjectId::invalid());

    key.assign(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(valid.size() - 1U));
    EXPECT_EQ(LinkKeyEncoder::decode_source_from_inverse(key), ObjectId::invalid());

    EXPECT_EQ(LinkKeyEncoder::decode_source_from_inverse(prefix), ObjectId::invalid());
}

TEST(LinkKeyEncoderTest, KeysSortBySourceThenLinkThenTarget) {
    const ObjectId src_a{1U, 1U};
    const ObjectId src_b{2U, 1U};
    const ObjectId tgt_a{5U, 1U};
    const ObjectId tgt_b{5U, 2U};

    const auto key_src_a = LinkKeyEncoder::encode_forward_key(src_a, "alpha", tgt_a);
    const auto key_src_b = LinkKeyEncoder::encode_forward_key(src_b, "alpha", tgt_a);
    const auto key_link_beta = LinkKeyEncoder::encode_forward_key(src_a, "beta", tgt_a);
    const auto key_target_b = LinkKeyEncoder::encode_forward_key(src_a, "alpha", tgt_b);

    EXPECT_TRUE(std::lexicographical_compare(key_src_a.begin(), key_src_a.end(), key_src_b.begin(),
                                             key_src_b.end()));
    EXPECT_TRUE(std::lexicographical_compare(key_src_a.begin(), key_src_a.end(),
                                             key_link_beta.begin(), key_link_beta.end()));
    EXPECT_TRUE(std::lexicographical_compare(key_src_a.begin(), key_src_a.end(),
                                             key_target_b.begin(), key_target_b.end()));
}

TEST(LinkKeyEncoderTest, DifferentLinksProduceDifferentKeys) {
    const ObjectId src{0x0102030405060708ULL, 0x1112131415161718ULL};
    const ObjectId tgt{0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL};

    const auto key_a = LinkKeyEncoder::encode_forward_key(src, "likes", tgt);
    const auto key_b = LinkKeyEncoder::encode_forward_key(src, "hates", tgt);

    EXPECT_NE(key_a, key_b);
}

}  // namespace
}  // namespace dotvm::core::link
