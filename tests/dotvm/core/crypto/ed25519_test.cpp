#include "dotvm/core/crypto/ed25519.hpp"

#include <gtest/gtest.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace dotvm::core::crypto {
namespace {

/// Convert bytes to hex string
std::string to_hex(std::span<const std::uint8_t> data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint8_t byte : data) {
        oss << std::setw(2) << static_cast<unsigned>(byte);
    }
    return oss.str();
}

/// Parse hex string to bytes
std::vector<std::uint8_t> from_hex(std::string_view hex) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        std::uint8_t byte = 0;
        for (std::size_t j = 0; j < 2; ++j) {
            char c = hex[i + j];
            std::uint8_t nibble = 0;
            if (c >= '0' && c <= '9') {
                nibble = static_cast<std::uint8_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                nibble = static_cast<std::uint8_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                nibble = static_cast<std::uint8_t>(c - 'A' + 10);
            }
            byte = static_cast<std::uint8_t>((byte << 4) | nibble);
        }
        bytes.push_back(byte);
    }
    return bytes;
}

// ============================================================================
// RFC 8032 Section 7.1 Test Vectors
// https://www.rfc-editor.org/rfc/rfc8032#section-7.1
// ============================================================================

/// Test Vector 1: Empty message
/// SECRET KEY: 9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60
/// PUBLIC KEY: d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a
/// MESSAGE: (empty)
/// SIGNATURE: e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b
TEST(Ed25519Test, RFC8032_Test1_EmptyMessage) {
    auto seed_hex = from_hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    auto expected_pubkey_hex = from_hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    auto expected_sig_hex = from_hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");

    // Generate keypair from seed
    Ed25519::Seed seed{};
    std::copy(seed_hex.begin(), seed_hex.end(), seed.begin());

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    // Verify public key matches expected
    EXPECT_EQ(to_hex(public_key), to_hex(expected_pubkey_hex));

    // Sign empty message
    std::vector<std::uint8_t> message;
    auto signature = Ed25519::sign(std::span<const std::uint8_t>{message}, private_key);

    // Verify signature matches expected
    EXPECT_EQ(to_hex(signature), to_hex(expected_sig_hex));

    // Verify signature
    bool valid = Ed25519::verify(std::span<const std::uint8_t>{message}, signature, public_key);
    EXPECT_TRUE(valid);
}

