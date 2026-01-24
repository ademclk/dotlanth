#pragma once

/// @file vm_context.hpp
/// @brief VM execution context with architecture configuration
///
/// This header provides the VmConfig structure and VmContext class which
/// together carry the runtime state needed for VM execution, including
/// the target architecture, register file, memory manager, ALU, and SIMD support.

#include <cstdint>
#include <memory>
#include <optional>

#include "alu.hpp"
#include "arch_config.hpp"
#include "arch_types.hpp"
#include "call_stack.hpp"
#include "cfi.hpp"
#include "exception_context.hpp"
#include "memory.hpp"
#include "register_file.hpp"
#include "security_stats.hpp"
#include "simd/cpu_features.hpp"
#include "simd/simd_alu.hpp"
#include "simd/vector_register_file.hpp"
#include "dotvm/exec/state_execution_context.hpp"

// Forward declaration for JIT support
namespace dotvm::jit {
class JitContext;
struct JitConfig;
}  // namespace dotvm::jit

namespace dotvm::core {

// ============================================================================
// Resource Limits
// ============================================================================

/// Resource limit policy for VM execution
///
/// Defines limits that can be enforced during bytecode execution
/// to prevent resource exhaustion attacks. Use with VmConfig::sandboxed()
/// for untrusted bytecode.
struct ResourceLimits {
    /// Maximum number of instructions to execute (0 = unlimited)
    ///
    /// When the limit is reached, execution pauses with an error.
    /// Use to prevent infinite loops or excessive computation.
    std::uint64_t max_instructions = 0;

    /// Maximum number of memory allocations (0 = unlimited)
    ///
    /// Limits the number of allocate() calls, not total memory.
    std::uint64_t max_allocations = 0;

    /// Maximum total allocated bytes (0 = use VmConfig::max_memory)
    ///
    /// Separate from individual allocation size limits.
    std::size_t max_total_memory = 0;

    /// Maximum call stack depth (overrides CfiPolicy if set, 0 = use CfiPolicy)
    std::uint32_t max_call_depth = 0;

    /// Maximum backward jumps (overrides CfiPolicy if set, 0 = use CfiPolicy)
    std::uint32_t max_backward_jumps = 0;

    /// Creates default unlimited policy (no restrictions)
    [[nodiscard]] static constexpr ResourceLimits unlimited() noexcept { return ResourceLimits{}; }

    /// Creates a restricted policy suitable for untrusted bytecode
    ///
    /// Default limits:
    /// - 1M instructions
    /// - 1K allocations
    /// - 16MB total memory
    /// - 256 call depth
    /// - 10K backward jumps
    [[nodiscard]] static constexpr ResourceLimits restricted() noexcept {
        return ResourceLimits{.max_instructions = 1'000'000,
                              .max_allocations = 1'000,
                              .max_total_memory = std::size_t{16} * 1024 * 1024,
                              .max_call_depth = 256,
                              .max_backward_jumps = 10'000};
    }

    /// Check if any limits are set
    [[nodiscard]] constexpr bool has_limits() const noexcept {
        return max_instructions > 0 || max_allocations > 0 || max_total_memory > 0 ||
               max_call_depth > 0 || max_backward_jumps > 0;
    }

    constexpr bool operator==(const ResourceLimits&) const noexcept = default;
};

// ============================================================================
// VM Configuration
// ============================================================================

/// Runtime configuration for VM execution
///
/// This structure contains the configuration options that affect how
/// the VM executes bytecode. It is typically initialized from a
/// BytecodeHeader or explicitly constructed for testing.
struct VmConfig {
    /// Target architecture for execution
    ///
    /// Arch32: 32-bit integers, 4GB address space
    /// Arch64: 48-bit integers (NaN-boxing), full address space
    Architecture arch = Architecture::Arch64;

    /// Whether to use strict overflow checking
    ///
    /// If true, arithmetic overflow may generate errors.
    /// If false (default), overflow wraps silently.
    bool strict_overflow = false;

