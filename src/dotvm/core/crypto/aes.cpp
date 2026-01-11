#include "dotvm/core/crypto/aes.hpp"
#include "dotvm/core/simd/cpu_features.hpp"

#include <algorithm>
#include <cstring>

// Platform-specific intrinsics for hardware acceleration
#if defined(__x86_64__) || defined(_M_X64)
    #include <wmmintrin.h>  // AES-NI
    #include <emmintrin.h>  // SSE2
    #define DOTVM_X86_AES 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    #if defined(__ARM_FEATURE_CRYPTO) || defined(_MSC_VER)
        #include <arm_neon.h>
        #define DOTVM_ARM_AES 1
    #endif
#endif

namespace dotvm::core::crypto {

// ============================================================================
// AES Constants (FIPS-197)
// ============================================================================

namespace {

/// AES S-box (substitution box) for SubBytes transformation
constexpr std::array<std::uint8_t, 256> SBOX = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/// AES inverse S-box for InvSubBytes transformation
constexpr std::array<std::uint8_t, 256> INV_SBOX = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

/// Round constants for key expansion
constexpr std::array<std::uint8_t, 10> RCON = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

/// Galois Field multiplication by 2
constexpr std::uint8_t gf_mul2(std::uint8_t x) noexcept {
    return static_cast<std::uint8_t>((x << 1) ^ ((x >> 7) * 0x1b));
}

/// Galois Field multiplication by 3
constexpr std::uint8_t gf_mul3(std::uint8_t x) noexcept {
    return static_cast<std::uint8_t>(gf_mul2(x) ^ x);
}

/// Galois Field multiplication by 9
constexpr std::uint8_t gf_mul9(std::uint8_t x) noexcept {
    return static_cast<std::uint8_t>(gf_mul2(gf_mul2(gf_mul2(x))) ^ x);
}

/// Galois Field multiplication by 11
constexpr std::uint8_t gf_mul11(std::uint8_t x) noexcept {
    return static_cast<std::uint8_t>(gf_mul2(gf_mul2(gf_mul2(x)) ^ x) ^ x);
}

/// Galois Field multiplication by 13
constexpr std::uint8_t gf_mul13(std::uint8_t x) noexcept {
    return static_cast<std::uint8_t>(gf_mul2(gf_mul2(gf_mul2(x) ^ x)) ^ x);
}

/// Galois Field multiplication by 14
constexpr std::uint8_t gf_mul14(std::uint8_t x) noexcept {
    return static_cast<std::uint8_t>(gf_mul2(gf_mul2(gf_mul2(x) ^ x) ^ x));
}

/// XOR two 16-byte blocks
inline void xor_block(std::uint8_t* dst, const std::uint8_t* src1, const std::uint8_t* src2) noexcept {
    for (int i = 0; i < 16; ++i) {
        dst[i] = static_cast<std::uint8_t>(src1[i] ^ src2[i]);
    }
}

/// Apply SubBytes transformation
inline void sub_bytes(std::uint8_t* state) noexcept {
    for (int i = 0; i < 16; ++i) {
        state[i] = SBOX[state[i]];
    }
}

/// Apply InvSubBytes transformation
inline void inv_sub_bytes(std::uint8_t* state) noexcept {
    for (int i = 0; i < 16; ++i) {
        state[i] = INV_SBOX[state[i]];
    }
}

/// Apply ShiftRows transformation
/// State layout (column-major):
/// [0  4  8 12]
/// [1  5  9 13]
/// [2  6 10 14]
/// [3  7 11 15]
inline void shift_rows(std::uint8_t* state) noexcept {
    std::uint8_t tmp;

    // Row 1: shift left by 1
    tmp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = tmp;

    // Row 2: shift left by 2
    tmp = state[2];
    state[2] = state[10];
    state[10] = tmp;
    tmp = state[6];
    state[6] = state[14];
    state[14] = tmp;

    // Row 3: shift left by 3 (= shift right by 1)
    tmp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = tmp;
}

/// Apply InvShiftRows transformation
inline void inv_shift_rows(std::uint8_t* state) noexcept {
    std::uint8_t tmp;

    // Row 1: shift right by 1
    tmp = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = tmp;

    // Row 2: shift right by 2
    tmp = state[2];
    state[2] = state[10];
    state[10] = tmp;
    tmp = state[6];
    state[6] = state[14];
    state[14] = tmp;

    // Row 3: shift right by 3 (= shift left by 1)
    tmp = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = tmp;
}

/// Apply MixColumns transformation
inline void mix_columns(std::uint8_t* state) noexcept {
    for (int col = 0; col < 4; ++col) {
        int i = col * 4;
        std::uint8_t a = state[i];
        std::uint8_t b = state[i + 1];
        std::uint8_t c = state[i + 2];
        std::uint8_t d = state[i + 3];

        // Matrix multiplication in GF(2^8)
        // [2 3 1 1]   [a]
        // [1 2 3 1] * [b]
        // [1 1 2 3]   [c]
        // [3 1 1 2]   [d]
        state[i]     = static_cast<std::uint8_t>(gf_mul2(a) ^ gf_mul3(b) ^ c ^ d);
        state[i + 1] = static_cast<std::uint8_t>(a ^ gf_mul2(b) ^ gf_mul3(c) ^ d);
        state[i + 2] = static_cast<std::uint8_t>(a ^ b ^ gf_mul2(c) ^ gf_mul3(d));
        state[i + 3] = static_cast<std::uint8_t>(gf_mul3(a) ^ b ^ c ^ gf_mul2(d));
    }
}

/// Apply InvMixColumns transformation
inline void inv_mix_columns(std::uint8_t* state) noexcept {
    for (int col = 0; col < 4; ++col) {
        int i = col * 4;
        std::uint8_t a = state[i];
        std::uint8_t b = state[i + 1];
        std::uint8_t c = state[i + 2];
        std::uint8_t d = state[i + 3];

        // Inverse matrix multiplication in GF(2^8)
        // [14 11 13  9]   [a]
        // [ 9 14 11 13] * [b]
        // [13  9 14 11]   [c]
        // [11 13  9 14]   [d]
        state[i]     = static_cast<std::uint8_t>(gf_mul14(a) ^ gf_mul11(b) ^ gf_mul13(c) ^ gf_mul9(d));
        state[i + 1] = static_cast<std::uint8_t>(gf_mul9(a) ^ gf_mul14(b) ^ gf_mul11(c) ^ gf_mul13(d));
        state[i + 2] = static_cast<std::uint8_t>(gf_mul13(a) ^ gf_mul9(b) ^ gf_mul14(c) ^ gf_mul11(d));
        state[i + 3] = static_cast<std::uint8_t>(gf_mul11(a) ^ gf_mul13(b) ^ gf_mul9(c) ^ gf_mul14(d));
    }
}

}  // namespace

// ============================================================================
// Aes128 Implementation
// ============================================================================

Aes128::Aes128(const Key& key) noexcept {
    key_expansion(key);
}

void Aes128::key_expansion(const Key& key) noexcept {
    // Copy the original key as the first round key
    std::memcpy(round_keys_[0].data(), key.data(), KEY_SIZE);

    // Generate remaining round keys
    for (std::size_t round = 1; round <= NUM_ROUNDS; ++round) {
        const auto& prev = round_keys_[round - 1];
        auto& curr = round_keys_[round];

        // RotWord + SubWord + Rcon on last word of previous key
        std::uint8_t temp[4];
        temp[0] = static_cast<std::uint8_t>(SBOX[prev[13]] ^ RCON[round - 1]);
        temp[1] = SBOX[prev[14]];
        temp[2] = SBOX[prev[15]];
        temp[3] = SBOX[prev[12]];

        // Generate new round key words
        for (std::size_t i = 0; i < 4; ++i) {
            curr[i] = static_cast<std::uint8_t>(prev[i] ^ temp[i]);
        }

        for (std::size_t word = 1; word < 4; ++word) {
            for (std::size_t i = 0; i < 4; ++i) {
                curr[word * 4 + i] = static_cast<std::uint8_t>(curr[(word - 1) * 4 + i] ^ prev[word * 4 + i]);
            }
        }
    }
}

Aes128::Block Aes128::encrypt_block_scalar(std::span<const std::uint8_t, 16> plaintext) const noexcept {
    Block state;
    std::memcpy(state.data(), plaintext.data(), BLOCK_SIZE);

    // Initial round key addition
    xor_block(state.data(), state.data(), round_keys_[0].data());

    // Main rounds (1 to NUM_ROUNDS-1)
    for (std::size_t round = 1; round < NUM_ROUNDS; ++round) {
        sub_bytes(state.data());
        shift_rows(state.data());
        mix_columns(state.data());
        xor_block(state.data(), state.data(), round_keys_[round].data());
    }

    // Final round (no MixColumns)
    sub_bytes(state.data());
    shift_rows(state.data());
    xor_block(state.data(), state.data(), round_keys_[NUM_ROUNDS].data());

    return state;
}

Aes128::Block Aes128::decrypt_block_scalar(std::span<const std::uint8_t, 16> ciphertext) const noexcept {
    Block state;
    std::memcpy(state.data(), ciphertext.data(), BLOCK_SIZE);

    // Initial round key addition (using last round key)
    xor_block(state.data(), state.data(), round_keys_[NUM_ROUNDS].data());

    // Main rounds (NUM_ROUNDS-1 down to 1)
    for (std::size_t round = NUM_ROUNDS - 1; round >= 1; --round) {
        inv_shift_rows(state.data());
        inv_sub_bytes(state.data());
        xor_block(state.data(), state.data(), round_keys_[round].data());
        inv_mix_columns(state.data());
    }

    // Final round (no InvMixColumns)
    inv_shift_rows(state.data());
    inv_sub_bytes(state.data());
    xor_block(state.data(), state.data(), round_keys_[0].data());

    return state;
}

// ============================================================================
// x86-64 AES-NI Implementation
// ============================================================================

#if defined(DOTVM_X86_AES)

__attribute__((target("aes,sse2")))
Aes128::Block Aes128::encrypt_block_aesni(std::span<const std::uint8_t, 16> plaintext) const noexcept {
    __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(plaintext.data()));

