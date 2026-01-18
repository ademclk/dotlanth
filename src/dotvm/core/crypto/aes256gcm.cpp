#include "dotvm/core/crypto/aes256gcm.hpp"

#include <algorithm>
#include <cstring>
#include <random>

#include "dotvm/core/simd/cpu_features.hpp"

#if defined(DOTVM_SIMD_ENABLED)
    #if defined(__x86_64__) || defined(_M_X64)
        #include <immintrin.h>
        #include <wmmintrin.h>  // AES-NI
        #define DOTVM_X86_AES256GCM 1
    #endif
#endif

namespace dotvm::core::crypto {

// ============================================================================
// AES Constants
// ============================================================================

namespace {

// AES S-box
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
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

// AES round constants
constexpr std::array<std::uint8_t, 10> RCON = {0x01, 0x02, 0x04, 0x08, 0x10,
                                               0x20, 0x40, 0x80, 0x1b, 0x36};

// Galois field multiplication by 2 (kept for potential optimization)
[[maybe_unused]] constexpr std::uint8_t xtime(std::uint8_t x) noexcept {
    return static_cast<std::uint8_t>((x << 1) ^ ((x >> 7) * 0x1b));
}

// Galois field multiplication
constexpr std::uint8_t gf_mul(std::uint8_t a, std::uint8_t b) noexcept {
    std::uint8_t result = 0;
    std::uint8_t temp = a;
    for (int i = 0; i < 8; ++i) {
        if (b & 1)
            result ^= temp;
        bool hi_bit = (temp & 0x80) != 0;
        temp <<= 1;
        if (hi_bit)
            temp ^= 0x1b;
        b >>= 1;
    }
    return result;
}

}  // namespace

// ============================================================================
// AES-256 Key Expansion
// ============================================================================

void Aes256Gcm::key_expansion(RoundKeys& round_keys, const Key& key) noexcept {
    // First 2 round keys are directly from the key
    std::memcpy(round_keys[0].data(), key.data(), 16);
    std::memcpy(round_keys[1].data(), key.data() + 16, 16);

    std::array<std::uint8_t, 4> temp;

    for (std::size_t i = 2; i <= NUM_ROUNDS; ++i) {
        // Get last 4 bytes of previous round key
        std::memcpy(temp.data(), round_keys[i - 1].data() + 12, 4);

        if (i % 2 == 0) {
            // RotWord + SubWord + Rcon
            std::uint8_t t = temp[0];
            temp[0] = SBOX[temp[1]] ^ RCON[i / 2 - 1];
            temp[1] = SBOX[temp[2]];
            temp[2] = SBOX[temp[3]];
            temp[3] = SBOX[t];
        } else {
            // SubWord only
            for (auto& b : temp) {
                b = SBOX[b];
            }
        }

        // XOR with round key from 2 rounds ago
        for (std::size_t j = 0; j < 16; ++j) {
            if (j < 4) {
                round_keys[i][j] = round_keys[i - 2][j] ^ temp[j];
            } else {
                round_keys[i][j] = round_keys[i - 2][j] ^ round_keys[i][j - 4];
            }
        }
    }
}

// ============================================================================
// AES Round Transformations
// ============================================================================

void Aes256Gcm::sub_bytes(Block& state) noexcept {
    for (auto& b : state) {
        b = SBOX[b];
    }
}

void Aes256Gcm::shift_rows(Block& state) noexcept {
    // Row 1: shift left by 1
    std::uint8_t temp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temp;

    // Row 2: shift left by 2
    std::swap(state[2], state[10]);
    std::swap(state[6], state[14]);

    // Row 3: shift left by 3 (= right by 1)
    temp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temp;
}

void Aes256Gcm::mix_columns(Block& state) noexcept {
    for (std::size_t i = 0; i < 4; ++i) {
        std::size_t col = i * 4;
        std::uint8_t a = state[col];
        std::uint8_t b = state[col + 1];
        std::uint8_t c = state[col + 2];
        std::uint8_t d = state[col + 3];

        state[col] = gf_mul(a, 2) ^ gf_mul(b, 3) ^ c ^ d;
        state[col + 1] = a ^ gf_mul(b, 2) ^ gf_mul(c, 3) ^ d;
        state[col + 2] = a ^ b ^ gf_mul(c, 2) ^ gf_mul(d, 3);
        state[col + 3] = gf_mul(a, 3) ^ b ^ c ^ gf_mul(d, 2);
    }
}

void Aes256Gcm::add_round_key(Block& state, const Block& round_key) noexcept {
    for (std::size_t i = 0; i < BLOCK_SIZE; ++i) {
        state[i] ^= round_key[i];
    }
}

Aes256Gcm::Block Aes256Gcm::encrypt_block(const Block& plaintext,
                                          const RoundKeys& round_keys) noexcept {
    Block state = plaintext;

    // Initial round key addition
    add_round_key(state, round_keys[0]);

    // Main rounds (1 to NUM_ROUNDS - 1)
    for (std::size_t round = 1; round < NUM_ROUNDS; ++round) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, round_keys[round]);
    }

    // Final round (no MixColumns)
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, round_keys[NUM_ROUNDS]);

    return state;
}

