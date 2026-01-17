#pragma once

/// @file benchmark_config.hpp
/// @brief Benchmark framework for DotVM performance testing
///
/// Provides a lightweight, header-heavy benchmark framework for measuring
/// performance of SIMD, BigInt, and Crypto operations. Includes utilities
/// to prevent compiler optimization of benchmark code.
///
/// Part of CORE-008: Performance Benchmarks for the dotlanth VM

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace dotvm::core::bench {

// ============================================================================
// Benchmark Result Structure
// ============================================================================

/// @brief Results from a benchmark run
///
/// Contains timing statistics and throughput measurements for a benchmark.
struct BenchmarkResult {
    /// Name of the benchmark
    std::string name;

    /// Number of iterations performed
    std::size_t iterations = 0;

    /// Total time for all iterations
    std::chrono::nanoseconds total_time{0};

    /// Minimum time for a single iteration
    std::chrono::nanoseconds min_time{std::numeric_limits<std::int64_t>::max()};

    /// Maximum time for a single iteration
    std::chrono::nanoseconds max_time{0};

    /// Operations per second
    double ops_per_second = 0.0;

    /// Throughput in MB/s (for memory-bound operations)
    double throughput_mb_per_sec = 0.0;

    /// Print results to stdout in table format
    void print() const;

    /// Calculate average time per operation in nanoseconds
    [[nodiscard]] double avg_ns_per_op() const noexcept {
        if (iterations == 0)
            return 0.0;
        return static_cast<double>(total_time.count()) / static_cast<double>(iterations);
    }

    /// Calculate standard deviation (requires storing all times, so approximated)
    /// Uses range-based approximation: stddev ~ (max - min) / 4
    [[nodiscard]] double approx_stddev_ns() const noexcept {
        return static_cast<double>((max_time - min_time).count()) / 4.0;
    }
};

// ============================================================================
// Compiler Optimization Barriers
// ============================================================================

/// @brief Prevent compiler from optimizing away a value
///
/// Uses inline assembly or compiler-specific intrinsics to force the compiler
/// to consider the value as having side effects, preventing dead code elimination.
///
/// @tparam T Type of value to preserve
/// @param value Value that should not be optimized away
template <typename T>
inline void do_not_optimize(const T& value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    // For non-trivial types, use memory constraint to prevent optimization
    // The "m" constraint tells the compiler the value is in memory
    asm volatile("" : : "g"(&value) : "memory");
#elif defined(_MSC_VER)
    // MSVC: Use volatile read to prevent optimization
    volatile auto* ptr = &value;
    (void)*ptr;
#else
    // Fallback: volatile cast
    *const_cast<volatile std::remove_reference_t<T>*>(&value);
#endif
}

/// @brief Specialization for pointer types
template <typename T>
inline void do_not_optimize(T* ptr) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(ptr) : "memory");
#else
    volatile auto sink = reinterpret_cast<std::uintptr_t>(ptr);
    (void)sink;
#endif
}

/// @brief Specialization for integral types (can use register constraint)
template <typename T>
    requires std::is_integral_v<std::remove_reference_t<T>>
inline void do_not_optimize(T&& value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : "+r"(value) : : "memory");
#elif defined(_MSC_VER)
    volatile auto sink = value;
    (void)sink;
#else
    *const_cast<volatile std::remove_reference_t<T>*>(&value);
#endif
}

/// @brief Specialization for floating-point types (can use register constraint)
template <typename T>
    requires std::is_floating_point_v<std::remove_reference_t<T>>
inline void do_not_optimize(T&& value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : "+r"(value) : : "memory");
#elif defined(_MSC_VER)
    volatile auto sink = value;
    (void)sink;
#else
    *const_cast<volatile std::remove_reference_t<T>*>(&value);
#endif
}

/// @brief Memory fence to ensure accurate timing measurements
///
/// Inserts a compiler memory barrier to prevent instruction reordering
/// across measurement boundaries.
inline void memory_fence() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#endif
}

