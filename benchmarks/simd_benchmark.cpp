/// @file simd_benchmark.cpp
/// @brief SIMD performance benchmarks for DotVM
///
/// Benchmarks comparing scalar vs SIMD operations at various vector widths:
/// - Vector addition (VADD)
/// - Vector multiplication (VMUL)
/// - Vector dot product (VDOT)
/// - Memory bandwidth (VLOAD/VSTORE)
/// - Fused multiply-add (FMA)
///
/// Part of CORE-008: Performance Benchmarks for the dotlanth VM

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

#include "dotvm/core/arch_types.hpp"
#include "dotvm/core/bench/benchmark_config.hpp"
#include "dotvm/core/simd/cpu_features.hpp"
#include "dotvm/core/simd/vector_types.hpp"

using namespace dotvm::core;
using namespace dotvm::core::bench;
using namespace dotvm::core::simd;

// ============================================================================
// Test Data Generation
// ============================================================================

/// Generate random float data for benchmarks
template <std::size_t N>
std::array<float, N> generate_float_data() {
    std::array<float, N> data;
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (auto& val : data) {
        val = dist(rng);
    }
    return data;
}

/// Generate random int32 data for benchmarks
template <std::size_t N>
std::array<std::int32_t, N> generate_int_data() {
    std::array<std::int32_t, N> data;
    std::mt19937 rng(42);
    std::uniform_int_distribution<std::int32_t> dist(-1000, 1000);

    for (auto& val : data) {
        val = dist(rng);
    }
    return data;
}

// ============================================================================
// Scalar Operations (Baseline)
// ============================================================================

/// Scalar vector addition
template <typename T, std::size_t N>
void scalar_add(const T* a, const T* b, T* result) {
    for (std::size_t i = 0; i < N; ++i) {
        result[i] = a[i] + b[i];
    }
}

/// Scalar vector multiplication
template <typename T, std::size_t N>
void scalar_mul(const T* a, const T* b, T* result) {
    for (std::size_t i = 0; i < N; ++i) {
        result[i] = a[i] * b[i];
    }
}