// ============================================================================
// GCM Operations
// ============================================================================

void Aes256Gcm::increment_counter(Block& counter) noexcept {
    // Increment last 4 bytes as big-endian counter
    for (std::size_t i = 15; i >= 12; --i) {
        if (++counter[i] != 0)
            break;
        if (i == 12)
            break;  // Prevent underflow when decrementing size_t
    }
}

Aes256Gcm::Block Aes256Gcm::ghash_multiply(const Block& X, const Block& H) noexcept {
    // Multiplication in GF(2^128) with polynomial x^128 + x^7 + x^2 + x + 1
    Block Z{};
    Block V = H;

    for (std::size_t i = 0; i < 16; ++i) {
        for (std::size_t j = 8; j > 0; --j) {
            std::size_t bit_idx = j - 1;
            if ((X[i] >> bit_idx) & 1) {
                for (std::size_t k = 0; k < 16; ++k) {
                    Z[k] ^= V[k];
                }
            }

            // Multiply V by x (shift right and reduce)
            bool lsb = (V[15] & 1) != 0;
            for (std::size_t k = 15; k > 0; --k) {
                V[k] = static_cast<std::uint8_t>((V[k] >> 1) | ((V[k - 1] & 1) << 7));
            }
            V[0] >>= 1;

            if (lsb) {
                V[0] ^= 0xe1;  // Reduction polynomial: x^128 + x^7 + x^2 + x + 1
            }
        }
    }

    return Z;
}

