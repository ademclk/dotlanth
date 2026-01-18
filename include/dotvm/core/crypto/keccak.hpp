#pragma once

/// @file keccak.hpp
/// @brief Keccak-256 cryptographic hash function implementation
///
/// Provides Keccak-256 hashing (Ethereum-compatible variant):
/// - Uses 0x01 padding suffix (original Keccak)
/// - NOT NIST SHA-3 (which uses 0x06 padding)
/// - Fallback: Portable scalar implementation
///
/// Part of SEC-008: Cryptographic Operations for the dotlanth VM

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace dotvm::core::crypto {

/// Keccak-256 hash function implementation (Ethereum-compatible)
///
/// Implements Keccak-256 as used by Ethereum and other blockchains.
/// Uses the original Keccak padding (0x01), not NIST SHA-3 (0x06).
///
/// @note This is specifically Keccak-256, not SHA3-256. While they use
/// the same permutation, they have different domain separation bytes.
///
/// @example
/// ```cpp
/// // One-shot usage
/// auto digest = Keccak256::hash("hello world");
///
/// // Incremental usage
/// Keccak256 hasher;
/// hasher.update("hello ");
/// hasher.update("world");
/// auto digest = hasher.finalize();
/// ```
class Keccak256 {
public:
    static constexpr std::size_t DIGEST_SIZE = 32;  // 256 bits
    static constexpr std::size_t RATE = 136;        // 1088 bits (136 bytes)
    static constexpr std::size_t CAPACITY = 64;     // 512 bits (64 bytes)
    static constexpr std::size_t STATE_SIZE = 200;  // 1600 bits (200 bytes)

    using Digest = std::array<std::uint8_t, DIGEST_SIZE>;

    /// Construct a new Keccak-256 hasher with initial state
    Keccak256() noexcept;

    /// Update hash state with arbitrary data
    /// @param data Data to hash (can be any length)
    void update(std::span<const std::uint8_t> data) noexcept;

    /// Update hash state with string data
    /// @param str String to hash
    void update(std::string_view str) noexcept;

    /// Finalize and return the 256-bit digest
    /// @return 32-byte hash digest
    /// @note After calling finalize(), the hasher is reset to initial state
    [[nodiscard]] Digest finalize() noexcept;

    /// One-shot hash computation
    /// @param data Data to hash
    /// @return 32-byte hash digest
    [[nodiscard]] static Digest hash(std::span<const std::uint8_t> data) noexcept;

    /// One-shot hash computation for strings
    /// @param str String to hash
    /// @return 32-byte hash digest
    [[nodiscard]] static Digest hash(std::string_view str) noexcept;

    /// Check if hardware acceleration is available
    /// @return Always false (no hardware support for Keccak)
    [[nodiscard]] static bool has_hardware_acceleration() noexcept { return false; }

private:
    // Internal state: 5x5 matrix of 64-bit words = 1600 bits
    std::array<std::uint64_t, 25> state_{};
    std::array<std::uint8_t, RATE> buffer_{};
    std::size_t buffer_len_ = 0;

    /// Keccak-f[1600] permutation
    static void keccak_f(std::array<std::uint64_t, 25>& state) noexcept;

    /// Absorb a rate-sized block into state
    void absorb_block(std::span<const std::uint8_t, RATE> block) noexcept;

    /// Reset to initial state
    void reset() noexcept;
};

}  // namespace dotvm::core::crypto