    /// Maximum memory that can be allocated
    ///
    /// Defaults to the global maximum. Can be reduced for sandboxing.
    std::size_t max_memory = mem_config::MAX_ALLOCATION_SIZE;

    /// Whether to enable CFI (Control Flow Integrity) checks
    ///
    /// When enabled, jump targets and call/return pairs are validated.
    bool cfi_enabled = false;

    /// CFI policy configuration (used when cfi_enabled is true)
    cfi::CfiPolicy cfi_policy{};

    /// Resource limits for sandboxed execution (optional)
    ResourceLimits resource_limits{};

    /// SIMD vector width (0 = auto-detect from CPU)
    ///
    /// When set to 0, the optimal SIMD width is detected at runtime.
    /// Valid explicit values: 128, 256, 512
    std::size_t simd_width = 0;

    /// Enable SIMD operations
    ///
    /// When false (default), SIMD operations are disabled and vector
    /// registers/ALU are not initialized.
    bool simd_enabled = false;

    /// Enable JIT compilation (EXEC-012)
    ///
    /// When true, hot functions are compiled to native code.
    /// Requires x86-64 platform.
    bool jit_enabled = false;

    /// JIT compilation threshold (calls before JIT)
    ///
    /// Default is 10,000 calls. Lower values mean faster compilation
    /// but more compilation overhead.
    std::uint32_t jit_call_threshold = 10'000;

    /// JIT OSR (On-Stack Replacement) threshold
    ///
    /// Number of loop iterations before OSR triggers.
    /// Default is 100,000. Set to 0 to disable OSR.
    std::uint32_t jit_loop_threshold = 100'000;

    /// Creates a default configuration for the given architecture
    [[nodiscard]] static constexpr VmConfig for_arch(Architecture arch) noexcept {
        return VmConfig{.arch = arch};
    }

    /// Creates a 32-bit mode configuration
    [[nodiscard]] static constexpr VmConfig arch32() noexcept {
        return VmConfig{.arch = Architecture::Arch32};
    }

    /// Creates a 64-bit mode configuration
    [[nodiscard]] static constexpr VmConfig arch64() noexcept {
        return VmConfig{.arch = Architecture::Arch64};
    }

    /// Creates a security-hardened configuration
    [[nodiscard]] static constexpr VmConfig secure() noexcept {
        return VmConfig{.arch = Architecture::Arch64,
                        .strict_overflow = true,
                        .max_memory = mem_config::MAX_ALLOCATION_SIZE,
                        .cfi_enabled = true,
                        .cfi_policy = cfi::CfiPolicy::strict()};
    }

    /// Creates a fully sandboxed configuration for untrusted bytecode
    ///
    /// Combines security features with resource limits:
    /// - CFI with strict policy
    /// - Strict overflow checking
    /// - Resource limits (instruction, memory, call depth)
    /// - Reduced memory allocation cap
    [[nodiscard]] static constexpr VmConfig sandboxed() noexcept {
        return VmConfig{.arch = Architecture::Arch64,
                        .strict_overflow = true,
                        .max_memory = std::size_t{16} * 1024 * 1024,  // 16MB per-allocation limit
                        .cfi_enabled = true,
                        .cfi_policy = cfi::CfiPolicy::strict(),
                        .resource_limits = ResourceLimits::restricted()};
    }

    /// Creates a scalar-only configuration (no SIMD)
    ///
    /// SIMD operations are explicitly disabled.
    [[nodiscard]] static constexpr VmConfig scalar_only() noexcept {
        return VmConfig{.arch = Architecture::Arch64, .simd_width = 0, .simd_enabled = false};
    }

    /// Creates a 128-bit SIMD configuration
    ///
    /// Enables SIMD with 128-bit vectors (SSE/NEON equivalent).
    [[nodiscard]] static constexpr VmConfig simd128() noexcept {
        return VmConfig{.arch = Architecture::Arch64, .simd_width = 128, .simd_enabled = true};
    }

