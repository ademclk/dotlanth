/// @file jit_osr.hpp
/// @brief On-Stack Replacement (OSR) manager for hot loops
///
/// Provides state transfer logic for transitioning from interpreter
/// to JIT-compiled code at loop headers.

#pragma once

#include "jit_config.hpp"
#include "jit_profiler.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace dotvm::jit {

// Forward declarations
class JitContext;

/// @brief State that needs to be transferred during OSR
///
/// Contains all live values at the OSR entry point that the
/// compiled code needs to continue execution.
struct OsrState {
    /// @brief PC in bytecode where OSR is happening
    std::size_t bytecode_pc{0};

    /// @brief Target PC in native code
    const std::uint8_t* native_pc{nullptr};

    /// @brief Pointer to register file
    void* registers{nullptr};

    /// @brief Pointer to VM context
    void* context{nullptr};

    /// @brief Loop iteration count at transfer
    std::uint32_t iteration_count{0};
};

/// @brief On-Stack Replacement manager
///
/// Handles the transition from interpreted code to JIT-compiled
/// code when a hot loop is detected. This includes:
/// - State capture from interpreter
/// - State mapping to compiled code layout
/// - Transfer execution to native code
///
/// @example
/// ```cpp
/// OsrManager osr(jit_ctx);
///
/// // At a backward jump, check if OSR should trigger
/// if (osr.should_trigger(loop_id)) {
///     OsrState state;
///     state.bytecode_pc = current_pc;
///     state.registers = &regs;
///     state.context = &ctx;
///
///     auto result = osr.transfer(loop_id, state);
///     if (result == OsrStatus::Success) {
///         // Execution continues in native code
///     }
/// }
/// ```
class OsrManager {
public:
    /// @brief Create an OSR manager
    explicit OsrManager(JitContext& jit_ctx) noexcept;

    /// @brief Check if OSR should trigger for a loop
    ///
    /// @param loop_id Loop ID
    /// @return true if OSR is ready and should be attempted
    [[nodiscard]] bool should_trigger(LoopId loop_id) const noexcept;

    /// @brief Attempt OSR transfer
    ///
    /// Transfers execution from interpreter to compiled code.
    /// On success, does not return (continues in native code).
    /// On failure, returns error status.
    ///
    /// @param loop_id Target loop
    /// @param state Current interpreter state
    /// @return OsrStatus::Success if transfer succeeded, error otherwise
    [[nodiscard]] OsrStatus transfer(LoopId loop_id, const OsrState& state) noexcept;

    /// @brief Check if OSR is available for a loop
    ///
    /// @param loop_id Loop to check
    /// @return true if OSR entry point exists
    [[nodiscard]] bool has_osr_entry(LoopId loop_id) const noexcept;

    /// @brief Get OSR statistics
    struct Stats {
        std::size_t transfers_attempted{0};
        std::size_t transfers_succeeded{0};
        std::size_t transfers_failed{0};
    };

    [[nodiscard]] Stats stats() const noexcept { return stats_; }

private:
    /// @brief Reference to JIT context
    JitContext& jit_ctx_;

    /// @brief Statistics
    Stats stats_;
};

} // namespace dotvm::jit
