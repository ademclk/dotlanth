/// @file jit_profiler.hpp
/// @brief Per-function and per-loop profiling for JIT compilation decisions
///
/// Tracks execution counts for functions and loops to determine when
/// code should be JIT compiled. Uses cache-line aligned structures
/// to avoid false sharing in concurrent scenarios.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "jit_config.hpp"

namespace dotvm::jit {

/// @brief Cache line size for alignment
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

/// @brief Profile data for a single function
///
/// Tracks call count and PC boundaries for a function.
/// Cache-line aligned to prevent false sharing.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding) - intentional cache-line alignment
struct alignas(CACHE_LINE_SIZE) FunctionProfile {
    /// @brief Number of times this function has been called
    std::atomic<std::uint32_t> call_count{0};

    /// @brief PC of the function entry point
    std::size_t entry_pc{0};

    /// @brief PC of the last instruction (exclusive end)
    std::size_t end_pc{0};

    /// @brief Whether this function has been JIT compiled
    std::atomic<bool> compiled{false};

    /// @brief Padding to fill cache line
    // NOLINTNEXTLINE(readability-identifier-naming) - intentional trailing underscore for padding
    std::uint8_t padding_[CACHE_LINE_SIZE - sizeof(std::atomic<std::uint32_t>) -
                          (sizeof(std::size_t) * 2) - sizeof(std::atomic<bool>)]{};

    /// @brief Increment call count and return new value
    [[nodiscard]] std::uint32_t increment() noexcept {
        return call_count.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    /// @brief Check if function has reached compilation threshold
    [[nodiscard]] bool reached_threshold(std::uint32_t threshold) const noexcept {
        return call_count.load(std::memory_order_relaxed) >= threshold;
    }

    /// @brief Reset the profile counters
    void reset() noexcept {
        call_count.store(0, std::memory_order_relaxed);
        compiled.store(false, std::memory_order_relaxed);
    }
};

/// @brief Profile data for a single loop
///
/// Tracks iteration count and PC locations for loop OSR decisions.
/// Cache-line aligned to prevent false sharing.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding) - intentional cache-line alignment
struct alignas(CACHE_LINE_SIZE) LoopProfile {
    /// @brief Number of iterations of this loop
    std::atomic<std::uint32_t> iteration_count{0};

    /// @brief PC of the loop header (first instruction)
    std::size_t header_pc{0};

    /// @brief PC of the backward edge (jump back to header)
    std::size_t backedge_pc{0};

    /// @brief Whether OSR has been triggered for this loop
    std::atomic<bool> osr_triggered{false};

    /// @brief Padding to fill cache line
    // NOLINTNEXTLINE(readability-identifier-naming) - intentional trailing underscore for padding
    std::uint8_t padding_[CACHE_LINE_SIZE - sizeof(std::atomic<std::uint32_t>) -
                          (sizeof(std::size_t) * 2) - sizeof(std::atomic<bool>)]{};

    /// @brief Increment iteration count and return new value
    [[nodiscard]] std::uint32_t increment() noexcept {
        return iteration_count.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    /// @brief Check if loop has reached OSR threshold
    [[nodiscard]] bool reached_threshold(std::uint32_t threshold) const noexcept {
        return iteration_count.load(std::memory_order_relaxed) >= threshold;
    }

    /// @brief Reset the profile counters
    void reset() noexcept {
        iteration_count.store(0, std::memory_order_relaxed);
        osr_triggered.store(false, std::memory_order_relaxed);
    }
};

/// @brief Function ID type
using FunctionId = std::uint32_t;

/// @brief Loop ID type (combines function ID and loop index)
using LoopId = std::uint64_t;

/// @brief Create a loop ID from function ID and loop index
[[nodiscard]] constexpr LoopId make_loop_id(FunctionId func_id, std::uint32_t loop_index) noexcept {
    return (static_cast<std::uint64_t>(func_id) << 32) | loop_index;
}

/// @brief Extract function ID from loop ID
[[nodiscard]] constexpr FunctionId loop_id_function(LoopId loop_id) noexcept {
    return static_cast<FunctionId>(loop_id >> 32);
}

/// @brief Extract loop index from loop ID
[[nodiscard]] constexpr std::uint32_t loop_id_index(LoopId loop_id) noexcept {
    return static_cast<std::uint32_t>(loop_id & 0xFFFFFFFF);
}

/// @brief JIT profiler for tracking function and loop execution
///
/// Maintains execution counts for functions and loops to determine
/// when they should be JIT compiled. Thread-safe for counter updates.
///
/// @example
/// ```cpp
/// JitProfiler profiler;
///
/// // Register a function
/// auto func_id = profiler.register_function(0x100, 0x200);
///
/// // Track calls in the interpreter
/// auto count = profiler.record_call(func_id);
/// if (count >= config.call_threshold && !profiler.is_compiled(func_id)) {
///     // Trigger JIT compilation
/// }
/// ```
class JitProfiler {
public:
    /// @brief Create a profiler with default configuration
    JitProfiler() = default;