    // Load round keys
    __m128i k0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[0].data()));
    __m128i k1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[1].data()));
    __m128i k2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[2].data()));
    __m128i k3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[3].data()));
    __m128i k4 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[4].data()));
    __m128i k5 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[5].data()));
    __m128i k6 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[6].data()));
    __m128i k7 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[7].data()));
    __m128i k8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[8].data()));
    __m128i k9 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[9].data()));
    __m128i k10 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[10].data()));

    // AES encryption rounds
    block = _mm_xor_si128(block, k0);
    block = _mm_aesenc_si128(block, k1);
    block = _mm_aesenc_si128(block, k2);
    block = _mm_aesenc_si128(block, k3);
    block = _mm_aesenc_si128(block, k4);
    block = _mm_aesenc_si128(block, k5);
    block = _mm_aesenc_si128(block, k6);
    block = _mm_aesenc_si128(block, k7);
    block = _mm_aesenc_si128(block, k8);
    block = _mm_aesenc_si128(block, k9);
    block = _mm_aesenclast_si128(block, k10);

    Block result;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(result.data()), block);
    return result;
}

__attribute__((target("aes,sse2")))
Aes128::Block Aes128::decrypt_block_aesni(std::span<const std::uint8_t, 16> ciphertext) const noexcept {
    __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ciphertext.data()));

    // Load round keys
    __m128i k0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[0].data()));
    __m128i k1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[1].data()));
    __m128i k2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[2].data()));
    __m128i k3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[3].data()));
    __m128i k4 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[4].data()));
    __m128i k5 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[5].data()));
    __m128i k6 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[6].data()));
    __m128i k7 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[7].data()));
    __m128i k8 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[8].data()));
    __m128i k9 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[9].data()));
    __m128i k10 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys_[10].data()));

    // Compute decryption round keys using InvMixColumns
    // AES-NI decryption requires round keys in reverse order with InvMixColumns applied
    __m128i dk1 = _mm_aesimc_si128(k9);
    __m128i dk2 = _mm_aesimc_si128(k8);
    __m128i dk3 = _mm_aesimc_si128(k7);
    __m128i dk4 = _mm_aesimc_si128(k6);
    __m128i dk5 = _mm_aesimc_si128(k5);
    __m128i dk6 = _mm_aesimc_si128(k4);
    __m128i dk7 = _mm_aesimc_si128(k3);
    __m128i dk8 = _mm_aesimc_si128(k2);
    __m128i dk9 = _mm_aesimc_si128(k1);

    // AES decryption rounds
    block = _mm_xor_si128(block, k10);
    block = _mm_aesdec_si128(block, dk1);
    block = _mm_aesdec_si128(block, dk2);
    block = _mm_aesdec_si128(block, dk3);
    block = _mm_aesdec_si128(block, dk4);
    block = _mm_aesdec_si128(block, dk5);
    block = _mm_aesdec_si128(block, dk6);
    block = _mm_aesdec_si128(block, dk7);
    block = _mm_aesdec_si128(block, dk8);
    block = _mm_aesdec_si128(block, dk9);
    block = _mm_aesdeclast_si128(block, k0);

    Block result;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(result.data()), block);
    return result;
}