/// Scalar dot product
template <typename T, std::size_t N>
T scalar_dot(const T* a, const T* b) {
    T sum = T{0};
    for (std::size_t i = 0; i < N; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

/// Scalar FMA (fused multiply-add): result = a * b + c
template <typename T, std::size_t N>
void scalar_fma(const T* a, const T* b, const T* c, T* result) {
    for (std::size_t i = 0; i < N; ++i) {
        result[i] = a[i] * b[i] + c[i];
    }
}

// ============================================================================
// SIMD Operations using Vector<> template
// ============================================================================

/// SIMD vector addition using Vector<Width, Lane>
template <std::size_t Width, typename Lane>
void simd_add(const Vector<Width, Lane>& a, const Vector<Width, Lane>& b,
              Vector<Width, Lane>& result) {
    for (std::size_t i = 0; i < Vector<Width, Lane>::LANE_COUNT; ++i) {
        result[i] = a[i] + b[i];
    }
}

/// SIMD vector multiplication using Vector<Width, Lane>
template <std::size_t Width, typename Lane>
void simd_mul(const Vector<Width, Lane>& a, const Vector<Width, Lane>& b,
              Vector<Width, Lane>& result) {
    for (std::size_t i = 0; i < Vector<Width, Lane>::LANE_COUNT; ++i) {
        result[i] = a[i] * b[i];
    }
}

/// SIMD dot product using Vector<Width, Lane>
template <std::size_t Width, typename Lane>
Lane simd_dot(const Vector<Width, Lane>& a, const Vector<Width, Lane>& b) {
    Vector<Width, Lane> prod;
    simd_mul(a, b, prod);
    return prod.horizontal_sum();
}

/// SIMD FMA using Vector<Width, Lane>
template <std::size_t Width, typename Lane>
void simd_fma(const Vector<Width, Lane>& a, const Vector<Width, Lane>& b,
              const Vector<Width, Lane>& c, Vector<Width, Lane>& result) {
    for (std::size_t i = 0; i < Vector<Width, Lane>::LANE_COUNT; ++i) {
        result[i] = a[i] * b[i] + c[i];
    }
}

// ============================================================================
// Memory Bandwidth Benchmarks
// ============================================================================

/// Memory copy benchmark (baseline)
void memory_copy_scalar(const float* src, float* dst, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

/// Memory copy using Vector types
template <std::size_t Width>
void memory_copy_simd(const float* src, float* dst, std::size_t count) {
    constexpr std::size_t lanes = Width / (sizeof(float) * 8);
    const std::size_t vectors = count / lanes;

    for (std::size_t v = 0; v < vectors; ++v) {
        Vector<Width, float> vec;
        for (std::size_t i = 0; i < lanes; ++i) {
            vec[i] = src[v * lanes + i];
        }
        for (std::size_t i = 0; i < lanes; ++i) {
            dst[v * lanes + i] = vec[i];
        }
    }

    // Handle remainder
    for (std::size_t i = vectors * lanes; i < count; ++i) {
        dst[i] = src[i];
    }
}

// ============================================================================
// Benchmark Runners
// ============================================================================

void run_addition_benchmarks() {
    print_header("Vector Addition Benchmarks");

    // Test data sizes
    constexpr std::size_t N = 1024;
    auto data_a = generate_float_data<N>();
    auto data_b = generate_float_data<N>();
    std::array<float, N> result{};

    // Scalar baseline
    {
        Benchmark bench("Scalar Add (1024 floats)");
        bench.warmup([&]() { scalar_add<float, N>(data_a.data(), data_b.data(), result.data()); });
        auto res = bench.run(
            [&]() {
                scalar_add<float, N>(data_a.data(), data_b.data(), result.data());
                do_not_optimize(result.data());
            },
            100000);
        res.print();
    }

    // 128-bit SIMD (4 floats per vector)
    {
        Benchmark bench("SIMD Add 128-bit (4 x float)");
        constexpr std::size_t vec_count = N / 4;
        std::array<Vector128f32, vec_count> va, vb, vr;

        // Load data into vectors
        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                va[i][j] = data_a[i * 4 + j];
                vb[i][j] = data_b[i * 4 + j];
            }
        }

        bench.warmup([&]() {
            for (std::size_t i = 0; i < vec_count; ++i) {
                simd_add(va[i], vb[i], vr[i]);
            }
        });

        auto res = bench.run(
            [&]() {
                for (std::size_t i = 0; i < vec_count; ++i) {
                    simd_add(va[i], vb[i], vr[i]);
                }
                do_not_optimize(vr.data());
            },
            100000);
        res.print();
    }

    // 256-bit SIMD (8 floats per vector)
    {
        Benchmark bench("SIMD Add 256-bit (8 x float)");
        constexpr std::size_t vec_count = N / 8;
        std::array<Vector256f32, vec_count> va, vb, vr;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 8; ++j) {
                va[i][j] = data_a[i * 8 + j];
                vb[i][j] = data_b[i * 8 + j];
            }
        }

        bench.warmup([&]() {
            for (std::size_t i = 0; i < vec_count; ++i) {
                simd_add(va[i], vb[i], vr[i]);
            }
        });

        auto res = bench.run(
            [&]() {
                for (std::size_t i = 0; i < vec_count; ++i) {
                    simd_add(va[i], vb[i], vr[i]);
                }
                do_not_optimize(vr.data());
            },
            100000);
        res.print();
    }

    // 512-bit SIMD (16 floats per vector)
    {
        Benchmark bench("SIMD Add 512-bit (16 x float)");
        constexpr std::size_t vec_count = N / 16;
        std::array<Vector512f32, vec_count> va, vb, vr;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 16; ++j) {
                va[i][j] = data_a[i * 16 + j];
                vb[i][j] = data_b[i * 16 + j];
            }
        }

        bench.warmup([&]() {
            for (std::size_t i = 0; i < vec_count; ++i) {
                simd_add(va[i], vb[i], vr[i]);
            }
        });

        auto res = bench.run(
            [&]() {
                for (std::size_t i = 0; i < vec_count; ++i) {
                    simd_add(va[i], vb[i], vr[i]);
                }
                do_not_optimize(vr.data());
            },
            100000);
        res.print();
    }

    print_separator();
}

