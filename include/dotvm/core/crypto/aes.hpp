#pragma once

/// @file aes.hpp
/// @brief AES-128 block cipher implementation
///
/// Provides AES-128 encryption/decryption with hardware acceleration:
/// - x86-64: AES-NI instructions when available
/// - ARM64: ARMv8 Crypto extensions when available
/// - Fallback: Portable scalar implementation with constant-time operations
///
/// Part of CORE-008: Crypto Primitives for the dotlanth VM

#include <array>
#include <cstdint>
#include <span>

namespace dotvm::core::crypto {

/// AES-128 block cipher implementation
///
/// Implements FIPS-197 AES-128 with hardware acceleration when available.
/// Provides single-block encryption/decryption operations.
///
/// @example
/// ```cpp
/// Aes128::Key key = {0x00, 0x01, ..., 0x0f};
/// Aes128 cipher(key);
///
/// Aes128::Block plaintext = {...};
/// auto ciphertext = cipher.encrypt_block(plaintext);
/// auto decrypted = cipher.decrypt_block(ciphertext);
/// ```
///
/// @note This is a low-level block cipher. For secure encryption of arbitrary
/// data, use an appropriate mode of operation (CBC, CTR, GCM, etc.).
class Aes128 {
public:
    static constexpr std::size_t BLOCK_SIZE = 16;
    static constexpr std::size_t KEY_SIZE = 16;
    static constexpr std::size_t NUM_ROUNDS = 10;

    using Block = std::array<std::uint8_t, BLOCK_SIZE>;
    using Key = std::array<std::uint8_t, KEY_SIZE>;

    /// Construct cipher with the given key
    /// @param key 128-bit (16-byte) encryption key
    explicit Aes128(const Key& key) noexcept;

    /// Encrypt a single 128-bit block
    /// @param plaintext 16-byte plaintext block
    /// @return 16-byte ciphertext block
    [[nodiscard]] Block
    encrypt_block(std::span<const std::uint8_t, BLOCK_SIZE> plaintext) const noexcept;

    /// Decrypt a single 128-bit block
    /// @param ciphertext 16-byte ciphertext block
    /// @return 16-byte plaintext block
    [[nodiscard]] Block
    decrypt_block(std::span<const std::uint8_t, BLOCK_SIZE> ciphertext) const noexcept;

    /// Check if hardware acceleration is available
    /// @return true if AES-NI (x86) or ARM Crypto extensions are available
    [[nodiscard]] static bool has_hardware_acceleration() noexcept;

private:
    std::array<std::array<std::uint8_t, 16>, NUM_ROUNDS + 1> round_keys_;

    /// Perform AES key expansion
    void key_expansion(const Key& key) noexcept;

    /// Scalar (portable) encryption
    Block encrypt_block_scalar(std::span<const std::uint8_t, 16> plaintext) const noexcept;

    /// Scalar (portable) decryption
    Block decrypt_block_scalar(std::span<const std::uint8_t, 16> ciphertext) const noexcept;

#if defined(__x86_64__) || defined(_M_X64)
    /// AES-NI hardware-accelerated encryption (x86-64)
    Block encrypt_block_aesni(std::span<const std::uint8_t, 16> plaintext) const noexcept;

    /// AES-NI hardware-accelerated decryption (x86-64)
    Block decrypt_block_aesni(std::span<const std::uint8_t, 16> ciphertext) const noexcept;
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    /// ARM Crypto hardware-accelerated encryption (AArch64)
    Block encrypt_block_arm_crypto(std::span<const std::uint8_t, 16> plaintext) const noexcept;

    /// ARM Crypto hardware-accelerated decryption (AArch64)
    Block decrypt_block_arm_crypto(std::span<const std::uint8_t, 16> ciphertext) const noexcept;
#endif
};

}  // namespace dotvm::core::crypto
