#include "dotvm/core/crypto/blake3.hpp"

#include <algorithm>
#include <cstring>

#include "dotvm/core/simd/cpu_features.hpp"

// Platform-specific intrinsics for hardware acceleration
#if defined(DOTVM_SIMD_ENABLED)
    #if defined(__x86_64__) || defined(_M_X64)
        #include <immintrin.h>
        #define DOTVM_X86_BLAKE3 1
    #endif
#endif

namespace dotvm::core::crypto {

// ============================================================================
// BLAKE3 Constants
// ============================================================================

namespace {

// Message schedule permutation for each round
constexpr std::array<std::array<std::size_t, 16>, 7> MSG_SCHEDULE = {
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
     {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
     {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
     {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
     {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
     {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
     {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13}}};

// Rotation constants
constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32 - n));
}

// G mixing function
constexpr void g(std::array<std::uint32_t, 16>& state, std::size_t a, std::size_t b, std::size_t c,
                 std::size_t d, std::uint32_t mx, std::uint32_t my) noexcept {
    state[a] = state[a] + state[b] + mx;
    state[d] = rotr(state[d] ^ state[a], 16);
    state[c] = state[c] + state[d];
    state[b] = rotr(state[b] ^ state[c], 12);
    state[a] = state[a] + state[b] + my;
    state[d] = rotr(state[d] ^ state[a], 8);
    state[c] = state[c] + state[d];
    state[b] = rotr(state[b] ^ state[c], 7);
}

// Round function
constexpr void round_fn(std::array<std::uint32_t, 16>& state,
                        const std::array<std::uint32_t, 16>& msg, std::size_t round) noexcept {
    const auto& s = MSG_SCHEDULE[round];

    // Mix columns
    g(state, 0, 4, 8, 12, msg[s[0]], msg[s[1]]);
    g(state, 1, 5, 9, 13, msg[s[2]], msg[s[3]]);
    g(state, 2, 6, 10, 14, msg[s[4]], msg[s[5]]);
    g(state, 3, 7, 11, 15, msg[s[6]], msg[s[7]]);

    // Mix diagonals
    g(state, 0, 5, 10, 15, msg[s[8]], msg[s[9]]);
    g(state, 1, 6, 11, 12, msg[s[10]], msg[s[11]]);
    g(state, 2, 7, 8, 13, msg[s[12]], msg[s[13]]);
    g(state, 3, 4, 9, 14, msg[s[14]], msg[s[15]]);
}

/// Load 32-bit little-endian value
inline std::uint32_t load_le32(const std::uint8_t* ptr) noexcept {
    return static_cast<std::uint32_t>(ptr[0]) | (static_cast<std::uint32_t>(ptr[1]) << 8) |
           (static_cast<std::uint32_t>(ptr[2]) << 16) | (static_cast<std::uint32_t>(ptr[3]) << 24);
}

/// Store 32-bit little-endian value
inline void store_le32(std::uint8_t* ptr, std::uint32_t val) noexcept {
    ptr[0] = static_cast<std::uint8_t>(val);
    ptr[1] = static_cast<std::uint8_t>(val >> 8);
    ptr[2] = static_cast<std::uint8_t>(val >> 16);
    ptr[3] = static_cast<std::uint8_t>(val >> 24);
}

/// Load message block into words
inline void load_message_block(std::array<std::uint32_t, 16>& msg,
                               const std::uint8_t* block) noexcept {
    for (std::size_t i = 0; i < 16; ++i) {
        msg[i] = load_le32(block + i * 4);
    }
}

}  // namespace

// ============================================================================
// Blake3 Implementation
// ============================================================================

Blake3::Blake3() noexcept
    : key_words_(IV),
      cv_(IV),
      chunk_buffer_{},
      chunk_buffer_len_(0),
      chunk_counter_(0),
      flags_(0),
      cv_stack_{},
      cv_stack_len_(0) {}

Blake3::Blake3(const Key& key) noexcept
    : chunk_buffer_{},
      chunk_buffer_len_(0),
      chunk_counter_(0),
      flags_(KEYED_HASH),
      cv_stack_{},
      cv_stack_len_(0) {
    // Initialize key_words_ from key bytes
    for (std::size_t i = 0; i < 8; ++i) {
        key_words_[i] = load_le32(key.data() + i * 4);
    }
    cv_ = key_words_;
}

void Blake3::reset() noexcept {
    cv_ = key_words_;
    chunk_buffer_len_ = 0;
    chunk_counter_ = 0;
    cv_stack_len_ = 0;
}

void Blake3::compress_scalar(std::array<std::uint32_t, 16>& state,
                             const std::array<std::uint32_t, 16>& msg, std::uint64_t counter,
                             std::uint32_t block_len, std::uint32_t flags) noexcept {
    // Initialize state with counter and flags
    state[12] = static_cast<std::uint32_t>(counter);
    state[13] = static_cast<std::uint32_t>(counter >> 32);
    state[14] = block_len;
    state[15] = flags;

    // 7 rounds
    for (std::size_t r = 0; r < 7; ++r) {
        round_fn(state, msg, r);
    }
}

void Blake3::compress(std::array<std::uint32_t, 16>& state,
                      const std::array<std::uint32_t, 16>& msg, std::uint64_t counter,
                      std::uint32_t block_len, std::uint32_t flags) noexcept {
    compress_scalar(state, msg, counter, block_len, flags);
}

void Blake3::add_chunk_cv(const std::array<std::uint32_t, 8>& cv) noexcept {
    // Add CV to stack, merging as needed to maintain binary tree property
    std::array<std::uint32_t, 8> new_cv = cv;

    // Count trailing ones in chunk_counter to determine merge depth
    std::uint64_t total_chunks = chunk_counter_;

    while ((total_chunks & 1) == 1) {
        // Merge with parent from stack
        if (cv_stack_len_ == 0)
            break;

        // Pop from stack
        --cv_stack_len_;
        const auto& left = cv_stack_[cv_stack_len_];

        // Create parent input (left || right)
        std::array<std::uint32_t, 16> parent_msg{};
        std::copy(left.begin(), left.end(), parent_msg.begin());
        std::copy(new_cv.begin(), new_cv.end(), parent_msg.begin() + 8);

        // Compress parent
        std::array<std::uint32_t, 16> state{};
        std::copy(key_words_.begin(), key_words_.end(), state.begin());
        std::copy(IV.begin(), IV.end(), state.begin() + 8);

        compress(state, parent_msg, 0, BLOCK_SIZE, flags_ | PARENT);

        // XOR lower and upper halves to get new CV
        for (std::size_t i = 0; i < 8; ++i) {
            new_cv[i] = state[i] ^ state[i + 8];
        }

        total_chunks >>= 1;
    }

    // Push to stack
    cv_stack_[cv_stack_len_++] = new_cv;
}

void Blake3::process_chunk(std::span<const std::uint8_t, CHUNK_SIZE> chunk) noexcept {
    std::array<std::uint32_t, 8> cv = cv_;
    std::array<std::uint32_t, 16> msg{};

    // Process 16 blocks in the chunk
    for (std::size_t block = 0; block < 16; ++block) {
        load_message_block(msg, chunk.data() + block * BLOCK_SIZE);

        std::uint32_t block_flags = flags_;
        if (block == 0)
            block_flags |= CHUNK_START;
        if (block == 15)
            block_flags |= CHUNK_END;

        // Initialize state: CV || IV
        std::array<std::uint32_t, 16> state{};
        std::copy(cv.begin(), cv.end(), state.begin());
        std::copy(IV.begin(), IV.end(), state.begin() + 8);

        compress(state, msg, chunk_counter_, BLOCK_SIZE, block_flags);

        // XOR to get new CV
        for (std::size_t i = 0; i < 8; ++i) {
            cv[i] = state[i] ^ state[i + 8];
        }
    }

    // Add chunk CV to Merkle tree
    add_chunk_cv(cv);
    ++chunk_counter_;
    cv_ = key_words_;  // Reset CV for next chunk
}

std::array<std::uint32_t, 8> Blake3::finalize_chunk(std::span<const std::uint8_t> data,
                                                    bool is_root) noexcept {
    std::array<std::uint32_t, 8> cv = cv_;
    std::array<std::uint32_t, 16> msg{};

    std::size_t num_blocks = (data.size() + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (num_blocks == 0)
        num_blocks = 1;  // At least one block

    std::size_t offset = 0;
    for (std::size_t block = 0; block < num_blocks; ++block) {
        std::size_t block_len = std::min(BLOCK_SIZE, data.size() - offset);

        // Zero-pad last block
        std::array<std::uint8_t, BLOCK_SIZE> padded{};
        if (offset < data.size()) {
            std::memcpy(padded.data(), data.data() + offset, block_len);
        }
        load_message_block(msg, padded.data());

        std::uint32_t block_flags = flags_;
        if (block == 0)
            block_flags |= CHUNK_START;
        if (block == num_blocks - 1) {
            block_flags |= CHUNK_END;
            if (is_root && cv_stack_len_ == 0) {
                block_flags |= ROOT;
            }
        }

        // Initialize state: CV || IV
        std::array<std::uint32_t, 16> state{};
        std::copy(cv.begin(), cv.end(), state.begin());
        std::copy(IV.begin(), IV.end(), state.begin() + 8);

        compress(state, msg, chunk_counter_, static_cast<std::uint32_t>(block_len), block_flags);

        // XOR to get new CV
        for (std::size_t i = 0; i < 8; ++i) {
            cv[i] = state[i] ^ state[i + 8];
        }

        offset += BLOCK_SIZE;
    }

    return cv;
}

void Blake3::merge_cv_stack() noexcept {
    // Merge all stack entries to get root
    while (cv_stack_len_ > 1) {
        // Pop right
        --cv_stack_len_;
        auto right = cv_stack_[cv_stack_len_];

        // Pop left
        --cv_stack_len_;
        auto left = cv_stack_[cv_stack_len_];

        // Create parent input
        std::array<std::uint32_t, 16> parent_msg{};
        std::copy(left.begin(), left.end(), parent_msg.begin());
        std::copy(right.begin(), right.end(), parent_msg.begin() + 8);

        // Compress
        std::array<std::uint32_t, 16> state{};
        std::copy(key_words_.begin(), key_words_.end(), state.begin());
        std::copy(IV.begin(), IV.end(), state.begin() + 8);

        std::uint32_t parent_flags = flags_ | PARENT;
        if (cv_stack_len_ == 0) {
            parent_flags |= ROOT;
        }

        compress(state, parent_msg, 0, BLOCK_SIZE, parent_flags);

        // XOR to get CV
        std::array<std::uint32_t, 8> cv{};
        for (std::size_t i = 0; i < 8; ++i) {
            cv[i] = state[i] ^ state[i + 8];
        }

        // Push result
        cv_stack_[cv_stack_len_++] = cv;
    }
}

void Blake3::update(std::span<const std::uint8_t> data) noexcept {
    const auto* ptr = data.data();
    auto remaining = data.size();

    // Fill buffer if we have partial data
    if (chunk_buffer_len_ > 0) {
        auto to_copy = std::min(CHUNK_SIZE - chunk_buffer_len_, remaining);
        std::memcpy(chunk_buffer_.data() + chunk_buffer_len_, ptr, to_copy);
        chunk_buffer_len_ += to_copy;
        ptr += to_copy;
        remaining -= to_copy;

        if (chunk_buffer_len_ == CHUNK_SIZE) {
            process_chunk(std::span<const std::uint8_t, CHUNK_SIZE>{chunk_buffer_});
            chunk_buffer_len_ = 0;
        }
    }

    // Process complete chunks
    while (remaining >= CHUNK_SIZE) {
        process_chunk(std::span<const std::uint8_t, CHUNK_SIZE>{ptr, CHUNK_SIZE});
        ptr += CHUNK_SIZE;
        remaining -= CHUNK_SIZE;
    }

    // Buffer remaining
    if (remaining > 0) {
        std::memcpy(chunk_buffer_.data(), ptr, remaining);
        chunk_buffer_len_ = remaining;
    }
}

void Blake3::update(std::string_view str) noexcept {
    update(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(str.data()),
                                         str.size()});
}

Blake3::Digest Blake3::finalize() noexcept {
    // Finalize current chunk
    auto final_cv = finalize_chunk(
        std::span<const std::uint8_t>{chunk_buffer_.data(), chunk_buffer_len_}, cv_stack_len_ == 0);

    // Add to stack if not root
    if (cv_stack_len_ > 0 || chunk_counter_ > 0) {
        add_chunk_cv(final_cv);
        ++chunk_counter_;

        // Merge entire stack
        merge_cv_stack();

        // Final CV is on stack
        final_cv = cv_stack_[0];
    }

    // Convert CV to digest
    Digest digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        store_le32(digest.data() + i * 4, final_cv[i]);
    }

