#pragma once

/// @file sha256.hpp
/// @brief SHA-256 cryptographic hash function implementation
///
/// Provides SHA-256 hashing with hardware acceleration support:
/// - x86-64: SHA-NI instructions when available
/// - ARM64: ARMv8 Crypto extensions when available
/// - Fallback: Portable scalar implementation
///
/// Part of CORE-008: Crypto Primitives for the dotlanth VM

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace dotvm::core::crypto {

/// SHA-256 hash function implementation
///
/// Implements FIPS 180-4 SHA-256 with hardware acceleration when available.
/// Supports incremental hashing via update() or one-shot via hash().
///
/// @example
/// ```cpp
/// // One-shot usage
/// auto digest = Sha256::hash("hello world");
///
/// // Incremental usage
/// Sha256 hasher;
/// hasher.update("hello ");
/// hasher.update("world");
/// auto digest = hasher.finalize();
/// ```
class Sha256 {
public:
    static constexpr std::size_t BLOCK_SIZE = 64;   // 512 bits
    static constexpr std::size_t DIGEST_SIZE = 32;  // 256 bits

    using Digest = std::array<std::uint8_t, DIGEST_SIZE>;
    using Block = std::array<std::uint8_t, BLOCK_SIZE>;

    /// Construct a new SHA-256 hasher with initial state
    Sha256() noexcept;

    /// Process a complete 64-byte block
    /// @param block Exactly 64 bytes of data to process
    void process_block(std::span<const std::uint8_t, BLOCK_SIZE> block) noexcept;

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
    /// @return true if SHA-NI (x86) or ARM Crypto extensions are available
    [[nodiscard]] static bool has_hardware_acceleration() noexcept;

private:
    std::array<std::uint32_t, 8> state_;  // H0-H7
    std::array<std::uint8_t, 64> buffer_;
    std::size_t buffer_len_ = 0;
    std::uint64_t total_len_ = 0;

    /// Scalar (portable) block processing
    void process_block_scalar(std::span<const std::uint8_t, 64> block) noexcept;

#if defined(__x86_64__) || defined(_M_X64)
    /// SHA-NI hardware-accelerated block processing (x86-64)
    void process_block_sha_ni(std::span<const std::uint8_t, 64> block) noexcept;
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    /// ARM Crypto hardware-accelerated block processing (AArch64)
    void process_block_arm_crypto(std::span<const std::uint8_t, 64> block) noexcept;
#endif
};

}  // namespace dotvm::core::crypto
