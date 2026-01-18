#pragma once

/// @file aes256gcm.hpp
/// @brief AES-256-GCM authenticated encryption implementation
///
/// Provides AES-256-GCM (Galois/Counter Mode):
/// - 256-bit key (32 bytes)
/// - 12-byte nonce (random, prepended to ciphertext)
/// - 16-byte authentication tag (appended to ciphertext)
/// - Hardware acceleration: AES-NI + PCLMULQDQ when available
///
/// Part of SEC-008: Cryptographic Operations for the dotlanth VM

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace dotvm::core::crypto {

/// AES-256-GCM authenticated encryption
///
/// Implements NIST SP 800-38D AES-GCM with 256-bit keys.
/// Provides authenticated encryption with associated data (AEAD).
///
/// Output format: nonce(12) || ciphertext || tag(16)
///
/// @example
/// ```cpp
/// Aes256Gcm::Key key = {...};
/// auto ciphertext = Aes256Gcm::encrypt(plaintext, key);
/// auto plaintext = Aes256Gcm::decrypt(ciphertext, key);
/// ```
class Aes256Gcm {
public:
    static constexpr std::size_t KEY_SIZE = 32;    // 256 bits
    static constexpr std::size_t NONCE_SIZE = 12;  // 96 bits (recommended)
    static constexpr std::size_t TAG_SIZE = 16;    // 128 bits
    static constexpr std::size_t BLOCK_SIZE = 16;  // 128 bits
    static constexpr std::size_t NUM_ROUNDS = 14;  // AES-256 rounds

    using Key = std::array<std::uint8_t, KEY_SIZE>;
    using Nonce = std::array<std::uint8_t, NONCE_SIZE>;
    using Tag = std::array<std::uint8_t, TAG_SIZE>;
    using Block = std::array<std::uint8_t, BLOCK_SIZE>;

    /// Encrypt plaintext with AES-256-GCM
    /// @param plaintext Data to encrypt
    /// @param key 32-byte encryption key
    /// @param aad Optional additional authenticated data (not encrypted)
    /// @return nonce(12) || ciphertext || tag(16)
    [[nodiscard]] static std::vector<std::uint8_t>
    encrypt(std::span<const std::uint8_t> plaintext, const Key& key,
            std::span<const std::uint8_t> aad = {}) noexcept;

    /// Decrypt ciphertext with AES-256-GCM
    /// @param ciphertext Format: nonce(12) || ciphertext || tag(16)
    /// @param key 32-byte encryption key
    /// @param aad Optional additional authenticated data
    /// @return Decrypted plaintext, or empty if authentication fails
    [[nodiscard]] static std::vector<std::uint8_t>
    decrypt(std::span<const std::uint8_t> ciphertext, const Key& key,
            std::span<const std::uint8_t> aad = {}) noexcept;

    /// Encrypt with explicit nonce (for advanced usage)
    /// @param plaintext Data to encrypt
    /// @param key 32-byte encryption key
    /// @param nonce 12-byte nonce (MUST be unique per key)
    /// @param aad Optional additional authenticated data
    /// @return ciphertext || tag(16) (nonce NOT included)
    [[nodiscard]] static std::vector<std::uint8_t>
    encrypt_with_nonce(std::span<const std::uint8_t> plaintext, const Key& key, const Nonce& nonce,
                       std::span<const std::uint8_t> aad = {}) noexcept;

    /// Decrypt with explicit nonce (for advanced usage)
    /// @param ciphertext Format: ciphertext || tag(16)
    /// @param key 32-byte encryption key
    /// @param nonce 12-byte nonce used during encryption
    /// @param aad Optional additional authenticated data
    /// @return Decrypted plaintext, or empty if authentication fails
    [[nodiscard]] static std::vector<std::uint8_t>
    decrypt_with_nonce(std::span<const std::uint8_t> ciphertext, const Key& key, const Nonce& nonce,
                       std::span<const std::uint8_t> aad = {}) noexcept;

    /// Check if hardware acceleration is available
    /// @return true if AES-NI and PCLMULQDQ are available
    [[nodiscard]] static bool has_hardware_acceleration() noexcept;

private:
    // AES-256 key schedule (15 round keys)
    using RoundKeys = std::array<Block, NUM_ROUNDS + 1>;

    /// Expand 256-bit key into round keys
    static void key_expansion(RoundKeys& round_keys, const Key& key) noexcept;

    /// Encrypt single block with AES-256
    static Block encrypt_block(const Block& plaintext, const RoundKeys& round_keys) noexcept;

    /// GHASH multiplication in GF(2^128)
    static Block ghash_multiply(const Block& X, const Block& H) noexcept;

    /// GHASH function for authentication
    static Block ghash(const Block& H, std::span<const std::uint8_t> aad,
                       std::span<const std::uint8_t> ciphertext) noexcept;

    /// Increment counter (last 4 bytes, big-endian)
    static void increment_counter(Block& counter) noexcept;

    /// Generate random nonce
    static Nonce generate_nonce() noexcept;

    /// AES round transformations
    static void sub_bytes(Block& state) noexcept;
    static void shift_rows(Block& state) noexcept;
    static void mix_columns(Block& state) noexcept;
    static void add_round_key(Block& state, const Block& round_key) noexcept;

#if defined(__x86_64__) || defined(_M_X64)
    /// AES-NI accelerated encryption
    static Block encrypt_block_aesni(const Block& plaintext, const RoundKeys& round_keys) noexcept;

    /// PCLMULQDQ accelerated GHASH
    static Block ghash_multiply_pclmul(const Block& X, const Block& H) noexcept;
#endif
};

}  // namespace dotvm::core::crypto