Aes256Gcm::Block Aes256Gcm::ghash(const Block& H, std::span<const std::uint8_t> aad,
                                  std::span<const std::uint8_t> ciphertext) noexcept {
    Block Y{};
    Block X{};

    // Process AAD
    std::size_t aad_blocks = (aad.size() + 15) / 16;
    for (std::size_t i = 0; i < aad_blocks; ++i) {
        X.fill(0);
        std::size_t block_size = std::min(std::size_t{16}, aad.size() - i * 16);
        std::memcpy(X.data(), aad.data() + i * 16, block_size);

        for (std::size_t j = 0; j < 16; ++j) {
            Y[j] ^= X[j];
        }
        Y = ghash_multiply(Y, H);
    }

    // Process ciphertext
    std::size_t ct_blocks = (ciphertext.size() + 15) / 16;
    for (std::size_t i = 0; i < ct_blocks; ++i) {
        X.fill(0);
        std::size_t block_size = std::min(std::size_t{16}, ciphertext.size() - i * 16);
        std::memcpy(X.data(), ciphertext.data() + i * 16, block_size);

        for (std::size_t j = 0; j < 16; ++j) {
            Y[j] ^= X[j];
        }
        Y = ghash_multiply(Y, H);
    }

    // Append lengths (in bits, big-endian)
    Block len_block{};
    std::uint64_t aad_bits = aad.size() * 8;
    std::uint64_t ct_bits = ciphertext.size() * 8;

    len_block[0] = static_cast<std::uint8_t>(aad_bits >> 56);
    len_block[1] = static_cast<std::uint8_t>(aad_bits >> 48);
    len_block[2] = static_cast<std::uint8_t>(aad_bits >> 40);
    len_block[3] = static_cast<std::uint8_t>(aad_bits >> 32);
    len_block[4] = static_cast<std::uint8_t>(aad_bits >> 24);
    len_block[5] = static_cast<std::uint8_t>(aad_bits >> 16);
    len_block[6] = static_cast<std::uint8_t>(aad_bits >> 8);
    len_block[7] = static_cast<std::uint8_t>(aad_bits);
    len_block[8] = static_cast<std::uint8_t>(ct_bits >> 56);
    len_block[9] = static_cast<std::uint8_t>(ct_bits >> 48);
    len_block[10] = static_cast<std::uint8_t>(ct_bits >> 40);
    len_block[11] = static_cast<std::uint8_t>(ct_bits >> 32);
    len_block[12] = static_cast<std::uint8_t>(ct_bits >> 24);
    len_block[13] = static_cast<std::uint8_t>(ct_bits >> 16);
    len_block[14] = static_cast<std::uint8_t>(ct_bits >> 8);
    len_block[15] = static_cast<std::uint8_t>(ct_bits);

    for (std::size_t j = 0; j < 16; ++j) {
        Y[j] ^= len_block[j];
    }
    Y = ghash_multiply(Y, H);

    return Y;
}

Aes256Gcm::Nonce Aes256Gcm::generate_nonce() noexcept {
    Nonce nonce{};
    std::random_device rd;
    std::uniform_int_distribution<unsigned> dist(0, 255);
    for (auto& b : nonce) {
        b = static_cast<std::uint8_t>(dist(rd));
    }
    return nonce;
}

// ============================================================================
// Public API
// ============================================================================

std::vector<std::uint8_t> Aes256Gcm::encrypt(std::span<const std::uint8_t> plaintext,
                                             const Key& key,
                                             std::span<const std::uint8_t> aad) noexcept {
    Nonce nonce = generate_nonce();
    auto ciphertext_tag = encrypt_with_nonce(plaintext, key, nonce, aad);

    // Prepend nonce to result
    std::vector<std::uint8_t> result;
    result.reserve(NONCE_SIZE + ciphertext_tag.size());
    result.insert(result.end(), nonce.begin(), nonce.end());
    result.insert(result.end(), ciphertext_tag.begin(), ciphertext_tag.end());

    return result;
}

std::vector<std::uint8_t> Aes256Gcm::decrypt(std::span<const std::uint8_t> ciphertext,
                                             const Key& key,
                                             std::span<const std::uint8_t> aad) noexcept {
    // Minimum size: nonce(12) + tag(16)
    if (ciphertext.size() < NONCE_SIZE + TAG_SIZE) {
        return {};
    }

    // Extract nonce
    Nonce nonce;
    std::memcpy(nonce.data(), ciphertext.data(), NONCE_SIZE);

    // Decrypt with extracted nonce
    return decrypt_with_nonce(ciphertext.subspan(NONCE_SIZE), key, nonce, aad);
}

