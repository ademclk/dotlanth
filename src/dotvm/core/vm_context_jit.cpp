/// @file vm_context_jit.cpp
/// @brief JIT-related VmContext method implementations
///
/// Separated from vm_context.hpp to avoid circular dependencies
/// and reduce header size.

#include "dotvm/core/vm_context.hpp"
#include "dotvm/jit/jit_config.hpp"
#include "dotvm/jit/jit_context.hpp"

namespace dotvm::core {

// ============================================================================
// Constructors/Destructor (defined here where JitContext is complete)
// ============================================================================

VmContext::VmContext(VmConfig config) noexcept
    : config_{config},
      regs_{config.arch},
      mem_{config.max_memory},
      alu_{config.arch},
      call_stack_{determine_max_call_depth(config)},
      simd_enabled_{config.simd_enabled} {
    // Initialize CFI context if enabled
    if (config.cfi_enabled) {
        cfi_.emplace(config.cfi_policy, &mem_.security_stats());
    }

    // Initialize SIMD if enabled
    if (config.simd_enabled) {
        initialize_simd(config.simd_width);
    }

    // Initialize JIT if enabled (EXEC-012)
    if (config.jit_enabled) {
        enable_jit();
    }
}

VmContext::VmContext(Architecture arch) noexcept : VmContext{VmConfig::for_arch(arch)} {}

// Destructor must be defined here where JitContext is complete
// (unique_ptr requires complete type for destruction)
VmContext::~VmContext() = default;

bool VmContext::enable_jit(const jit::JitConfig& config) {
    // Don't re-enable if already enabled
    if (jit_ctx_) {
        return true;
    }

#if defined(__x86_64__) || defined(_M_X64)
    jit_ctx_ = jit::JitContext::create(config);
    return jit_ctx_ != nullptr;
#else
    // JIT only supported on x86-64
    (void)config;
    return false;
#endif
}

bool VmContext::enable_jit() {
    jit::JitConfig config;
    config.call_threshold = config_.jit_call_threshold;
    config.loop_threshold = config_.jit_loop_threshold;
    config.enabled = true;
    config.osr_enabled = config_.jit_loop_threshold > 0;
    return enable_jit(config);
}

void VmContext::disable_jit() noexcept {
    jit_ctx_.reset();
}

void VmContext::reset() noexcept {
    regs_.clear();
    vec_regs_.clear();
    call_stack_.clear();
    exception_ctx_.clear();
    if (cfi_) {
        cfi_->reset();
    }
    // JIT cache is cleared but JIT remains enabled
    if (jit_ctx_) {
        jit_ctx_->clear_cache();
    }
    // Rollback any active state transactions (STATE-004)
    state_ctx_.rollback_all();
}

}  // namespace dotvm::core