    /// Creates a 256-bit SIMD configuration
    ///
    /// Enables SIMD with 256-bit vectors (AVX2 equivalent).
    [[nodiscard]] static constexpr VmConfig simd256() noexcept {
        return VmConfig{.arch = Architecture::Arch64, .simd_width = 256, .simd_enabled = true};
    }

    /// Creates a 512-bit SIMD configuration
    ///
    /// Enables SIMD with 512-bit vectors (AVX-512 equivalent).
    [[nodiscard]] static constexpr VmConfig simd512() noexcept {
        return VmConfig{.arch = Architecture::Arch64, .simd_width = 512, .simd_enabled = true};
    }

    /// Creates a configuration with auto-detected SIMD
    ///
    /// Enables SIMD and auto-detects the optimal vector width
    /// based on CPU capabilities at runtime.
    [[nodiscard]] static constexpr VmConfig auto_detect() noexcept {
        return VmConfig{.arch = Architecture::Arch64,
                        .simd_width = 0,  // 0 = auto-detect
                        .simd_enabled = true};
    }

    /// Creates a JIT-enabled configuration (EXEC-012)
    ///
    /// Enables JIT compilation with default thresholds.
    [[nodiscard]] static constexpr VmConfig with_jit() noexcept {
        return VmConfig{.arch = Architecture::Arch64,
                        .jit_enabled = true,
                        .jit_call_threshold = 10'000,
                        .jit_loop_threshold = 100'000};
    }

    /// Creates an aggressive JIT configuration
    ///
    /// Lower thresholds for faster compilation, useful for benchmarks.
    [[nodiscard]] static constexpr VmConfig with_jit_aggressive() noexcept {
        return VmConfig{.arch = Architecture::Arch64,
                        .jit_enabled = true,
                        .jit_call_threshold = 1'000,
                        .jit_loop_threshold = 10'000};
    }

    constexpr bool operator==(const VmConfig&) const noexcept = default;
};

// ============================================================================
// VM Execution Context
// ============================================================================

/// Execution context for the VM
///
/// VmContext encapsulates all the state needed to execute bytecode:
/// - Configuration (architecture, limits)
/// - Register file (architecture-aware)
/// - Memory manager
/// - ALU (architecture-aware arithmetic)
/// - SIMD vector registers and ALU (optional)
///
/// The context provides convenience methods for creating values and
/// computing addresses that respect the configured architecture.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class VmContext {
public:
    /// Construct a context with the given configuration
    ///
    /// @param config VM configuration
    /// @note Defined in vm_context_jit.cpp to keep JitContext an incomplete type
    explicit VmContext(VmConfig config = {}) noexcept;

    /// Construct a context for a specific architecture
    ///
    /// @param arch Target architecture
    explicit VmContext(Architecture arch) noexcept;

    /// Destructor
    ///
    /// Defined in vm_context_jit.cpp to allow JitContext destructor to be called
    /// (unique_ptr requires complete type at point of destruction).
    ~VmContext();

    // Non-copyable, non-movable (MemoryManager is non-movable)
    VmContext(const VmContext&) = delete;
    VmContext& operator=(const VmContext&) = delete;
    VmContext(VmContext&&) = delete;
    VmContext& operator=(VmContext&&) = delete;

    // =========================================================================
    // Configuration Access
    // =========================================================================

    /// Get the VM configuration
    [[nodiscard]] const VmConfig& config() const noexcept { return config_; }

    /// Get the target architecture
    [[nodiscard]] Architecture arch() const noexcept { return config_.arch; }

    /// Check if running in 32-bit mode
    [[nodiscard]] bool is_arch32() const noexcept { return arch_config::is_arch32(config_.arch); }

    /// Check if running in 64-bit mode
    [[nodiscard]] bool is_arch64() const noexcept { return arch_config::is_arch64(config_.arch); }

    // =========================================================================
    // Architecture-Aware Value Creation
    // =========================================================================

    /// Create an integer Value with architecture-appropriate masking
    ///
    /// In Arch32 mode, the value is masked to 32 bits with sign extension.
    /// In Arch64 mode, the value is stored using full 48-bit range.
    ///
    /// @param val The integer value
    /// @return A Value containing the masked integer
    [[nodiscard]] Value make_int(std::int64_t val) const noexcept {
        return Value::from_int(arch_config::mask_int(val, config_.arch));
    }

    /// Mask an existing Value to the architecture width
    ///
    /// @param val The value to mask
    /// @return A Value with integer masked to architecture width
    [[nodiscard]] Value mask_value(Value val) const noexcept {
        return val.mask_to_arch(config_.arch);
    }

    // =========================================================================
    // Architecture-Aware Address Computation
    // =========================================================================

    /// Mask a memory address for the architecture
    ///
    /// In Arch32 mode, addresses are limited to 4GB.
    /// In Arch64 mode, addresses use the 48-bit canonical range.
    ///
    /// @param addr The address to mask
    /// @return The masked address
    [[nodiscard]] std::uint64_t mask_address(std::uint64_t addr) const noexcept {
        return arch_config::mask_addr(addr, config_.arch);
    }

    /// Compute an address from base + offset with architecture masking
    ///
    /// @param base Base address
    /// @param offset Offset to add
    /// @return The computed and masked address
    [[nodiscard]] std::size_t compute_address(std::size_t base, std::size_t offset) const noexcept {
        return MemoryManager::compute_address(base, offset, config_.arch);
    }

    /// Check if an address is valid for the architecture
    ///
    /// @param addr The address to check
    /// @return true if the address fits in the architecture's address space
    [[nodiscard]] bool is_valid_address(std::uint64_t addr) const noexcept {
        return arch_config::addr_fits_in_arch(addr, config_.arch);
    }

    // =========================================================================
    // Component Access
    // =========================================================================

    /// Get mutable reference to the register file
    [[nodiscard]] ArchRegisterFile& registers() noexcept { return regs_; }

    /// Get const reference to the register file
    [[nodiscard]] const ArchRegisterFile& registers() const noexcept { return regs_; }

    /// Get mutable reference to the memory manager
    [[nodiscard]] MemoryManager& memory() noexcept { return mem_; }

    /// Get const reference to the memory manager
    [[nodiscard]] const MemoryManager& memory() const noexcept { return mem_; }

    /// Get mutable reference to the ALU
    [[nodiscard]] ALU& alu() noexcept { return alu_; }

    /// Get const reference to the ALU
    [[nodiscard]] const ALU& alu() const noexcept { return alu_; }

    // =========================================================================
    // CFI Access (Control Flow Integrity)
    // =========================================================================

    /// Check if CFI is enabled
    [[nodiscard]] bool cfi_enabled() const noexcept { return cfi_.has_value(); }

    /// Get mutable reference to CFI context (only valid if cfi_enabled())
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    [[nodiscard]] cfi::CfiContext& cfi() noexcept { return *cfi_; }

    /// Get const reference to CFI context (only valid if cfi_enabled())
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    [[nodiscard]] const cfi::CfiContext& cfi() const noexcept { return *cfi_; }

    /// Get optional CFI context (safe access)
    [[nodiscard]] std::optional<cfi::CfiContext>& cfi_opt() noexcept { return cfi_; }

    /// Get const optional CFI context (safe access)
    [[nodiscard]] const std::optional<cfi::CfiContext>& cfi_opt() const noexcept { return cfi_; }

    // =========================================================================
    // Call Stack Access (EXEC-007)
    // =========================================================================

    /// Get mutable reference to the call stack
    ///
    /// The call stack manages function call frames including saved
    /// callee-saved registers (R16-R31) and return addresses.
    [[nodiscard]] CallStack& call_stack() noexcept { return call_stack_; }

    /// Get const reference to the call stack
    [[nodiscard]] const CallStack& call_stack() const noexcept { return call_stack_; }

    // =========================================================================
    // Exception Context Access (EXEC-011)
    // =========================================================================

    /// Get mutable reference to the exception context
    ///
    /// The exception context manages exception frames for TRY/CATCH blocks
    /// and tracks the current exception state during execution.
    [[nodiscard]] ExceptionContext& exception_context() noexcept { return exception_ctx_; }

    /// Get const reference to the exception context
    [[nodiscard]] const ExceptionContext& exception_context() const noexcept {
        return exception_ctx_;
    }

    /// Check if an exception is currently pending
    ///
    /// @return true if there is an active exception waiting to be handled
    [[nodiscard]] bool has_pending_exception() const noexcept {
        return exception_ctx_.has_pending_exception();
    }

    // =========================================================================
    // SIMD Access
    // =========================================================================

    /// Check if SIMD is enabled
    ///
    /// @return true if SIMD operations are available
    [[nodiscard]] bool simd_enabled() const noexcept { return simd_enabled_; }

    /// Get the active SIMD architecture
    ///
    /// @return The SIMD architecture in use, or Arch64 if SIMD is disabled
    [[nodiscard]] Architecture simd_architecture() const noexcept { return simd_arch_; }

    /// Access vector registers (V0-V31)
    ///
    /// @return Mutable reference to the vector register file
    /// @note Always available, but operations have no effect if SIMD disabled
    [[nodiscard]] simd::VectorRegisterFile& vec_registers() noexcept { return vec_regs_; }

    /// Access vector registers (V0-V31) - const version
    ///
    /// @return Const reference to the vector register file
    [[nodiscard]] const simd::VectorRegisterFile& vec_registers() const noexcept {
        return vec_regs_;
    }

    /// Access SIMD ALU
    ///
    /// @return Pointer to the SIMD ALU, or nullptr if SIMD is disabled
    [[nodiscard]] simd::SimdAlu* simd_alu() noexcept { return simd_alu_.get(); }

    /// Access SIMD ALU - const version
    ///
    /// @return Const pointer to the SIMD ALU, or nullptr if SIMD is disabled
    [[nodiscard]] const simd::SimdAlu* simd_alu() const noexcept { return simd_alu_.get(); }

    // =========================================================================
    // JIT Access (EXEC-012)
    // =========================================================================

    /// Check if JIT compilation is enabled
    ///
    /// @return true if JIT is available
    [[nodiscard]] bool jit_enabled() const noexcept { return jit_ctx_ != nullptr; }

    /// Get mutable pointer to JIT context
    ///
    /// @return Pointer to JIT context, or nullptr if JIT disabled
    [[nodiscard]] jit::JitContext* jit_context() noexcept { return jit_ctx_.get(); }

    /// Get const pointer to JIT context
    ///
    /// @return Const pointer to JIT context, or nullptr if JIT disabled
    [[nodiscard]] const jit::JitContext* jit_context() const noexcept { return jit_ctx_.get(); }

    /// Enable JIT compilation with custom configuration
    ///
    /// @param config JIT configuration (call and loop thresholds, etc.)
    /// @return true if JIT was successfully enabled
    bool enable_jit(const jit::JitConfig& config);

    /// Enable JIT compilation with default configuration from VmConfig
    ///
    /// Uses the jit_call_threshold and jit_loop_threshold from VmConfig.
    /// @return true if JIT was successfully enabled
    bool enable_jit();

    /// Disable JIT compilation and clear compiled code cache
    void disable_jit() noexcept;

    // =========================================================================
    // Security Statistics
    // =========================================================================

    /// Get const reference to security statistics
    [[nodiscard]] const SecurityStats& security_stats() const noexcept {
        return mem_.security_stats();
    }

    /// Get mutable reference to security statistics
    [[nodiscard]] SecurityStats& security_stats() noexcept { return mem_.security_stats(); }

    // =========================================================================
    // State Backend Access (STATE-004)
    // =========================================================================

    /// Check if state operations are enabled
    [[nodiscard]] bool state_enabled() const noexcept { return state_ctx_.is_enabled(); }

    /// Get mutable reference to the state execution context
    ///
    /// The state context manages transaction handles for state opcodes.
    [[nodiscard]] exec::StateExecutionContext& state_context() noexcept { return state_ctx_; }

    /// Get const reference to the state execution context
    [[nodiscard]] const exec::StateExecutionContext& state_context() const noexcept {
        return state_ctx_;
    }

    /// Enable state operations with a TransactionManager
    ///
    /// @param tx_mgr Pointer to TransactionManager (must outlive this VmContext)
    void enable_state(state::TransactionManager* tx_mgr) noexcept { state_ctx_.enable(tx_mgr); }

    /// Disable state operations and rollback all active transactions
    void disable_state() noexcept { state_ctx_.disable(); }

    // =========================================================================
    // State Management
    // =========================================================================

    /// Reset the context to initial state
    ///
    /// Clears all registers (scalar and vector), call stack, exception context,
    /// CFI state, and JIT cache. Memory allocations persist until deallocated
    /// individually. Configuration is preserved.
    void reset() noexcept;

    /// Get statistics about the context state
    struct Stats {
        std::size_t active_allocations;
        std::size_t total_allocated_bytes;
    };

    [[nodiscard]] Stats stats() const noexcept {
        return Stats{.active_allocations = mem_.active_allocations(),
                     .total_allocated_bytes = mem_.total_allocated_bytes()};
    }

private:
    /// Determine the maximum call depth from configuration
    ///
    /// Priority: ResourceLimits > CfiPolicy > Default (1024)
    [[nodiscard]] static constexpr std::size_t
    determine_max_call_depth(const VmConfig& config) noexcept {
        if (config.resource_limits.max_call_depth > 0) {
            return config.resource_limits.max_call_depth;
        }
        if (config.cfi_policy.max_call_depth > 0) {
            return config.cfi_policy.max_call_depth;
        }
        return DEFAULT_MAX_CALL_DEPTH;
    }

    /// Initialize SIMD subsystem
    ///
    /// @param width Requested SIMD width (0 = auto-detect)
    void initialize_simd(std::size_t width) noexcept {
        // Determine the SIMD architecture based on width or auto-detection
        if (width == 0) {
            // Auto-detect optimal architecture
            simd_arch_ = simd::select_optimal_simd_arch();
        } else if (width >= 512) {
            simd_arch_ = Architecture::Arch512;
        } else if (width >= 256) {
            simd_arch_ = Architecture::Arch256;
        } else {
            simd_arch_ = Architecture::Arch128;
        }

        // Create the SIMD ALU with the determined architecture
        simd_alu_ = std::make_unique<simd::SimdAlu>(simd_arch_);

        // Verify CPU support - fall back if necessary
        if (!simd_alu_->features().supports_arch(simd_arch_)) {
            simd_arch_ = simd_alu_->arch();  // Use ALU's fallback
        }
    }

    VmConfig config_;
    ArchRegisterFile regs_;
    MemoryManager mem_;
    ALU alu_;
    std::optional<cfi::CfiContext> cfi_;
    CallStack call_stack_;            // EXEC-007: Call stack for frame management
    ExceptionContext exception_ctx_;  // EXEC-011: Exception handling context

    // State support (STATE-004)
    exec::StateExecutionContext state_ctx_;

    // SIMD support
    simd::VectorRegisterFile vec_regs_;
    std::unique_ptr<simd::SimdAlu> simd_alu_;
    bool simd_enabled_ = false;
    Architecture simd_arch_ = Architecture::Arch64;

    // JIT support (EXEC-012)
    std::unique_ptr<jit::JitContext> jit_ctx_;
};

}  // namespace dotvm::core
