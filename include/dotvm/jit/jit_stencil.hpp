/// @file jit_stencil.hpp
/// @brief Pre-compiled code stencils for copy-and-patch JIT
///
/// Defines stencil structures and registry for the copy-and-patch
/// compilation approach. Stencils are pre-compiled code templates
/// with holes that get patched with runtime values.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dotvm::jit {

/// @brief Types of holes in stencils (what needs to be patched)
enum class HoleType : std::uint8_t {
    /// @brief Immediate value (32-bit)
    Immediate32,

    /// @brief Immediate value (64-bit)
    Immediate64,

    /// @brief Register index
    RegisterIndex,

    /// @brief Relative offset for jumps
    RelativeOffset32,

    /// @brief Absolute address (64-bit)
    AbsoluteAddress,

    /// @brief PC-relative address
    PcRelative,
};

/// @brief Convert hole type to string for debugging
[[nodiscard]] constexpr const char* hole_type_string(HoleType type) noexcept {
    switch (type) {
        case HoleType::Immediate32:
            return "Immediate32";
        case HoleType::Immediate64:
            return "Immediate64";
        case HoleType::RegisterIndex:
            return "RegisterIndex";
        case HoleType::RelativeOffset32:
            return "RelativeOffset32";
        case HoleType::AbsoluteAddress:
            return "AbsoluteAddress";
        case HoleType::PcRelative:
            return "PcRelative";
    }
    return "Unknown";
}

/// @brief Describes a hole in a stencil that needs patching
struct StencilHole {
    /// @brief Offset from start of stencil code
    std::size_t offset{0};

    /// @brief Type of value expected
    HoleType type{HoleType::Immediate32};

    /// @brief Which operand this hole corresponds to (0 = dst, 1 = src1, 2 = src2)
    std::uint8_t operand_index{0};

    /// @brief Additional flags or adjustment values
    std::int8_t adjustment{0};
};

/// @brief Maximum number of holes per stencil
inline constexpr std::size_t MAX_STENCIL_HOLES = 8;

/// @brief Maximum stencil code size
inline constexpr std::size_t MAX_STENCIL_CODE_SIZE = 256;

/// @brief A pre-compiled code stencil
///
/// Stencils are templates of native code with holes that get patched
/// with actual operand values during compilation. The copy-and-patch
/// approach copies the stencil, then patches the holes.
struct Stencil {
    /// @brief Pre-compiled native code
    const std::uint8_t* code{nullptr};

    /// @brief Size of the code in bytes
    std::size_t code_size{0};

    /// @brief Array of holes to patch
    std::array<StencilHole, MAX_STENCIL_HOLES> holes{};

    /// @brief Number of holes in the stencil
    std::size_t hole_count{0};

    /// @brief Opcode this stencil handles
    std::uint8_t opcode{0};

    /// @brief Human-readable name for debugging
    const char* name{nullptr};

    /// @brief Check if stencil is valid
    [[nodiscard]] constexpr bool valid() const noexcept { return code != nullptr && code_size > 0; }

    /// @brief Get span of holes
    [[nodiscard]] constexpr std::span<const StencilHole> get_holes() const noexcept {
        return std::span<const StencilHole>(holes.data(), hole_count);
    }
};

/// @brief VM opcodes supported by the JIT
///
/// Must match the opcodes defined in the VM instruction set.
/// Only opcodes with stencils can be JIT compiled; others fall back to interpreter.
enum class JitOpcode : std::uint8_t {
    // Arithmetic
    ADD = 0x01,
    SUB = 0x02,
    MUL = 0x03,
    DIV = 0x04,
    MOD = 0x05,

    // Bitwise
    AND = 0x10,
    OR = 0x11,
    XOR = 0x12,
    NOT = 0x13,
    SHL = 0x14,
    SHR = 0x15,
    SAR = 0x16,  // Arithmetic shift right

    // Comparison
    CMP_EQ = 0x20,
    CMP_NE = 0x21,
    CMP_LT = 0x22,
    CMP_LE = 0x23,
    CMP_GT = 0x24,
    CMP_GE = 0x25,

    // Control flow
    JMP = 0x30,
    JMP_Z = 0x31,   // Jump if zero
    JMP_NZ = 0x32,  // Jump if not zero

    // Load/Store (memory)
    LOAD = 0x40,
    STORE = 0x41,
    LOAD_IMM = 0x42,

    // Register moves
    MOV = 0x50,

    // Function calls
    CALL = 0x60,
    RET = 0x61,

    // Special
    NOP = 0x00,
    HALT = 0xFF,
};

/// @brief Maximum number of opcodes we track
inline constexpr std::size_t MAX_OPCODES = 256;

