#include "dotvm/core/crypto/keccak.hpp"

#include <algorithm>
#include <cstring>

namespace dotvm::core::crypto {

// ============================================================================
// Keccak Constants
// ============================================================================

namespace {

/// Round constants for iota step
constexpr std::array<std::uint64_t, 24> ROUND_CONSTANTS = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};

/// Rotation offsets for rho step (precomputed)
constexpr std::array<unsigned, 25> RHO_OFFSETS = {
    0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14};

/// Pi step lane permutation
constexpr std::array<std::size_t, 25> PI_LANES = {0,  10, 20, 5, 15, 16, 1,  11, 21, 6, 7,  17, 2,
                                                  12, 22, 23, 8, 18, 3,  13, 14, 24, 9, 19, 4};

/// Rotate left 64-bit
constexpr std::uint64_t rotl64(std::uint64_t x, unsigned n) noexcept {
    return (x << n) | (x >> (64 - n));
}

}  // namespace

// ============================================================================
// Keccak256 Implementation
// ============================================================================

Keccak256::Keccak256() noexcept : state_{}, buffer_{}, buffer_len_(0) {}

void Keccak256::reset() noexcept {
    state_.fill(0);
    buffer_.fill(0);
    buffer_len_ = 0;
}

void Keccak256::keccak_f(std::array<std::uint64_t, 25>& state) noexcept {
    // 24 rounds
    for (std::size_t round = 0; round < 24; ++round) {
        // Theta step
        std::array<std::uint64_t, 5> C{};
        for (std::size_t x = 0; x < 5; ++x) {
            C[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }

        std::array<std::uint64_t, 5> D{};
        for (std::size_t x = 0; x < 5; ++x) {
            D[x] = C[(x + 4) % 5] ^ rotl64(C[(x + 1) % 5], 1);
        }

        for (std::size_t i = 0; i < 25; ++i) {
            state[i] ^= D[i % 5];
        }

        // Rho and Pi steps combined
        std::array<std::uint64_t, 25> temp{};
        for (std::size_t i = 0; i < 25; ++i) {
            temp[PI_LANES[i]] = rotl64(state[i], RHO_OFFSETS[i]);
        }

        // Chi step
        for (std::size_t y = 0; y < 5; ++y) {
            for (std::size_t x = 0; x < 5; ++x) {
                std::size_t i = x + 5 * y;
                state[i] = temp[i] ^ ((~temp[(x + 1) % 5 + 5 * y]) & temp[(x + 2) % 5 + 5 * y]);
            }
        }

        // Iota step
        state[0] ^= ROUND_CONSTANTS[round];
    }
}

void Keccak256::absorb_block(std::span<const std::uint8_t, RATE> block) noexcept {
    // XOR block into state (little-endian)
    for (std::size_t i = 0; i < RATE / 8; ++i) {
        std::uint64_t lane = 0;
        for (std::size_t j = 0; j < 8; ++j) {
            lane |= static_cast<std::uint64_t>(block[i * 8 + j]) << (j * 8);
        }
        state_[i] ^= lane;
    }

    // Apply permutation
    keccak_f(state_);
}

void Keccak256::update(std::span<const std::uint8_t> data) noexcept {
    const auto* ptr = data.data();
    auto remaining = data.size();

    // Fill buffer if we have partial data
    if (buffer_len_ > 0) {
        auto to_copy = std::min(RATE - buffer_len_, remaining);
        std::memcpy(buffer_.data() + buffer_len_, ptr, to_copy);
        buffer_len_ += to_copy;
        ptr += to_copy;
        remaining -= to_copy;

        if (buffer_len_ == RATE) {
            absorb_block(std::span<const std::uint8_t, RATE>{buffer_});
            buffer_len_ = 0;
        }
    }

    // Process complete blocks
    while (remaining >= RATE) {
        absorb_block(std::span<const std::uint8_t, RATE>{ptr, RATE});
        ptr += RATE;
        remaining -= RATE;
    }

    // Buffer remaining
    if (remaining > 0) {
        std::memcpy(buffer_.data(), ptr, remaining);
        buffer_len_ = remaining;
    }
}

void Keccak256::update(std::string_view str) noexcept {
    update(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(str.data()),
                                         str.size()});
}

Keccak256::Digest Keccak256::finalize() noexcept {
    // Pad message with Keccak padding (0x01...0x80)
    // For Ethereum Keccak-256: domain separator is 0x01
    // For NIST SHA-3: domain separator is 0x06

    // Zero remaining buffer
    std::memset(buffer_.data() + buffer_len_, 0, RATE - buffer_len_);

    // Add Keccak domain separator (0x01 for Keccak, 0x06 for SHA-3)
    buffer_[buffer_len_] = 0x01;

    // Add final padding bit
    buffer_[RATE - 1] |= 0x80;

    // Absorb final padded block
    absorb_block(std::span<const std::uint8_t, RATE>{buffer_});

    // Extract digest (squeeze phase, but we only need 32 bytes)
    Digest digest{};
    for (std::size_t i = 0; i < DIGEST_SIZE / 8; ++i) {
        for (std::size_t j = 0; j < 8; ++j) {
            digest[i * 8 + j] = static_cast<std::uint8_t>(state_[i] >> (j * 8));
        }
    }

    // Reset for potential reuse
    reset();

    return digest;
}

Keccak256::Digest Keccak256::hash(std::span<const std::uint8_t> data) noexcept {
    Keccak256 hasher;
    hasher.update(data);
    return hasher.finalize();
}

Keccak256::Digest Keccak256::hash(std::string_view str) noexcept {
    Keccak256 hasher;
    hasher.update(str);
    return hasher.finalize();
}

}  // namespace dotvm::core::crypto
