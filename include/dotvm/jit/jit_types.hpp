// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2025 DotLanth Project

#ifndef DOTVM_JIT_TYPES_HPP
#define DOTVM_JIT_TYPES_HPP

#include <cstddef>
#include <cstdint>

namespace dotvm::jit {

/// Architecture detection for JIT compilation
#if defined(__x86_64__) || defined(_M_X64)
    #define DOTVM_JIT_X86_64 1
    #define DOTVM_JIT_SUPPORTED 1
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define DOTVM_JIT_ARM64 1
    #define DOTVM_JIT_SUPPORTED 1
#else
    #define DOTVM_JIT_UNSUPPORTED 1
#endif

/// Target architecture for JIT compilation
enum class TargetArch : std::uint8_t {
    X86_64 = 0,
    ARM64 = 1,
    Unsupported = 255
};

/// Get the current target architecture at compile time
[[nodiscard]] constexpr auto current_target_arch() noexcept -> TargetArch {
#if defined(DOTVM_JIT_X86_64)
    return TargetArch::X86_64;
#elif defined(DOTVM_JIT_ARM64)
    return TargetArch::ARM64;
#else
    return TargetArch::Unsupported;
#endif
}

/// Function identifier (bytecode entry PC)
using FunctionId = std::size_t;

/// Loop identifier (loop header PC)
using LoopId = std::size_t;

/// Native code pointer
using NativeCodePtr = void*;

/// Compilation status
enum class CompileStatus : std::uint8_t {
    Success = 0,
    Pending = 1,
    InProgress = 2,
    Failed = 3,
    Unsupported = 4
};

/// JIT compilation thresholds
namespace threshold {
    /// Number of function calls before JIT compilation is triggered
    inline constexpr std::uint64_t FUNCTION_CALLS = 10'000;

    /// Number of loop iterations before JIT compilation is triggered
    inline constexpr std::uint64_t LOOP_ITERATIONS = 100'000;

    /// Default JIT cache size in bytes (64 MB)
    inline constexpr std::size_t DEFAULT_CACHE_SIZE = 64 * 1024 * 1024;

    /// Minimum allocation size for code regions (4 KB page)
    inline constexpr std::size_t MIN_ALLOCATION_SIZE = 4096;
}

/// Patch location type for copy-and-patch compilation
enum class PatchType : std::uint8_t {
    /// 8-bit immediate value
    Imm8 = 0,
    /// 16-bit immediate value
    Imm16 = 1,
    /// 32-bit immediate value
    Imm32 = 2,
    /// 64-bit immediate value
    Imm64 = 3,
    /// Register file offset (scaled by 8 for 64-bit values)
    RegOffset = 4,
    /// Relative branch offset (PC-relative)
    RelBranch32 = 5,
    /// Absolute address
    AbsAddr64 = 6
};

/// Patch location descriptor
struct PatchLocation {
    /// Byte offset within the template
    std::size_t offset;
    /// Type of patch to apply
    PatchType type;
    /// Which operand to patch (0=Rd, 1=Rs1, 2=Rs2, etc.)
    std::uint8_t operand_index;
    /// Size of the patch in bytes
    std::uint8_t size;
};

/// JIT configuration options
struct JITConfig {
    /// Master enable switch
    bool enabled{false};

    /// Enable on-stack replacement for hot loops
    bool osr_enabled{true};

    /// Function call threshold for JIT compilation
    std::uint64_t function_threshold{threshold::FUNCTION_CALLS};

    /// Loop iteration threshold for JIT compilation
    std::uint64_t loop_threshold{threshold::LOOP_ITERATIONS};

    /// Maximum cache size in bytes
    std::size_t cache_size_bytes{threshold::DEFAULT_CACHE_SIZE};

    /// Target architecture (auto-detected by default)
    TargetArch target_arch{current_target_arch()};
};

/// Result of a JIT compilation attempt
struct CompileResult {
    /// Status of the compilation
    CompileStatus status{CompileStatus::Failed};

    /// Pointer to compiled native code (nullptr on failure)
    NativeCodePtr code_ptr{nullptr};

    /// Size of the compiled code in bytes
    std::size_t code_size{0};

    /// Error message on failure (empty on success)
    const char* error_message{nullptr};

    [[nodiscard]] constexpr auto is_success() const noexcept -> bool {
        return status == CompileStatus::Success && code_ptr != nullptr;
    }
};

/// x86-64 register indices for the JIT compiler
namespace x86_64 {
    /// Pinned register for RegisterFile base pointer
    inline constexpr int REG_FILE_BASE = 3;  // rbx

    /// Scratch registers for operations
    inline constexpr int SCRATCH_RAX = 0;
    inline constexpr int SCRATCH_RCX = 1;
    inline constexpr int SCRATCH_RDX = 2;
    inline constexpr int SCRATCH_RSI = 6;
    inline constexpr int SCRATCH_RDI = 7;
    inline constexpr int SCRATCH_R8 = 8;
    inline constexpr int SCRATCH_R9 = 9;
    inline constexpr int SCRATCH_R10 = 10;
    inline constexpr int SCRATCH_R11 = 11;

    /// Hot register cache (callee-saved, used for frequently-accessed DotVM regs)
    inline constexpr int HOT_R12 = 12;
    inline constexpr int HOT_R13 = 13;
    inline constexpr int HOT_R14 = 14;
    inline constexpr int HOT_R15 = 15;
}

/// ARM64 register indices for the JIT compiler
namespace arm64 {
    /// Pinned register for RegisterFile base pointer
    inline constexpr int REG_FILE_BASE = 19;  // x19

    /// Scratch registers for operations
    inline constexpr int SCRATCH_X0 = 0;
    inline constexpr int SCRATCH_X1 = 1;
    inline constexpr int SCRATCH_X2 = 2;
    inline constexpr int SCRATCH_X3 = 3;
    // ... x4-x18 also available

    /// Hot register cache (callee-saved)
    inline constexpr int HOT_X20 = 20;
    inline constexpr int HOT_X21 = 21;
    inline constexpr int HOT_X22 = 22;
    inline constexpr int HOT_X23 = 23;
    inline constexpr int HOT_X24 = 24;
    inline constexpr int HOT_X25 = 25;
    inline constexpr int HOT_X26 = 26;
    inline constexpr int HOT_X27 = 27;
    inline constexpr int HOT_X28 = 28;
}

}  // namespace dotvm::jit

#endif  // DOTVM_JIT_TYPES_HPP
