#pragma once

#include "value.hpp"
#include "register_conventions.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dotvm::core {

// Cache line size for alignment (typically 64 bytes on x86-64)
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

class alignas(CACHE_LINE_SIZE) RegisterFile {
public:
    // Default constructor initializes all registers to zero
    constexpr RegisterFile() noexcept {
        for (auto& reg : regs_) {
            reg = Value::zero();
        }
    }

    // Read a register value
    // R0 always returns zero regardless of what was written
    [[nodiscard]] constexpr Value read(std::uint8_t reg) const noexcept {
        if (reg == REG_ZERO) [[unlikely]] {
            return Value::zero();
        }
        return regs_[reg];
    }

    // Write a register value
    // Writes to R0 are silently ignored (no-op)
    constexpr void write(std::uint8_t reg, Value val) noexcept {
        if (reg == REG_ZERO) [[unlikely]] {
            return; // R0 is hardwired to zero
        }
        regs_[reg] = val;
    }

    // Proxy class for non-const operator[] access
    class RegisterProxy {
    public:
        constexpr RegisterProxy(RegisterFile& rf, std::uint8_t reg) noexcept
            : rf_{rf}, reg_{reg} {}

        constexpr operator Value() const noexcept {
            return rf_.read(reg_);
        }

        constexpr RegisterProxy& operator=(Value val) noexcept {
            rf_.write(reg_, val);
            return *this;
        }

    private:
        RegisterFile& rf_;
        std::uint8_t reg_;
    };

    [[nodiscard]] constexpr RegisterProxy operator[](std::uint8_t reg) noexcept {
        return RegisterProxy{*this, reg};
    }

    [[nodiscard]] constexpr Value operator[](std::uint8_t reg) const noexcept {
        return read(reg);
    }

    // Bulk operations for context switching
    [[nodiscard]] std::span<const Value> caller_saved_regs() const noexcept {
        return std::span{regs_}.subspan(reg_range::CALLER_SAVED_START,
                                         reg_range::CALLER_SAVED_COUNT);
    }

    [[nodiscard]] std::span<const Value> callee_saved_regs() const noexcept {
        return std::span{regs_}.subspan(reg_range::CALLEE_SAVED_START,
                                         reg_range::CALLEE_SAVED_COUNT);
    }

    // Save/restore for context switching
    void save_caller_saved(
        std::span<Value, reg_range::CALLER_SAVED_COUNT> out) const noexcept {
        std::copy_n(regs_.begin() + reg_range::CALLER_SAVED_START,
                    reg_range::CALLER_SAVED_COUNT, out.begin());
    }

    void restore_caller_saved(
        std::span<const Value, reg_range::CALLER_SAVED_COUNT> in) noexcept {
        std::copy(in.begin(), in.end(),
                  regs_.begin() + reg_range::CALLER_SAVED_START);
    }

    void save_callee_saved(
        std::span<Value, reg_range::CALLEE_SAVED_COUNT> out) const noexcept {
        std::copy_n(regs_.begin() + reg_range::CALLEE_SAVED_START,
                    reg_range::CALLEE_SAVED_COUNT, out.begin());
    }

    void restore_callee_saved(
        std::span<const Value, reg_range::CALLEE_SAVED_COUNT> in) noexcept {
        std::copy(in.begin(), in.end(),
                  regs_.begin() + reg_range::CALLEE_SAVED_START);
    }

    // Clear all registers (reset to zero)
    constexpr void clear() noexcept {
        for (auto& reg : regs_) {
            reg = Value::zero();
        }
    }

    // Size information
    [[nodiscard]] static constexpr std::size_t size() noexcept {
        return REGISTER_FILE_SIZE;
    }

    [[nodiscard]] static constexpr std::size_t byte_size() noexcept {
        return sizeof(RegisterFile);
    }

    // Raw access for serialization/debugging
    [[nodiscard]] std::span<const Value, REGISTER_FILE_SIZE> raw_view()
        const noexcept {
        return std::span<const Value, REGISTER_FILE_SIZE>{regs_};
    }

private:
    std::array<Value, REGISTER_FILE_SIZE> regs_;
};

// Static assertions for size guarantees
static_assert(sizeof(Value) == 8, "Value must be 8 bytes for <16 byte target");
static_assert(alignof(RegisterFile) == CACHE_LINE_SIZE,
              "RegisterFile must be cache-line aligned");

} // namespace dotvm::core