/// @brief Registry of all available stencils
///
/// Provides O(1) lookup from opcode to stencil. Pre-populated with
/// stencils at startup.
class StencilRegistry {
public:
    /// @brief Create an empty registry
    StencilRegistry() = default;

    /// @brief Register a stencil for an opcode
    void register_stencil(const Stencil& stencil) noexcept {
        // uint8_t opcode is always < 256 (MAX_OPCODES), so no bounds check needed
        stencils_[stencil.opcode] = stencil;
    }

    /// @brief Look up stencil by opcode
    [[nodiscard]] const Stencil* get(std::uint8_t opcode) const noexcept {
        // uint8_t opcode is always < 256 (MAX_OPCODES), so no bounds check needed
        const auto& stencil = stencils_[opcode];
        return stencil.valid() ? &stencil : nullptr;
    }

    /// @brief Look up stencil by JitOpcode
    [[nodiscard]] const Stencil* get(JitOpcode opcode) const noexcept {
        return get(static_cast<std::uint8_t>(opcode));
    }

    /// @brief Check if opcode has a stencil
    [[nodiscard]] bool has_stencil(std::uint8_t opcode) const noexcept {
        return get(opcode) != nullptr;
    }

    /// @brief Get count of registered stencils
    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t n = 0;
        for (const auto& s : stencils_) {
            if (s.valid())
                ++n;
        }
        return n;
    }

    /// @brief Create registry with default stencils
    [[nodiscard]] static StencilRegistry create_default();

private:
    std::array<Stencil, MAX_OPCODES> stencils_{};
};

/// @brief x86-64 stencil definitions
///
/// These are generated at build time and linked in. For now, we define
/// minimal inline stencils for arithmetic operations.
namespace stencils::x86_64 {

// ============================================================================
// Prologue/Epilogue stencils (called once per compiled function)
// ============================================================================

/// @brief Function prologue - save callee-saved registers
///
/// Standard x86-64 ABI prologue:
///   push rbp
///   mov rbp, rsp
///   sub rsp, <frame_size>
extern const Stencil prologue;

/// @brief Function epilogue - restore and return
///
/// Standard x86-64 ABI epilogue:
///   add rsp, <frame_size>
///   pop rbp
///   ret
extern const Stencil epilogue;

// ============================================================================
// Arithmetic stencils (R-type: dst = src1 op src2)
// ============================================================================

/// @brief ADD dst, src1, src2
///
/// Template: mov rax, [regs + src1*8]
///           add rax, [regs + src2*8]
///           mov [regs + dst*8], rax
extern const Stencil add;

/// @brief SUB dst, src1, src2
extern const Stencil sub;

/// @brief MUL dst, src1, src2
extern const Stencil mul;

/// @brief DIV dst, src1, src2 (with zero check)
extern const Stencil div;

/// @brief MOD dst, src1, src2 (with zero check)
extern const Stencil mod;

// ============================================================================
// Bitwise stencils
// ============================================================================

/// @brief AND dst, src1, src2
extern const Stencil and_op;

/// @brief OR dst, src1, src2
extern const Stencil or_op;

/// @brief XOR dst, src1, src2
extern const Stencil xor_op;

/// @brief NOT dst, src (bitwise complement)
extern const Stencil not_op;

/// @brief SHL dst, src1, src2 (shift left)
extern const Stencil shl;

/// @brief SHR dst, src1, src2 (logical shift right)
extern const Stencil shr;

// ============================================================================
// Comparison stencils (set dst to 1 or 0)
// ============================================================================

/// @brief CMP_EQ dst, src1, src2 (dst = src1 == src2 ? 1 : 0)
extern const Stencil cmp_eq;

/// @brief CMP_LT dst, src1, src2 (dst = src1 < src2 ? 1 : 0)
extern const Stencil cmp_lt;

// ============================================================================
// Control flow stencils
// ============================================================================

/// @brief Unconditional jump
extern const Stencil jmp;

/// @brief Jump if zero
extern const Stencil jmp_z;

/// @brief Jump if not zero
extern const Stencil jmp_nz;

// ============================================================================
// Memory stencils
// ============================================================================

/// @brief LOAD dst, addr_reg (load from memory)
extern const Stencil load;

/// @brief STORE val_reg, addr_reg (store to memory)
extern const Stencil store;

/// @brief LOAD_IMM dst, imm (load immediate value)
extern const Stencil load_imm;

/// @brief MOV dst, src (register move)
extern const Stencil mov;

// ============================================================================
// Interpreter fallback stencil
// ============================================================================

/// @brief Fallback to interpreter for unsupported opcodes
///
/// Calls back into the interpreter to execute a single instruction.
/// Used when encountering opcodes without native stencils.
extern const Stencil interpreter_fallback;

}  // namespace stencils::x86_64

}  // namespace dotvm::jit
