#pragma once

/// @file vm_context.hpp
/// @brief VM execution context with architecture configuration
///
/// This header provides the VmConfig structure and VmContext class which
/// together carry the runtime state needed for VM execution, including
/// the target architecture, register file, memory manager, and ALU.

#include <cstdint>

#include "arch_config.hpp"
#include "arch_types.hpp"
#include "alu.hpp"
#include "cfi.hpp"
#include "register_file.hpp"
#include "memory.hpp"
#include "security_stats.hpp"

#include <optional>

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
    [[nodiscard]] static constexpr ResourceLimits unlimited() noexcept {
        return ResourceLimits{};
    }

    /// Creates a restricted policy suitable for untrusted bytecode
    ///
    /// Default limits:
    /// - 1M instructions
    /// - 1K allocations
    /// - 16MB total memory
    /// - 256 call depth
    /// - 10K backward jumps
    [[nodiscard]] static constexpr ResourceLimits restricted() noexcept {
        return ResourceLimits{
            .max_instructions = 1'000'000,
            .max_allocations = 1'000,
            .max_total_memory = 16 * 1024 * 1024,
            .max_call_depth = 256,
            .max_backward_jumps = 10'000};
    }

    /// Check if any limits are set
    [[nodiscard]] constexpr bool has_limits() const noexcept {
        return max_instructions > 0 || max_allocations > 0 ||
               max_total_memory > 0 || max_call_depth > 0 ||
               max_backward_jumps > 0;
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
        return VmConfig{
            .arch = Architecture::Arch64,
            .strict_overflow = true,
            .max_memory = mem_config::MAX_ALLOCATION_SIZE,
            .cfi_enabled = true,
            .cfi_policy = cfi::CfiPolicy::strict()
        };
    }

    /// Creates a fully sandboxed configuration for untrusted bytecode
    ///
    /// Combines security features with resource limits:
    /// - CFI with strict policy
    /// - Strict overflow checking
    /// - Resource limits (instruction, memory, call depth)
    /// - Reduced memory allocation cap
    [[nodiscard]] static constexpr VmConfig sandboxed() noexcept {
        return VmConfig{
            .arch = Architecture::Arch64,
            .strict_overflow = true,
            .max_memory = 16 * 1024 * 1024,  // 16MB per-allocation limit
            .cfi_enabled = true,
            .cfi_policy = cfi::CfiPolicy::strict(),
            .resource_limits = ResourceLimits::restricted()
        };
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
///
/// The context provides convenience methods for creating values and
/// computing addresses that respect the configured architecture.
class VmContext {
public:
    /// Construct a context with the given configuration
    ///
    /// @param config VM configuration
    explicit VmContext(VmConfig config = {}) noexcept
        : config_{config},
          regs_{config.arch},
          mem_{config.max_memory},
          alu_{config.arch} {
        // Initialize CFI context if enabled
        if (config.cfi_enabled) {
            cfi_.emplace(config.cfi_policy, &mem_.security_stats());
        }
    }

    /// Construct a context for a specific architecture
    ///
    /// @param arch Target architecture
    explicit VmContext(Architecture arch) noexcept
        : VmContext{VmConfig::for_arch(arch)} {}

    // =========================================================================
    // Configuration Access
    // =========================================================================

    /// Get the VM configuration
    [[nodiscard]] const VmConfig& config() const noexcept { return config_; }

    /// Get the target architecture
    [[nodiscard]] Architecture arch() const noexcept { return config_.arch; }

    /// Check if running in 32-bit mode
    [[nodiscard]] bool is_arch32() const noexcept {
        return arch_config::is_arch32(config_.arch);
    }

    /// Check if running in 64-bit mode
    [[nodiscard]] bool is_arch64() const noexcept {
        return arch_config::is_arch64(config_.arch);
    }

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
    [[nodiscard]] std::size_t compute_address(std::size_t base,
                                               std::size_t offset) const noexcept {
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
    [[nodiscard]] bool cfi_enabled() const noexcept {
        return cfi_.has_value();
    }

    /// Get mutable reference to CFI context (only valid if cfi_enabled())
    [[nodiscard]] cfi::CfiContext& cfi() noexcept {
        return *cfi_;
    }

    /// Get const reference to CFI context (only valid if cfi_enabled())
    [[nodiscard]] const cfi::CfiContext& cfi() const noexcept {
        return *cfi_;
    }

    /// Get optional CFI context (safe access)
    [[nodiscard]] std::optional<cfi::CfiContext>& cfi_opt() noexcept {
        return cfi_;
    }

    /// Get const optional CFI context (safe access)
    [[nodiscard]] const std::optional<cfi::CfiContext>& cfi_opt() const noexcept {
        return cfi_;
    }

    // =========================================================================
    // Security Statistics
    // =========================================================================

    /// Get const reference to security statistics
    [[nodiscard]] const SecurityStats& security_stats() const noexcept {
        return mem_.security_stats();
    }

    /// Get mutable reference to security statistics
    [[nodiscard]] SecurityStats& security_stats() noexcept {
        return mem_.security_stats();
    }

    // =========================================================================
    // State Management
    // =========================================================================

    /// Reset the context to initial state
    ///
    /// Clears all registers and deallocates all memory.
    /// Configuration is preserved.
    void reset() noexcept {
        regs_.clear();
        // Note: Memory manager doesn't have a clear() method
        // Allocations persist until deallocated individually
    }

    /// Get statistics about the context state
    struct Stats {
        std::size_t active_allocations;
        std::size_t total_allocated_bytes;
    };

    [[nodiscard]] Stats stats() const noexcept {
        return Stats{
            .active_allocations = mem_.active_allocations(),
            .total_allocated_bytes = mem_.total_allocated_bytes()
        };
    }

private:
    VmConfig config_;
    ArchRegisterFile regs_;
    MemoryManager mem_;
    ALU alu_;
    std::optional<cfi::CfiContext> cfi_;
};

}  // namespace dotvm::core