/// @brief Full memory barrier including CPU fence
///
/// Inserts both a compiler barrier and CPU memory fence for
/// the most accurate timing across multi-threaded scenarios.
inline void full_memory_fence() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    asm volatile("mfence" : : : "memory");
    #elif defined(__aarch64__)
    asm volatile("dmb ish" : : : "memory");
    #else
    __sync_synchronize();
    #endif
#elif defined(_MSC_VER)
    _mm_mfence();
#endif
}

// ============================================================================
// Benchmark Class
// ============================================================================

/// @brief Main benchmark runner class
///
/// Provides methods to run benchmarks with warmup, timing, and statistics.
///
/// @example
/// ```cpp
/// Benchmark bench("vector_add");
/// bench.warmup([&]() { do_vector_add(); });
/// auto result = bench.run([&]() { do_vector_add(); }, 1000000);
/// result.print();
/// ```
class Benchmark {
public:
    /// @brief Construct a benchmark with the given name
    /// @param name Descriptive name for the benchmark
    explicit Benchmark(std::string name) : name_(std::move(name)) {}

    /// @brief Get the benchmark name
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    /// @brief Run warmup iterations before measurement
    ///
    /// Warmup helps stabilize CPU frequency and populate caches before
    /// actual timing measurements begin.
    ///
    /// @tparam Fn Callable type (function, lambda, functor)
    /// @param fn Function to execute during warmup
    /// @param iterations Number of warmup iterations (default: 1000)
    template <typename Fn>
    void warmup(Fn&& fn, std::size_t iterations = 1000) {
        for (std::size_t i = 0; i < iterations; ++i) {
            fn();
            memory_fence();
        }
    }

    /// @brief Run the benchmark and measure timing
    ///
    /// Executes the function for the specified number of iterations,
    /// measuring total time, min/max times, and calculating ops/sec.
    ///
    /// @tparam Fn Callable type
    /// @param fn Function to benchmark
    /// @param iterations Number of iterations to run
    /// @return BenchmarkResult with timing statistics
    template <typename Fn>
    BenchmarkResult run(Fn&& fn, std::size_t iterations = 1000000) {
        BenchmarkResult result;
        result.name = name_;
        result.iterations = iterations;
        result.min_time = std::chrono::nanoseconds{std::numeric_limits<std::int64_t>::max()};
        result.max_time = std::chrono::nanoseconds{0};

        using clock = std::chrono::high_resolution_clock;

        // Batch iterations for more accurate timing of fast operations
        constexpr std::size_t batch_size = 100;
        const std::size_t num_batches = iterations / batch_size;
        const std::size_t remaining = iterations % batch_size;

        auto total_start = clock::now();

        // Run batched iterations
        for (std::size_t b = 0; b < num_batches; ++b) {
            memory_fence();
            auto batch_start = clock::now();

            for (std::size_t i = 0; i < batch_size; ++i) {
                fn();
            }

            memory_fence();
            auto batch_end = clock::now();

            auto batch_time =
                std::chrono::duration_cast<std::chrono::nanoseconds>(batch_end - batch_start);
            auto per_iter = std::chrono::nanoseconds(batch_time.count() /
                                                     static_cast<std::int64_t>(batch_size));

            if (per_iter < result.min_time)
                result.min_time = per_iter;
            if (per_iter > result.max_time)
                result.max_time = per_iter;
        }

        // Run remaining iterations
        for (std::size_t i = 0; i < remaining; ++i) {
            memory_fence();
            auto iter_start = clock::now();
            fn();
            memory_fence();
            auto iter_end = clock::now();

            auto iter_time =
                std::chrono::duration_cast<std::chrono::nanoseconds>(iter_end - iter_start);
            if (iter_time < result.min_time)
                result.min_time = iter_time;
            if (iter_time > result.max_time)
                result.max_time = iter_time;
        }

        auto total_end = clock::now();
        result.total_time =
            std::chrono::duration_cast<std::chrono::nanoseconds>(total_end - total_start);

        // Calculate ops/sec
        double total_seconds = static_cast<double>(result.total_time.count()) / 1e9;
        if (total_seconds > 0) {
            result.ops_per_second = static_cast<double>(iterations) / total_seconds;
        }

        return result;
    }