    /// @brief Create a profiler with specific configuration
    explicit JitProfiler(const JitConfig& config) noexcept;

    /// @brief Destructor
    ~JitProfiler() = default;

    // Non-copyable due to atomic counters
    JitProfiler(const JitProfiler&) = delete;
    JitProfiler& operator=(const JitProfiler&) = delete;

    // Movable
    JitProfiler(JitProfiler&&) noexcept = default;
    JitProfiler& operator=(JitProfiler&&) noexcept = default;

    /// @brief Register a new function for profiling
    ///
    /// @param entry_pc PC of function entry point
    /// @param end_pc PC past last instruction
    /// @return Function ID for future reference
    [[nodiscard]] FunctionId register_function(std::size_t entry_pc, std::size_t end_pc);

    /// @brief Register a loop within a function
    ///
    /// @param func_id Function containing the loop
    /// @param header_pc PC of loop header
    /// @param backedge_pc PC of backward edge
    /// @return Loop ID for future reference
    [[nodiscard]] LoopId register_loop(FunctionId func_id, std::size_t header_pc,
                                       std::size_t backedge_pc);

    /// @brief Record a function call
    ///
    /// @param func_id Function being called
    /// @return New call count after increment
    [[nodiscard]] std::uint32_t record_call(FunctionId func_id) noexcept;

    /// @brief Record a loop iteration
    ///
    /// @param loop_id Loop being iterated
    /// @return New iteration count after increment
    [[nodiscard]] std::uint32_t record_iteration(LoopId loop_id) noexcept;

    /// @brief Check if function has reached compilation threshold
    [[nodiscard]] bool should_compile(FunctionId func_id) const noexcept;

    /// @brief Check if loop has reached OSR threshold
    [[nodiscard]] bool should_osr(LoopId loop_id) const noexcept;

    /// @brief Mark function as compiled
    void mark_compiled(FunctionId func_id) noexcept;

    /// @brief Mark loop as having triggered OSR
    void mark_osr_triggered(LoopId loop_id) noexcept;

    /// @brief Check if function is already compiled
    [[nodiscard]] bool is_compiled(FunctionId func_id) const noexcept;

    /// @brief Check if loop has already triggered OSR
    [[nodiscard]] bool is_osr_triggered(LoopId loop_id) const noexcept;

    /// @brief Get function profile by ID
    [[nodiscard]] const FunctionProfile* get_function(FunctionId func_id) const noexcept;

    /// @brief Get loop profile by ID
    [[nodiscard]] const LoopProfile* get_loop(LoopId loop_id) const noexcept;

    /// @brief Find function by entry PC
    [[nodiscard]] std::optional<FunctionId> find_function_by_pc(std::size_t pc) const noexcept;

    /// @brief Find loop by backedge PC
    [[nodiscard]] std::optional<LoopId>
    find_loop_by_backedge(std::size_t backedge_pc) const noexcept;

    /// @brief Get current compilation threshold
    [[nodiscard]] std::uint32_t call_threshold() const noexcept { return call_threshold_; }

    /// @brief Get current OSR threshold
    [[nodiscard]] std::uint32_t loop_threshold() const noexcept { return loop_threshold_; }

    /// @brief Set compilation threshold
    void set_call_threshold(std::uint32_t threshold) noexcept { call_threshold_ = threshold; }

    /// @brief Set OSR threshold
    void set_loop_threshold(std::uint32_t threshold) noexcept { loop_threshold_ = threshold; }

    /// @brief Get number of registered functions
    [[nodiscard]] std::size_t function_count() const noexcept { return functions_.size(); }

    /// @brief Get number of registered loops
    [[nodiscard]] std::size_t loop_count() const noexcept { return loops_.size(); }

    /// @brief Reset all profiling data
    void reset();

    /// @brief Get profiling statistics
    struct Stats {
        std::size_t total_functions{0};
        std::size_t compiled_functions{0};
        std::size_t total_loops{0};
        std::size_t osr_triggered_loops{0};
        std::uint64_t total_calls{0};
        std::uint64_t total_iterations{0};
    };

    [[nodiscard]] Stats get_stats() const noexcept;

private:
    /// @brief Function profiles indexed by ID (unique_ptr for non-copyable atomics)
    std::vector<std::unique_ptr<FunctionProfile>> functions_;

    /// @brief Loop profiles indexed by ID (unique_ptr for non-copyable atomics)
    std::unordered_map<LoopId, std::unique_ptr<LoopProfile>> loops_;

    /// @brief Map from entry PC to function ID for fast lookup
    std::unordered_map<std::size_t, FunctionId> pc_to_function_;

    /// @brief Map from backedge PC to loop ID
    std::unordered_map<std::size_t, LoopId> backedge_to_loop_;

    /// @brief Compilation threshold
    std::uint32_t call_threshold_ = thresholds::CALL_THRESHOLD;

    /// @brief OSR threshold
    std::uint32_t loop_threshold_ = thresholds::LOOP_THRESHOLD;
};

}  // namespace dotvm::jit