#endif  // DOTVM_X86_AES

// ============================================================================
// ARM Crypto Extension Implementation
// ============================================================================

#if defined(DOTVM_ARM_AES)

Aes128::Block Aes128::encrypt_block_arm_crypto(std::span<const std::uint8_t, 16> plaintext) const noexcept {
    uint8x16_t block = vld1q_u8(plaintext.data());

    // Load round keys
    uint8x16_t k0 = vld1q_u8(round_keys_[0].data());
    uint8x16_t k1 = vld1q_u8(round_keys_[1].data());
    uint8x16_t k2 = vld1q_u8(round_keys_[2].data());
    uint8x16_t k3 = vld1q_u8(round_keys_[3].data());
    uint8x16_t k4 = vld1q_u8(round_keys_[4].data());
    uint8x16_t k5 = vld1q_u8(round_keys_[5].data());
    uint8x16_t k6 = vld1q_u8(round_keys_[6].data());
    uint8x16_t k7 = vld1q_u8(round_keys_[7].data());
    uint8x16_t k8 = vld1q_u8(round_keys_[8].data());
    uint8x16_t k9 = vld1q_u8(round_keys_[9].data());
    uint8x16_t k10 = vld1q_u8(round_keys_[10].data());

    // ARM AES encryption: vaeseq performs SubBytes + ShiftRows, vaesmcq performs MixColumns
    // Round 0: AddRoundKey only
    block = veorq_u8(block, k0);

    // Rounds 1-9: SubBytes + ShiftRows, then AddRoundKey, then MixColumns
    // Note: ARM vaeseq_u8 performs SubBytes + ShiftRows + AddRoundKey combined
    // So we use: vaeseq_u8(block, key), then vaesmcq_u8(result)
    block = vaeseq_u8(block, k1);
    block = vaesmcq_u8(block);
    block = vaeseq_u8(block, k2);
    block = vaesmcq_u8(block);
    block = vaeseq_u8(block, k3);
    block = vaesmcq_u8(block);
    block = vaeseq_u8(block, k4);
    block = vaesmcq_u8(block);
    block = vaeseq_u8(block, k5);
    block = vaesmcq_u8(block);
    block = vaeseq_u8(block, k6);
    block = vaesmcq_u8(block);
    block = vaeseq_u8(block, k7);
    block = vaesmcq_u8(block);
    block = vaeseq_u8(block, k8);
    block = vaesmcq_u8(block);
    block = vaeseq_u8(block, k9);
    block = vaesmcq_u8(block);

    // Round 10: SubBytes + ShiftRows + AddRoundKey (no MixColumns)
    block = vaeseq_u8(block, vdupq_n_u8(0));  // SubBytes + ShiftRows with zero key
    block = veorq_u8(block, k10);             // Final AddRoundKey

    Block result;
    vst1q_u8(result.data(), block);
    return result;
}

