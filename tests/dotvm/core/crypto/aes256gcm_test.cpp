#include "dotvm/core/crypto/aes256gcm.hpp"

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
// NIST SP 800-38D Test Vectors (AES-256-GCM)
// ============================================================================

/// Test Case 13: 256-bit key, 96-bit IV, empty plaintext, empty AAD
/// Key: 0000000000000000000000000000000000000000000000000000000000000000
/// IV:  000000000000000000000000
/// PT:  (empty)
/// AAD: (empty)
/// CT:  (empty)
/// Tag: 530f8afbc74536b9a963b4f1c4cb738b
TEST(Aes256GcmTest, NIST_TestCase13_EmptyPTEmptyAAD) {
    auto key_hex = from_hex("0000000000000000000000000000000000000000000000000000000000000000");
    auto iv_hex = from_hex("000000000000000000000000");
    auto expected_tag_hex = from_hex("530f8afbc74536b9a963b4f1c4cb738b");

    Aes256Gcm::Key key{};
    std::copy(key_hex.begin(), key_hex.end(), key.begin());

    Aes256Gcm::Nonce nonce{};
    std::copy(iv_hex.begin(), iv_hex.end(), nonce.begin());

    // Empty plaintext
    std::vector<std::uint8_t> plaintext;

    auto ciphertext = Aes256Gcm::encrypt_with_nonce(std::span<const std::uint8_t>{plaintext}, key, nonce);

    // Ciphertext should be nonce (12) + ciphertext (0) + tag (16) = 28 bytes
    EXPECT_EQ(ciphertext.size(), Aes256Gcm::NONCE_SIZE + Aes256Gcm::TAG_SIZE);

    // Extract tag (last 16 bytes)
    std::span<const std::uint8_t> tag_span{ciphertext.data() + Aes256Gcm::NONCE_SIZE, Aes256Gcm::TAG_SIZE};
    EXPECT_EQ(to_hex(tag_span), to_hex(expected_tag_hex));
}

/// Test Case 14: 256-bit key, 96-bit IV, 128-bit plaintext, empty AAD
/// Key: 0000000000000000000000000000000000000000000000000000000000000000
/// IV:  000000000000000000000000
/// PT:  00000000000000000000000000000000
/// AAD: (empty)
/// CT:  cea7403d4d606b6e074ec5d3baf39d18
/// Tag: d0d1c8a799996bf0265b98b5d48ab919
TEST(Aes256GcmTest, NIST_TestCase14_16BytePTEmptyAAD) {
    auto key_hex = from_hex("0000000000000000000000000000000000000000000000000000000000000000");
    auto iv_hex = from_hex("000000000000000000000000");
    auto pt_hex = from_hex("00000000000000000000000000000000");
    auto expected_ct_hex = from_hex("cea7403d4d606b6e074ec5d3baf39d18");
    auto expected_tag_hex = from_hex("d0d1c8a799996bf0265b98b5d48ab919");

    Aes256Gcm::Key key{};
    std::copy(key_hex.begin(), key_hex.end(), key.begin());

    Aes256Gcm::Nonce nonce{};
    std::copy(iv_hex.begin(), iv_hex.end(), nonce.begin());

    auto ciphertext = Aes256Gcm::encrypt_with_nonce(std::span<const std::uint8_t>{pt_hex}, key, nonce);

    // Ciphertext should be nonce (12) + ciphertext (16) + tag (16) = 44 bytes
    EXPECT_EQ(ciphertext.size(), Aes256Gcm::NONCE_SIZE + 16 + Aes256Gcm::TAG_SIZE);

    // Extract ciphertext (after nonce, before tag)
    std::span<const std::uint8_t> ct_span{ciphertext.data() + Aes256Gcm::NONCE_SIZE, 16};
    EXPECT_EQ(to_hex(ct_span), to_hex(expected_ct_hex));

    // Extract tag (last 16 bytes)
    std::span<const std::uint8_t> tag_span{ciphertext.data() + Aes256Gcm::NONCE_SIZE + 16, Aes256Gcm::TAG_SIZE};
    EXPECT_EQ(to_hex(tag_span), to_hex(expected_tag_hex));
}

