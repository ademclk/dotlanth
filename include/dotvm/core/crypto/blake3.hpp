#pragma once

/// @file blake3.hpp
/// @brief BLAKE3 cryptographic hash function implementation
///
/// Provides BLAKE3 hashing with hardware acceleration support:
/// - x86-64: AVX2/AVX-512 when available for parallel chunk processing
/// - Fallback: Portable scalar implementation
///
/// Part of SEC-008: Cryptographic Operations for the dotlanth VM

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace dotvm::core::crypto {

/// BLAKE3 hash function implementation
///
/// Implements the BLAKE3 cryptographic hash function with hardware acceleration.
/// Supports incremental hashing via update() or one-shot via hash().
///
/// BLAKE3 is designed for high performance with:
/// - Merkle tree structure for parallelism
/// - Only 7 rounds per compression (vs 10 in BLAKE2)
/// - AVX2/AVX-512 vectorization support
///
/// @example
/// ```cpp
/// // One-shot usage
/// auto digest = Blake3::hash("hello world");
///
/// // Incremental usage
/// Blake3 hasher;
/// hasher.update("hello ");
/// hasher.update("world");
/// auto digest = hasher.finalize();
/// ```
class Blake3 {
public:
    static constexpr std::size_t BLOCK_SIZE = 64;    // 512 bits
    static constexpr std::size_t DIGEST_SIZE = 32;   // 256 bits default
    static constexpr std::size_t KEY_SIZE = 32;      // 256 bits for keyed mode
    static constexpr std::size_t CHUNK_SIZE = 1024;  // 16 blocks per chunk

    using Digest = std::array<std::uint8_t, DIGEST_SIZE>;
    using Block = std::array<std::uint8_t, BLOCK_SIZE>;
    using Key = std::array<std::uint8_t, KEY_SIZE>;

    /// Construct a new BLAKE3 hasher with initial state
    Blake3() noexcept;

    /// Construct a BLAKE3 hasher in keyed mode
    /// @param key 32-byte key for keyed hashing (MAC)
    explicit Blake3(const Key& key) noexcept;

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

    /// Keyed hash (MAC) computation
    /// @param key 32-byte key
    /// @param data Data to hash
    /// @return 32-byte hash digest
    [[nodiscard]] static Digest keyed_hash(const Key& key,
                                           std::span<const std::uint8_t> data) noexcept;

    /// Check if hardware acceleration is available
    /// @return true if AVX2 or AVX-512 is available for parallel processing
    [[nodiscard]] static bool has_hardware_acceleration() noexcept;

private:
    // BLAKE3 IV (same as BLAKE2s)
    static constexpr std::array<std::uint32_t, 8> IV = {0x6A09E667, 0xBB67AE85, 0x3C6EF372,
                                                        0xA54FF53A, 0x510E527F, 0x9B05688C,
                                                        0x1F83D9AB, 0x5BE0CD19};

    // Domain separation flags
    static constexpr std::uint32_t CHUNK_START = 1 << 0;
    static constexpr std::uint32_t CHUNK_END = 1 << 1;
    static constexpr std::uint32_t PARENT = 1 << 2;
    static constexpr std::uint32_t ROOT = 1 << 3;
    static constexpr std::uint32_t KEYED_HASH = 1 << 4;
    static constexpr std::uint32_t DERIVE_KEY_CONTEXT = 1 << 5;
    static constexpr std::uint32_t DERIVE_KEY_MATERIAL = 1 << 6;

    // Internal state
    std::array<std::uint32_t, 8> key_words_;  // Key or IV
    std::array<std::uint32_t, 8> cv_;         // Current chaining value
    std::array<std::uint8_t, CHUNK_SIZE> chunk_buffer_;
    std::size_t chunk_buffer_len_ = 0;
    std::uint64_t chunk_counter_ = 0;
    std::uint32_t flags_ = 0;

    // Stack for Merkle tree (max 54 levels for 2^64 bytes input)
    std::array<std::array<std::uint32_t, 8>, 54> cv_stack_;
    std::size_t cv_stack_len_ = 0;

    /// Process a single block
    static void compress(std::array<std::uint32_t, 16>& state,
                         const std::array<std::uint32_t, 16>& msg, std::uint64_t counter,
                         std::uint32_t block_len, std::uint32_t flags) noexcept;

    /// Compress chunk and add to stack
    void add_chunk_cv(const std::array<std::uint32_t, 8>& cv) noexcept;

    /// Process a complete chunk
    void process_chunk(std::span<const std::uint8_t, CHUNK_SIZE> chunk) noexcept;

    /// Process final partial chunk
    std::array<std::uint32_t, 8> finalize_chunk(std::span<const std::uint8_t> data,
                                                bool is_root) noexcept;

    /// Merge stack entries
    void merge_cv_stack() noexcept;

    /// Reset to initial state
    void reset() noexcept;

    /// Scalar compression function
    static void compress_scalar(std::array<std::uint32_t, 16>& state,
                                const std::array<std::uint32_t, 16>& msg, std::uint64_t counter,
                                std::uint32_t block_len, std::uint32_t flags) noexcept;

#if defined(__x86_64__) || defined(_M_X64)
    /// AVX2 accelerated chunk processing (processes 8 blocks in parallel)
    void process_chunk_avx2(std::span<const std::uint8_t, CHUNK_SIZE> chunk) noexcept;
#endif
};

}  // namespace dotvm::core::crypto