std::vector<std::uint8_t>
Aes256Gcm::encrypt_with_nonce(std::span<const std::uint8_t> plaintext, const Key& key,
                              const Nonce& nonce, std::span<const std::uint8_t> aad) noexcept {
    // Expand key
    RoundKeys round_keys;
    key_expansion(round_keys, key);

    // Compute H = AES_K(0^128)
    Block zero_block{};
    Block H = encrypt_block(zero_block, round_keys);

    // Initialize counter: nonce || 0^31 || 1
    Block counter{};
    std::memcpy(counter.data(), nonce.data(), NONCE_SIZE);
    counter[15] = 1;

    // Encrypt counter for final tag XOR (J0)
    Block E_J0 = encrypt_block(counter, round_keys);

    // Encrypt plaintext in CTR mode
    std::vector<std::uint8_t> ciphertext;
    ciphertext.reserve(plaintext.size() + TAG_SIZE);

    std::size_t num_blocks = (plaintext.size() + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (std::size_t i = 0; i < num_blocks; ++i) {
        increment_counter(counter);
        Block encrypted_counter = encrypt_block(counter, round_keys);

        std::size_t block_start = i * BLOCK_SIZE;
        std::size_t block_size = std::min(BLOCK_SIZE, plaintext.size() - block_start);

        for (std::size_t j = 0; j < block_size; ++j) {
            ciphertext.push_back(plaintext[block_start + j] ^ encrypted_counter[j]);
        }
    }

    // Compute authentication tag
    Block S = ghash(H, aad, std::span<const std::uint8_t>{ciphertext.data(), ciphertext.size()});

    // Tag = GHASH XOR E(J0)
    for (std::size_t i = 0; i < TAG_SIZE; ++i) {
        ciphertext.push_back(S[i] ^ E_J0[i]);
    }

    return ciphertext;
}

std::vector<std::uint8_t>
Aes256Gcm::decrypt_with_nonce(std::span<const std::uint8_t> ciphertext, const Key& key,
                              const Nonce& nonce, std::span<const std::uint8_t> aad) noexcept {
    // Minimum size: tag(16)
    if (ciphertext.size() < TAG_SIZE) {
        return {};
    }

    // Expand key
    RoundKeys round_keys;
    key_expansion(round_keys, key);

    // Compute H = AES_K(0^128)
    Block zero_block{};
    Block H = encrypt_block(zero_block, round_keys);

    // Initialize counter: nonce || 0^31 || 1
    Block counter{};
    std::memcpy(counter.data(), nonce.data(), NONCE_SIZE);
    counter[15] = 1;

    // Encrypt counter for final tag XOR (J0)
    Block E_J0 = encrypt_block(counter, round_keys);

    // Extract provided tag
    Tag provided_tag;
    std::size_t ct_len = ciphertext.size() - TAG_SIZE;
    std::memcpy(provided_tag.data(), ciphertext.data() + ct_len, TAG_SIZE);

    // Compute expected authentication tag
    Block S = ghash(H, aad, ciphertext.subspan(0, ct_len));

    Tag expected_tag;
    for (std::size_t i = 0; i < TAG_SIZE; ++i) {
        expected_tag[i] = S[i] ^ E_J0[i];
    }

    // Constant-time tag comparison
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < TAG_SIZE; ++i) {
        diff |= static_cast<std::uint8_t>(provided_tag[i] ^ expected_tag[i]);
    }

    if (diff != 0) {
        return {};  // Authentication failed
    }

    // Decrypt ciphertext in CTR mode
    std::vector<std::uint8_t> plaintext;
    plaintext.reserve(ct_len);

    std::size_t num_blocks = (ct_len + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (std::size_t i = 0; i < num_blocks; ++i) {
        increment_counter(counter);
        Block encrypted_counter = encrypt_block(counter, round_keys);

        std::size_t block_start = i * BLOCK_SIZE;
        std::size_t block_size = std::min(BLOCK_SIZE, ct_len - block_start);

        for (std::size_t j = 0; j < block_size; ++j) {
            plaintext.push_back(ciphertext[block_start + j] ^ encrypted_counter[j]);
        }
    }

    return plaintext;
}

bool Aes256Gcm::has_hardware_acceleration() noexcept {
    const auto& features = simd::detect_cpu_features();
    return features.aesni && features.pclmul;
}

// ============================================================================
// Hardware Accelerated Implementations (x86-64)
// ============================================================================

#if defined(DOTVM_X86_AES256GCM)

__attribute__((target("aes"))) Aes256Gcm::Block
Aes256Gcm::encrypt_block_aesni(const Block& plaintext, const RoundKeys& round_keys) noexcept {
    __m128i state = _mm_loadu_si128(reinterpret_cast<const __m128i*>(plaintext.data()));

    // Initial round key addition
    state = _mm_xor_si128(state,
                          _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys[0].data())));

    // Main rounds (1 to NUM_ROUNDS - 1)
    for (std::size_t round = 1; round < NUM_ROUNDS; ++round) {
        state = _mm_aesenc_si128(
            state, _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys[round].data())));
    }

    // Final round
    state = _mm_aesenclast_si128(
        state, _mm_loadu_si128(reinterpret_cast<const __m128i*>(round_keys[NUM_ROUNDS].data())));

    Block result;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(result.data()), state);
    return result;
}