/// Test Case 15: 256-bit key, different IV
/// Key: feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308
/// IV:  cafebabefacedbaddecaf888
/// PT:  d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255
/// CT:  522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad
/// Tag: b094dac5d93471bdec1a502270e3cc6c
TEST(Aes256GcmTest, NIST_TestCase15_LongerPT) {
    auto key_hex = from_hex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    auto iv_hex = from_hex("cafebabefacedbaddecaf888");
    auto pt_hex = from_hex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255");
    auto expected_ct_hex = from_hex("522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad");
    auto expected_tag_hex = from_hex("b094dac5d93471bdec1a502270e3cc6c");

    Aes256Gcm::Key key{};
    std::copy(key_hex.begin(), key_hex.end(), key.begin());

    Aes256Gcm::Nonce nonce{};
    std::copy(iv_hex.begin(), iv_hex.end(), nonce.begin());

    auto ciphertext = Aes256Gcm::encrypt_with_nonce(std::span<const std::uint8_t>{pt_hex}, key, nonce);

    std::size_t pt_len = pt_hex.size();
    EXPECT_EQ(ciphertext.size(), Aes256Gcm::NONCE_SIZE + pt_len + Aes256Gcm::TAG_SIZE);

    // Extract ciphertext
    std::span<const std::uint8_t> ct_span{ciphertext.data() + Aes256Gcm::NONCE_SIZE, pt_len};
    EXPECT_EQ(to_hex(ct_span), to_hex(expected_ct_hex));

    // Extract tag
    std::span<const std::uint8_t> tag_span{ciphertext.data() + Aes256Gcm::NONCE_SIZE + pt_len, Aes256Gcm::TAG_SIZE};
    EXPECT_EQ(to_hex(tag_span), to_hex(expected_tag_hex));
}

// ============================================================================
// Encryption/Decryption Round-Trip Tests
// ============================================================================

TEST(Aes256GcmTest, RoundTrip_SmallMessage) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }

    std::string message = "Hello, AES-256-GCM!";
    std::vector<std::uint8_t> plaintext(message.begin(), message.end());

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);
    auto decrypted = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

    EXPECT_EQ(decrypted, plaintext);
}

TEST(Aes256GcmTest, RoundTrip_ExactlyOneBlock) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i * 3);
    }

    std::vector<std::uint8_t> plaintext(16);  // Exactly one AES block
    for (std::size_t i = 0; i < 16; ++i) {
        plaintext[i] = static_cast<std::uint8_t>(i);
    }

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);
    auto decrypted = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

    EXPECT_EQ(decrypted, plaintext);
}

TEST(Aes256GcmTest, RoundTrip_MultipleBlocks) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i * 7);
    }

    // 100 bytes = 6 full blocks + 4 bytes
    std::vector<std::uint8_t> plaintext(100);
    for (std::size_t i = 0; i < 100; ++i) {
        plaintext[i] = static_cast<std::uint8_t>(i % 256);
    }

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);
    auto decrypted = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

    EXPECT_EQ(decrypted, plaintext);
}

TEST(Aes256GcmTest, RoundTrip_LargeMessage) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i * 11);
    }

    // 64KB of data
    std::vector<std::uint8_t> plaintext(64 * 1024);
    for (std::size_t i = 0; i < plaintext.size(); ++i) {
        plaintext[i] = static_cast<std::uint8_t>(i % 256);
    }

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);
    auto decrypted = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

    EXPECT_EQ(decrypted, plaintext);
}

TEST(Aes256GcmTest, RoundTrip_EmptyMessage) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }

    std::vector<std::uint8_t> plaintext;  // Empty

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);
    auto decrypted = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

    EXPECT_EQ(decrypted, plaintext);
}

// ============================================================================
// Authentication Failure Tests
// ============================================================================

TEST(Aes256GcmTest, AuthenticationFailure_ModifiedCiphertext) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }

    std::string message = "Test message for authentication";
    std::vector<std::uint8_t> plaintext(message.begin(), message.end());

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);

    // Modify ciphertext (after nonce, before tag)
    ciphertext[Aes256Gcm::NONCE_SIZE] ^= 0x01;

    auto decrypted = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

    // Decryption should fail (empty result or different from original)
    EXPECT_NE(decrypted, plaintext);
}