    /// @brief Run benchmark with throughput measurement
    ///
    /// Similar to run(), but also calculates throughput in MB/s based on
    /// the number of bytes processed per operation.
    ///
    /// @tparam Fn Callable type
    /// @param fn Function to benchmark
    /// @param bytes_per_op Number of bytes processed per operation
    /// @param iterations Number of iterations to run
    /// @return BenchmarkResult with timing and throughput statistics
    template <typename Fn>
    BenchmarkResult run_throughput(Fn&& fn, std::size_t bytes_per_op,
                                   std::size_t iterations = 100000) {
        auto result = run(std::forward<Fn>(fn), iterations);

        // Calculate throughput in MB/s
        double total_bytes = static_cast<double>(bytes_per_op * iterations);
        double total_seconds = static_cast<double>(result.total_time.count()) / 1e9;
        if (total_seconds > 0) {
            result.throughput_mb_per_sec = (total_bytes / (1024.0 * 1024.0)) / total_seconds;
        }

        return result;
    }

    /// @brief Run benchmark with automatic iteration count
    ///
    /// Automatically determines the iteration count to achieve a minimum
    /// total runtime, providing more stable measurements.
    ///
    /// @tparam Fn Callable type
    /// @param fn Function to benchmark
    /// @param min_runtime_ms Minimum total runtime in milliseconds
    /// @return BenchmarkResult with timing statistics
    template <typename Fn>
    BenchmarkResult run_auto(Fn&& fn, std::size_t min_runtime_ms = 1000) {
        using clock = std::chrono::high_resolution_clock;

        // First, estimate time per operation with a small sample
        constexpr std::size_t sample_size = 1000;
        warmup(fn, 100);

        auto sample_start = clock::now();
        for (std::size_t i = 0; i < sample_size; ++i) {
            fn();
            memory_fence();
        }
        auto sample_end = clock::now();

        auto sample_time =
            std::chrono::duration_cast<std::chrono::nanoseconds>(sample_end - sample_start);
        double ns_per_op =
            static_cast<double>(sample_time.count()) / static_cast<double>(sample_size);

        // Calculate iterations needed for minimum runtime
        double target_ns = static_cast<double>(min_runtime_ms) * 1e6;
        auto estimated_iterations = static_cast<std::size_t>(target_ns / ns_per_op);

        // Clamp to reasonable range
        estimated_iterations = std::max(std::size_t{1000}, estimated_iterations);
        estimated_iterations = std::min(std::size_t{100000000}, estimated_iterations);

        return run(std::forward<Fn>(fn), estimated_iterations);
    }

private:
    std::string name_;
};

// ============================================================================
// Benchmark Suite Helper
// ============================================================================

/// @brief Collection of benchmarks to run together
class BenchmarkSuite {
public:
    /// @brief Construct a suite with a name
    explicit BenchmarkSuite(std::string name) : name_(std::move(name)) {}

    /// @brief Add a benchmark result to the suite
    void add_result(BenchmarkResult result) { results_.push_back(std::move(result)); }

    /// @brief Get all results
    [[nodiscard]] const std::vector<BenchmarkResult>& results() const noexcept { return results_; }

    /// @brief Print all results with header
    void print_all() const;

    /// @brief Print a comparison between two results (speedup)
    static void print_comparison(const BenchmarkResult& baseline, const BenchmarkResult& optimized);

private:
    std::string name_;
    std::vector<BenchmarkResult> results_;
};

// ============================================================================
// Utility Functions
// ============================================================================

/// @brief Print a separator line
void print_separator(char ch = '-', std::size_t width = 80);

/// @brief Print a header for benchmark output
void print_header(const std::string& title);

/// @brief Format nanoseconds to human-readable string
[[nodiscard]] std::string format_time(std::chrono::nanoseconds ns);

/// @brief Format throughput to human-readable string
[[nodiscard]] std::string format_throughput(double mb_per_sec);

}  // namespace dotvm::core::bench
