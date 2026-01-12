/// @file bitwise_bench.cpp
/// @brief Performance benchmarks for bitwise opcodes
///
/// Benchmarks for AND, OR, XOR, NOT, SHL, SHR, SAR, ROL, ROR operations
/// and their immediate variants (SHLI, SHRI, SARI, ANDI, ORI, XORI).

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

/// Encode instruction to bytes and append to code vector
void append_instr(std::vector<std::uint8_t>& code, std::uint32_t instr) {
    code.push_back(static_cast<std::uint8_t>(instr & 0xFF));
    code.push_back(static_cast<std::uint8_t>((instr >> 8) & 0xFF));
    code.push_back(static_cast<std::uint8_t>((instr >> 16) & 0xFF));
    code.push_back(static_cast<std::uint8_t>((instr >> 24) & 0xFF));
}

/// Create a loop of a single bitwise instruction
std::vector<std::uint8_t> make_bitwise_loop(std::uint8_t opcode, std::size_t count) {
    std::vector<std::uint8_t> code;
    code.reserve((count + 1) * 4);

    for (std::size_t i = 0; i < count; ++i) {
        auto instr = encode_type_a(opcode, 1, 1, 2);
        append_instr(code, instr);
    }

    // HALT at the end
    append_instr(code, encode_type_c(opcode::HALT, 0));

    return code;
}

/// Create a loop of shift-immediate instructions
std::vector<std::uint8_t> make_shift_imm_loop(std::uint8_t opcode, std::uint8_t shamt,
                                               std::size_t count) {
    std::vector<std::uint8_t> code;
    code.reserve((count + 1) * 4);

    for (std::size_t i = 0; i < count; ++i) {
        auto instr = encode_type_s(opcode, 1, 1, shamt);
        append_instr(code, instr);
    }

    append_instr(code, encode_type_c(opcode::HALT, 0));

    return code;
}

/// Create a mixed bitwise operation loop
std::vector<std::uint8_t> make_mixed_bitwise_loop(std::size_t iterations) {
    std::vector<std::uint8_t> code;

    for (std::size_t i = 0; i < iterations; ++i) {
        // AND R1, R1, R2
        // OR R3, R3, R4
        // XOR R5, R5, R6
        // SHL R7, R7, R8
        std::array<std::uint32_t, 4> ops = {
            encode_type_a(opcode::AND, 1, 1, 2),
            encode_type_a(opcode::OR, 3, 3, 4),
            encode_type_a(opcode::XOR, 5, 5, 6),
            encode_type_a(opcode::SHL, 7, 7, 8),
        };

        for (auto instr : ops) {
            append_instr(code, instr);
        }
    }

    append_instr(code, encode_type_c(opcode::HALT, 0));

    return code;
}

/// Create a rotate operations loop
std::vector<std::uint8_t> make_rotate_loop(std::size_t iterations) {
    std::vector<std::uint8_t> code;

    for (std::size_t i = 0; i < iterations; ++i) {
        // ROL R1, R1, R2
        // ROR R3, R3, R4
        std::array<std::uint32_t, 2> ops = {
            encode_type_a(opcode::ROL, 1, 1, 2),
            encode_type_a(opcode::ROR, 3, 3, 4),
        };

        for (auto instr : ops) {
            append_instr(code, instr);
        }
    }

    append_instr(code, encode_type_c(opcode::HALT, 0));

    return code;
}

// ============================================================================
// Single Operation Benchmarks - Type A (Register-Register)
// ============================================================================

static void BM_SingleAND(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x12345678LL));
    ctx.registers().write(2, Value::from_int(0x00FF00FFLL));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::AND, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleAND);

static void BM_SingleOR(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x0F0F0F0F0F0F0F0FLL));
    ctx.registers().write(2, Value::from_int(0x00F0F0F0F0F0F0F0LL));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::OR, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleOR);

static void BM_SingleXOR(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x5555555555555555LL));
    ctx.registers().write(2, Value::from_int(0x5555555555555555LL));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::XOR, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleXOR);