TEST(Aes256GcmTest, AuthenticationFailure_ModifiedTag) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }

    std::string message = "Test message for authentication";
    std::vector<std::uint8_t> plaintext(message.begin(), message.end());

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);

    // Modify tag (last byte)
    ciphertext.back() ^= 0x01;

    auto decrypted = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

    // Decryption should fail
    EXPECT_TRUE(decrypted.empty() || decrypted != plaintext);
}

TEST(Aes256GcmTest, AuthenticationFailure_ModifiedNonce) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }

    std::string message = "Test message for authentication";
    std::vector<std::uint8_t> plaintext(message.begin(), message.end());

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);

    // Modify nonce (first byte)
    ciphertext[0] ^= 0x01;

    auto decrypted = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key);

    // Decryption should fail
    EXPECT_TRUE(decrypted.empty() || decrypted != plaintext);
}

TEST(Aes256GcmTest, AuthenticationFailure_WrongKey) {
    Aes256Gcm::Key key1{};
    Aes256Gcm::Key key2{};
    for (std::size_t i = 0; i < 32; ++i) {
        key1[i] = static_cast<std::uint8_t>(i);
        key2[i] = static_cast<std::uint8_t>(i + 1);
    }

    std::string message = "Test message";
    std::vector<std::uint8_t> plaintext(message.begin(), message.end());

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key1);
    auto decrypted = Aes256Gcm::decrypt(std::span<const std::uint8_t>{ciphertext}, key2);

    // Decryption should fail with wrong key
    EXPECT_TRUE(decrypted.empty() || decrypted != plaintext);
}

// ============================================================================
// Ciphertext Format Tests
// ============================================================================

TEST(Aes256GcmTest, CiphertextFormat) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }

    std::vector<std::uint8_t> plaintext(50);
    for (std::size_t i = 0; i < 50; ++i) {
        plaintext[i] = static_cast<std::uint8_t>(i);
    }

    auto ciphertext = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);

    // Ciphertext format: nonce (12) || ciphertext (50) || tag (16)
    EXPECT_EQ(ciphertext.size(), Aes256Gcm::NONCE_SIZE + 50 + Aes256Gcm::TAG_SIZE);
}

// ============================================================================
// Determinism Tests
// ============================================================================

TEST(Aes256GcmTest, SameNonceProducesSameOutput) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }

    Aes256Gcm::Nonce nonce{};
    for (std::size_t i = 0; i < 12; ++i) {
        nonce[i] = static_cast<std::uint8_t>(i * 2);
    }

    std::string message = "Test determinism";
    std::vector<std::uint8_t> plaintext(message.begin(), message.end());

    auto ct1 = Aes256Gcm::encrypt_with_nonce(std::span<const std::uint8_t>{plaintext}, key, nonce);
    auto ct2 = Aes256Gcm::encrypt_with_nonce(std::span<const std::uint8_t>{plaintext}, key, nonce);

    EXPECT_EQ(ct1, ct2);
}

TEST(Aes256GcmTest, DifferentNoncesProduceDifferentOutput) {
    Aes256Gcm::Key key{};
    for (std::size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<std::uint8_t>(i);
    }

    std::string message = "Test nonce variation";
    std::vector<std::uint8_t> plaintext(message.begin(), message.end());

    // Random nonces (from encrypt) should differ
    auto ct1 = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);
    auto ct2 = Aes256Gcm::encrypt(std::span<const std::uint8_t>{plaintext}, key);

    // Nonces should be different (first 12 bytes)
    bool nonces_equal = std::equal(ct1.begin(), ct1.begin() + Aes256Gcm::NONCE_SIZE,
                                    ct2.begin(), ct2.begin() + Aes256Gcm::NONCE_SIZE);
    // It's statistically unlikely but possible for random nonces to match
    // In practice this test just verifies the function works
    if (!nonces_equal) {
        // Ciphertexts should differ when nonces differ
        EXPECT_NE(ct1, ct2);
    }
}

// ============================================================================
// Hardware Acceleration Detection
// ============================================================================

TEST(Aes256GcmTest, HardwareAccelerationQuery) {
    bool has_hw = Aes256Gcm::has_hardware_acceleration();

    if (has_hw) {
        std::cout << "[   INFO   ] AES-256-GCM hardware acceleration: AVAILABLE\n";
    } else {
        std::cout << "[   INFO   ] AES-256-GCM hardware acceleration: NOT AVAILABLE\n";
    }
}

}  // namespace
}  // namespace dotvm::core::crypto
