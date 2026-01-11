/// @file bigint_benchmark.cpp
/// @brief BigInt performance benchmarks for DotVM
///
/// Benchmarks for arbitrary precision integer operations:
/// - Addition at various bit sizes (64, 256, 1024, 4096)
/// - Multiplication at various bit sizes
/// - Division at various bit sizes
/// - Modular exponentiation
/// - GCD computation
///
/// Part of CORE-008: Performance Benchmarks for the dotlanth VM

#include "dotvm/core/bench/benchmark_config.hpp"
#include "dotvm/core/bigint/bigint.hpp"

#include <cstdint>
#include <iostream>
#include <random>
#include <string>

using namespace dotvm::core::bench;
using namespace dotvm::core::bigint;

// ============================================================================
// Test Data Generation
// ============================================================================

/// Generate a random BigInt with approximately the specified number of bits
BigInt generate_random_bigint(std::size_t bits, std::mt19937_64& rng) {
    if (bits <= 64) {
        std::uniform_int_distribution<std::uint64_t> dist(
            1ULL << (bits - 1),
            bits == 64 ? std::numeric_limits<std::uint64_t>::max() : (1ULL << bits) - 1
        );
        return BigInt(dist(rng));
    }

    // For larger numbers, build from hex string
    std::size_t hex_digits = (bits + 3) / 4;
    std::string hex = "0x";
    hex.reserve(hex_digits + 2);

    // First digit should not be zero
    std::uniform_int_distribution<int> first_dist(1, 15);
    hex += "0123456789ABCDEF"[first_dist(rng)];

    std::uniform_int_distribution<int> hex_dist(0, 15);
    for (std::size_t i = 1; i < hex_digits; ++i) {
        hex += "0123456789ABCDEF"[hex_dist(rng)];
    }

    return BigInt(hex);
}

/// Generate pair of BigInts for benchmarking
std::pair<BigInt, BigInt> generate_pair(std::size_t bits, std::mt19937_64& rng) {
    return {generate_random_bigint(bits, rng), generate_random_bigint(bits, rng)};
}

// ============================================================================
// Native uint64_t Operations (Baseline for small numbers)
// ============================================================================

