#include "dotvm/core/crypto/sha256.hpp"
#include "dotvm/core/simd/cpu_features.hpp"

#include <algorithm>
#include <cstring>

// Platform-specific intrinsics for hardware acceleration
// Only enable when SIMD is enabled at build time (DOTVM_SIMD_ENABLED)
#if defined(DOTVM_SIMD_ENABLED)
    #if defined(__x86_64__) || defined(_M_X64)
        #include <immintrin.h>
        #define DOTVM_X86_SHA256 1
    #endif

    #if defined(__aarch64__) || defined(_M_ARM64)
        #if defined(__ARM_FEATURE_CRYPTO) || defined(_MSC_VER)
            #include <arm_neon.h>
            #define DOTVM_ARM_SHA256 1
        #endif
    #endif
#endif

namespace dotvm::core::crypto {

// ============================================================================
// SHA-256 Constants (FIPS 180-4)
// ============================================================================

namespace {

/// SHA-256 initial hash values (first 32 bits of fractional parts of
/// square roots of first 8 primes: 2, 3, 5, 7, 11, 13, 17, 19)
constexpr std::array<std::uint32_t, 8> INITIAL_HASH = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/// SHA-256 round constants (first 32 bits of fractional parts of
/// cube roots of first 64 primes)
constexpr std::array<std::uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

// Helper functions for SHA-256 operations
constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32 - n));
}

constexpr std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

constexpr std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