__attribute__((target("pclmul,sse4.1"))) Aes256Gcm::Block
Aes256Gcm::ghash_multiply_pclmul(const Block& X, const Block& H) noexcept {
    // Load operands (reverse byte order for GCM)
    __m128i a =
        _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(X.data())),
                         _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
    __m128i b =
        _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(H.data())),
                         _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

    // Carry-less multiplication
    __m128i tmp3 = _mm_clmulepi64_si128(a, b, 0x00);  // a_lo * b_lo
    __m128i tmp6 = _mm_clmulepi64_si128(a, b, 0x11);  // a_hi * b_hi
    __m128i tmp4 = _mm_clmulepi64_si128(a, b, 0x10);  // a_hi * b_lo
    __m128i tmp5 = _mm_clmulepi64_si128(a, b, 0x01);  // a_lo * b_hi

    tmp4 = _mm_xor_si128(tmp4, tmp5);
    tmp5 = _mm_slli_si128(tmp4, 8);
    tmp4 = _mm_srli_si128(tmp4, 8);
    tmp3 = _mm_xor_si128(tmp3, tmp5);
    tmp6 = _mm_xor_si128(tmp6, tmp4);

    // Reduction
    __m128i tmp7 = _mm_srli_epi32(tmp3, 31);
    __m128i tmp8 = _mm_srli_epi32(tmp6, 31);
    tmp3 = _mm_slli_epi32(tmp3, 1);
    tmp6 = _mm_slli_epi32(tmp6, 1);
    __m128i tmp9 = _mm_srli_si128(tmp7, 12);
    tmp8 = _mm_slli_si128(tmp8, 4);
    tmp7 = _mm_slli_si128(tmp7, 4);
    tmp3 = _mm_or_si128(tmp3, tmp7);
    tmp6 = _mm_or_si128(tmp6, tmp8);
    tmp6 = _mm_or_si128(tmp6, tmp9);

    tmp7 = _mm_slli_epi32(tmp3, 31);
    tmp8 = _mm_slli_epi32(tmp3, 30);
    tmp9 = _mm_slli_epi32(tmp3, 25);
    tmp7 = _mm_xor_si128(tmp7, tmp8);
    tmp7 = _mm_xor_si128(tmp7, tmp9);
    tmp8 = _mm_srli_si128(tmp7, 4);
    tmp7 = _mm_slli_si128(tmp7, 12);
    tmp3 = _mm_xor_si128(tmp3, tmp7);

    __m128i tmp2 = _mm_srli_epi32(tmp3, 1);
    __m128i tmp4b = _mm_srli_epi32(tmp3, 2);
    __m128i tmp5b = _mm_srli_epi32(tmp3, 7);
    tmp2 = _mm_xor_si128(tmp2, tmp4b);
    tmp2 = _mm_xor_si128(tmp2, tmp5b);
    tmp2 = _mm_xor_si128(tmp2, tmp8);
    tmp3 = _mm_xor_si128(tmp3, tmp2);
    tmp6 = _mm_xor_si128(tmp6, tmp3);

    // Reverse byte order and store
    __m128i result =
        _mm_shuffle_epi8(tmp6, _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));

    Block out;
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out.data()), result);
    return out;
}

#endif  // DOTVM_X86_AES256GCM

}  // namespace dotvm::core::crypto
