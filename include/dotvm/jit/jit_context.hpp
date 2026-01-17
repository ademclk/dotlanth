/// @file jit_context.hpp
/// @brief Aggregate JIT context managing all JIT components
///
/// Provides a unified interface to the JIT subsystem, owning and
/// coordinating the profiler, code buffer, cache, and compiler.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "jit_cache.hpp"
#include "jit_code_buffer.hpp"
#include "jit_compiler.hpp"
#include "jit_config.hpp"
#include "jit_profiler.hpp"
#include "jit_stencil.hpp"

namespace dotvm::jit {

/// @brief Aggregate JIT context
///
/// Manages all JIT subsystem components and provides a high-level
/// interface for compilation and execution. This is the main entry
/// point for JIT functionality from the VM.
///
/// @example
/// ```cpp
/// // Create JIT context with default config
/// auto jit_ctx = JitContext::create();
///
/// // Register a function for profiling
/// auto func_id = jit_ctx->register_function(entry_pc, end_pc);
///
/// // Record calls (done by ExecutionEngine during interpretation)
/// if (jit_ctx->record_call(func_id)) {
///     // Threshold reached, compile the function
///     auto result = jit_ctx->compile_function(func_id, bytecode);
/// }
///
/// // Execute compiled code if available
/// if (auto* entry = jit_ctx->lookup(func_id)) {
///     jit_ctx->execute(entry, regs, ctx);
/// }
/// ```
class JitContext {
public:
    /// @brief Create a JIT context with default configuration
    [[nodiscard]] static std::unique_ptr<JitContext> create();

    /// @brief Create a JIT context with custom configuration
    [[nodiscard]] static std::unique_ptr<JitContext> create(const JitConfig& config);

    /// @brief Destructor
    ~JitContext();

    // Non-copyable
    JitContext(const JitContext&) = delete;
    JitContext& operator=(const JitContext&) = delete;

    // Non-movable (contains self-references)
    JitContext(JitContext&&) = delete;
    JitContext& operator=(JitContext&&) = delete;

    // ========================================================================
    // Configuration
    // ========================================================================

    /// @brief Get the JIT configuration
    [[nodiscard]] const JitConfig& config() const noexcept { return config_; }

    /// @brief Check if JIT is enabled
    [[nodiscard]] bool enabled() const noexcept { return config_.enabled; }

    /// @brief Check if OSR is enabled
    [[nodiscard]] bool osr_enabled() const noexcept { return config_.osr_enabled; }

    // ========================================================================
    // Function Registration and Profiling
    // ========================================================================

    /// @brief Register a function for profiling
    ///
    /// @param entry_pc PC of function entry
    /// @param end_pc PC past last instruction
    /// @return Function ID for future reference
    [[nodiscard]] FunctionId register_function(std::size_t entry_pc, std::size_t end_pc);

    /// @brief Register a loop within a function
    [[nodiscard]] LoopId register_loop(FunctionId func_id, std::size_t header_pc,
                                       std::size_t backedge_pc);

    /// @brief Record a function call
    ///
    /// @param func_id Function being called
    /// @return true if threshold reached and compilation should be triggered
    [[nodiscard]] bool record_call(FunctionId func_id) noexcept;

    /// @brief Record a loop iteration
    ///
    /// @param loop_id Loop being iterated
    /// @return true if OSR threshold reached
    [[nodiscard]] bool record_iteration(LoopId loop_id) noexcept;

    /// @brief Find function by entry PC
    [[nodiscard]] std::optional<FunctionId> find_function(std::size_t entry_pc) const noexcept;

    /// @brief Find loop by backedge PC
    [[nodiscard]] std::optional<LoopId> find_loop(std::size_t backedge_pc) const noexcept;

    // ========================================================================
    // Compilation
    // ========================================================================

    /// @brief Compile a function to native code
    ///
    /// @param func_id Function ID
    /// @param bytecode Full bytecode of the module
    /// @return Compilation status
    [[nodiscard]] JitStatus compile_function(FunctionId func_id,
                                             std::span<const std::uint8_t> bytecode);

    /// @brief Trigger OSR compilation for a loop
    [[nodiscard]] OsrStatus compile_osr(LoopId loop_id, std::span<const std::uint8_t> bytecode);

    /// @brief Check if function is compiled
    [[nodiscard]] bool is_compiled(FunctionId func_id) const noexcept;

    // ========================================================================
    // Execution
    // ========================================================================

    /// @brief Look up compiled code for a function
    [[nodiscard]] const CompiledEntry* lookup(FunctionId func_id) noexcept;

    /// @brief Look up compiled code by entry PC
    [[nodiscard]] const CompiledEntry* lookup_by_pc(std::size_t entry_pc) noexcept;

    /// @brief Look up OSR entry point
    [[nodiscard]] const OsrEntry* lookup_osr(LoopId loop_id) noexcept;

    /// @brief Execute compiled code
    ///
    /// @param entry Compiled entry to execute
    /// @param regs Pointer to register file
    /// @param ctx Pointer to VM context
    void execute(const CompiledEntry* entry, void* regs, void* ctx) noexcept;

    // ========================================================================
    // Cache Management
    // ========================================================================

    /// @brief Invalidate a function's compiled code
    void invalidate(FunctionId func_id) noexcept;

    /// @brief Invalidate all code in a PC range
    void invalidate_range(std::size_t start_pc, std::size_t end_pc) noexcept;

    /// @brief Clear all compiled code
    void clear_cache() noexcept;

    // ========================================================================
    // Statistics
    // ========================================================================

    /// @brief Combined statistics from all components
    struct Stats {
        // Profiler stats
        std::size_t registered_functions{0};
        std::size_t registered_loops{0};
        std::uint64_t total_calls{0};
        std::uint64_t total_iterations{0};

        // Compiler stats
        std::size_t functions_compiled{0};
        std::size_t bytes_generated{0};
        std::size_t fallbacks_emitted{0};

        // Cache stats
        std::size_t cache_entries{0};
        std::size_t cache_bytes_used{0};
        std::uint64_t cache_hits{0};
        std::uint64_t cache_misses{0};
        double cache_hit_rate{0.0};
    };

    [[nodiscard]] Stats stats() const noexcept;

    // ========================================================================
    // Component Access (for advanced usage)
    // ========================================================================

    /// @brief Get the profiler
    [[nodiscard]] JitProfiler& profiler() noexcept { return profiler_; }
    [[nodiscard]] const JitProfiler& profiler() const noexcept { return profiler_; }

    /// @brief Get the cache
    [[nodiscard]] JitCache& cache() noexcept { return cache_; }
    [[nodiscard]] const JitCache& cache() const noexcept { return cache_; }

    /// @brief Get the stencil registry
    [[nodiscard]] const StencilRegistry& stencils() const noexcept { return stencils_; }

private:
    /// @brief Private constructor - use create() factory
    explicit JitContext(const JitConfig& config);

    /// @brief Initialize all components
    [[nodiscard]] bool initialize();

    /// @brief JIT configuration
    JitConfig config_;

    /// @brief Profiler for tracking execution counts
    JitProfiler profiler_;

    /// @brief Stencil registry
    StencilRegistry stencils_;

    /// @brief Code buffer for compiled code
    std::unique_ptr<JitCodeBuffer> code_buffer_;

    /// @brief Compiled code cache
    JitCache cache_;

    /// @brief Whether initialization succeeded
    bool initialized_{false};
};

}  // namespace dotvm::jit