Aes128::Block Aes128::decrypt_block_arm_crypto(std::span<const std::uint8_t, 16> ciphertext) const noexcept {
    uint8x16_t block = vld1q_u8(ciphertext.data());

    // Load round keys
    uint8x16_t k0 = vld1q_u8(round_keys_[0].data());
    uint8x16_t k1 = vld1q_u8(round_keys_[1].data());
    uint8x16_t k2 = vld1q_u8(round_keys_[2].data());
    uint8x16_t k3 = vld1q_u8(round_keys_[3].data());
    uint8x16_t k4 = vld1q_u8(round_keys_[4].data());
    uint8x16_t k5 = vld1q_u8(round_keys_[5].data());
    uint8x16_t k6 = vld1q_u8(round_keys_[6].data());
    uint8x16_t k7 = vld1q_u8(round_keys_[7].data());
    uint8x16_t k8 = vld1q_u8(round_keys_[8].data());
    uint8x16_t k9 = vld1q_u8(round_keys_[9].data());
    uint8x16_t k10 = vld1q_u8(round_keys_[10].data());

    // ARM AES decryption: vaesdq performs InvSubBytes + InvShiftRows, vaesimcq performs InvMixColumns
    // Round 0: AddRoundKey with last round key
    block = veorq_u8(block, k10);

    // Rounds 9-1: InvSubBytes + InvShiftRows, then AddRoundKey, then InvMixColumns
    block = vaesdq_u8(block, k9);
    block = vaesimcq_u8(block);
    block = vaesdq_u8(block, k8);
    block = vaesimcq_u8(block);
    block = vaesdq_u8(block, k7);
    block = vaesimcq_u8(block);
    block = vaesdq_u8(block, k6);
    block = vaesimcq_u8(block);
    block = vaesdq_u8(block, k5);
    block = vaesimcq_u8(block);
    block = vaesdq_u8(block, k4);
    block = vaesimcq_u8(block);
    block = vaesdq_u8(block, k3);
    block = vaesimcq_u8(block);
    block = vaesdq_u8(block, k2);
    block = vaesimcq_u8(block);
    block = vaesdq_u8(block, k1);
    block = vaesimcq_u8(block);

    // Final round: InvSubBytes + InvShiftRows + AddRoundKey (no InvMixColumns)
    block = vaesdq_u8(block, vdupq_n_u8(0));  // InvSubBytes + InvShiftRows with zero key
    block = veorq_u8(block, k0);              // Final AddRoundKey

    Block result;
    vst1q_u8(result.data(), block);
    return result;
}