void run_multiplication_benchmarks() {
    print_header("Vector Multiplication Benchmarks");

    constexpr std::size_t N = 1024;
    auto data_a = generate_float_data<N>();
    auto data_b = generate_float_data<N>();
    std::array<float, N> result{};

    // Scalar baseline
    {
        Benchmark bench("Scalar Mul (1024 floats)");
        bench.warmup([&]() { scalar_mul<float, N>(data_a.data(), data_b.data(), result.data()); });
        auto res = bench.run(
            [&]() {
                scalar_mul<float, N>(data_a.data(), data_b.data(), result.data());
                do_not_optimize(result.data());
            },
            100000);
        res.print();
    }

    // 128-bit SIMD
    {
        Benchmark bench("SIMD Mul 128-bit (4 x float)");
        constexpr std::size_t vec_count = N / 4;
        std::array<Vector128f32, vec_count> va, vb, vr;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                va[i][j] = data_a[i * 4 + j];
                vb[i][j] = data_b[i * 4 + j];
            }
        }

        auto res = bench.run(
            [&]() {
                for (std::size_t i = 0; i < vec_count; ++i) {
                    simd_mul(va[i], vb[i], vr[i]);
                }
                do_not_optimize(vr.data());
            },
            100000);
        res.print();
    }

    // 256-bit SIMD
    {
        Benchmark bench("SIMD Mul 256-bit (8 x float)");
        constexpr std::size_t vec_count = N / 8;
        std::array<Vector256f32, vec_count> va, vb, vr;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 8; ++j) {
                va[i][j] = data_a[i * 8 + j];
                vb[i][j] = data_b[i * 8 + j];
            }
        }

        auto res = bench.run(
            [&]() {
                for (std::size_t i = 0; i < vec_count; ++i) {
                    simd_mul(va[i], vb[i], vr[i]);
                }
                do_not_optimize(vr.data());
            },
            100000);
        res.print();
    }

    print_separator();
}

void run_dot_product_benchmarks() {
    print_header("Dot Product Benchmarks");

    constexpr std::size_t N = 1024;
    auto data_a = generate_float_data<N>();
    auto data_b = generate_float_data<N>();

    // Scalar baseline
    {
        Benchmark bench("Scalar Dot (1024 floats)");
        bench.warmup([&]() {
            auto r = scalar_dot<float, N>(data_a.data(), data_b.data());
            do_not_optimize(r);
        });
        auto res = bench.run(
            [&]() {
                auto r = scalar_dot<float, N>(data_a.data(), data_b.data());
                do_not_optimize(r);
            },
            100000);
        res.print();
    }

    // 128-bit SIMD dot product
    {
        Benchmark bench("SIMD Dot 128-bit");
        constexpr std::size_t vec_count = N / 4;
        std::array<Vector128f32, vec_count> va, vb;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                va[i][j] = data_a[i * 4 + j];
                vb[i][j] = data_b[i * 4 + j];
            }
        }

        auto res = bench.run(
            [&]() {
                float total = 0.0f;
                for (std::size_t i = 0; i < vec_count; ++i) {
                    total += simd_dot(va[i], vb[i]);
                }
                do_not_optimize(total);
            },
            100000);
        res.print();
    }

    // 256-bit SIMD dot product
    {
        Benchmark bench("SIMD Dot 256-bit");
        constexpr std::size_t vec_count = N / 8;
        std::array<Vector256f32, vec_count> va, vb;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 8; ++j) {
                va[i][j] = data_a[i * 8 + j];
                vb[i][j] = data_b[i * 8 + j];
            }
        }

        auto res = bench.run(
            [&]() {
                float total = 0.0f;
                for (std::size_t i = 0; i < vec_count; ++i) {
                    total += simd_dot(va[i], vb[i]);
                }
                do_not_optimize(total);
            },
            100000);
        res.print();
    }

    print_separator();
}

