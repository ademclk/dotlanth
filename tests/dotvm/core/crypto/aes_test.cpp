#include "dotvm/core/crypto/aes.hpp"

#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>
#include <string>

namespace dotvm::core::crypto {
namespace {

/// Convert block to hex string for comparison
std::string to_hex(const Aes128::Block& block) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::uint8_t byte : block) {
        oss << std::setw(2) << static_cast<unsigned>(byte);
    }
    return oss.str();
}

/// Parse hex string to block
Aes128::Block from_hex(const std::string& hex) {
    Aes128::Block block{};
    for (std::size_t i = 0; i < 16 && i * 2 + 1 < hex.size(); ++i) {
        unsigned int byte;
        std::stringstream ss;
        ss << std::hex << hex.substr(i * 2, 2);
        ss >> byte;
        block[i] = static_cast<std::uint8_t>(byte);
    }
    return block;
}

/// Parse hex string to key
Aes128::Key key_from_hex(const std::string& hex) {
    Aes128::Key key{};
    for (std::size_t i = 0; i < 16 && i * 2 + 1 < hex.size(); ++i) {
        unsigned int byte;
        std::stringstream ss;
        ss << std::hex << hex.substr(i * 2, 2);
        ss >> byte;
        key[i] = static_cast<std::uint8_t>(byte);
    }
    return key;
}

// ============================================================================
// FIPS-197 AES-128 Test Vectors
// ============================================================================

/// FIPS-197 Appendix C.1: AES-128 Test Vector
/// Key:        000102030405060708090a0b0c0d0e0f
/// Plaintext:  00112233445566778899aabbccddeeff
/// Ciphertext: 69c4e0d86a7b0430d8cdb78070b4c55a
TEST(Aes128Test, FIPS197_C1_Encrypt) {
    Aes128::Key key = key_from_hex("000102030405060708090a0b0c0d0e0f");
    Aes128::Block plaintext = from_hex("00112233445566778899aabbccddeeff");

    Aes128 cipher(key);
    auto ciphertext = cipher.encrypt_block(plaintext);

    EXPECT_EQ(to_hex(ciphertext), "69c4e0d86a7b0430d8cdb78070b4c55a");
}

TEST(Aes128Test, FIPS197_C1_Decrypt) {
    Aes128::Key key = key_from_hex("000102030405060708090a0b0c0d0e0f");
    Aes128::Block ciphertext = from_hex("69c4e0d86a7b0430d8cdb78070b4c55a");

    Aes128 cipher(key);
    auto plaintext = cipher.decrypt_block(ciphertext);

    EXPECT_EQ(to_hex(plaintext), "00112233445566778899aabbccddeeff");
}

TEST(Aes128Test, FIPS197_C1_RoundTrip) {
    Aes128::Key key = key_from_hex("000102030405060708090a0b0c0d0e0f");
    Aes128::Block original = from_hex("00112233445566778899aabbccddeeff");

    Aes128 cipher(key);
    auto encrypted = cipher.encrypt_block(original);
    auto decrypted = cipher.decrypt_block(encrypted);

    EXPECT_EQ(original, decrypted);
}

// ============================================================================
// NIST SP 800-38A Test Vectors (ECB Mode)
// ============================================================================

/// NIST SP 800-38A Section F.1.1: ECB-AES128.Encrypt
/// Key: 2b7e151628aed2a6abf7158809cf4f3c
TEST(Aes128Test, NIST_ECB_AES128_Block1_Encrypt) {
    Aes128::Key key = key_from_hex("2b7e151628aed2a6abf7158809cf4f3c");
    Aes128::Block plaintext = from_hex("6bc1bee22e409f96e93d7e117393172a");

    Aes128 cipher(key);
    auto ciphertext = cipher.encrypt_block(plaintext);

    EXPECT_EQ(to_hex(ciphertext), "3ad77bb40d7a3660a89ecaf32466ef97");
}

TEST(Aes128Test, NIST_ECB_AES128_Block2_Encrypt) {
    Aes128::Key key = key_from_hex("2b7e151628aed2a6abf7158809cf4f3c");
    Aes128::Block plaintext = from_hex("ae2d8a571e03ac9c9eb76fac45af8e51");

    Aes128 cipher(key);
    auto ciphertext = cipher.encrypt_block(plaintext);

    EXPECT_EQ(to_hex(ciphertext), "f5d3d58503b9699de785895a96fdbaaf");
}

