// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2025 DotLanth Project

#ifndef DOTVM_JIT_PROFILING_CONTEXT_HPP
#define DOTVM_JIT_PROFILING_CONTEXT_HPP

#include "jit_types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dotvm::jit {

/// Profile data for a single function
struct FunctionProfile {
    /// Function entry point (bytecode PC where CALL jumps to)
    std::size_t entry_pc{0};

    /// Number of times this function has been called
    std::uint64_t call_count{0};

    /// Total instructions executed within this function
    std::uint64_t total_instructions{0};

    /// Whether this function has been JIT compiled
    bool is_compiled{false};

    /// Check if this function is hot (exceeds call threshold)
    [[nodiscard]] constexpr auto is_hot() const noexcept -> bool {
        return call_count >= threshold::FUNCTION_CALLS;
    }
};

/// Profile data for a single loop
struct LoopProfile {
    /// Loop header PC (backward jump target)
    std::size_t header_pc{0};

    /// Backward branch PC (the instruction that jumps back)
    std::size_t backedge_pc{0};

    /// Number of times this loop has iterated
    std::uint64_t iteration_count{0};

    /// Whether this loop has been JIT compiled (OSR)
    bool is_compiled{false};

    /// Check if this loop is hot (exceeds iteration threshold)
    [[nodiscard]] constexpr auto is_hot() const noexcept -> bool {
        return iteration_count >= threshold::LOOP_ITERATIONS;
    }
};

/// Compilation request queued for the JIT compiler
struct CompilationRequest {
    enum class Type : std::uint8_t {
        Function = 0,
        Loop = 1
    };

    Type type;
    std::size_t pc;  // Entry PC for function, header PC for loop
    std::uint64_t priority;  // Higher = more urgent (based on hotness)
};

/// Profiling context for JIT compilation decisions
///
/// Tracks per-function call counts and per-loop iteration counts
/// to identify hot code paths for JIT compilation.
class ProfilingContext {
public:
    /// Default constructor
    ProfilingContext() = default;

    // Non-copyable (contains maps)
    ProfilingContext(const ProfilingContext&) = delete;
    ProfilingContext& operator=(const ProfilingContext&) = delete;

    // Movable
    ProfilingContext(ProfilingContext&&) = default;
    ProfilingContext& operator=(ProfilingContext&&) = default;