void run_fma_benchmarks() {
    print_header("Fused Multiply-Add Benchmarks");

    constexpr std::size_t N = 1024;
    auto data_a = generate_float_data<N>();
    auto data_b = generate_float_data<N>();
    auto data_c = generate_float_data<N>();
    std::array<float, N> result{};

    // Scalar baseline
    {
        Benchmark bench("Scalar FMA (1024 floats)");
        bench.warmup([&]() {
            scalar_fma<float, N>(data_a.data(), data_b.data(), data_c.data(), result.data());
        });
        auto res = bench.run(
            [&]() {
                scalar_fma<float, N>(data_a.data(), data_b.data(), data_c.data(), result.data());
                do_not_optimize(result.data());
            },
            100000);
        res.print();
    }

    // 128-bit SIMD FMA
    {
        Benchmark bench("SIMD FMA 128-bit");
        constexpr std::size_t vec_count = N / 4;
        std::array<Vector128f32, vec_count> va, vb, vc, vr;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                va[i][j] = data_a[i * 4 + j];
                vb[i][j] = data_b[i * 4 + j];
                vc[i][j] = data_c[i * 4 + j];
            }
        }

        auto res = bench.run(
            [&]() {
                for (std::size_t i = 0; i < vec_count; ++i) {
                    simd_fma(va[i], vb[i], vc[i], vr[i]);
                }
                do_not_optimize(vr.data());
            },
            100000);
        res.print();
    }

    // 256-bit SIMD FMA
    {
        Benchmark bench("SIMD FMA 256-bit");
        constexpr std::size_t vec_count = N / 8;
        std::array<Vector256f32, vec_count> va, vb, vc, vr;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 8; ++j) {
                va[i][j] = data_a[i * 8 + j];
                vb[i][j] = data_b[i * 8 + j];
                vc[i][j] = data_c[i * 8 + j];
            }
        }

        auto res = bench.run(
            [&]() {
                for (std::size_t i = 0; i < vec_count; ++i) {
                    simd_fma(va[i], vb[i], vc[i], vr[i]);
                }
                do_not_optimize(vr.data());
            },
            100000);
        res.print();
    }

    print_separator();
}

void run_memory_benchmarks() {
    print_header("Memory Bandwidth Benchmarks");

    constexpr std::size_t N = 65536;  // 64K floats = 256KB
    std::vector<float> src(N);
    std::vector<float> dst(N);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& val : src) {
        val = dist(rng);
    }

    constexpr std::size_t bytes = N * sizeof(float);

    // Scalar copy
    {
        Benchmark bench("Scalar Copy (256 KB)");
        bench.warmup([&]() { memory_copy_scalar(src.data(), dst.data(), N); });
        auto res = bench.run_throughput(
            [&]() {
                memory_copy_scalar(src.data(), dst.data(), N);
                do_not_optimize(dst.data());
            },
            bytes, 10000);
        res.print();
    }

    // 128-bit SIMD copy
    {
        Benchmark bench("SIMD Copy 128-bit (256 KB)");
        bench.warmup([&]() { memory_copy_simd<128>(src.data(), dst.data(), N); });
        auto res = bench.run_throughput(
            [&]() {
                memory_copy_simd<128>(src.data(), dst.data(), N);
                do_not_optimize(dst.data());
            },
            bytes, 10000);
        res.print();
    }

    // 256-bit SIMD copy
    {
        Benchmark bench("SIMD Copy 256-bit (256 KB)");
        bench.warmup([&]() { memory_copy_simd<256>(src.data(), dst.data(), N); });
        auto res = bench.run_throughput(
            [&]() {
                memory_copy_simd<256>(src.data(), dst.data(), N);
                do_not_optimize(dst.data());
            },
            bytes, 10000);
        res.print();
    }

    // 512-bit SIMD copy
    {
        Benchmark bench("SIMD Copy 512-bit (256 KB)");
        bench.warmup([&]() { memory_copy_simd<512>(src.data(), dst.data(), N); });
        auto res = bench.run_throughput(
            [&]() {
                memory_copy_simd<512>(src.data(), dst.data(), N);
                do_not_optimize(dst.data());
            },
            bytes, 10000);
        res.print();
    }

    print_separator();
}