static void BM_SingleNOT(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x123456789LL));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::NOT, 3, 1, 0));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleNOT);

static void BM_SingleSHL(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(1));
    ctx.registers().write(2, Value::from_int(16));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::SHL, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleSHL);

static void BM_SingleSHR(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x00FF000000000000LL));
    ctx.registers().write(2, Value::from_int(48));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::SHR, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleSHR);

static void BM_SingleSAR(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(-1024));  // Negative number for SAR
    ctx.registers().write(2, Value::from_int(4));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::SAR, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleSAR);

static void BM_SingleROL(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x7000000000000001LL));  // Safe positive value
    ctx.registers().write(2, Value::from_int(4));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::ROL, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleROL);

static void BM_SingleROR(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x7000000000000001LL));  // Safe positive value
    ctx.registers().write(2, Value::from_int(4));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_a(encode_type_a(opcode::ROR, 3, 1, 2));

    for (auto _ : state) {
        (void)exec.execute_type_a(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleROR);

// ============================================================================
// Single Operation Benchmarks - Type S (Shift-Immediate)
// ============================================================================

static void BM_SingleSHLI(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(1));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_s(encode_type_s(opcode::SHLI, 3, 1, 16));

    for (auto _ : state) {
        (void)exec.execute_type_s(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleSHLI);

static void BM_SingleSHRI(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x00FF0000LL));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_s(encode_type_s(opcode::SHRI, 3, 1, 16));

    for (auto _ : state) {
        (void)exec.execute_type_s(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleSHRI);

static void BM_SingleSARI(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(-65536));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_s(encode_type_s(opcode::SARI, 3, 1, 8));

    for (auto _ : state) {
        (void)exec.execute_type_s(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(3));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleSARI);

// ============================================================================
// Single Operation Benchmarks - Type B (Accumulator-Immediate)
// ============================================================================

static void BM_SingleANDI(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x7FFFFFFFLL));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_b(encode_type_b(opcode::ANDI, 1, 0x00FF));

    for (auto _ : state) {
        (void)exec.execute_type_b(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(1));
        // Reset for next iteration
        ctx.registers().write(1, Value::from_int(0x7FFFFFFFLL));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleANDI);

static void BM_SingleORI(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_b(encode_type_b(opcode::ORI, 1, 0xFF00));

    for (auto _ : state) {
        (void)exec.execute_type_b(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(1));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleORI);

static void BM_SingleXORI(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0xFFFF));

    BitwiseExecutor exec{ctx};
    auto decoded = decode_type_b(encode_type_b(opcode::XORI, 1, 0xFFFF));

    for (auto _ : state) {
        (void)exec.execute_type_b(decoded);
        benchmark::DoNotOptimize(ctx.registers().read(1));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SingleXORI);

// ============================================================================
// Throughput Benchmarks
// ============================================================================

static void BM_ANDLoop_10K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x7FFFFFFFFFFFFFFFLL));
    ctx.registers().write(2, Value::from_int(0x7FFFFFFFFFFFFFFELL));

    auto code = make_bitwise_loop(opcode::AND, 10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));

        // Reset
        ctx.registers().write(1, Value::from_int(0x7FFFFFFFFFFFFFFFLL));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_ANDLoop_10K);

static void BM_SHLLoop_10K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(1));
    ctx.registers().write(2, Value::from_int(0));  // Shift by 0 to prevent overflow

    auto code = make_bitwise_loop(opcode::SHL, 10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));

        ctx.registers().write(1, Value::from_int(1));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_SHLLoop_10K);

static void BM_ROLLoop_10K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x7000000000000001LL));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_bitwise_loop(opcode::ROL, 10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));

        ctx.registers().write(1, Value::from_int(0x7000000000000001LL));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_ROLLoop_10K);

static void BM_SHLILoop_10K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(1));

    auto code = make_shift_imm_loop(opcode::SHLI, 0, 10000);  // Shift by 0

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_SHLILoop_10K);

