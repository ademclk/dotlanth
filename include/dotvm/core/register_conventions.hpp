/// @file register_conventions.hpp
/// @brief Register file conventions and classification for DotVM.
///
/// This header defines the register file layout and calling conventions:
/// - R0: Hardwired zero register (writes ignored, reads return zero)
/// - R1-R15: Caller-saved registers (temporaries, not preserved across calls)
/// - R16-R31: Callee-saved registers (preserved across calls)
/// - R32-R255: General purpose registers
///
/// These conventions enable efficient function calls and register allocation
/// while maintaining compatibility with common ABI patterns.

#pragma once

#include <cstddef>
#include <cstdint>

namespace dotvm::core {

/// @brief Total number of registers in the register file.
inline constexpr std::size_t REGISTER_FILE_SIZE = 256;

/// @brief Index of the zero register (R0, hardwired to zero).
inline constexpr std::uint8_t REG_ZERO = 0;

/// @brief Register range constants.
namespace reg_range {
/// @brief Zero register index (R0, hardwired).
inline constexpr std::uint8_t ZERO = 0;

/// @brief First caller-saved register index (R1).
inline constexpr std::uint8_t CALLER_SAVED_START = 1;
/// @brief Last caller-saved register index (R15).
inline constexpr std::uint8_t CALLER_SAVED_END = 15;
/// @brief Number of caller-saved registers (15).
inline constexpr std::size_t CALLER_SAVED_COUNT = 15;

/// @brief First callee-saved register index (R16).
inline constexpr std::uint8_t CALLEE_SAVED_START = 16;
/// @brief Last callee-saved register index (R31).
inline constexpr std::uint8_t CALLEE_SAVED_END = 31;
/// @brief Number of callee-saved registers (16).
inline constexpr std::size_t CALLEE_SAVED_COUNT = 16;

/// @brief First general purpose register index (R32).
inline constexpr std::uint8_t GENERAL_START = 32;
/// @brief Last general purpose register index (R255).
inline constexpr std::uint8_t GENERAL_END = 255;
/// @brief Number of general purpose registers (224).
inline constexpr std::size_t GENERAL_COUNT = 224;
}  // namespace reg_range

/// @brief Classification of registers by calling convention.
enum class RegisterClass : std::uint8_t {
    Zero,         ///< R0: Hardwired zero register.
    CallerSaved,  ///< R1-R15: Not preserved across calls.
    CalleeSaved,  ///< R16-R31: Preserved across calls.
    General       ///< R32-R255: General purpose.
};

/// @brief Classifies a register index by its calling convention role.
/// @param reg Register index (0-255).
/// @return The register's classification.
[[nodiscard]] constexpr RegisterClass classify_register(std::uint8_t reg) noexcept {
    if (reg == 0) {
        return RegisterClass::Zero;
    }
    if (reg <= 15) {
        return RegisterClass::CallerSaved;
    }
    if (reg <= 31) {
        return RegisterClass::CalleeSaved;
    }
    return RegisterClass::General;
}

/// @brief Checks if a register is caller-saved (R1-R15).
/// @param reg Register index.
/// @return true if the register is caller-saved.
[[nodiscard]] constexpr bool is_caller_saved(std::uint8_t reg) noexcept {
    return reg >= reg_range::CALLER_SAVED_START && reg <= reg_range::CALLER_SAVED_END;
}

/// @brief Checks if a register is callee-saved (R16-R31).
/// @param reg Register index.
/// @return true if the register is callee-saved.
[[nodiscard]] constexpr bool is_callee_saved(std::uint8_t reg) noexcept {
    return reg >= reg_range::CALLEE_SAVED_START && reg <= reg_range::CALLEE_SAVED_END;
}

/// @brief Checks if a register is general purpose (R32-R255).
/// @param reg Register index.
/// @return true if the register is general purpose.
[[nodiscard]] constexpr bool is_general(std::uint8_t reg) noexcept {
    return reg >= reg_range::GENERAL_START;
}

}  // namespace dotvm::core