constexpr std::uint32_t sigma0(std::uint32_t x) noexcept {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

constexpr std::uint32_t sigma1(std::uint32_t x) noexcept {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

constexpr std::uint32_t gamma0(std::uint32_t x) noexcept {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

constexpr std::uint32_t gamma1(std::uint32_t x) noexcept {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

/// Load 32-bit big-endian value
inline std::uint32_t load_be32(const std::uint8_t* ptr) noexcept {
    return (static_cast<std::uint32_t>(ptr[0]) << 24) |
           (static_cast<std::uint32_t>(ptr[1]) << 16) |
           (static_cast<std::uint32_t>(ptr[2]) << 8) |
           static_cast<std::uint32_t>(ptr[3]);
}

/// Store 32-bit big-endian value
inline void store_be32(std::uint8_t* ptr, std::uint32_t val) noexcept {
    ptr[0] = static_cast<std::uint8_t>(val >> 24);
    ptr[1] = static_cast<std::uint8_t>(val >> 16);
    ptr[2] = static_cast<std::uint8_t>(val >> 8);
    ptr[3] = static_cast<std::uint8_t>(val);
}

/// Store 64-bit big-endian value
inline void store_be64(std::uint8_t* ptr, std::uint64_t val) noexcept {
    ptr[0] = static_cast<std::uint8_t>(val >> 56);
    ptr[1] = static_cast<std::uint8_t>(val >> 48);
    ptr[2] = static_cast<std::uint8_t>(val >> 40);
    ptr[3] = static_cast<std::uint8_t>(val >> 32);
    ptr[4] = static_cast<std::uint8_t>(val >> 24);
    ptr[5] = static_cast<std::uint8_t>(val >> 16);
    ptr[6] = static_cast<std::uint8_t>(val >> 8);
    ptr[7] = static_cast<std::uint8_t>(val);
}

}  // namespace

// ============================================================================
// Sha256 Implementation
// ============================================================================

Sha256::Sha256() noexcept
    : state_(INITIAL_HASH)
    , buffer_{}
    , buffer_len_(0)
    , total_len_(0) {
}

void Sha256::process_block_scalar(std::span<const std::uint8_t, 64> block) noexcept {
    // Message schedule array
    std::array<std::uint32_t, 64> W{};

    // Load block into first 16 words (big-endian)
    for (std::size_t i = 0; i < 16; ++i) {
        W[i] = load_be32(block.data() + i * 4);
    }

    // Extend message schedule
    for (std::size_t i = 16; i < 64; ++i) {
        W[i] = gamma1(W[i - 2]) + W[i - 7] + gamma0(W[i - 15]) + W[i - 16];
    }

    // Initialize working variables
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    // Main compression loop (64 rounds)
    for (std::size_t i = 0; i < 64; ++i) {
        std::uint32_t t1 = h + sigma1(e) + ch(e, f, g) + K[i] + W[i];
        std::uint32_t t2 = sigma0(a) + maj(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    // Update state
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

// ============================================================================
// x86-64 SHA-NI Implementation
// ============================================================================

#if defined(DOTVM_X86_SHA256)

// SHA-NI implementation using Intel SHA extensions
// This provides significant speedup on CPUs with SHA-NI support
__attribute__((target("sha,sse4.1")))
void Sha256::process_block_sha_ni(std::span<const std::uint8_t, 64> block) noexcept {
    // Load current state into SSE registers
    // State is: [H0, H1, H2, H3, H4, H5, H6, H7]
    // ABEF = [A, B, E, F] = [H0, H1, H4, H5]
    // CDGH = [C, D, G, H] = [H2, H3, H6, H7]

    __m128i STATE0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[0]));
    __m128i STATE1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[4]));

    // Shuffle mask for byte swapping (big-endian to little-endian)
    const __m128i SHUF_MASK = _mm_set_epi8(
        12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3
    );

    // Transform state from [H0, H1, H2, H3] [H4, H5, H6, H7] to
    // ABEF: [H0, H1, H4, H5] and CDGH: [H2, H3, H6, H7]
    __m128i TMP = _mm_shuffle_epi32(STATE0, 0xB1);           // [H1, H0, H3, H2]
    STATE1 = _mm_shuffle_epi32(STATE1, 0x1B);                // [H7, H6, H5, H4]
    STATE0 = _mm_alignr_epi8(TMP, STATE1, 8);                // [H3, H2, H1, H0] -> ABEF style
    STATE1 = _mm_blend_epi16(STATE1, TMP, 0xF0);             // CDGH style

    // Save initial state for final addition
    __m128i ABEF_SAVE = STATE0;
    __m128i CDGH_SAVE = STATE1;

    // Load message and convert from big-endian
    __m128i MSG0 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(block.data() + 0)), SHUF_MASK);
    __m128i MSG1 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(block.data() + 16)), SHUF_MASK);
    __m128i MSG2 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(block.data() + 32)), SHUF_MASK);
    __m128i MSG3 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(block.data() + 48)), SHUF_MASK);

    __m128i MSG;

    // Rounds 0-3
    MSG = _mm_add_epi32(MSG0, _mm_set_epi64x(
        static_cast<std::int64_t>(0xE9B5DBA5B5C0FBCFull),
        static_cast<std::int64_t>(0x71374491428A2F98ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

    // Rounds 4-7
    MSG = _mm_add_epi32(MSG1, _mm_set_epi64x(
        static_cast<std::int64_t>(0xAB1C5ED5923F82A4ull),
        static_cast<std::int64_t>(0x59F111F13956C25Bull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

    // Rounds 8-11
    MSG = _mm_add_epi32(MSG2, _mm_set_epi64x(
        static_cast<std::int64_t>(0x550C7DC3243185BEull),
        static_cast<std::int64_t>(0x12835B01D807AA98ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

    // Rounds 12-15
    MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(
        static_cast<std::int64_t>(0xC19BF174C19BF174ull),  // Note: repeated value, fix below
        static_cast<std::int64_t>(0x80DEB1FE72BE5D74ull)));
    // Correct K values for rounds 12-15: 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
    MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(
        static_cast<std::int64_t>(0xC19BF1749BDC06A7ull),
        static_cast<std::int64_t>(0x80DEB1FE72BE5D74ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG3, MSG2, 4);
    MSG0 = _mm_add_epi32(MSG0, TMP);
    MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

    // Rounds 16-19
    MSG = _mm_add_epi32(MSG0, _mm_set_epi64x(
        static_cast<std::int64_t>(0x240CA1CC0FC19DC6ull),
        static_cast<std::int64_t>(0xEFBE4786E49B69C1ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG0, MSG3, 4);
    MSG1 = _mm_add_epi32(MSG1, TMP);
    MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

    // Rounds 20-23
    MSG = _mm_add_epi32(MSG1, _mm_set_epi64x(
        static_cast<std::int64_t>(0x76F988DA5CB0A9DCull),
        static_cast<std::int64_t>(0x4A7484AA2DE92C6Full)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG1, MSG0, 4);
    MSG2 = _mm_add_epi32(MSG2, TMP);
    MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

    // Rounds 24-27
    MSG = _mm_add_epi32(MSG2, _mm_set_epi64x(
        static_cast<std::int64_t>(0xBF597FC7B00327C8ull),
        static_cast<std::int64_t>(0xA831C66D983E5152ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG2, MSG1, 4);
    MSG3 = _mm_add_epi32(MSG3, TMP);
    MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

    // Rounds 28-31
    MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(
        static_cast<std::int64_t>(0x14292967142929ull),  // Placeholder
        static_cast<std::int64_t>(0xD5A79147C6E00BF3ull)));
    MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(
        static_cast<std::int64_t>(0x1429296706CA6351ull),
        static_cast<std::int64_t>(0xD5A79147C6E00BF3ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG3, MSG2, 4);
    MSG0 = _mm_add_epi32(MSG0, TMP);
    MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

    // Rounds 32-35
    MSG = _mm_add_epi32(MSG0, _mm_set_epi64x(
        static_cast<std::int64_t>(0x53380D134D2C6DFCull),
        static_cast<std::int64_t>(0x2E1B213827B70A85ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG0, MSG3, 4);
    MSG1 = _mm_add_epi32(MSG1, TMP);
    MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

    // Rounds 36-39
    MSG = _mm_add_epi32(MSG1, _mm_set_epi64x(
        static_cast<std::int64_t>(0x92722C8581C2C92Eull),
        static_cast<std::int64_t>(0x766A0ABB650A7354ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG1, MSG0, 4);
    MSG2 = _mm_add_epi32(MSG2, TMP);
    MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

    // Rounds 40-43
    MSG = _mm_add_epi32(MSG2, _mm_set_epi64x(
        static_cast<std::int64_t>(0xC76C51A3C24B8B70ull),
        static_cast<std::int64_t>(0xA81A664BA2BFE8A1ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG2, MSG1, 4);
    MSG3 = _mm_add_epi32(MSG3, TMP);
    MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

    // Rounds 44-47
    MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(
        static_cast<std::int64_t>(0x106AA070F40E3585ull),
        static_cast<std::int64_t>(0xD6990624D192E819ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG3, MSG2, 4);
    MSG0 = _mm_add_epi32(MSG0, TMP);
    MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

    // Rounds 48-51
    MSG = _mm_add_epi32(MSG0, _mm_set_epi64x(
        static_cast<std::int64_t>(0x34B0BCB52748774Cull),
        static_cast<std::int64_t>(0x1E376C0819A4C116ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG0, MSG3, 4);
    MSG1 = _mm_add_epi32(MSG1, TMP);
    MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
    MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

    // Rounds 52-55
    MSG = _mm_add_epi32(MSG1, _mm_set_epi64x(
        static_cast<std::int64_t>(0x682E6FF35B9CCA4Full),
        static_cast<std::int64_t>(0x4ED8AA4A391C0CB3ull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG1, MSG0, 4);
    MSG2 = _mm_add_epi32(MSG2, TMP);
    MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

    // Rounds 56-59
    MSG = _mm_add_epi32(MSG2, _mm_set_epi64x(
        static_cast<std::int64_t>(0x8CC7020884C87814ull),
        static_cast<std::int64_t>(0x78A5636F748F82EEull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    TMP = _mm_alignr_epi8(MSG2, MSG1, 4);
    MSG3 = _mm_add_epi32(MSG3, TMP);
    MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

    // Rounds 60-63
    MSG = _mm_add_epi32(MSG3, _mm_set_epi64x(
        static_cast<std::int64_t>(0xC67178F2BEF9A3F7ull),
        static_cast<std::int64_t>(0xA4506CEBB90BEFFAull)));
    STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
    MSG = _mm_shuffle_epi32(MSG, 0x0E);
    STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

    // Add saved state
    STATE0 = _mm_add_epi32(STATE0, ABEF_SAVE);
    STATE1 = _mm_add_epi32(STATE1, CDGH_SAVE);

    // Transform back to standard state layout and store
    TMP = _mm_shuffle_epi32(STATE0, 0x1B);
    STATE1 = _mm_shuffle_epi32(STATE1, 0xB1);
    STATE0 = _mm_blend_epi16(TMP, STATE1, 0xF0);
    STATE1 = _mm_alignr_epi8(STATE1, TMP, 8);

    _mm_storeu_si128(reinterpret_cast<__m128i*>(&state_[0]), STATE0);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(&state_[4]), STATE1);
}

#endif  // DOTVM_X86_SHA256

// ============================================================================
// ARM Crypto Extension Implementation
// ============================================================================

#if defined(DOTVM_ARM_SHA256)

void Sha256::process_block_arm_crypto(std::span<const std::uint8_t, 64> block) noexcept {
    // Load current state
    uint32x4_t STATE0 = vld1q_u32(&state_[0]);
    uint32x4_t STATE1 = vld1q_u32(&state_[4]);

    // Save for final addition
    uint32x4_t ABCD_SAVE = STATE0;
    uint32x4_t EFGH_SAVE = STATE1;

    // Load message (ARM is little-endian, need to byte-swap for SHA-256)
    uint32x4_t MSG0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block.data() + 0)));
    uint32x4_t MSG1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block.data() + 16)));
    uint32x4_t MSG2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block.data() + 32)));
    uint32x4_t MSG3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block.data() + 48)));

    uint32x4_t TMP0, TMP1, TMP2;

    // Rounds 0-3
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[0]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG0 = vsha256su0q_u32(MSG0, MSG1);

    // Rounds 4-7
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[4]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG0 = vsha256su1q_u32(MSG0, MSG2, MSG3);
    MSG1 = vsha256su0q_u32(MSG1, MSG2);

    // Rounds 8-11
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[8]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG1 = vsha256su1q_u32(MSG1, MSG3, MSG0);
    MSG2 = vsha256su0q_u32(MSG2, MSG3);

    // Rounds 12-15
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[12]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG2 = vsha256su1q_u32(MSG2, MSG0, MSG1);
    MSG3 = vsha256su0q_u32(MSG3, MSG0);

    // Rounds 16-19
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[16]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG3 = vsha256su1q_u32(MSG3, MSG1, MSG2);
    MSG0 = vsha256su0q_u32(MSG0, MSG1);

    // Rounds 20-23
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[20]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG0 = vsha256su1q_u32(MSG0, MSG2, MSG3);
    MSG1 = vsha256su0q_u32(MSG1, MSG2);

    // Rounds 24-27
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[24]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG1 = vsha256su1q_u32(MSG1, MSG3, MSG0);
    MSG2 = vsha256su0q_u32(MSG2, MSG3);

    // Rounds 28-31
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[28]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG2 = vsha256su1q_u32(MSG2, MSG0, MSG1);
    MSG3 = vsha256su0q_u32(MSG3, MSG0);

    // Rounds 32-35
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[32]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG3 = vsha256su1q_u32(MSG3, MSG1, MSG2);
    MSG0 = vsha256su0q_u32(MSG0, MSG1);

    // Rounds 36-39
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[36]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG0 = vsha256su1q_u32(MSG0, MSG2, MSG3);
    MSG1 = vsha256su0q_u32(MSG1, MSG2);

    // Rounds 40-43
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[40]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG1 = vsha256su1q_u32(MSG1, MSG3, MSG0);
    MSG2 = vsha256su0q_u32(MSG2, MSG3);

    // Rounds 44-47
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[44]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG2 = vsha256su1q_u32(MSG2, MSG0, MSG1);
    MSG3 = vsha256su0q_u32(MSG3, MSG0);

    // Rounds 48-51
    TMP0 = vaddq_u32(MSG0, vld1q_u32(&K[48]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);
    MSG3 = vsha256su1q_u32(MSG3, MSG1, MSG2);

    // Rounds 52-55
    TMP0 = vaddq_u32(MSG1, vld1q_u32(&K[52]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);

    // Rounds 56-59
    TMP0 = vaddq_u32(MSG2, vld1q_u32(&K[56]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);

    // Rounds 60-63
    TMP0 = vaddq_u32(MSG3, vld1q_u32(&K[60]));
    TMP2 = STATE0;
    STATE0 = vsha256hq_u32(STATE0, STATE1, TMP0);
    STATE1 = vsha256h2q_u32(STATE1, TMP2, TMP0);

    // Add saved state
    STATE0 = vaddq_u32(STATE0, ABCD_SAVE);
    STATE1 = vaddq_u32(STATE1, EFGH_SAVE);

    // Store result
    vst1q_u32(&state_[0], STATE0);
    vst1q_u32(&state_[4], STATE1);
}

#endif  // DOTVM_ARM_SHA256

// ============================================================================
// Runtime Dispatch
// ============================================================================

void Sha256::process_block(std::span<const std::uint8_t, BLOCK_SIZE> block) noexcept {
    const auto& features = simd::detect_cpu_features();

#if defined(DOTVM_X86_SHA256)
    if (features.sha) {
        process_block_sha_ni(block);
        return;
    }
#endif

#if defined(DOTVM_ARM_SHA256)
    if (features.neon_sha2) {
        process_block_arm_crypto(block);
        return;
    }
#endif

    // Fallback to scalar implementation
    (void)features;  // Suppress unused warning on unsupported platforms
    process_block_scalar(block);
}

void Sha256::update(std::span<const std::uint8_t> data) noexcept {
    const auto* ptr = data.data();
    auto remaining = data.size();

    total_len_ += remaining;

    // If we have buffered data, try to complete a block
    if (buffer_len_ > 0) {
        const auto to_copy = std::min(BLOCK_SIZE - buffer_len_, remaining);
        std::memcpy(buffer_.data() + buffer_len_, ptr, to_copy);
        buffer_len_ += to_copy;
        ptr += to_copy;
        remaining -= to_copy;

        if (buffer_len_ == BLOCK_SIZE) {
            process_block(std::span<const std::uint8_t, BLOCK_SIZE>{buffer_});
            buffer_len_ = 0;
        }
    }

    // Process complete blocks directly from input
    while (remaining >= BLOCK_SIZE) {
        process_block(std::span<const std::uint8_t, BLOCK_SIZE>{ptr, BLOCK_SIZE});
        ptr += BLOCK_SIZE;
        remaining -= BLOCK_SIZE;
    }

    // Buffer remaining data
    if (remaining > 0) {
        std::memcpy(buffer_.data(), ptr, remaining);
        buffer_len_ = remaining;
    }
}

void Sha256::update(std::string_view str) noexcept {
    update(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(str.data()),
        str.size()
    });
}

Sha256::Digest Sha256::finalize() noexcept {
    // Pad message
    // Append 0x80 byte
    buffer_[buffer_len_++] = 0x80;

    // If not enough room for length (8 bytes), pad to block boundary and process
    if (buffer_len_ > 56) {
        std::memset(buffer_.data() + buffer_len_, 0, BLOCK_SIZE - buffer_len_);
        process_block(std::span<const std::uint8_t, BLOCK_SIZE>{buffer_});
        buffer_len_ = 0;
    }

    // Pad remaining space with zeros
    std::memset(buffer_.data() + buffer_len_, 0, 56 - buffer_len_);

    // Append message length in bits (big-endian)
    store_be64(buffer_.data() + 56, total_len_ * 8);

    // Process final block
    process_block(std::span<const std::uint8_t, BLOCK_SIZE>{buffer_});

    // Convert state to digest (big-endian)
    Digest digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        store_be32(digest.data() + i * 4, state_[i]);
    }

    // Reset state for potential reuse
    state_ = INITIAL_HASH;
    buffer_len_ = 0;
    total_len_ = 0;

    return digest;
}

Sha256::Digest Sha256::hash(std::span<const std::uint8_t> data) noexcept {
    Sha256 hasher;
    hasher.update(data);
    return hasher.finalize();
}

Sha256::Digest Sha256::hash(std::string_view str) noexcept {
    Sha256 hasher;
    hasher.update(str);
    return hasher.finalize();
}

bool Sha256::has_hardware_acceleration() noexcept {
    return simd::detect_cpu_features().has_sha_acceleration();
}

}  // namespace dotvm::core::crypto
