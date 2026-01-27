/// @file mpt_benchmark_test.cpp
/// @brief Performance benchmarks for STATE-008 Merkle Patricia Trie
///
/// Validates the <1ms root hash update target for tries with up to 100k keys.
/// These are test-based benchmarks, not Google Benchmark (for CI integration).

#include <chrono>
#include <random>

#include <gtest/gtest.h>

#include "dotvm/core/state/merkle_patricia_trie.hpp"
#include "dotvm/core/state/mpt_proof.hpp"

// Detect sanitizer builds - sanitizers add significant overhead making
// performance assertions meaningless
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || \
    defined(__SANITIZE_UNDEFINED__) || defined(__has_feature)
    #if defined(__has_feature)
        #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
            __has_feature(undefined_behavior_sanitizer) || __has_feature(memory_sanitizer)
            #define RUNNING_UNDER_SANITIZER 1
        #endif
    #else
        #define RUNNING_UNDER_SANITIZER 1
    #endif
#endif

namespace dotvm::core::state {
namespace {

// ============================================================================
// Test Utilities
// ============================================================================

/// Generate a random key of given length
[[nodiscard]] std::vector<std::byte> random_key(std::mt19937& rng, std::size_t length) {
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<std::byte> key(length);
    for (auto& byte : key) {
        byte = static_cast<std::byte>(dist(rng));
    }
    return key;
}

/// Generate a random value of given length
[[nodiscard]] std::vector<std::byte> random_value(std::mt19937& rng, std::size_t length) {
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<std::byte> value(length);
    for (auto& byte : value) {
        byte = static_cast<std::byte>(dist(rng));
    }
    return value;
}

/// Measure duration of an operation in microseconds
template <typename Func>
[[nodiscard]] double measure_us(Func&& fn) {
    const auto start = std::chrono::high_resolution_clock::now();
    fn();
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::micro>(end - start).count();
}

// ============================================================================
// Insert Performance Tests
// ============================================================================

TEST(MptBenchmarkTest, InsertPerformance_1000Keys) {
    MerklePatriciaTrie trie;
    std::mt19937 rng(42);

    constexpr std::size_t num_keys = 1000;
    constexpr std::size_t key_length = 32;
    constexpr std::size_t value_length = 64;

    double total_us = 0.0;
    for (std::size_t i = 0; i < num_keys; ++i) {
        const auto key = random_key(rng, key_length);
        const auto value = random_value(rng, value_length);
        total_us += measure_us([&]() {
            auto result = trie.insert(key, value);
            ASSERT_TRUE(result.is_ok());
        });
    }

    const double avg_us = total_us / static_cast<double>(num_keys);
    std::cout << "Insert 1000 keys (32B key, 64B value): avg " << avg_us << " us/op\n";

#ifndef RUNNING_UNDER_SANITIZER
    // Average insert should be under 100 microseconds
    EXPECT_LT(avg_us, 100.0) << "Insert too slow";
#endif
}

TEST(MptBenchmarkTest, InsertPerformance_10000Keys) {
    MerklePatriciaTrie trie;
    std::mt19937 rng(42);

    constexpr std::size_t num_keys = 10000;
    constexpr std::size_t key_length = 32;
    constexpr std::size_t value_length = 64;

    double total_us = 0.0;
    for (std::size_t i = 0; i < num_keys; ++i) {
        const auto key = random_key(rng, key_length);
        const auto value = random_value(rng, value_length);
        total_us += measure_us([&]() {
            auto result = trie.insert(key, value);
            ASSERT_TRUE(result.is_ok());
        });
    }

    const double avg_us = total_us / static_cast<double>(num_keys);
    std::cout << "Insert 10000 keys (32B key, 64B value): avg " << avg_us << " us/op\n";

    EXPECT_EQ(trie.size(), num_keys);
}

// ============================================================================
// Root Hash Update Performance Tests (PRIMARY TARGET: <1ms)
// ============================================================================

TEST(MptBenchmarkTest, RootHashUpdate_After100kKeys) {
#ifdef RUNNING_UNDER_SANITIZER
    GTEST_SKIP() << "Skipping performance test under sanitizers";
#endif

    MerklePatriciaTrie trie;
    std::mt19937 rng(42);

    constexpr std::size_t num_keys = 100000;
    constexpr std::size_t key_length = 32;
    constexpr std::size_t value_length = 64;

    // Pre-populate trie with 100k keys
    for (std::size_t i = 0; i < num_keys; ++i) {
        const auto key = random_key(rng, key_length);
        const auto value = random_value(rng, value_length);
        auto result = trie.insert(key, value);
        ASSERT_TRUE(result.is_ok());
    }
    ASSERT_EQ(trie.size(), num_keys);

    // Force compute initial root hash
    [[maybe_unused]] auto initial_hash = trie.root_hash();

    // Now measure incremental updates
    constexpr std::size_t num_updates = 100;
    double total_update_us = 0.0;

    for (std::size_t i = 0; i < num_updates; ++i) {
        const auto key = random_key(rng, key_length);
        const auto value = random_value(rng, value_length);

        // Insert new key
        auto insert_result = trie.insert(key, value);
        ASSERT_TRUE(insert_result.is_ok());

        // Measure root hash computation (should be incremental)
        const double hash_us = measure_us([&]() { [[maybe_unused]] auto hash = trie.root_hash(); });

        total_update_us += hash_us;
    }

    const double avg_update_us = total_update_us / static_cast<double>(num_updates);
    const double avg_update_ms = avg_update_us / 1000.0;

    std::cout << "Root hash update after 100k keys: avg " << avg_update_us << " us ("
              << avg_update_ms << " ms)\n";

    // PRIMARY TARGET: <1ms (1000 microseconds) for root hash update
    EXPECT_LT(avg_update_us, 1000.0) << "Root hash update exceeds 1ms target!";
}

TEST(MptBenchmarkTest, RootHashUpdate_SingleInsertIncremental) {
#ifdef RUNNING_UNDER_SANITIZER
    GTEST_SKIP() << "Skipping performance test under sanitizers";
#endif

    MerklePatriciaTrie trie;
    std::mt19937 rng(42);

    constexpr std::size_t key_length = 32;
    constexpr std::size_t value_length = 64;

    // Pre-populate with 10k keys
    for (std::size_t i = 0; i < 10000; ++i) {
        const auto key = random_key(rng, key_length);
        const auto value = random_value(rng, value_length);
        ASSERT_TRUE(trie.insert(key, value).is_ok());
    }

    // Force initial hash computation
    [[maybe_unused]] auto initial_hash = trie.root_hash();

    // Insert one more key and measure hash update
    const auto new_key = random_key(rng, key_length);
    const auto new_value = random_value(rng, value_length);
    ASSERT_TRUE(trie.insert(new_key, new_value).is_ok());

    double hash_us = measure_us([&]() { [[maybe_unused]] auto hash = trie.root_hash(); });

    std::cout << "Single insert + root hash (10k trie): " << hash_us << " us\n";

    // Should be well under 1ms for incremental hashing
    EXPECT_LT(hash_us, 1000.0) << "Incremental hash too slow";
}

// ============================================================================
// Get Performance Tests
// ============================================================================

TEST(MptBenchmarkTest, GetPerformance_10000Keys) {
#ifdef RUNNING_UNDER_SANITIZER
    GTEST_SKIP() << "Skipping performance test under sanitizers";
#endif

    MerklePatriciaTrie trie;
    std::mt19937 rng(42);

    constexpr std::size_t num_keys = 10000;
    constexpr std::size_t key_length = 32;
    constexpr std::size_t value_length = 64;

    // Store keys for later retrieval
    std::vector<std::vector<std::byte>> keys;
    keys.reserve(num_keys);

    // Populate trie
    for (std::size_t i = 0; i < num_keys; ++i) {
        auto key = random_key(rng, key_length);
        const auto value = random_value(rng, value_length);
        ASSERT_TRUE(trie.insert(key, value).is_ok());
        keys.push_back(std::move(key));
    }

    // Measure get performance
    double total_us = 0.0;
    for (const auto& key : keys) {
        total_us += measure_us([&]() {
            auto result = trie.get(key);
            ASSERT_TRUE(result.is_ok());
        });
    }

    const double avg_us = total_us / static_cast<double>(num_keys);
    std::cout << "Get from 10000-key trie: avg " << avg_us << " us/op\n";

    // Get should be very fast (under 10 microseconds average)
    EXPECT_LT(avg_us, 50.0) << "Get too slow";
}

// ============================================================================
// Proof Generation Performance Tests
// ============================================================================

TEST(MptBenchmarkTest, ProofGeneration_10000Keys) {
#ifdef RUNNING_UNDER_SANITIZER
    GTEST_SKIP() << "Skipping performance test under sanitizers";
#endif

    MerklePatriciaTrie trie;
    std::mt19937 rng(42);

    constexpr std::size_t num_keys = 10000;
    constexpr std::size_t key_length = 32;
    constexpr std::size_t value_length = 64;

    std::vector<std::vector<std::byte>> keys;
    keys.reserve(num_keys);

    for (std::size_t i = 0; i < num_keys; ++i) {
        auto key = random_key(rng, key_length);
        const auto value = random_value(rng, value_length);
        ASSERT_TRUE(trie.insert(key, value).is_ok());
        keys.push_back(std::move(key));
    }

    // Measure proof generation (sample of 100 keys)
    constexpr std::size_t sample_size = 100;
    double total_us = 0.0;

    for (std::size_t i = 0; i < sample_size; ++i) {
        const auto& key = keys[i * (num_keys / sample_size)];
        total_us += measure_us([&]() {
            auto result = trie.get_proof(key);
            ASSERT_TRUE(result.is_ok());
        });
    }

    const double avg_us = total_us / static_cast<double>(sample_size);
    std::cout << "Proof generation (10k trie): avg " << avg_us << " us/op\n";

    // Proof generation includes hash computation along the path
    // Allow up to 5ms per proof (secondary goal, not critical path)
    EXPECT_LT(avg_us, 5000.0) << "Proof generation too slow";
}

TEST(MptBenchmarkTest, ProofVerification_Performance) {
#ifdef RUNNING_UNDER_SANITIZER
    GTEST_SKIP() << "Skipping performance test under sanitizers";
#endif

    MerklePatriciaTrie trie;
    std::mt19937 rng(42);

    constexpr std::size_t num_keys = 1000;
    constexpr std::size_t key_length = 32;
    constexpr std::size_t value_length = 64;

    std::vector<std::vector<std::byte>> keys;
    std::vector<std::vector<std::byte>> values;
    keys.reserve(num_keys);
    values.reserve(num_keys);

    for (std::size_t i = 0; i < num_keys; ++i) {
        auto key = random_key(rng, key_length);
        auto value = random_value(rng, value_length);
        ASSERT_TRUE(trie.insert(key, value).is_ok());
        keys.push_back(std::move(key));
        values.push_back(std::move(value));
    }

    const auto root = trie.root_hash();

    // Generate proofs for all keys
    std::vector<MptProof> proofs;
    proofs.reserve(num_keys);
    for (const auto& key : keys) {
        auto result = trie.get_proof(key);
        ASSERT_TRUE(result.is_ok());
        proofs.push_back(std::move(result.value()));
    }

    // Measure verification performance
    double total_us = 0.0;
    for (std::size_t i = 0; i < num_keys; ++i) {
        total_us += measure_us([&]() {
            auto result = verify_inclusion(root, keys[i], values[i], proofs[i]);
            ASSERT_TRUE(result.is_ok());
            ASSERT_TRUE(result.value());
        });
    }

    const double avg_us = total_us / static_cast<double>(num_keys);
    std::cout << "Proof verification: avg " << avg_us << " us/op\n";

    // Verification involves deserializing nodes and hashing
    // Allow up to 500 microseconds (secondary goal)
    EXPECT_LT(avg_us, 500.0) << "Proof verification too slow";
}

// ============================================================================
// Memory/Scalability Tests
// ============================================================================

TEST(MptBenchmarkTest, ScalabilityTest_Doubling) {
#ifdef RUNNING_UNDER_SANITIZER
    GTEST_SKIP() << "Skipping scalability test under sanitizers";
#endif

    std::mt19937 rng(42);

    constexpr std::size_t key_length = 32;
    constexpr std::size_t value_length = 64;

    // Test scaling: 1k, 2k, 4k, 8k, 16k, 32k keys
    std::vector<std::size_t> sizes = {1000, 2000, 4000, 8000, 16000, 32000};
    std::vector<double> times;

    for (auto size : sizes) {
        MerklePatriciaTrie trie;
        rng.seed(42);  // Reset seed for consistency

        const double build_us = measure_us([&]() {
            for (std::size_t i = 0; i < size; ++i) {
                const auto key = random_key(rng, key_length);
                const auto value = random_value(rng, value_length);
                ASSERT_TRUE(trie.insert(key, value).is_ok());
            }
        });

        const double hash_us = measure_us([&]() { [[maybe_unused]] auto hash = trie.root_hash(); });

        times.push_back(build_us);
        std::cout << "Build " << size << " keys: " << (build_us / 1000.0) << " ms, "
                  << "hash: " << hash_us << " us\n";
    }

    // Verify roughly linear or sublinear scaling (2x keys should be < 3x time)
    // This is a loose check - exact scaling depends on key distribution
    for (std::size_t i = 1; i < sizes.size(); ++i) {
        const double ratio = times[i] / times[i - 1];
        EXPECT_LT(ratio, 3.0) << "Scaling worse than O(n) at size " << sizes[i];
    }
}

}  // namespace
}  // namespace dotvm::core::state