#endif  // DOTVM_ARM_AES

// ============================================================================
// Runtime Dispatch
// ============================================================================

Aes128::Block Aes128::encrypt_block(std::span<const std::uint8_t, BLOCK_SIZE> plaintext) const noexcept {
    const auto& features = simd::detect_cpu_features();

#if defined(DOTVM_X86_AES)
    if (features.aesni) {
        return encrypt_block_aesni(plaintext);
    }
#endif

#if defined(DOTVM_ARM_AES)
    if (features.neon_aes) {
        return encrypt_block_arm_crypto(plaintext);
    }
#endif

    // Fallback to scalar implementation
    (void)features;  // Suppress unused warning on unsupported platforms
    return encrypt_block_scalar(plaintext);
}

Aes128::Block Aes128::decrypt_block(std::span<const std::uint8_t, BLOCK_SIZE> ciphertext) const noexcept {
    const auto& features = simd::detect_cpu_features();

#if defined(DOTVM_X86_AES)
    if (features.aesni) {
        return decrypt_block_aesni(ciphertext);
    }
#endif

#if defined(DOTVM_ARM_AES)
    if (features.neon_aes) {
        return decrypt_block_arm_crypto(ciphertext);
    }
#endif

    // Fallback to scalar implementation
    (void)features;  // Suppress unused warning on unsupported platforms
    return decrypt_block_scalar(ciphertext);
}

bool Aes128::has_hardware_acceleration() noexcept {
    return simd::detect_cpu_features().has_aes_acceleration();
}

}  // namespace dotvm::core::crypto
