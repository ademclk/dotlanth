/// @file register_file.hpp
/// @brief Register file implementation for DotVM execution.
///
/// This header provides the 256-register file used during VM execution:
/// - RegisterFile: Basic register storage with R0 hardwired to zero
/// - ArchRegisterFile: Architecture-aware wrapper with automatic value masking
///
/// The register file is cache-line aligned for optimal memory access patterns.
///
/// @see RegisterFile for basic register access
/// @see ArchRegisterFile for architecture-aware register access
/// @see register_conventions.hpp for register classification

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

#include "arch_config.hpp"
#include "register_conventions.hpp"
#include "value.hpp"

namespace dotvm::core {

/// @brief Cache line size for alignment (typically 64 bytes on x86-64).
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

/// @brief 256-entry register file storing NaN-boxed Values.
///
/// The register file provides the primary storage for VM execution state.
/// R0 is hardwired to zero: reads always return zero, writes are ignored.
/// All other registers (R1-R255) are general-purpose.
///
/// Thread Safety: NOT thread-safe. Use one RegisterFile per execution thread.
class alignas(CACHE_LINE_SIZE) RegisterFile {
public:
    /// @brief Default constructor initializes all registers to zero.
    constexpr RegisterFile() noexcept {
        for (auto& reg : regs_) {
            reg = Value::zero();
        }
    }

    /// @brief Reads a register value.
    /// @param reg Register index (0-255).
    /// @return The register value (R0 always returns zero).
    [[nodiscard]] constexpr Value read(std::uint8_t reg) const noexcept {
        if (reg == REG_ZERO) [[unlikely]] {
            return Value::zero();
        }
        return regs_[reg];
    }

    /// @brief Writes a register value.
    /// @param reg Register index (0-255).
    /// @param val Value to write (writes to R0 are silently ignored).
    constexpr void write(std::uint8_t reg, Value val) noexcept {
        if (reg == REG_ZERO) [[unlikely]] {
            return;  // R0 is hardwired to zero
        }
        regs_[reg] = val;
    }

    /// @brief Proxy class for non-const operator[] access.
    ///
    /// Enables syntax like `regs[5] = Value::from_int(42)` while respecting
    /// the R0 hardwired-zero behavior.
    class RegisterProxy {
    public:
        constexpr RegisterProxy(RegisterFile& rf, std::uint8_t reg) noexcept : rf_{rf}, reg_{reg} {}

        /// Implicit conversion to Value (reads the register).
        /// Implicit conversion is intentional for proxy pattern ergonomics: allows `Value v =
        /// regs[1];`
        // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
        constexpr operator Value() const noexcept { return rf_.read(reg_); }

        constexpr RegisterProxy& operator=(Value val) noexcept {
            rf_.write(reg_, val);
            return *this;
        }

    private:
        /// Reference to parent RegisterFile. Must be a reference since proxies are non-owning
        /// views.
        RegisterFile&
            rf_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): Proxy pattern
        std::uint8_t reg_;
    };

    /// @brief Provides mutable access to a register by index.
    /// @param reg Register index (0-255).
    /// @return A proxy object supporting read and write operations.
    [[nodiscard]] constexpr RegisterProxy operator[](std::uint8_t reg) noexcept {
        return RegisterProxy{*this, reg};
    }

    /// @brief Provides read-only access to a register by index.
    /// @param reg Register index (0-255).
    /// @return The register value.
    [[nodiscard]] constexpr Value operator[](std::uint8_t reg) const noexcept { return read(reg); }

    /// @brief Returns a span of caller-saved registers (R1-R15) for context switching.
    /// @return A const span of 15 Values.
    [[nodiscard]] std::span<const Value> caller_saved_regs() const noexcept {
        return std::span{regs_}.subspan(reg_range::CALLER_SAVED_START,
                                        reg_range::CALLER_SAVED_COUNT);
    }

