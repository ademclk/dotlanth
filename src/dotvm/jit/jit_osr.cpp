/// @file jit_osr.cpp
/// @brief Implementation of On-Stack Replacement manager

#include "dotvm/jit/jit_osr.hpp"

#include "dotvm/jit/jit_context.hpp"

namespace dotvm::jit {

OsrManager::OsrManager(JitContext& jit_ctx) noexcept : jit_ctx_(jit_ctx) {}

bool OsrManager::should_trigger(LoopId loop_id) const noexcept {
    // Check if OSR is enabled and entry point exists
    if (!jit_ctx_.osr_enabled()) {
        return false;
    }

    // Check if loop is registered and has triggered OSR
    const auto* loop_profile = jit_ctx_.profiler().get_loop(loop_id);
    if (!loop_profile) {
        return false;
    }

    // OSR is ready if the entry point is available
    return jit_ctx_.cache().lookup_osr(loop_id) != nullptr;
}

OsrStatus OsrManager::transfer(LoopId loop_id, const OsrState& state) noexcept {
    ++stats_.transfers_attempted;

    if (!jit_ctx_.osr_enabled()) [[unlikely]] {
        ++stats_.transfers_failed;
        return OsrStatus::Disabled;
    }

    // Look up OSR entry point
    const auto* osr_entry = jit_ctx_.lookup_osr(loop_id);
    if (!osr_entry) [[unlikely]] {
        ++stats_.transfers_failed;
        return OsrStatus::NoEntryPoint;
    }

    // Validate state
    if (!state.registers || !state.context) [[unlikely]] {
        ++stats_.transfers_failed;
        return OsrStatus::StateTransferFailed;
    }

    // Execute the compiled code starting at the OSR entry point
    // Note: In a real implementation, this would:
    // 1. Save the current stack state
    // 2. Set up registers according to compiled code layout
    // 3. Jump to the native code entry point
    // For now, we use a simplified approach via function call

    auto fn =
        reinterpret_cast<void (*)(void*, void*)>(const_cast<std::uint8_t*>(osr_entry->entry_point));

    // Call the compiled code
    fn(state.registers, state.context);

    ++stats_.transfers_succeeded;
    return OsrStatus::Success;
}

bool OsrManager::has_osr_entry(LoopId loop_id) const noexcept {
    return jit_ctx_.lookup_osr(loop_id) != nullptr;
}

}  // namespace dotvm::jit
