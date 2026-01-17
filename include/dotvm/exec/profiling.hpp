#pragma once

/// @file profiling.hpp
/// @brief Performance profiling utilities for dispatch loop optimization
///
/// This header provides utilities for measuring dispatch loop performance,
/// including CPU cycle counting and branch prediction statistics.

#include <array>
#include <chrono>
#include <cstdint>

namespace dotvm::exec {

// ============================================================================
// CPU Cycle Counter (RDTSC)
// ============================================================================

/// Read the Time Stamp Counter (x86-64 specific)
///
/// Returns the current CPU cycle count. This is useful for precise
/// timing measurements of the dispatch loop.
///
/// @note The TSC frequency varies between CPUs. Use rdtsc_frequency()
///       to convert cycles to time.
/// @note On non-x86 platforms, this returns 0.
#if defined(__x86_64__) || defined(_M_X64)
inline std::uint64_t rdtsc() noexcept {
    std::uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

/// Read RDTSC with serialization (more accurate but slower)
///
/// Uses CPUID to serialize the pipeline before reading TSC,
/// ensuring all previous instructions have completed.
inline std::uint64_t rdtsc_serialized() noexcept {
    std::uint32_t lo, hi;
    __asm__ volatile("cpuid\n\t"
                     "rdtsc"
                     : "=a"(lo), "=d"(hi)
                     : "a"(0)
                     : "rbx", "rcx");
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

/// Read RDTSCP (serializing read, includes processor ID)
///
/// RDTSCP is better than RDTSC for timing because it waits for
/// all previous instructions to complete.
inline std::uint64_t rdtscp() noexcept {
    std::uint32_t lo, hi, aux;
    __asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
    return (static_cast<std::uint64_t>(hi) << 32) | lo;
}

#elif defined(__aarch64__)
// ARM64 cycle counter
inline std::uint64_t rdtsc() noexcept {
    std::uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

inline std::uint64_t rdtsc_serialized() noexcept {
    __asm__ volatile("isb");
    return rdtsc();
}

inline std::uint64_t rdtscp() noexcept {
    return rdtsc_serialized();
}

#else
// Fallback for unsupported platforms
inline std::uint64_t rdtsc() noexcept {
    return 0;
}
inline std::uint64_t rdtsc_serialized() noexcept {
    return 0;
}
inline std::uint64_t rdtscp() noexcept {
    return 0;
}
#endif

// ============================================================================
// Scoped Cycle Counter
// ============================================================================

/// RAII helper for measuring cycles in a scope
///
/// @example
/// ```cpp
/// std::uint64_t cycles = 0;
/// {
///     ScopedCycleCounter counter(cycles);
///     // Code to measure
/// }
/// std::cout << "Cycles: " << cycles << "\n";
/// ```
class ScopedCycleCounter {
public:
    /// Start counting cycles
    /// @param accumulator Reference to counter to add cycles to
    explicit ScopedCycleCounter(std::uint64_t& accumulator) noexcept
        : accumulator_{accumulator}, start_{rdtsc()} {}

    /// Stop counting and add to accumulator
    ~ScopedCycleCounter() noexcept { accumulator_ += rdtsc() - start_; }

    // Non-copyable
    ScopedCycleCounter(const ScopedCycleCounter&) = delete;
    ScopedCycleCounter& operator=(const ScopedCycleCounter&) = delete;

private:
    std::uint64_t& accumulator_;
    std::uint64_t start_;
};

// ============================================================================
// Dispatch Performance Statistics
// ============================================================================

/// Statistics for dispatch loop performance
struct DispatchStats {
    /// Total CPU cycles spent in dispatch loop
    std::uint64_t total_cycles{0};

    /// Number of instructions executed
    std::uint64_t instructions_executed{0};

    /// Number of taken branches (jumps, calls, etc.)
    std::uint64_t taken_branches{0};

    /// Number of not-taken branches
    std::uint64_t not_taken_branches{0};

    /// Number of indirect jumps (JMPR, RET)
    std::uint64_t indirect_jumps{0};

    /// Number of backward jumps (potential loops)
    std::uint64_t backward_jumps{0};

    /// Number of forward jumps
    std::uint64_t forward_jumps{0};

    /// Number of CALL instructions
    std::uint64_t call_count{0};

    /// Number of RET instructions
    std::uint64_t ret_count{0};

    /// Calculate average cycles per instruction
    [[nodiscard]] double avg_cycles_per_instruction() const noexcept {
        return instructions_executed > 0
                   ? static_cast<double>(total_cycles) / static_cast<double>(instructions_executed)
                   : 0.0;
    }

    /// Calculate branch taken ratio
    [[nodiscard]] double branch_taken_ratio() const noexcept {
        auto total = taken_branches + not_taken_branches;
        return total > 0 ? static_cast<double>(taken_branches) / static_cast<double>(total) : 0.0;
    }

    /// Calculate backward jump ratio
    [[nodiscard]] double backward_jump_ratio() const noexcept {
        auto total = backward_jumps + forward_jumps;
        return total > 0 ? static_cast<double>(backward_jumps) / static_cast<double>(total) : 0.0;
    }

    /// Reset all statistics
    void reset() noexcept { *this = DispatchStats{}; }

    /// Merge statistics from another instance
    void merge(const DispatchStats& other) noexcept {
        total_cycles += other.total_cycles;
        instructions_executed += other.instructions_executed;
        taken_branches += other.taken_branches;
        not_taken_branches += other.not_taken_branches;
        indirect_jumps += other.indirect_jumps;
        backward_jumps += other.backward_jumps;
        forward_jumps += other.forward_jumps;
        call_count += other.call_count;
        ret_count += other.ret_count;
    }
};

// ============================================================================
// Per-Opcode Statistics
// ============================================================================

/// Per-opcode execution statistics
struct OpcodeStats {
    /// Execution count per opcode
    std::array<std::uint64_t, 256> execution_count{};

    /// Total cycles per opcode
    std::array<std::uint64_t, 256> total_cycles{};

    /// Calculate average cycles for an opcode
    [[nodiscard]] double avg_cycles(std::uint8_t opcode) const noexcept {
        return execution_count[opcode] > 0 ? static_cast<double>(total_cycles[opcode]) /
                                                 static_cast<double>(execution_count[opcode])
                                           : 0.0;
    }

    /// Get most executed opcode
    [[nodiscard]] std::uint8_t most_executed() const noexcept {
        std::uint8_t max_opcode = 0;
        std::uint64_t max_count = 0;
        for (std::size_t i = 0; i < 256; ++i) {
            if (execution_count[i] > max_count) {
                max_count = execution_count[i];
                max_opcode = static_cast<std::uint8_t>(i);
            }
        }
        return max_opcode;
    }

    /// Get slowest opcode (highest average cycles)
    [[nodiscard]] std::uint8_t slowest() const noexcept {
        std::uint8_t max_opcode = 0;
        double max_avg = 0.0;
        for (std::size_t i = 0; i < 256; ++i) {
            double avg = avg_cycles(static_cast<std::uint8_t>(i));
            if (avg > max_avg) {
                max_avg = avg;
                max_opcode = static_cast<std::uint8_t>(i);
            }
        }
        return max_opcode;
    }

    /// Reset all statistics
    void reset() noexcept {
        execution_count.fill(0);
        total_cycles.fill(0);
    }
};

// ============================================================================
// High-Resolution Timer
// ============================================================================

/// High-resolution wall-clock timer
class HighResTimer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::nanoseconds;

    /// Start the timer
    void start() noexcept { start_ = Clock::now(); }

    /// Stop the timer and return elapsed nanoseconds
    [[nodiscard]] std::uint64_t stop() noexcept {
        auto end = Clock::now();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<Duration>(end - start_).count());
    }

    /// Get elapsed nanoseconds without stopping
    [[nodiscard]] std::uint64_t elapsed() const noexcept {
        auto now = Clock::now();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<Duration>(now - start_).count());
    }

private:
    TimePoint start_;
};

// ============================================================================
// Benchmark Utilities
// ============================================================================

/// Calculate dispatch overhead in cycles
///
/// @param total_cycles Total cycles for execution
/// @param instruction_count Number of instructions executed
/// @param estimated_handler_cycles Estimated cycles per handler (default: 5)
/// @return Estimated dispatch overhead in cycles
[[nodiscard]] constexpr double
estimate_dispatch_overhead(std::uint64_t total_cycles, std::uint64_t instruction_count,
                           double estimated_handler_cycles = 5.0) noexcept {
    if (instruction_count == 0)
        return 0.0;
    double avg = static_cast<double>(total_cycles) / static_cast<double>(instruction_count);
    return avg - estimated_handler_cycles;
}

/// Check if dispatch overhead meets target
///
/// @param total_cycles Total cycles for execution
/// @param instruction_count Number of instructions executed
/// @param target_cycles Target dispatch cycles (default: 10)
/// @return true if average cycles per instruction is below target
[[nodiscard]] constexpr bool meets_dispatch_target(std::uint64_t total_cycles,
                                                   std::uint64_t instruction_count,
                                                   double target_cycles = 10.0) noexcept {
    if (instruction_count == 0)
        return false;
    double avg = static_cast<double>(total_cycles) / static_cast<double>(instruction_count);
    return avg < target_cycles;
}

}  // namespace dotvm::exec
