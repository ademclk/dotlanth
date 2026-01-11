/// @file crypto_benchmark.cpp
/// @brief Cryptographic primitives benchmarks for DotVM
///
/// Benchmarks for cryptographic operations:
/// - SHA-256 throughput at various block sizes (1KB, 64KB, 1MB)
/// - SHA-256 scalar vs hardware acceleration
/// - AES-128 encrypt/decrypt throughput
/// - AES-128 scalar vs hardware acceleration
///
/// Part of CORE-008: Performance Benchmarks for the dotlanth VM

#include "dotvm/core/bench/benchmark_config.hpp"
#include "dotvm/core/crypto/sha256.hpp"
#include "dotvm/core/crypto/aes.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <random>
#include <span>
#include <vector>

using namespace dotvm::core::bench;
using namespace dotvm::core::crypto;

// ============================================================================
// Test Data Generation
// ============================================================================

/// Generate random byte data for benchmarks
std::vector<std::uint8_t> generate_random_bytes(std::size_t count, std::uint32_t seed = 42) {
    std::vector<std::uint8_t> data(count);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);

    for (auto& byte : data) {
        byte = static_cast<std::uint8_t>(dist(rng));
    }
    return data;
}

/// Generate a random AES-128 key
Aes128::Key generate_random_key(std::uint32_t seed = 42) {
    Aes128::Key key;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);

    for (auto& byte : key) {
        byte = static_cast<std::uint8_t>(dist(rng));
    }
    return key;
}

/// Generate a random AES block (16 bytes)
Aes128::Block generate_random_block(std::uint32_t seed = 42) {
    Aes128::Block block;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);

    for (auto& byte : block) {
        byte = static_cast<std::uint8_t>(dist(rng));
    }
    return block;
}

// ============================================================================
// Helper to create spans
// ============================================================================

/// Create a span from a std::array (automatic size deduction)
template<typename T, std::size_t N>
std::span<const T, N> make_span(const std::array<T, N>& arr) {
    return std::span<const T, N>(arr);
}

/// Create a dynamic span from a vector
template<typename T>
std::span<const T> make_span(const std::vector<T>& vec) {
    return std::span<const T>(vec.data(), vec.size());
}

// ============================================================================
// SHA-256 Benchmarks
// ============================================================================

void run_sha256_benchmarks() {
    print_header("SHA-256 Benchmarks");

    // Check hardware acceleration
    bool hw_accel = Sha256::has_hardware_acceleration();
    std::cout << "Hardware acceleration: " << (hw_accel ? "Available" : "Not available") << "\n";
    print_separator('-');

    // 1KB block
    {
        constexpr std::size_t size = 1024;  // 1 KB
        auto data = generate_random_bytes(size);

        Benchmark bench("SHA-256 (1 KB)");
        bench.warmup([&]() {
            auto digest = Sha256::hash(make_span(data));
            do_not_optimize(digest);
        });

        auto res = bench.run_throughput([&]() {
            auto digest = Sha256::hash(make_span(data));
            do_not_optimize(digest);
        }, size, 100000);
        res.print();
    }

    // 64KB block
    {
        constexpr std::size_t size = 64 * 1024;  // 64 KB
        auto data = generate_random_bytes(size);

        Benchmark bench("SHA-256 (64 KB)");
        bench.warmup([&]() {
            auto digest = Sha256::hash(make_span(data));
            do_not_optimize(digest);
        });

        auto res = bench.run_throughput([&]() {
            auto digest = Sha256::hash(make_span(data));
            do_not_optimize(digest);
        }, size, 10000);
        res.print();
    }

    // 1MB block
    {
        constexpr std::size_t size = 1024 * 1024;  // 1 MB
        auto data = generate_random_bytes(size);

        Benchmark bench("SHA-256 (1 MB)");
        bench.warmup([&]() {
            auto digest = Sha256::hash(make_span(data));
            do_not_optimize(digest);
        }, 100);

        auto res = bench.run_throughput([&]() {
            auto digest = Sha256::hash(make_span(data));
            do_not_optimize(digest);
        }, size, 1000);
        res.print();
    }

    // Single block (64 bytes) - minimum overhead
    {
        constexpr std::size_t size = 64;  // One block
        auto data = generate_random_bytes(size);

        Benchmark bench("SHA-256 (64 bytes, 1 block)");
        bench.warmup([&]() {
            auto digest = Sha256::hash(make_span(data));
            do_not_optimize(digest);
        });

        auto res = bench.run([&]() {
            auto digest = Sha256::hash(make_span(data));
            do_not_optimize(digest);
        }, 500000);
        res.print();
    }

    // Incremental hashing benchmark
    {
        constexpr std::size_t chunk_size = 4096;
        constexpr std::size_t num_chunks = 256;  // 1 MB total
        auto data = generate_random_bytes(chunk_size);

        Benchmark bench("SHA-256 Incremental (256 x 4KB)");
        bench.warmup([&]() {
            Sha256 hasher;
            for (std::size_t i = 0; i < num_chunks; ++i) {
                hasher.update(make_span(data));
            }
            auto digest = hasher.finalize();
            do_not_optimize(digest);
        }, 100);

        auto res = bench.run_throughput([&]() {
            Sha256 hasher;
            for (std::size_t i = 0; i < num_chunks; ++i) {
                hasher.update(make_span(data));
            }
            auto digest = hasher.finalize();
            do_not_optimize(digest);
        }, chunk_size * num_chunks, 1000);
        res.print();
    }

    print_separator();
}