    /// Enable or disable profiling
    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }

    /// Check if profiling is enabled
    [[nodiscard]] auto is_enabled() const noexcept -> bool { return enabled_; }

    // ========================================================================
    // Function Profiling
    // ========================================================================

    /// Record a function call (called when CALL instruction executes)
    /// @param target_pc The PC being called (function entry point)
    /// @return true if the function just became hot
    auto record_call(std::size_t target_pc) noexcept -> bool {
        if (!enabled_) return false;

        auto& profile = functions_[target_pc];
        if (profile.entry_pc == 0) {
            profile.entry_pc = target_pc;
        }

        ++profile.call_count;

        // Check if just crossed threshold
        if (!profile.is_compiled && profile.call_count == threshold::FUNCTION_CALLS) {
            pending_functions_.push_back(target_pc);
            return true;
        }
        return false;
    }

    /// Get the profile for a function
    /// @param entry_pc The function entry PC
    /// @return The profile, or nullopt if not tracked
    [[nodiscard]] auto get_function_profile(std::size_t entry_pc) const noexcept
        -> std::optional<FunctionProfile> {
        auto it = functions_.find(entry_pc);
        if (it == functions_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /// Mark a function as compiled
    void mark_function_compiled(std::size_t entry_pc) noexcept {
        auto it = functions_.find(entry_pc);
        if (it != functions_.end()) {
            it->second.is_compiled = true;
        }
    }

    /// Get the number of tracked functions
    [[nodiscard]] auto function_count() const noexcept -> std::size_t {
        return functions_.size();
    }

    /// Get pending functions that need compilation
    [[nodiscard]] auto pending_functions() const noexcept -> const std::vector<std::size_t>& {
        return pending_functions_;
    }

    /// Clear pending functions after processing
    void clear_pending_functions() noexcept {
        pending_functions_.clear();
    }

    // ========================================================================
    // Loop Profiling
    // ========================================================================

    /// Record a backward branch (potential loop iteration)
    /// @param branch_pc The PC of the backward branch instruction
    /// @param target_pc The PC being jumped to (loop header)
    /// @return true if the loop just became hot
    auto record_backward_branch(std::size_t branch_pc, std::size_t target_pc) noexcept -> bool {
        if (!enabled_) return false;

        // Use target_pc (loop header) as the key
        auto& profile = loops_[target_pc];
        if (profile.header_pc == 0) {
            profile.header_pc = target_pc;
            profile.backedge_pc = branch_pc;
        }

        ++profile.iteration_count;

        // Check if just crossed threshold
        if (!profile.is_compiled && profile.iteration_count == threshold::LOOP_ITERATIONS) {
            pending_loops_.push_back(target_pc);
            return true;
        }
        return false;
    }

    /// Get the profile for a loop
    /// @param header_pc The loop header PC
    /// @return The profile, or nullopt if not tracked
    [[nodiscard]] auto get_loop_profile(std::size_t header_pc) const noexcept
        -> std::optional<LoopProfile> {
        auto it = loops_.find(header_pc);
        if (it == loops_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /// Mark a loop as compiled (OSR-ready)
    void mark_loop_compiled(std::size_t header_pc) noexcept {
        auto it = loops_.find(header_pc);
        if (it != loops_.end()) {
            it->second.is_compiled = true;
        }
    }

    /// Get the number of tracked loops
    [[nodiscard]] auto loop_count() const noexcept -> std::size_t {
        return loops_.size();
    }

    /// Get pending loops that need compilation
    [[nodiscard]] auto pending_loops() const noexcept -> const std::vector<std::size_t>& {
        return pending_loops_;
    }

    /// Clear pending loops after processing
    void clear_pending_loops() noexcept {
        pending_loops_.clear();
    }

    // ========================================================================
    // Compilation Queue
    // ========================================================================

    /// Check if there are any hot functions or loops pending compilation
    [[nodiscard]] auto has_pending_compilations() const noexcept -> bool {
        return !pending_functions_.empty() || !pending_loops_.empty();
    }

    /// Get the next compilation request (highest priority)
    /// @return The request, or nullopt if queue is empty
    [[nodiscard]] auto pop_compilation_request() noexcept -> std::optional<CompilationRequest> {
        // Prioritize functions over loops (simpler to compile)
        if (!pending_functions_.empty()) {
            auto pc = pending_functions_.back();
            pending_functions_.pop_back();

            auto it = functions_.find(pc);
            std::uint64_t priority = (it != functions_.end()) ? it->second.call_count : 0;

            return CompilationRequest{
                .type = CompilationRequest::Type::Function,
                .pc = pc,
                .priority = priority
            };
        }

        if (!pending_loops_.empty()) {
            auto pc = pending_loops_.back();
            pending_loops_.pop_back();

            auto it = loops_.find(pc);
            std::uint64_t priority = (it != loops_.end()) ? it->second.iteration_count : 0;

            return CompilationRequest{
                .type = CompilationRequest::Type::Loop,
                .pc = pc,
                .priority = priority
            };
        }

        return std::nullopt;
    }

    // ========================================================================
    // Reset and Statistics
    // ========================================================================

    /// Reset all profiling data
    void reset() noexcept {
        functions_.clear();
        loops_.clear();
        pending_functions_.clear();
        pending_loops_.clear();
    }

    /// Get total number of function calls recorded
    [[nodiscard]] auto total_function_calls() const noexcept -> std::uint64_t {
        std::uint64_t total = 0;
        for (const auto& [pc, profile] : functions_) {
            total += profile.call_count;
        }
        return total;
    }

    /// Get total number of loop iterations recorded
    [[nodiscard]] auto total_loop_iterations() const noexcept -> std::uint64_t {
        std::uint64_t total = 0;
        for (const auto& [pc, profile] : loops_) {
            total += profile.iteration_count;
        }
        return total;
    }

    /// Get number of hot functions (above threshold)
    [[nodiscard]] auto hot_function_count() const noexcept -> std::size_t {
        std::size_t count = 0;
        for (const auto& [pc, profile] : functions_) {
            if (profile.is_hot()) ++count;
        }
        return count;
    }

    /// Get number of hot loops (above threshold)
    [[nodiscard]] auto hot_loop_count() const noexcept -> std::size_t {
        std::size_t count = 0;
        for (const auto& [pc, profile] : loops_) {
            if (profile.is_hot()) ++count;
        }
        return count;
    }

private:
    /// Whether profiling is enabled
    bool enabled_{false};

    /// Per-function profiles (keyed by entry PC)
    std::unordered_map<std::size_t, FunctionProfile> functions_;

    /// Per-loop profiles (keyed by header PC)
    std::unordered_map<std::size_t, LoopProfile> loops_;

    /// Functions that have become hot and need compilation
    std::vector<std::size_t> pending_functions_;

    /// Loops that have become hot and need compilation
    std::vector<std::size_t> pending_loops_;
};

}  // namespace dotvm::jit

#endif  // DOTVM_JIT_PROFILING_CONTEXT_HPP