    // Reset for potential reuse
    reset();

    return digest;
}

Blake3::Digest Blake3::hash(std::span<const std::uint8_t> data) noexcept {
    Blake3 hasher;
    hasher.update(data);
    return hasher.finalize();
}

Blake3::Digest Blake3::hash(std::string_view str) noexcept {
    Blake3 hasher;
    hasher.update(str);
    return hasher.finalize();
}

Blake3::Digest Blake3::keyed_hash(const Key& key, std::span<const std::uint8_t> data) noexcept {
    Blake3 hasher(key);
    hasher.update(data);
    return hasher.finalize();
}

bool Blake3::has_hardware_acceleration() noexcept {
    const auto& features = simd::detect_cpu_features();
    return features.avx2 || features.avx512f;
}

// ============================================================================
// AVX2 Implementation (optional acceleration)
// ============================================================================

#if defined(DOTVM_X86_BLAKE3)

__attribute__((target("avx2"))) void
Blake3::process_chunk_avx2(std::span<const std::uint8_t, CHUNK_SIZE> chunk) noexcept {
    // For simplicity, fall back to scalar for now
    // A full AVX2 implementation would process multiple blocks in parallel
    process_chunk(chunk);
}

#endif  // DOTVM_X86_BLAKE3

}  // namespace dotvm::core::crypto