TEST(Aes128Test, NIST_ECB_AES128_Block3_Encrypt) {
    Aes128::Key key = key_from_hex("2b7e151628aed2a6abf7158809cf4f3c");
    Aes128::Block plaintext = from_hex("30c81c46a35ce411e5fbc1191a0a52ef");

    Aes128 cipher(key);
    auto ciphertext = cipher.encrypt_block(plaintext);

    EXPECT_EQ(to_hex(ciphertext), "43b1cd7f598ece23881b00e3ed030688");
}

TEST(Aes128Test, NIST_ECB_AES128_Block4_Encrypt) {
    Aes128::Key key = key_from_hex("2b7e151628aed2a6abf7158809cf4f3c");
    Aes128::Block plaintext = from_hex("f69f2445df4f9b17ad2b417be66c3710");

    Aes128 cipher(key);
    auto ciphertext = cipher.encrypt_block(plaintext);

    EXPECT_EQ(to_hex(ciphertext), "7b0c785e27e8ad3f8223207104725dd4");
}

/// NIST SP 800-38A Section F.1.2: ECB-AES128.Decrypt
TEST(Aes128Test, NIST_ECB_AES128_Block1_Decrypt) {
    Aes128::Key key = key_from_hex("2b7e151628aed2a6abf7158809cf4f3c");
    Aes128::Block ciphertext = from_hex("3ad77bb40d7a3660a89ecaf32466ef97");

    Aes128 cipher(key);
    auto plaintext = cipher.decrypt_block(ciphertext);

    EXPECT_EQ(to_hex(plaintext), "6bc1bee22e409f96e93d7e117393172a");
}

TEST(Aes128Test, NIST_ECB_AES128_Block2_Decrypt) {
    Aes128::Key key = key_from_hex("2b7e151628aed2a6abf7158809cf4f3c");
    Aes128::Block ciphertext = from_hex("f5d3d58503b9699de785895a96fdbaaf");

    Aes128 cipher(key);
    auto plaintext = cipher.decrypt_block(ciphertext);

    EXPECT_EQ(to_hex(plaintext), "ae2d8a571e03ac9c9eb76fac45af8e51");
}

// ============================================================================
// Round Trip Tests
// ============================================================================

TEST(Aes128Test, RoundTrip_AllZeros) {
    Aes128::Key key = {};  // All zeros
    Aes128::Block plaintext = {};  // All zeros

    Aes128 cipher(key);
    auto encrypted = cipher.encrypt_block(plaintext);
    auto decrypted = cipher.decrypt_block(encrypted);

    EXPECT_EQ(plaintext, decrypted);
    // Ciphertext should not equal plaintext
    EXPECT_NE(to_hex(encrypted), to_hex(plaintext));
}

TEST(Aes128Test, RoundTrip_AllOnes) {
    Aes128::Key key;
    Aes128::Block plaintext;
    std::fill(key.begin(), key.end(), 0xFF);
    std::fill(plaintext.begin(), plaintext.end(), 0xFF);

    Aes128 cipher(key);
    auto encrypted = cipher.encrypt_block(plaintext);
    auto decrypted = cipher.decrypt_block(encrypted);

    EXPECT_EQ(plaintext, decrypted);
}

TEST(Aes128Test, RoundTrip_RandomLooking) {
    Aes128::Key key = {
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0
    };
    Aes128::Block plaintext = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
    };

    Aes128 cipher(key);
    auto encrypted = cipher.encrypt_block(plaintext);
    auto decrypted = cipher.decrypt_block(encrypted);

    EXPECT_EQ(plaintext, decrypted);
}

// ============================================================================
// Multiple Encryptions with Same Key
// ============================================================================

