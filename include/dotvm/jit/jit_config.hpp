/// @file jit_config.hpp
/// @brief JIT compiler configuration and threshold constants
///
/// Defines configuration parameters for the JIT compilation subsystem including
/// compilation thresholds, code cache limits, and feature flags.

#pragma once

#include <cstddef>
#include <cstdint>

namespace dotvm::jit {

/// @brief JIT compilation thresholds
namespace thresholds {

/// @brief Number of function calls before JIT compilation triggers
inline constexpr std::uint32_t CALL_THRESHOLD = 10'000;

/// @brief Number of loop iterations before OSR triggers
inline constexpr std::uint32_t LOOP_THRESHOLD = 100'000;

/// @brief Default maximum code cache size (64 MiB)
inline constexpr std::size_t DEFAULT_MAX_CODE_CACHE = std::size_t{64} * 1024 * 1024;

/// @brief Minimum code buffer allocation size (4 KiB = 1 page)
inline constexpr std::size_t MIN_CODE_BUFFER_SIZE = std::size_t{4} * 1024;

/// @brief Maximum single function compiled size (1 MiB)
inline constexpr std::size_t MAX_FUNCTION_CODE_SIZE = std::size_t{1} * 1024 * 1024;

}  // namespace thresholds

/// @brief JIT compiler configuration
///
/// Controls the behavior of the JIT compilation subsystem including
/// when compilation triggers and resource limits.
///
/// @example
/// ```cpp
/// // Use default configuration
/// JitConfig config;
///
/// // Aggressive compilation (lower thresholds)
/// auto aggressive = JitConfig::aggressive();
///
/// // Disable JIT entirely
/// auto disabled = JitConfig::disabled();
/// ```
struct JitConfig {
    /// @brief Number of function calls before JIT compilation
    std::uint32_t call_threshold = thresholds::CALL_THRESHOLD;

    /// @brief Number of loop iterations before On-Stack Replacement
    std::uint32_t loop_threshold = thresholds::LOOP_THRESHOLD;

    /// @brief Maximum size of the compiled code cache in bytes
    std::size_t max_code_cache = thresholds::DEFAULT_MAX_CODE_CACHE;

    /// @brief Whether JIT compilation is enabled
    bool enabled = true;

    /// @brief Whether On-Stack Replacement is enabled for hot loops
    bool osr_enabled = true;

    /// @brief Enable debug information in compiled code
    bool debug_info = false;

    /// @brief Enable bounds checking in JIT-compiled code
    /// @note Disabling may improve performance but reduces safety
    bool bounds_checking = true;

    /// @brief Create default JIT configuration
    [[nodiscard]] static constexpr JitConfig defaults() noexcept { return JitConfig{}; }

    /// @brief Create aggressive JIT configuration with lower thresholds
    ///
    /// Triggers JIT compilation after fewer iterations, useful for
    /// benchmarks or when startup time is less important than peak perf.
    [[nodiscard]] static constexpr JitConfig aggressive() noexcept {
        JitConfig config;
        config.call_threshold = 1'000;
        config.loop_threshold = 10'000;
        return config;
    }

    /// @brief Create configuration with JIT disabled
    [[nodiscard]] static constexpr JitConfig disabled() noexcept {
        JitConfig config;
        config.enabled = false;
        config.osr_enabled = false;
        return config;
    }

    /// @brief Create configuration with only method JIT (no OSR)
    [[nodiscard]] static constexpr JitConfig method_only() noexcept {
        JitConfig config;
        config.osr_enabled = false;
        return config;
    }

    /// @brief Create debug configuration with extra safety checks
    [[nodiscard]] static constexpr JitConfig debug() noexcept {
        JitConfig config;
        config.debug_info = true;
        config.bounds_checking = true;
        config.call_threshold = 100;  // Compile quickly for debugging
        config.loop_threshold = 1'000;
        return config;
    }

    /// @brief Check if this configuration allows JIT compilation
    [[nodiscard]] constexpr bool allows_jit() const noexcept {
        return enabled && max_code_cache > 0;
    }

    /// @brief Check if this configuration allows OSR
    [[nodiscard]] constexpr bool allows_osr() const noexcept { return enabled && osr_enabled; }
};

/// @brief JIT compilation status codes
enum class JitStatus : std::uint8_t {
    /// @brief Compilation succeeded
    Success = 0,

    /// @brief JIT is disabled in configuration
    Disabled,

    /// @brief Function has not reached compilation threshold
    BelowThreshold,

    /// @brief Code cache is full
    CacheFull,

    /// @brief Function contains unsupported opcodes
    UnsupportedOpcode,

    /// @brief Memory allocation failed
    AllocationFailed,

    /// @brief Memory protection change failed
    ProtectionFailed,

    /// @brief Invalid function boundaries
    InvalidFunction,

    /// @brief Internal compiler error
    InternalError,
};

/// @brief Convert JIT status to string for debugging
[[nodiscard]] constexpr const char* jit_status_string(JitStatus status) noexcept {
    switch (status) {
        case JitStatus::Success:
            return "Success";
        case JitStatus::Disabled:
            return "Disabled";
        case JitStatus::BelowThreshold:
            return "BelowThreshold";
        case JitStatus::CacheFull:
            return "CacheFull";
        case JitStatus::UnsupportedOpcode:
            return "UnsupportedOpcode";
        case JitStatus::AllocationFailed:
            return "AllocationFailed";
        case JitStatus::ProtectionFailed:
            return "ProtectionFailed";
        case JitStatus::InvalidFunction:
            return "InvalidFunction";
        case JitStatus::InternalError:
            return "InternalError";
    }
    return "Unknown";
}

/// @brief OSR (On-Stack Replacement) status codes
enum class OsrStatus : std::uint8_t {
    /// @brief OSR succeeded
    Success = 0,

    /// @brief OSR is disabled
    Disabled,

    /// @brief Loop has not reached iteration threshold
    BelowThreshold,

    /// @brief No OSR entry point available
    NoEntryPoint,

    /// @brief State transfer failed
    StateTransferFailed,

    /// @brief Invalid loop structure
    InvalidLoop,
};

/// @brief Convert OSR status to string for debugging
[[nodiscard]] constexpr const char* osr_status_string(OsrStatus status) noexcept {
    switch (status) {
        case OsrStatus::Success:
            return "Success";
        case OsrStatus::Disabled:
            return "Disabled";
        case OsrStatus::BelowThreshold:
            return "BelowThreshold";
        case OsrStatus::NoEntryPoint:
            return "NoEntryPoint";
        case OsrStatus::StateTransferFailed:
            return "StateTransferFailed";
        case OsrStatus::InvalidLoop:
            return "InvalidLoop";
    }
    return "Unknown";
}

}  // namespace dotvm::jit