/// Test Vector 2: Single byte message 0x72
/// SECRET KEY: 4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb
/// PUBLIC KEY: 3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c
/// MESSAGE: 72
/// SIGNATURE: 92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00
TEST(Ed25519Test, RFC8032_Test2_SingleByte) {
    auto seed_hex = from_hex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
    auto expected_pubkey_hex = from_hex("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
    auto expected_sig_hex = from_hex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");

    Ed25519::Seed seed{};
    std::copy(seed_hex.begin(), seed_hex.end(), seed.begin());

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    EXPECT_EQ(to_hex(public_key), to_hex(expected_pubkey_hex));

    std::vector<std::uint8_t> message = {0x72};
    auto signature = Ed25519::sign(std::span<const std::uint8_t>{message}, private_key);

    EXPECT_EQ(to_hex(signature), to_hex(expected_sig_hex));

    bool valid = Ed25519::verify(std::span<const std::uint8_t>{message}, signature, public_key);
    EXPECT_TRUE(valid);
}

/// Test Vector 3: Two byte message
/// SECRET KEY: c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7
/// PUBLIC KEY: fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025
/// MESSAGE: af82
/// SIGNATURE: 6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a
TEST(Ed25519Test, RFC8032_Test3_TwoBytes) {
    auto seed_hex = from_hex("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7");
    auto expected_pubkey_hex = from_hex("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025");
    auto expected_sig_hex = from_hex("6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a");

    Ed25519::Seed seed{};
    std::copy(seed_hex.begin(), seed_hex.end(), seed.begin());

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    EXPECT_EQ(to_hex(public_key), to_hex(expected_pubkey_hex));

    auto message = from_hex("af82");
    auto signature = Ed25519::sign(std::span<const std::uint8_t>{message}, private_key);

    EXPECT_EQ(to_hex(signature), to_hex(expected_sig_hex));

    bool valid = Ed25519::verify(std::span<const std::uint8_t>{message}, signature, public_key);
    EXPECT_TRUE(valid);
}

// ============================================================================
// Verification Tests
// ============================================================================

TEST(Ed25519Test, VerifyValidSignature) {
    Ed25519::Seed seed{};
    for (std::size_t i = 0; i < 32; ++i) {
        seed[i] = static_cast<std::uint8_t>(i);
    }

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    std::string message = "Hello, Ed25519!";
    std::vector<std::uint8_t> msg_bytes(message.begin(), message.end());

    auto signature = Ed25519::sign(std::span<const std::uint8_t>{msg_bytes}, private_key);

    bool valid = Ed25519::verify(std::span<const std::uint8_t>{msg_bytes}, signature, public_key);
    EXPECT_TRUE(valid);
}

TEST(Ed25519Test, VerifyInvalidSignature_ModifiedMessage) {
    Ed25519::Seed seed{};
    for (std::size_t i = 0; i < 32; ++i) {
        seed[i] = static_cast<std::uint8_t>(i);
    }

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    std::string message = "Hello, Ed25519!";
    std::vector<std::uint8_t> msg_bytes(message.begin(), message.end());

    auto signature = Ed25519::sign(std::span<const std::uint8_t>{msg_bytes}, private_key);

    // Modify the message
    std::string modified = "Hello, Ed25519?";
    std::vector<std::uint8_t> modified_bytes(modified.begin(), modified.end());

    bool valid = Ed25519::verify(std::span<const std::uint8_t>{modified_bytes}, signature, public_key);
    EXPECT_FALSE(valid);
}

TEST(Ed25519Test, VerifyInvalidSignature_ModifiedSignature) {
    Ed25519::Seed seed{};
    for (std::size_t i = 0; i < 32; ++i) {
        seed[i] = static_cast<std::uint8_t>(i);
    }

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    std::string message = "Hello, Ed25519!";
    std::vector<std::uint8_t> msg_bytes(message.begin(), message.end());

    auto signature = Ed25519::sign(std::span<const std::uint8_t>{msg_bytes}, private_key);

    // Modify the signature
    signature[0] ^= 0x01;

    bool valid = Ed25519::verify(std::span<const std::uint8_t>{msg_bytes}, signature, public_key);
    EXPECT_FALSE(valid);
}

TEST(Ed25519Test, VerifyInvalidSignature_WrongPublicKey) {
    Ed25519::Seed seed1{};
    Ed25519::Seed seed2{};
    for (std::size_t i = 0; i < 32; ++i) {
        seed1[i] = static_cast<std::uint8_t>(i);
        seed2[i] = static_cast<std::uint8_t>(i + 32);
    }

    auto [public_key1, private_key1] = Ed25519::generate_keypair(seed1);
    auto [public_key2, private_key2] = Ed25519::generate_keypair(seed2);

    std::string message = "Hello, Ed25519!";
    std::vector<std::uint8_t> msg_bytes(message.begin(), message.end());

    auto signature = Ed25519::sign(std::span<const std::uint8_t>{msg_bytes}, private_key1);

    // Verify with wrong public key
    bool valid = Ed25519::verify(std::span<const std::uint8_t>{msg_bytes}, signature, public_key2);
    EXPECT_FALSE(valid);
}

// ============================================================================
// Key Generation Tests
// ============================================================================

TEST(Ed25519Test, KeypairGenerationDeterminism) {
    Ed25519::Seed seed{};
    for (std::size_t i = 0; i < 32; ++i) {
        seed[i] = static_cast<std::uint8_t>(i * 3 + 7);
    }

    auto [private_key1, public_key1] = Ed25519::generate_keypair(seed);
    auto [private_key2, public_key2] = Ed25519::generate_keypair(seed);

    EXPECT_EQ(to_hex(private_key1), to_hex(private_key2));
    EXPECT_EQ(to_hex(public_key1), to_hex(public_key2));
}

TEST(Ed25519Test, DifferentSeedsDifferentKeys) {
    Ed25519::Seed seed1{};
    Ed25519::Seed seed2{};
    for (std::size_t i = 0; i < 32; ++i) {
        seed1[i] = static_cast<std::uint8_t>(i);
        seed2[i] = static_cast<std::uint8_t>(i + 1);
    }

    auto [public_key1, private_key1] = Ed25519::generate_keypair(seed1);
    auto [public_key2, private_key2] = Ed25519::generate_keypair(seed2);

    EXPECT_NE(to_hex(public_key1), to_hex(public_key2));
}

// ============================================================================
// Signature Determinism Tests
// ============================================================================

TEST(Ed25519Test, SignatureDeterminism) {
    Ed25519::Seed seed{};
    for (std::size_t i = 0; i < 32; ++i) {
        seed[i] = static_cast<std::uint8_t>(i);
    }

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    std::string message = "Test message for determinism";
    std::vector<std::uint8_t> msg_bytes(message.begin(), message.end());

    auto sig1 = Ed25519::sign(std::span<const std::uint8_t>{msg_bytes}, private_key);
    auto sig2 = Ed25519::sign(std::span<const std::uint8_t>{msg_bytes}, private_key);
    auto sig3 = Ed25519::sign(std::span<const std::uint8_t>{msg_bytes}, private_key);

    EXPECT_EQ(to_hex(sig1), to_hex(sig2));
    EXPECT_EQ(to_hex(sig2), to_hex(sig3));
}

// ============================================================================
// Large Message Tests
// ============================================================================

TEST(Ed25519Test, SignLargeMessage) {
    Ed25519::Seed seed{};
    for (std::size_t i = 0; i < 32; ++i) {
        seed[i] = static_cast<std::uint8_t>(i);
    }

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    // 1MB message
    std::vector<std::uint8_t> large_message(1024 * 1024);
    for (std::size_t i = 0; i < large_message.size(); ++i) {
        large_message[i] = static_cast<std::uint8_t>(i % 256);
    }

    auto signature = Ed25519::sign(std::span<const std::uint8_t>{large_message}, private_key);

    bool valid = Ed25519::verify(std::span<const std::uint8_t>{large_message}, signature, public_key);
    EXPECT_TRUE(valid);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(Ed25519Test, ZeroSeed) {
    Ed25519::Seed seed{};  // All zeros

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    std::string message = "Test with zero seed";
    std::vector<std::uint8_t> msg_bytes(message.begin(), message.end());

    auto signature = Ed25519::sign(std::span<const std::uint8_t>{msg_bytes}, private_key);

    bool valid = Ed25519::verify(std::span<const std::uint8_t>{msg_bytes}, signature, public_key);
    EXPECT_TRUE(valid);
}

TEST(Ed25519Test, MaxSeed) {
    Ed25519::Seed seed{};
    std::fill(seed.begin(), seed.end(), 0xFF);

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    std::string message = "Test with max seed";
    std::vector<std::uint8_t> msg_bytes(message.begin(), message.end());

    auto signature = Ed25519::sign(std::span<const std::uint8_t>{msg_bytes}, private_key);

    bool valid = Ed25519::verify(std::span<const std::uint8_t>{msg_bytes}, signature, public_key);
    EXPECT_TRUE(valid);
}

// ============================================================================
// Public Key from Private Key
// ============================================================================

TEST(Ed25519Test, PublicKeyEmbeddedInPrivateKey) {
    Ed25519::Seed seed{};
    for (std::size_t i = 0; i < 32; ++i) {
        seed[i] = static_cast<std::uint8_t>(i);
    }

    auto [public_key, private_key] = Ed25519::generate_keypair(seed);

    // The last 32 bytes of the private key should be the public key
    Ed25519::PublicKey extracted_pubkey{};
    std::copy(private_key.begin() + 32, private_key.end(), extracted_pubkey.begin());

    EXPECT_EQ(to_hex(extracted_pubkey), to_hex(public_key));
}

}  // namespace
}  // namespace dotvm::core::crypto