// ============================================================================
// Mixed Operations Benchmarks
// ============================================================================

static void BM_MixedBitwise_10K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};

    // Initialize registers
    ctx.registers().write(1, Value::from_int(0x7FFFFFFFLL));
    ctx.registers().write(2, Value::from_int(0x0F0F0F0FLL));
    ctx.registers().write(3, Value::from_int(0));
    ctx.registers().write(4, Value::from_int(0x70F0F0F0LL));
    ctx.registers().write(5, Value::from_int(0x5AAAAAAAALL));
    ctx.registers().write(6, Value::from_int(0x55555555LL));
    ctx.registers().write(7, Value::from_int(1));
    ctx.registers().write(8, Value::from_int(0));  // Shift by 0

    auto code = make_mixed_bitwise_loop(10000);  // 40K operations total

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));

        // Reset
        ctx.registers().write(1, Value::from_int(0x7FFFFFFFLL));
        ctx.registers().write(3, Value::from_int(0));
        ctx.registers().write(5, Value::from_int(0x5AAAAAAAALL));
        ctx.registers().write(7, Value::from_int(1));
    }

    state.SetItemsProcessed(state.iterations() * 40000);
}

BENCHMARK(BM_MixedBitwise_10K);

static void BM_RotateLoop_10K(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x7000000000000001LL));
    ctx.registers().write(2, Value::from_int(1));
    ctx.registers().write(3, Value::from_int(0x0000000000000003LL));
    ctx.registers().write(4, Value::from_int(1));

    auto code = make_rotate_loop(10000);  // 20K operations total

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
        benchmark::DoNotOptimize(ctx.registers().read(3));

        // Reset
        ctx.registers().write(1, Value::from_int(0x7000000000000001LL));
        ctx.registers().write(3, Value::from_int(0x0000000000000003LL));
    }

    state.SetItemsProcessed(state.iterations() * 20000);
}

BENCHMARK(BM_RotateLoop_10K);

// ============================================================================
// Architecture Comparison
// ============================================================================

static void BM_AND_Arch32(benchmark::State& state) {
    VmContext ctx{VmConfig::arch32()};
    ctx.registers().write(1, Value::from_int(0x7FFFFFFF));
    ctx.registers().write(2, Value::from_int(0x0F0F0F0F));

    auto code = make_bitwise_loop(opcode::AND, 10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
        ctx.registers().write(1, Value::from_int(0x7FFFFFFF));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_AND_Arch32);

static void BM_AND_Arch64(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x7FFFFFFFFFFFFFFFLL));
    ctx.registers().write(2, Value::from_int(0x0F0F0F0F0F0F0F0FLL));

    auto code = make_bitwise_loop(opcode::AND, 10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
        ctx.registers().write(1, Value::from_int(0x7FFFFFFFFFFFFFFFLL));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_AND_Arch64);

static void BM_ROL_Arch32(benchmark::State& state) {
    VmContext ctx{VmConfig::arch32()};
    ctx.registers().write(1, Value::from_int(0x70000001));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_bitwise_loop(opcode::ROL, 10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
        ctx.registers().write(1, Value::from_int(0x70000001));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_ROL_Arch32);

static void BM_ROL_Arch64(benchmark::State& state) {
    VmContext ctx{VmConfig::arch64()};
    ctx.registers().write(1, Value::from_int(0x7000000000000001LL));
    ctx.registers().write(2, Value::from_int(1));

    auto code = make_bitwise_loop(opcode::ROL, 10000);

    for (auto _ : state) {
        Executor exec{ctx, code};
        (void)exec.run();
        benchmark::DoNotOptimize(ctx.registers().read(1));
        ctx.registers().write(1, Value::from_int(0x7000000000000001LL));
    }

    state.SetItemsProcessed(state.iterations() * 10000);
}

BENCHMARK(BM_ROL_Arch64);

}  // namespace
}  // namespace dotvm::core