// ============================================================================
// AES-128 Benchmarks
// ============================================================================

void run_aes_benchmarks() {
    print_header("AES-128 Benchmarks");

    // Check hardware acceleration
    bool hw_accel = Aes128::has_hardware_acceleration();
    std::cout << "Hardware acceleration: " << (hw_accel ? "Available" : "Not available") << "\n";
    print_separator('-');

    auto key = generate_random_key();
    Aes128 cipher(key);

    // Single block encryption
    {
        auto plaintext = generate_random_block();

        Benchmark bench("AES-128 Encrypt (1 block)");
        bench.warmup([&]() {
            auto ciphertext = cipher.encrypt_block(make_span(plaintext));
            do_not_optimize(ciphertext);
        });

        auto res = bench.run([&]() {
            auto ciphertext = cipher.encrypt_block(make_span(plaintext));
            do_not_optimize(ciphertext);
        }, 1000000);
        res.print();
    }

    // Single block decryption
    {
        auto plaintext = generate_random_block();
        auto ciphertext = cipher.encrypt_block(make_span(plaintext));

        Benchmark bench("AES-128 Decrypt (1 block)");
        bench.warmup([&]() {
            auto decrypted = cipher.decrypt_block(make_span(ciphertext));
            do_not_optimize(decrypted);
        });

        auto res = bench.run([&]() {
            auto decrypted = cipher.decrypt_block(make_span(ciphertext));
            do_not_optimize(decrypted);
        }, 1000000);
        res.print();
    }

    // Multi-block encryption (ECB mode, for benchmarking only)
    {
        constexpr std::size_t num_blocks = 1024;
        constexpr std::size_t total_bytes = num_blocks * Aes128::BLOCK_SIZE;  // 16 KB

        std::vector<Aes128::Block> blocks(num_blocks);
        for (std::size_t i = 0; i < num_blocks; ++i) {
            blocks[i] = generate_random_block(static_cast<std::uint32_t>(i));
        }

        Benchmark bench("AES-128 Encrypt (1024 blocks, 16 KB)");
        bench.warmup([&]() {
            for (auto& block : blocks) {
                block = cipher.encrypt_block(make_span(block));
            }
            do_not_optimize(blocks.data());
        });

        auto res = bench.run_throughput([&]() {
            for (auto& block : blocks) {
                block = cipher.encrypt_block(make_span(block));
            }
            do_not_optimize(blocks.data());
        }, total_bytes, 10000);
        res.print();
    }

    // Multi-block decryption
    {
        constexpr std::size_t num_blocks = 1024;
        constexpr std::size_t total_bytes = num_blocks * Aes128::BLOCK_SIZE;

        // First encrypt the blocks
        std::vector<Aes128::Block> blocks(num_blocks);
        for (std::size_t i = 0; i < num_blocks; ++i) {
            auto plain = generate_random_block(static_cast<std::uint32_t>(i));
            blocks[i] = cipher.encrypt_block(make_span(plain));
        }

        Benchmark bench("AES-128 Decrypt (1024 blocks, 16 KB)");
        bench.warmup([&]() {
            for (auto& block : blocks) {
                block = cipher.decrypt_block(make_span(block));
            }
            do_not_optimize(blocks.data());
        });

        auto res = bench.run_throughput([&]() {
            for (auto& block : blocks) {
                block = cipher.decrypt_block(make_span(block));
            }
            do_not_optimize(blocks.data());
        }, total_bytes, 10000);
        res.print();
    }

    // Large buffer encryption (64 KB)
    {
        constexpr std::size_t num_blocks = 4096;
        constexpr std::size_t total_bytes = num_blocks * Aes128::BLOCK_SIZE;  // 64 KB

        std::vector<Aes128::Block> blocks(num_blocks);
        for (std::size_t i = 0; i < num_blocks; ++i) {
            blocks[i] = generate_random_block(static_cast<std::uint32_t>(i));
        }

        Benchmark bench("AES-128 Encrypt (4096 blocks, 64 KB)");
        bench.warmup([&]() {
            for (auto& block : blocks) {
                block = cipher.encrypt_block(make_span(block));
            }
            do_not_optimize(blocks.data());
        }, 100);

        auto res = bench.run_throughput([&]() {
            for (auto& block : blocks) {
                block = cipher.encrypt_block(make_span(block));
            }
            do_not_optimize(blocks.data());
        }, total_bytes, 1000);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Key Expansion Benchmark
// ============================================================================

void run_key_expansion_benchmarks() {
    print_header("Key Operations Benchmarks");

    // Key expansion (creating a new cipher)
    {
        auto key = generate_random_key();

        Benchmark bench("AES-128 Key Expansion");
        bench.warmup([&]() {
            Aes128 cipher(key);
            do_not_optimize(cipher);
        });

        auto res = bench.run([&]() {
            Aes128 cipher(key);
            do_not_optimize(cipher);
        }, 500000);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Combined Throughput Comparison
// ============================================================================

void run_throughput_comparison() {
    print_header("Throughput Comparison (1 MB)");

    constexpr std::size_t data_size = 1024 * 1024;  // 1 MB

    // SHA-256 throughput
    {
        auto data = generate_random_bytes(data_size);

        Benchmark bench("SHA-256");
        auto res = bench.run_throughput([&]() {
            auto digest = Sha256::hash(make_span(data));
            do_not_optimize(digest);
        }, data_size, 1000);
        res.print();
    }

    // AES-128 encryption throughput
    {
        auto key = generate_random_key();
        Aes128 cipher(key);

        constexpr std::size_t num_blocks = data_size / Aes128::BLOCK_SIZE;
        std::vector<Aes128::Block> blocks(num_blocks);
        for (std::size_t i = 0; i < num_blocks; ++i) {
            blocks[i] = generate_random_block(static_cast<std::uint32_t>(i));
        }

        Benchmark bench("AES-128 Encrypt");
        auto res = bench.run_throughput([&]() {
            for (auto& block : blocks) {
                block = cipher.encrypt_block(make_span(block));
            }
            do_not_optimize(blocks.data());
        }, data_size, 100);
        res.print();
    }

    // AES-128 decryption throughput
    {
        auto key = generate_random_key();
        Aes128 cipher(key);

        constexpr std::size_t num_blocks = data_size / Aes128::BLOCK_SIZE;
        std::vector<Aes128::Block> blocks(num_blocks);
        for (std::size_t i = 0; i < num_blocks; ++i) {
            auto plain = generate_random_block(static_cast<std::uint32_t>(i));
            blocks[i] = cipher.encrypt_block(make_span(plain));
        }

        Benchmark bench("AES-128 Decrypt");
        auto res = bench.run_throughput([&]() {
            for (auto& block : blocks) {
                block = cipher.decrypt_block(make_span(block));
            }
            do_not_optimize(blocks.data());
        }, data_size, 100);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Latency Benchmarks
// ============================================================================

void run_latency_benchmarks() {
    print_header("Latency Benchmarks (single operation)");

    // SHA-256 single block latency
    {
        std::array<std::uint8_t, 64> block{};
        for (std::size_t i = 0; i < block.size(); ++i) {
            block[i] = static_cast<std::uint8_t>(i);
        }

        Benchmark bench("SHA-256 Single Block");
        auto res = bench.run([&]() {
            auto digest = Sha256::hash(std::span<const std::uint8_t>(block.data(), block.size()));
            do_not_optimize(digest);
        }, 1000000);
        res.print();
    }

    // AES-128 single block encrypt latency
    {
        auto key = generate_random_key();
        Aes128 cipher(key);
        auto block = generate_random_block();

        Benchmark bench("AES-128 Encrypt Single Block");
        auto res = bench.run([&]() {
            auto result = cipher.encrypt_block(make_span(block));
            do_not_optimize(result);
        }, 1000000);
        res.print();
    }

    // AES-128 single block decrypt latency
    {
        auto key = generate_random_key();
        Aes128 cipher(key);
        auto plain = generate_random_block();
        auto block = cipher.encrypt_block(make_span(plain));

        Benchmark bench("AES-128 Decrypt Single Block");
        auto res = bench.run([&]() {
            auto result = cipher.decrypt_block(make_span(block));
            do_not_optimize(result);
        }, 1000000);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main() {
    std::cout << "\n";
    print_separator('=', 80);
    std::cout << "  DotVM Crypto Benchmark Suite\n";
    print_separator('=', 80);
    std::cout << "\n";

    // Print configuration
    std::cout << "Configuration:\n";
    std::cout << "  - SHA-256 block size: " << Sha256::BLOCK_SIZE << " bytes\n";
    std::cout << "  - SHA-256 digest size: " << Sha256::DIGEST_SIZE << " bytes\n";
    std::cout << "  - AES-128 block size: " << Aes128::BLOCK_SIZE << " bytes\n";
    std::cout << "  - AES-128 key size: " << Aes128::KEY_SIZE << " bytes\n";
    std::cout << "  - AES-128 rounds: " << Aes128::NUM_ROUNDS << "\n";
    std::cout << "\n";

    std::cout << "Hardware Acceleration:\n";
    std::cout << "  - SHA-256: " << (Sha256::has_hardware_acceleration() ? "Available (SHA-NI/ARM Crypto)" : "Not available (using scalar)") << "\n";
    std::cout << "  - AES-128: " << (Aes128::has_hardware_acceleration() ? "Available (AES-NI/ARM Crypto)" : "Not available (using scalar)") << "\n";
    std::cout << "\n";

    // Run all benchmarks
    run_sha256_benchmarks();
    run_aes_benchmarks();
    run_key_expansion_benchmarks();
    run_throughput_comparison();
    run_latency_benchmarks();

    // Summary
    std::cout << "\n";
    print_separator('=', 80);
    std::cout << "  Benchmark Complete\n";
    print_separator('=', 80);
    std::cout << "\n";
    std::cout << "Notes:\n";
    std::cout << "  - SHA-256 implements FIPS 180-4\n";
    std::cout << "  - AES-128 implements FIPS-197\n";
    std::cout << "  - Multi-block AES benchmarks use ECB mode (for benchmarking only)\n";
    std::cout << "  - For production use, prefer authenticated modes (GCM, CCM)\n";
    std::cout << "\n";

    return 0;
}