void run_native_benchmarks() {
    print_header("Native uint64_t Baseline");

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<std::uint64_t> dist(1, std::numeric_limits<std::uint64_t>::max() - 1);

    // Pre-generate test data
    constexpr std::size_t N = 1000;
    std::array<std::uint64_t, N> a_vals, b_vals;
    for (std::size_t i = 0; i < N; ++i) {
        a_vals[i] = dist(rng);
        b_vals[i] = dist(rng);
    }

    // Addition
    {
        Benchmark bench("Native u64 Add");
        std::uint64_t result = 0;
        auto res = bench.run([&]() {
            for (std::size_t i = 0; i < N; ++i) {
                result += a_vals[i] + b_vals[i];
            }
            do_not_optimize(result);
        }, 100000);
        res.print();
    }

    // Multiplication
    {
        Benchmark bench("Native u64 Mul");
        std::uint64_t result = 1;
        auto res = bench.run([&]() {
            for (std::size_t i = 0; i < N; ++i) {
                result ^= a_vals[i] * b_vals[i];  // XOR to prevent overflow issues
            }
            do_not_optimize(result);
        }, 100000);
        res.print();
    }

    // Division
    {
        Benchmark bench("Native u64 Div");
        std::uint64_t result = 0;
        // Ensure divisors are non-zero
        std::array<std::uint64_t, N> divisors;
        for (std::size_t i = 0; i < N; ++i) {
            divisors[i] = b_vals[i] == 0 ? 1 : b_vals[i];
        }
        auto res = bench.run([&]() {
            for (std::size_t i = 0; i < N; ++i) {
                result += a_vals[i] / divisors[i];
            }
            do_not_optimize(result);
        }, 100000);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Addition Benchmarks
// ============================================================================

void run_addition_benchmarks() {
    print_header("BigInt Addition Benchmarks");

    std::mt19937_64 rng(42);

    // 64-bit addition
    {
        auto [a, b] = generate_pair(64, rng);
        Benchmark bench("BigInt Add (64-bit)");
        bench.warmup([&]() { auto r = a + b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a + b;
            do_not_optimize(r);
        }, 1000000);
        res.print();
    }

    // 256-bit addition
    {
        auto [a, b] = generate_pair(256, rng);
        Benchmark bench("BigInt Add (256-bit)");
        bench.warmup([&]() { auto r = a + b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a + b;
            do_not_optimize(r);
        }, 500000);
        res.print();
    }

    // 1024-bit addition
    {
        auto [a, b] = generate_pair(1024, rng);
        Benchmark bench("BigInt Add (1024-bit)");
        bench.warmup([&]() { auto r = a + b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a + b;
            do_not_optimize(r);
        }, 200000);
        res.print();
    }

    // 4096-bit addition
    {
        auto [a, b] = generate_pair(4096, rng);
        Benchmark bench("BigInt Add (4096-bit)");
        bench.warmup([&]() { auto r = a + b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a + b;
            do_not_optimize(r);
        }, 100000);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Multiplication Benchmarks
// ============================================================================

void run_multiplication_benchmarks() {
    print_header("BigInt Multiplication Benchmarks");

    std::mt19937_64 rng(42);

    // 64-bit multiplication
    {
        auto [a, b] = generate_pair(64, rng);
        Benchmark bench("BigInt Mul (64-bit)");
        bench.warmup([&]() { auto r = a * b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a * b;
            do_not_optimize(r);
        }, 500000);
        res.print();
    }

    // 256-bit multiplication
    {
        auto [a, b] = generate_pair(256, rng);
        Benchmark bench("BigInt Mul (256-bit)");
        bench.warmup([&]() { auto r = a * b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a * b;
            do_not_optimize(r);
        }, 100000);
        res.print();
    }

    // 1024-bit multiplication
    {
        auto [a, b] = generate_pair(1024, rng);
        Benchmark bench("BigInt Mul (1024-bit)");
        bench.warmup([&]() { auto r = a * b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a * b;
            do_not_optimize(r);
        }, 10000);
        res.print();
    }

    // 4096-bit multiplication
    {
        auto [a, b] = generate_pair(4096, rng);
        Benchmark bench("BigInt Mul (4096-bit)");
        bench.warmup([&]() { auto r = a * b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a * b;
            do_not_optimize(r);
        }, 1000);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Division Benchmarks
// ============================================================================

void run_division_benchmarks() {
    print_header("BigInt Division Benchmarks");

    std::mt19937_64 rng(42);

    // 64-bit division
    {
        auto a = generate_random_bigint(64, rng);
        auto b = generate_random_bigint(32, rng);  // Smaller divisor
        Benchmark bench("BigInt Div (64-bit / 32-bit)");
        bench.warmup([&]() { auto r = a / b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a / b;
            do_not_optimize(r);
        }, 500000);
        res.print();
    }

    // 256-bit division
    {
        auto a = generate_random_bigint(256, rng);
        auto b = generate_random_bigint(128, rng);
        Benchmark bench("BigInt Div (256-bit / 128-bit)");
        bench.warmup([&]() { auto r = a / b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a / b;
            do_not_optimize(r);
        }, 100000);
        res.print();
    }

    // 1024-bit division
    {
        auto a = generate_random_bigint(1024, rng);
        auto b = generate_random_bigint(512, rng);
        Benchmark bench("BigInt Div (1024-bit / 512-bit)");
        bench.warmup([&]() { auto r = a / b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a / b;
            do_not_optimize(r);
        }, 10000);
        res.print();
    }

    // 4096-bit division
    {
        auto a = generate_random_bigint(4096, rng);
        auto b = generate_random_bigint(2048, rng);
        Benchmark bench("BigInt Div (4096-bit / 2048-bit)");
        bench.warmup([&]() { auto r = a / b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a / b;
            do_not_optimize(r);
        }, 1000);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Modular Exponentiation Benchmarks
// ============================================================================

void run_modpow_benchmarks() {
    print_header("Modular Exponentiation Benchmarks");

    std::mt19937_64 rng(42);

    // 64-bit modpow
    {
        auto base = generate_random_bigint(64, rng);
        auto exp = generate_random_bigint(32, rng);
        auto mod = generate_random_bigint(64, rng);
        Benchmark bench("BigInt ModPow (64-bit base, 32-bit exp)");
        bench.warmup([&]() { auto r = base.mod_pow(exp, mod); do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = base.mod_pow(exp, mod);
            do_not_optimize(r);
        }, 10000);
        res.print();
    }

    // 256-bit modpow (common for cryptographic operations)
    {
        auto base = generate_random_bigint(256, rng);
        auto exp = generate_random_bigint(256, rng);
        auto mod = generate_random_bigint(256, rng);
        Benchmark bench("BigInt ModPow (256-bit, full)");
        bench.warmup([&]() { auto r = base.mod_pow(exp, mod); do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = base.mod_pow(exp, mod);
            do_not_optimize(r);
        }, 100);
        res.print();
    }

    // RSA-sized modpow (2048-bit)
    {
        auto base = generate_random_bigint(2048, rng);
        auto exp = generate_random_bigint(64, rng);  // Small exponent for speed
        auto mod = generate_random_bigint(2048, rng);
        Benchmark bench("BigInt ModPow (2048-bit base, 64-bit exp)");
        bench.warmup([&]() { auto r = base.mod_pow(exp, mod); do_not_optimize(r); }, 10);
        auto res = bench.run([&]() {
            auto r = base.mod_pow(exp, mod);
            do_not_optimize(r);
        }, 100);
        res.print();
    }

    print_separator();
}

// ============================================================================
// GCD Benchmarks
// ============================================================================

void run_gcd_benchmarks() {
    print_header("GCD Benchmarks");

    std::mt19937_64 rng(42);

    // 64-bit GCD
    {
        auto [a, b] = generate_pair(64, rng);
        Benchmark bench("BigInt GCD (64-bit)");
        bench.warmup([&]() { auto r = BigInt::gcd(a, b); do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = BigInt::gcd(a, b);
            do_not_optimize(r);
        }, 100000);
        res.print();
    }

    // 256-bit GCD
    {
        auto [a, b] = generate_pair(256, rng);
        Benchmark bench("BigInt GCD (256-bit)");
        bench.warmup([&]() { auto r = BigInt::gcd(a, b); do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = BigInt::gcd(a, b);
            do_not_optimize(r);
        }, 10000);
        res.print();
    }

    // 1024-bit GCD
    {
        auto [a, b] = generate_pair(1024, rng);
        Benchmark bench("BigInt GCD (1024-bit)");
        bench.warmup([&]() { auto r = BigInt::gcd(a, b); do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = BigInt::gcd(a, b);
            do_not_optimize(r);
        }, 1000);
        res.print();
    }

    // 4096-bit GCD
    {
        auto [a, b] = generate_pair(4096, rng);
        Benchmark bench("BigInt GCD (4096-bit)");
        bench.warmup([&]() { auto r = BigInt::gcd(a, b); do_not_optimize(r); }, 100);
        auto res = bench.run([&]() {
            auto r = BigInt::gcd(a, b);
            do_not_optimize(r);
        }, 100);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Shift and Bitwise Benchmarks
// ============================================================================

void run_bitwise_benchmarks() {
    print_header("Shift and Bitwise Benchmarks");

    std::mt19937_64 rng(42);

    // Left shift
    {
        auto a = generate_random_bigint(1024, rng);
        Benchmark bench("BigInt Left Shift (1024-bit << 64)");
        bench.warmup([&]() { auto r = a << 64; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a << 64;
            do_not_optimize(r);
        }, 100000);
        res.print();
    }

    // Right shift
    {
        auto a = generate_random_bigint(1024, rng);
        Benchmark bench("BigInt Right Shift (1024-bit >> 64)");
        bench.warmup([&]() { auto r = a >> 64; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a >> 64;
            do_not_optimize(r);
        }, 100000);
        res.print();
    }

    // Bitwise AND
    {
        auto [a, b] = generate_pair(1024, rng);
        Benchmark bench("BigInt AND (1024-bit)");
        bench.warmup([&]() { auto r = a & b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a & b;
            do_not_optimize(r);
        }, 100000);
        res.print();
    }

    // Bitwise OR
    {
        auto [a, b] = generate_pair(1024, rng);
        Benchmark bench("BigInt OR (1024-bit)");
        bench.warmup([&]() { auto r = a | b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a | b;
            do_not_optimize(r);
        }, 100000);
        res.print();
    }

    // Bitwise XOR
    {
        auto [a, b] = generate_pair(1024, rng);
        Benchmark bench("BigInt XOR (1024-bit)");
        bench.warmup([&]() { auto r = a ^ b; do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = a ^ b;
            do_not_optimize(r);
        }, 100000);
        res.print();
    }

    print_separator();
}

// ============================================================================
// Conversion Benchmarks
// ============================================================================

void run_conversion_benchmarks() {
    print_header("Conversion Benchmarks");

    std::mt19937_64 rng(42);

    // String to BigInt (256-bit)
    {
        auto n = generate_random_bigint(256, rng);
        std::string str = n.to_string();
        Benchmark bench("Parse Decimal (256-bit)");
        bench.warmup([&]() { BigInt r(str); do_not_optimize(r); });
        auto res = bench.run([&]() {
            BigInt r(str);
            do_not_optimize(r);
        }, 50000);
        res.print();
    }

    // String to BigInt (1024-bit)
    {
        auto n = generate_random_bigint(1024, rng);
        std::string str = n.to_string();
        Benchmark bench("Parse Decimal (1024-bit)");
        bench.warmup([&]() { BigInt r(str); do_not_optimize(r); });
        auto res = bench.run([&]() {
            BigInt r(str);
            do_not_optimize(r);
        }, 10000);
        res.print();
    }

    // BigInt to string (256-bit)
    {
        auto n = generate_random_bigint(256, rng);
        Benchmark bench("To Decimal String (256-bit)");
        bench.warmup([&]() { auto r = n.to_string(); do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = n.to_string();
            do_not_optimize(r);
        }, 50000);
        res.print();
    }

    // BigInt to string (1024-bit)
    {
        auto n = generate_random_bigint(1024, rng);
        Benchmark bench("To Decimal String (1024-bit)");
        bench.warmup([&]() { auto r = n.to_string(); do_not_optimize(r); });
        auto res = bench.run([&]() {
            auto r = n.to_string();
            do_not_optimize(r);
        }, 10000);
        res.print();
    }

    // Hex parsing (faster than decimal)
    {
        auto n = generate_random_bigint(1024, rng);
        std::string hex = n.to_hex_string();
        Benchmark bench("Parse Hex (1024-bit)");
        bench.warmup([&]() { BigInt r(hex); do_not_optimize(r); });
        auto res = bench.run([&]() {
            BigInt r(hex);
            do_not_optimize(r);
        }, 50000);
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
    std::cout << "  DotVM BigInt Benchmark Suite\n";
    print_separator('=', 80);
    std::cout << "\n";

    std::cout << "BigInt Configuration:\n";
    std::cout << "  - Limb size: " << BigInt::limb_bits << " bits\n";
    std::cout << "  - Limb type: uint64_t\n";
    std::cout << "\n";

    // Run all benchmarks
    run_native_benchmarks();
    run_addition_benchmarks();
    run_multiplication_benchmarks();
    run_division_benchmarks();
    run_modpow_benchmarks();
    run_gcd_benchmarks();
    run_bitwise_benchmarks();
    run_conversion_benchmarks();

    // Summary
    std::cout << "\n";
    print_separator('=', 80);
    std::cout << "  Benchmark Complete\n";
    print_separator('=', 80);
    std::cout << "\n";
    std::cout << "Notes:\n";
    std::cout << "  - Multiplication uses Karatsuba algorithm for large numbers\n";
    std::cout << "  - Division uses Knuth's Algorithm D\n";
    std::cout << "  - ModPow uses binary exponentiation (square-and-multiply)\n";
    std::cout << "  - GCD uses binary GCD algorithm\n";
    std::cout << "\n";

    return 0;
}