    /// @brief Returns a span of callee-saved registers (R16-R31) for context switching.
    /// @return A const span of 16 Values.
    [[nodiscard]] std::span<const Value> callee_saved_regs() const noexcept {
        return std::span{regs_}.subspan(reg_range::CALLEE_SAVED_START,
                                        reg_range::CALLEE_SAVED_COUNT);
    }

    /// @brief Saves caller-saved registers for context switching.
    /// @param out Destination span for 15 Values.
    void save_caller_saved(std::span<Value, reg_range::CALLER_SAVED_COUNT> out) const noexcept {
        std::copy_n(regs_.begin() + reg_range::CALLER_SAVED_START, reg_range::CALLER_SAVED_COUNT,
                    out.begin());
    }

    /// @brief Restores caller-saved registers after context switch.
    /// @param in Source span of 15 Values.
    void restore_caller_saved(std::span<const Value, reg_range::CALLER_SAVED_COUNT> in) noexcept {
        std::ranges::copy(in, regs_.begin() + reg_range::CALLER_SAVED_START);
    }

    /// @brief Saves callee-saved registers for function prologue.
    /// @param out Destination span for 16 Values.
    void save_callee_saved(std::span<Value, reg_range::CALLEE_SAVED_COUNT> out) const noexcept {
        std::copy_n(regs_.begin() + reg_range::CALLEE_SAVED_START, reg_range::CALLEE_SAVED_COUNT,
                    out.begin());
    }

    /// @brief Restores callee-saved registers in function epilogue.
    /// @param in Source span of 16 Values.
    void restore_callee_saved(std::span<const Value, reg_range::CALLEE_SAVED_COUNT> in) noexcept {
        std::ranges::copy(in, regs_.begin() + reg_range::CALLEE_SAVED_START);
    }

    /// @brief Clears all registers to zero.
    constexpr void clear() noexcept {
        for (auto& reg : regs_) {
            reg = Value::zero();
        }
    }

    /// @brief Returns the number of registers (256).
    /// @return Total register count.
    [[nodiscard]] static constexpr std::size_t size() noexcept { return REGISTER_FILE_SIZE; }

    /// @brief Returns the size of the register file in bytes.
    /// @return Size in bytes (256 * 8 = 2048 plus alignment padding).
    [[nodiscard]] static constexpr std::size_t byte_size() noexcept { return sizeof(RegisterFile); }

    /// @brief Returns a raw view of all register values for serialization.
    /// @return A const span of all 256 Values.
    [[nodiscard]] std::span<const Value, REGISTER_FILE_SIZE> raw_view() const noexcept {
        return std::span<const Value, REGISTER_FILE_SIZE>{regs_};
    }

private:
    std::array<Value, REGISTER_FILE_SIZE> regs_;
};

// Static assertions for size guarantees
static_assert(sizeof(Value) == 8, "Value must be 8 bytes for <16 byte target");
static_assert(alignof(RegisterFile) == CACHE_LINE_SIZE, "RegisterFile must be cache-line aligned");

// ============================================================================
// Architecture-Aware Register File Wrapper
// ============================================================================

/// Architecture-aware register file wrapper
///
/// This wrapper applies architecture-specific value masking on writes.
/// In Arch32 mode, integer values are automatically masked to 32 bits
/// with proper sign extension before being stored.
///
/// The underlying RegisterFile is preserved, allowing direct access
/// for operations that don't require masking.
class ArchRegisterFile {
public:
    /// Construct an architecture-aware register file
    ///
    /// @param arch Target architecture (default: Arch64)
    explicit ArchRegisterFile(Architecture arch = Architecture::Arch64) noexcept : arch_{arch} {}

    // =========================================================================
    // Architecture Configuration
    // =========================================================================

    /// Get the current architecture
    [[nodiscard]] Architecture arch() const noexcept { return arch_; }

