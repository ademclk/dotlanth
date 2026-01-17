#pragma once

#include <cstddef>
#include <cstdint>

namespace dotvm::core {

// Register file size
inline constexpr std::size_t REGISTER_FILE_SIZE = 256;

// Special registers
inline constexpr std::uint8_t REG_ZERO = 0;  // R0: hardwired zero

// Register ranges
namespace reg_range {
// R0: Zero register (hardwired)
inline constexpr std::uint8_t ZERO = 0;

// R1-R15: Caller-saved (temporaries, not preserved across calls)
inline constexpr std::uint8_t CALLER_SAVED_START = 1;
inline constexpr std::uint8_t CALLER_SAVED_END = 15;
inline constexpr std::size_t CALLER_SAVED_COUNT = 15;

// R16-R31: Callee-saved (preserved across calls)
inline constexpr std::uint8_t CALLEE_SAVED_START = 16;
inline constexpr std::uint8_t CALLEE_SAVED_END = 31;
inline constexpr std::size_t CALLEE_SAVED_COUNT = 16;

// R32-R255: General purpose
inline constexpr std::uint8_t GENERAL_START = 32;
inline constexpr std::uint8_t GENERAL_END = 255;
inline constexpr std::size_t GENERAL_COUNT = 224;
}  // namespace reg_range

// Register classification
enum class RegisterClass : std::uint8_t {
    Zero,         // R0
    CallerSaved,  // R1-R15
    CalleeSaved,  // R16-R31
    General       // R32-R255
};

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

[[nodiscard]] constexpr bool is_caller_saved(std::uint8_t reg) noexcept {
    return reg >= reg_range::CALLER_SAVED_START && reg <= reg_range::CALLER_SAVED_END;
}

[[nodiscard]] constexpr bool is_callee_saved(std::uint8_t reg) noexcept {
    return reg >= reg_range::CALLEE_SAVED_START && reg <= reg_range::CALLEE_SAVED_END;
}

[[nodiscard]] constexpr bool is_general(std::uint8_t reg) noexcept {
    return reg >= reg_range::GENERAL_START;
}

}  // namespace dotvm::core
