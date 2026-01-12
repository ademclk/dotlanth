/// @file arithmetic_bench.cpp
/// @brief Performance benchmarks for arithmetic opcodes
///
/// Target: >100M ops/sec throughput

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <vector>

#include "dotvm/core/executor.hpp"
#include "dotvm/core/instruction.hpp"
#include "dotvm/core/opcode.hpp"

namespace dotvm::core {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

/// Create a loop of ADD instructions
std::vector<std::uint8_t> make_add_loop(std::size_t count) {
    std::vector<std::uint8_t> code;
    code.reserve((count + 1) * 4);

    for (std::size_t i = 0; i < count; ++i) {
        auto instr = encode_type_a(opcode::ADD, 1, 1, 2);
        code.push_back(static_cast<std::uint8_t>(instr & 0xFF));
        code.push_back(static_cast<std::uint8_t>((instr >> 8) & 0xFF));
        code.push_back(static_cast<std::uint8_t>((instr >> 16) & 0xFF));
        code.push_back(static_cast<std::uint8_t>((instr >> 24) & 0xFF));
    }

    // HALT at the end
    auto halt = encode_type_c(opcode::HALT, 0);
    code.push_back(static_cast<std::uint8_t>(halt & 0xFF));
    code.push_back(static_cast<std::uint8_t>((halt >> 8) & 0xFF));
    code.push_back(static_cast<std::uint8_t>((halt >> 16) & 0xFF));
    code.push_back(static_cast<std::uint8_t>((halt >> 24) & 0xFF));

    return code;
}

/// Create a mixed arithmetic loop
std::vector<std::uint8_t> make_mixed_loop(std::size_t iterations) {
    std::vector<std::uint8_t> code;

    for (std::size_t i = 0; i < iterations; ++i) {
        // R1 += R2, R3 -= R4, R5 *= R6
        std::array<std::uint32_t, 3> ops = {
            encode_type_a(opcode::ADD, 1, 1, 2),
            encode_type_a(opcode::SUB, 3, 3, 4),
            encode_type_a(opcode::MUL, 5, 5, 6),
        };

        for (auto instr : ops) {
            code.push_back(static_cast<std::uint8_t>(instr & 0xFF));
            code.push_back(static_cast<std::uint8_t>((instr >> 8) & 0xFF));
            code.push_back(static_cast<std::uint8_t>((instr >> 16) & 0xFF));
            code.push_back(static_cast<std::uint8_t>((instr >> 24) & 0xFF));
        }
    }

    // HALT
    auto halt = encode_type_c(opcode::HALT, 0);
    code.push_back(static_cast<std::uint8_t>(halt & 0xFF));
    code.push_back(static_cast<std::uint8_t>((halt >> 8) & 0xFF));
    code.push_back(static_cast<std::uint8_t>((halt >> 16) & 0xFF));
    code.push_back(static_cast<std::uint8_t>((halt >> 24) & 0xFF));

    return code;
}

// ============================================================================
// Single Operation Benchmarks
// ============================================================================

static void BM_SingleADD(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(1000));
    ctx.registers().write(2, Value::from_int(1));

    ArithmeticExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::ADD, 1, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(1));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleADD);

static void BM_SingleSUB(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(1000000));
    ctx.registers().write(2, Value::from_int(1));

    ArithmeticExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::SUB, 1, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(1));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleSUB);

static void BM_SingleMUL(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(2));
    ctx.registers().write(2, Value::from_int(1));  // Multiply by 1 to avoid overflow

    ArithmeticExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::MUL, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleMUL);

static void BM_SingleDIV(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(1000000));
    ctx.registers().write(2, Value::from_int(7));

    ArithmeticExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::DIV, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleDIV);

static void BM_SingleADDI(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0));

    ArithmeticExecutor exec{ctx};
    auto decoded = decode_type_b(encode_type_b(opcode::ADDI, 1, 1));

    for (auto _ : state) {
        (void)exec.execute_type_b(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(1));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleADDI);

// ============================================================================
// Throughput Benchmarks
// ============================================================================

static void BM_ADDLoop_1K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_add_loop(1000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));

        // Reset for next iteration
        ctx.registers().write(1, Value::from_int(0));
    }

    state.SetItemsProcessed(state.iterations() * 1000);
}

BENCHMARK(BM_ADDLoop_1K);

static void BM_ADDLoop_10K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_add_loop(10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));

        ctx.registers().write(1, Value::from_int(0));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_ADDLoop_10K);

static void BM_ADDLoop_100K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_add_loop(100000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));

        ctx.registers().write(1, Value::from_int(0));
    }

    state.SetItemsProcessed(state.iterations() * 100000);
}

BENCHMARK(BM_ADDLoop_100K);

// ============================================================================
// Mixed Operation Benchmarks
// ============================================================================

static void BM_MixedLoop_10K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};

    // Initialize registers
    ctx.registers().write(1, Value::from_int(0));
    ctx.registers().write(2, Value::from_int(1));
    ctx.registers().write(3, Value::from_int(1000000));
    ctx.registers().write(4, Value::from_int(1));
    ctx.registers().write(5, Value::from_int(1));
    ctx.registers().write(6, Value::from_int(1));

    auto code = make_mixed_loop(10000);  // 30K operations total

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));

        // Reset
        ctx.registers().write(1, Value::from_int(0));
        ctx.registers().write(3, Value::from_int(1000000));
        ctx.registers().write(5, Value::from_int(1));
    }

    state.SetItemsProcessed(state.iterations() * 30000);
}

BENCHMARK(BM_MixedLoop_10K);

// ============================================================================
// Strict Mode Comparison
// ============================================================================

static void BM_ADD_NonStrict(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(1));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_add_loop(10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
        ctx.registers().write(1, Value::from_int(1));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_ADD_NonStrict);

static void BM_ADD_Strict(benchmark::State& state) {
    auto config = VmConfig::arch64();
    config.strict_overflow = true;
    VmContext ctx{config};
    ctx.registers().write(1, Value::from_int(1));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_add_loop(10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
        ctx.registers().write(1, Value::from_int(1));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_ADD_Strict);

// ============================================================================
// Architecture Comparison
// ============================================================================

static void BM_ADD_Arch32(benchmark::State& state) {
    VmContext ctx{VmConfig::arch32()};
    ctx.registers().write(1, Value::from_int(0));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_add_loop(10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
        ctx.registers().write(1, Value::from_int(0));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_ADD_Arch32);

static void BM_ADD_Arch64(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_add_loop(10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
        ctx.registers().write(1, Value::from_int(0));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_ADD_Arch64);

// ============================================================================
// Fetch-Decode-Execute Cycle Benchmarks
// ============================================================================

static void BM_FetchDecode(benchmark::State& state) {
    auto code = make_add_loop(1);

    for (auto _ : state) {
        // Measure just fetching and decoding
        const auto* ptr = code.data();
        std::uint32_t instr = static_cast<std::uint32_t>(ptr[0]) |
                              (static_cast<std::uint32_t>(ptr[1]) << 8) |
                              (static_cast<std::uint32_t>(ptr[2]) << 16) |
                              (static_cast<std::uint32_t>(ptr[3]) << 24);

        auto decoded = decode_type_a(instr);
        benchmark::DoNotOptimize(decoded);
    }
}

BENCHMARK(BM_FetchDecode);

}  // namespace
}  // namespace dotvm::core