TEST(Aes128Test, MultipleEncryptions) {
    Aes128::Key key = key_from_hex("000102030405060708090a0b0c0d0e0f");
    Aes128 cipher(key);

    Aes128::Block block1 = from_hex("00112233445566778899aabbccddeeff");
    Aes128::Block block2 = from_hex("00000000000000000000000000000000");
    Aes128::Block block3 = from_hex("ffffffffffffffffffffffffffffffff");

    auto ct1 = cipher.encrypt_block(block1);
    auto ct2 = cipher.encrypt_block(block2);
    auto ct3 = cipher.encrypt_block(block3);

    // Decrypt and verify
    EXPECT_EQ(cipher.decrypt_block(ct1), block1);
    EXPECT_EQ(cipher.decrypt_block(ct2), block2);
    EXPECT_EQ(cipher.decrypt_block(ct3), block3);

    // Different plaintexts should produce different ciphertexts
    EXPECT_NE(ct1, ct2);
    EXPECT_NE(ct2, ct3);
    EXPECT_NE(ct1, ct3);
}

// ============================================================================
// Key Sensitivity Tests
// ============================================================================

TEST(Aes128Test, DifferentKeysProduceDifferentCiphertexts) {
    Aes128::Block plaintext = from_hex("00112233445566778899aabbccddeeff");

    Aes128::Key key1 = key_from_hex("000102030405060708090a0b0c0d0e0f");
    Aes128::Key key2 = key_from_hex("000102030405060708090a0b0c0d0e0e");  // One bit different

    Aes128 cipher1(key1);
    Aes128 cipher2(key2);

    auto ct1 = cipher1.encrypt_block(plaintext);
    auto ct2 = cipher2.encrypt_block(plaintext);

    EXPECT_NE(ct1, ct2);
}

TEST(Aes128Test, WrongKeyProducesWrongPlaintext) {
    Aes128::Block original = from_hex("00112233445566778899aabbccddeeff");

    Aes128::Key correct_key = key_from_hex("000102030405060708090a0b0c0d0e0f");
    Aes128::Key wrong_key = key_from_hex("000102030405060708090a0b0c0d0e0e");

    Aes128 cipher_correct(correct_key);
    Aes128 cipher_wrong(wrong_key);

    auto ciphertext = cipher_correct.encrypt_block(original);
    auto wrong_plaintext = cipher_wrong.decrypt_block(ciphertext);

    EXPECT_NE(original, wrong_plaintext);
}

// ============================================================================
// Hardware Acceleration Detection
// ============================================================================

TEST(Aes128Test, HardwareAccelerationQuery) {
    bool has_hw = Aes128::has_hardware_acceleration();

    if (has_hw) {
        std::cout << "[   INFO   ] AES hardware acceleration: AVAILABLE\n";
    } else {
        std::cout << "[   INFO   ] AES hardware acceleration: NOT AVAILABLE (using scalar)\n";
    }
}

// ============================================================================
// Determinism Tests
// ============================================================================

TEST(Aes128Test, EncryptionIsDeterministic) {
    Aes128::Key key = key_from_hex("000102030405060708090a0b0c0d0e0f");
    Aes128::Block plaintext = from_hex("00112233445566778899aabbccddeeff");

    Aes128 cipher(key);

    auto ct1 = cipher.encrypt_block(plaintext);
    auto ct2 = cipher.encrypt_block(plaintext);
    auto ct3 = cipher.encrypt_block(plaintext);

    EXPECT_EQ(ct1, ct2);
    EXPECT_EQ(ct2, ct3);
}

TEST(Aes128Test, DecryptionIsDeterministic) {
    Aes128::Key key = key_from_hex("000102030405060708090a0b0c0d0e0f");
    Aes128::Block ciphertext = from_hex("69c4e0d86a7b0430d8cdb78070b4c55a");

    Aes128 cipher(key);

    auto pt1 = cipher.decrypt_block(ciphertext);
    auto pt2 = cipher.decrypt_block(ciphertext);
    auto pt3 = cipher.decrypt_block(ciphertext);

    EXPECT_EQ(pt1, pt2);
    EXPECT_EQ(pt2, pt3);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(Aes128Test, EncryptDecryptSameInstance) {
    Aes128::Key key = key_from_hex("2b7e151628aed2a6abf7158809cf4f3c");
    Aes128::Block plaintext = from_hex("6bc1bee22e409f96e93d7e117393172a");

    Aes128 cipher(key);

    // Multiple round trips
    for (int i = 0; i < 10; ++i) {
        auto encrypted = cipher.encrypt_block(plaintext);
        auto decrypted = cipher.decrypt_block(encrypted);
        EXPECT_EQ(plaintext, decrypted) << "Failed at iteration " << i;
    }
}

}  // namespace
}  // namespace dotvm::core::crypto