    /// Set the architecture
    void set_arch(Architecture arch) noexcept { arch_ = arch; }

    // =========================================================================
    // Register Access
    // =========================================================================

    /// Read a register value
    ///
    /// Reading does not mask the value - it returns the stored value as-is.
    /// R0 always returns zero regardless of architecture.
    ///
    /// @param reg Register index (0-255)
    /// @return The register value
    [[nodiscard]] Value read(std::uint8_t reg) const noexcept { return regs_.read(reg); }

    /// Write a register value with architecture-aware masking
    ///
    /// In Arch32 mode, integer values are masked to 32 bits with sign extension.
    /// Non-integer values (float, bool, handle, nil, pointer) are stored unchanged.
    /// Writes to R0 are silently ignored.
    ///
    /// @param reg Register index (0-255)
    /// @param val Value to write
    void write(std::uint8_t reg, Value val) noexcept {
        if (arch_ == Architecture::Arch32 && val.is_integer()) {
            val = Value::from_int(arch_config::mask_int(val.as_integer(), arch_));
        }
        regs_.write(reg, val);
    }

    /// Write an integer value with architecture-aware masking
    ///
    /// The integer is masked to the appropriate width for the architecture
    /// before being stored.
    ///
    /// @param reg Register index (0-255)
    /// @param val Integer value to write
    void write_int(std::uint8_t reg, std::int64_t val) noexcept {
        regs_.write(reg, Value::from_int(arch_config::mask_int(val, arch_)));
    }

    /// Write a value without masking
    ///
    /// Bypasses architecture masking - use for values that should not be masked.
    ///
    /// @param reg Register index (0-255)
    /// @param val Value to write
    void write_raw(std::uint8_t reg, Value val) noexcept { regs_.write(reg, val); }

    // =========================================================================
    // Proxy for operator[] access
    // =========================================================================

    /// Proxy class for architecture-aware operator[] access
    class ArchRegisterProxy {
    public:
        constexpr ArchRegisterProxy(ArchRegisterFile& rf, std::uint8_t reg) noexcept
            : rf_{rf}, reg_{reg} {}

        /// Implicit conversion to Value (reads the register).
        /// Implicit conversion is intentional for proxy pattern ergonomics: allows `Value v =
        /// regs[1];`
        // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
        operator Value() const noexcept { return rf_.read(reg_); }

        ArchRegisterProxy& operator=(Value val) noexcept {
            rf_.write(reg_, val);
            return *this;
        }

    private:
        /// Reference to parent ArchRegisterFile. Must be a reference since proxies are non-owning
        /// views.
        ArchRegisterFile&
            rf_;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): Proxy pattern
        std::uint8_t reg_;
    };

    [[nodiscard]] ArchRegisterProxy operator[](std::uint8_t reg) noexcept {
        return ArchRegisterProxy{*this, reg};
    }

    [[nodiscard]] Value operator[](std::uint8_t reg) const noexcept { return read(reg); }

    // =========================================================================
    // Bulk Operations
    // =========================================================================

    /// Clear all registers (reset to zero)
    void clear() noexcept { regs_.clear(); }

    /// Get the number of registers
    [[nodiscard]] static constexpr std::size_t size() noexcept { return RegisterFile::size(); }

    // =========================================================================
    // Raw Access
    // =========================================================================

    /// Get mutable access to the underlying RegisterFile
    ///
    /// Use for operations that don't require architecture-aware masking.
    [[nodiscard]] RegisterFile& raw() noexcept { return regs_; }

    /// Get const access to the underlying RegisterFile
    [[nodiscard]] const RegisterFile& raw() const noexcept { return regs_; }

    /// Get raw view of all register values
    [[nodiscard]] std::span<const Value, REGISTER_FILE_SIZE> raw_view() const noexcept {
        return regs_.raw_view();
    }

private:
    RegisterFile regs_;
    Architecture arch_;
};

}  // namespace dotvm::core