void run_integer_simd_benchmarks() {
    print_header("Integer SIMD Benchmarks");

    constexpr std::size_t N = 1024;
    auto data_a = generate_int_data<N>();
    auto data_b = generate_int_data<N>();
    std::array<std::int32_t, N> result{};

    // Scalar baseline
    {
        Benchmark bench("Scalar Int Add (1024 i32)");
        bench.warmup(
            [&]() { scalar_add<std::int32_t, N>(data_a.data(), data_b.data(), result.data()); });
        auto res = bench.run(
            [&]() {
                scalar_add<std::int32_t, N>(data_a.data(), data_b.data(), result.data());
                do_not_optimize(result.data());
            },
            100000);
        res.print();
    }

    // 128-bit integer SIMD
    {
        Benchmark bench("SIMD Int Add 128-bit (4 x i32)");
        constexpr std::size_t vec_count = N / 4;
        std::array<Vector128i32, vec_count> va, vb, vr;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 4; ++j) {
                va[i][j] = data_a[i * 4 + j];
                vb[i][j] = data_b[i * 4 + j];
            }
        }

        auto res = bench.run(
            [&]() {
                for (std::size_t i = 0; i < vec_count; ++i) {
                    simd_add(va[i], vb[i], vr[i]);
                }
                do_not_optimize(vr.data());
            },
            100000);
        res.print();
    }

    // 256-bit integer SIMD
    {
        Benchmark bench("SIMD Int Add 256-bit (8 x i32)");
        constexpr std::size_t vec_count = N / 8;
        std::array<Vector256i32, vec_count> va, vb, vr;

        for (std::size_t i = 0; i < vec_count; ++i) {
            for (std::size_t j = 0; j < 8; ++j) {
                va[i][j] = data_a[i * 8 + j];
                vb[i][j] = data_b[i * 8 + j];
            }
        }

        auto res = bench.run(
            [&]() {
                for (std::size_t i = 0; i < vec_count; ++i) {
                    simd_add(va[i], vb[i], vr[i]);
                }
                do_not_optimize(vr.data());
            },
            100000);
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
    std::cout << "  DotVM SIMD Benchmark Suite\n";
    print_separator('=', 80);
    std::cout << "\n";

    // Print CPU features
    const auto& features = detect_cpu_features();
    std::cout << "CPU Features: " << features.feature_string() << "\n";
    std::cout << "Max Vector Width: " << features.max_vector_width() << " bits\n";
    std::cout << "FMA3 Support: " << (features.fma3 ? "Yes" : "No") << "\n";
    std::cout << "\n";

    // Print architecture support
    std::cout << "Architecture Support:\n";
    std::cout << "  - 128-bit: " << (features.supports_arch(Architecture::Arch128) ? "Yes" : "No")
              << "\n";
    std::cout << "  - 256-bit: " << (features.supports_arch(Architecture::Arch256) ? "Yes" : "No")
              << "\n";
    std::cout << "  - 512-bit: " << (features.supports_arch(Architecture::Arch512) ? "Yes" : "No")
              << "\n";
    std::cout << "\n";

    // Run benchmarks
    run_addition_benchmarks();
    run_multiplication_benchmarks();
    run_dot_product_benchmarks();
    run_fma_benchmarks();
    run_memory_benchmarks();
    run_integer_simd_benchmarks();

    // Summary
    std::cout << "\n";
    print_separator('=', 80);
    std::cout << "  Benchmark Complete\n";
    print_separator('=', 80);
    std::cout << "\n";
    std::cout << "Note: These benchmarks use the portable Vector<> implementation.\n";
    std::cout << "Actual SIMD intrinsics (SSE/AVX/NEON) would provide additional speedup.\n";
    std::cout << "\n";

    return 0;
}
