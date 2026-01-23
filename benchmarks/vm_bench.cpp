/// @file vm_bench.cpp
/// @brief TOOL-010 VM component benchmarks using Google Benchmark
///
/// Benchmarks for core DotVM components:
/// - BigInt::fibonacci(n) computation
/// - Quicksort on random integers
/// - SHA-256 hashing throughput
/// - RegisterFile read/write operations
/// - MemoryManager allocate/read/write operations
///
/// Part of TOOL-010: CLI Benchmark Suite

#include <algorithm>
#include <benchmark/benchmark.h>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include "dotvm/core/bigint/bigint.hpp"
#include "dotvm/core/crypto/sha256.hpp"
#include "dotvm/core/memory.hpp"
#include "dotvm/core/register_file.hpp"
#include "dotvm/core/value.hpp"

namespace dotvm::bench {
namespace {

// ============================================================================
// Fibonacci Benchmarks (BigInt)
// ============================================================================

static void BM_Fibonacci(benchmark::State& state) {
    const auto n = static_cast<std::uint64_t>(state.range(0));

    for (auto _ : state) {
        auto result = core::bigint::BigInt::fibonacci(n);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}

// Benchmark fibonacci for various values of n
BENCHMARK(BM_Fibonacci)->Arg(10)->Arg(20)->Arg(35)->Arg(50)->Arg(100);

// ============================================================================
// Quicksort Benchmarks
// ============================================================================

/// Generate a vector of random integers for sorting benchmarks
static std::vector<int> generate_random_ints(std::size_t count, std::uint32_t seed = 42) {
    std::vector<int> data(count);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 1000000);

    for (auto& val : data) {
        val = dist(rng);
    }
    return data;
}

static void BM_Quicksort(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));

    // Generate fresh data for each iteration
    const auto template_data = generate_random_ints(count);

    for (auto _ : state) {
        // Copy data for this iteration (sorting is in-place)
        std::vector<int> data = template_data;
        std::sort(data.begin(), data.end());
        benchmark::DoNotOptimize(data.data());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
}

// Benchmark quicksort for various sizes
BENCHMARK(BM_Quicksort)->Arg(100)->Arg(1000)->Arg(10000)->Arg(100000);

// ============================================================================
// SHA-256 Benchmarks
// ============================================================================

/// Generate random bytes for hashing benchmarks
static std::vector<std::uint8_t> generate_random_bytes(std::size_t count, std::uint32_t seed = 42) {
    std::vector<std::uint8_t> data(count);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);

    for (auto& byte : data) {
        byte = static_cast<std::uint8_t>(dist(rng));
    }
    return data;
}

static void BM_SHA256(benchmark::State& state) {
    const auto size = static_cast<std::size_t>(state.range(0));
    const auto data = generate_random_bytes(size);

    for (auto _ : state) {
        auto digest = core::crypto::Sha256::hash(std::span<const std::uint8_t>(data));
        benchmark::DoNotOptimize(digest);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(size));
    state.SetItemsProcessed(state.iterations());
}

// Benchmark SHA-256 for various data sizes (bytes)
// Target: >100 MB/sec for 1MB input
BENCHMARK(BM_SHA256)->Arg(64)->Arg(1024)->Arg(65536)->Arg(1048576);

// ============================================================================
// RegisterFile Benchmarks
// ============================================================================

static void BM_RegisterReadWrite(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    core::RegisterFile regs;

    // Pre-fill registers with some values
    for (std::uint8_t i = 1; i < 32; ++i) {
        regs.write(i, core::Value::from_int(static_cast<std::int64_t>(i)));
    }

    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            // Read from one register, write to another
            std::uint8_t src = static_cast<std::uint8_t>((i % 31) + 1);
            std::uint8_t dst = static_cast<std::uint8_t>(((i + 1) % 31) + 1);

            auto val = regs.read(src);
            regs.write(dst, core::Value::from_int(val.as_integer() + 1));
        }
        benchmark::DoNotOptimize(regs.read(1));
    }

    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count) *
                            2);  // reads + writes
}

// Benchmark register operations for various iteration counts
BENCHMARK(BM_RegisterReadWrite)->Arg(100)->Arg(1000)->Arg(10000);

// ============================================================================
// MemoryManager Benchmarks
// ============================================================================

static void BM_MemoryReadWrite(benchmark::State& state) {
    const auto count = static_cast<std::size_t>(state.range(0));
    core::MemoryManager mem;

    // Allocate a block of memory
    auto handle_result = mem.allocate(count * sizeof(std::int64_t));
    if (!handle_result) {
        state.SkipWithError("Failed to allocate memory");
        return;
    }
    auto handle = *handle_result;

    // Pre-fill with values
    for (std::size_t i = 0; i < count; ++i) {
        (void)mem.write<std::int64_t>(handle, i * sizeof(std::int64_t),
                                      static_cast<std::int64_t>(i));
    }

    for (auto _ : state) {
        for (std::size_t i = 0; i < count; ++i) {
            // Read, modify, write back
            auto read_result = mem.read<std::int64_t>(handle, i * sizeof(std::int64_t));
            if (read_result) {
                (void)mem.write<std::int64_t>(handle, i * sizeof(std::int64_t), *read_result + 1);
            }
        }
        benchmark::ClobberMemory();
    }

    (void)mem.deallocate(handle);

    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count) *
                            2);  // reads + writes
}

// Benchmark memory read/write operations
BENCHMARK(BM_MemoryReadWrite)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_MemoryAllocate(benchmark::State& state) {
    const auto size = static_cast<std::size_t>(state.range(0));
    core::MemoryManager mem;

    for (auto _ : state) {
        auto handle_result = mem.allocate(size);
        if (handle_result) {
            (void)mem.deallocate(*handle_result);
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * 2);  // allocate + deallocate
}

// Benchmark memory allocation/deallocation for various sizes
BENCHMARK(BM_MemoryAllocate)->Arg(64)->Arg(4096)->Arg(65536)->Arg(1048576);

// ============================================================================
// Value Type Benchmarks
// ============================================================================

static void BM_ValueCreation(benchmark::State& state) {
    for (auto _ : state) {
        auto v1 = core::Value::from_int(42);
        auto v2 = core::Value::from_float(3.14);
        auto v3 = core::Value::from_bool(true);
        auto v4 = core::Value::nil();

        benchmark::DoNotOptimize(v1);
        benchmark::DoNotOptimize(v2);
        benchmark::DoNotOptimize(v3);
        benchmark::DoNotOptimize(v4);
    }

    state.SetItemsProcessed(state.iterations() * 4);
}

BENCHMARK(BM_ValueCreation);

static void BM_ValueTypeCheck(benchmark::State& state) {
    const auto values = std::array{
        core::Value::from_int(42),
        core::Value::from_float(3.14),
        core::Value::from_bool(true),
        core::Value::nil(),
    };

    for (auto _ : state) {
        for (const auto& v : values) {
            benchmark::DoNotOptimize(v.is_integer());
            benchmark::DoNotOptimize(v.is_float());
            benchmark::DoNotOptimize(v.is_bool());
            benchmark::DoNotOptimize(v.is_nil());
        }
    }

    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(values.size() * 4));
}

BENCHMARK(BM_ValueTypeCheck);

}  // namespace
}  // namespace dotvm::bench

// Google Benchmark main is provided by benchmark::benchmark_main
