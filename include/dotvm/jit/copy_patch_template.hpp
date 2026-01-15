// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2025 DotLanth Project

#ifndef DOTVM_JIT_COPY_PATCH_TEMPLATE_HPP
#define DOTVM_JIT_COPY_PATCH_TEMPLATE_HPP

#include "jit_types.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace dotvm::jit {

/// Template category for copy-and-patch compilation
enum class TemplateCategory : std::uint8_t {
    // Arithmetic operations (register-register)
    ADD_RRR = 0,      // Rd = Rs1 + Rs2 (integer)
    SUB_RRR = 1,      // Rd = Rs1 - Rs2 (integer)
    MUL_RRR = 2,      // Rd = Rs1 * Rs2 (integer)
    DIV_RRR = 3,      // Rd = Rs1 / Rs2 (integer, with div-by-zero check)
    MOD_RRR = 4,      // Rd = Rs1 % Rs2 (integer, with div-by-zero check)
    NEG_RR = 5,       // Rd = -Rs1 (integer)

    // Arithmetic operations (register-immediate)
    ADDI_RRI = 10,    // Rd = Rs1 + imm16 (integer)
    SUBI_RRI = 11,    // Rd = Rs1 - imm16 (integer)
    MULI_RRI = 12,    // Rd = Rs1 * imm16 (integer)

    // Floating-point operations
    FADD_RRR = 20,    // Rd = Rs1 + Rs2 (float)
    FSUB_RRR = 21,    // Rd = Rs1 - Rs2 (float)
    FMUL_RRR = 22,    // Rd = Rs1 * Rs2 (float)
    FDIV_RRR = 23,    // Rd = Rs1 / Rs2 (float)
    FNEG_RR = 24,     // Rd = -Rs1 (float)

    // Bitwise operations
    AND_RRR = 30,     // Rd = Rs1 & Rs2
    OR_RRR = 31,      // Rd = Rs1 | Rs2
    XOR_RRR = 32,     // Rd = Rs1 ^ Rs2
    NOT_RR = 33,      // Rd = ~Rs1
    SHL_RRR = 34,     // Rd = Rs1 << Rs2
    SHR_RRR = 35,     // Rd = Rs1 >> Rs2 (logical)
    SAR_RRR = 36,     // Rd = Rs1 >> Rs2 (arithmetic)

    // Comparison operations
    CMP_EQ_RRR = 40,  // Rd = (Rs1 == Rs2) ? 1 : 0
    CMP_NE_RRR = 41,  // Rd = (Rs1 != Rs2) ? 1 : 0
    CMP_LT_RRR = 42,  // Rd = (Rs1 < Rs2) ? 1 : 0
    CMP_LE_RRR = 43,  // Rd = (Rs1 <= Rs2) ? 1 : 0
    CMP_GT_RRR = 44,  // Rd = (Rs1 > Rs2) ? 1 : 0
    CMP_GE_RRR = 45,  // Rd = (Rs1 >= Rs2) ? 1 : 0

    // Memory operations
    LOAD64 = 50,      // Rd = Memory[Rs1 + offset]
    STORE64 = 51,     // Memory[Rs1 + offset] = Rs2

    // Control flow
    JMP = 60,         // Jump to offset
    JZ = 61,          // Jump if Rs == 0
    JNZ = 62,         // Jump if Rs != 0
    CALL = 63,        // Function call
    RET = 64,         // Return

    // Special templates
    PROLOGUE = 100,   // Function prologue
    EPILOGUE = 101,   // Function epilogue
    DEOPT_STUB = 102, // Deoptimization stub
    OSR_ENTRY = 103,  // OSR entry point
    OSR_EXIT = 104,   // OSR exit point

    // Type handling
    TYPE_CHECK_INT = 110,  // Check if value is integer
    UNBOX_INT = 111,       // Extract integer from NaN-boxed value
    BOX_INT = 112,         // Box integer as NaN-boxed value
};

/// Code template for copy-and-patch compilation
///
/// Contains pre-compiled native code with holes (patches) that are
/// filled in at JIT compile time with actual operand values.
struct CodeTemplate {
    /// The template category
    TemplateCategory category;

    /// Native code bytes (with placeholder holes)
    std::span<const std::uint8_t> code;

    /// Patch locations within the template
    std::span<const PatchLocation> patches;

    /// Required stack frame size (if any)
    std::size_t stack_frame_size{0};

    /// Whether this template includes a slow-path
    bool has_slow_path{false};

    /// Offset to slow-path within the template (if has_slow_path)
    std::size_t slow_path_offset{0};
};

/// Template registry for a specific architecture
///
/// Provides access to pre-compiled code templates for copy-and-patch
/// compilation. Templates are architecture-specific.
class TemplateRegistry {
public:
    /// Get the template for a given category
    /// @param category The template category
    /// @return The code template, or nullptr if not available
    [[nodiscard]] virtual auto get_template(TemplateCategory category) const noexcept
        -> const CodeTemplate* = 0;

    /// Check if a template is available
    /// @param category The template category
    /// @return true if the template is available
    [[nodiscard]] virtual auto has_template(TemplateCategory category) const noexcept
        -> bool = 0;

    /// Get the target architecture for this registry
    [[nodiscard]] virtual auto target_arch() const noexcept -> TargetArch = 0;

    virtual ~TemplateRegistry() = default;
};

/// Code buffer for building JIT'd code from templates
///
/// Accumulates native code by copying templates and applying patches.
class CodeBuffer {
public:
    /// Create a code buffer with the given initial capacity
    /// @param initial_capacity Initial capacity in bytes
    explicit CodeBuffer(std::size_t initial_capacity = 4096)
        : buffer_(initial_capacity) {}

    /// Get the current size of the buffer
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return position_;
    }

    /// Get the current capacity of the buffer
    [[nodiscard]] auto capacity() const noexcept -> std::size_t {
        return buffer_.size();
    }

    /// Get a span of the code bytes
    [[nodiscard]] auto code() const noexcept -> std::span<const std::uint8_t> {
        return std::span<const std::uint8_t>(buffer_.data(), position_);
    }

    /// Reserve additional capacity
    void reserve(std::size_t additional) {
        if (position_ + additional > buffer_.size()) {
            buffer_.resize(buffer_.size() * 2 + additional);
        }
    }

    /// Copy a template to the buffer
    /// @param tmpl The template to copy
    /// @return The offset where the template was placed
    auto copy_template(const CodeTemplate& tmpl) -> std::size_t {
        const auto offset = position_;
        reserve(tmpl.code.size());
        std::memcpy(buffer_.data() + position_, tmpl.code.data(), tmpl.code.size());
        position_ += tmpl.code.size();
        return offset;
    }

    /// Apply a patch at the given offset
    /// @param offset Offset within the buffer
    /// @param patch The patch location descriptor
    /// @param value The value to patch in
    void apply_patch(std::size_t offset, const PatchLocation& patch, std::uint64_t value) {
        const auto patch_offset = offset + patch.offset;

        switch (patch.type) {
            case PatchType::Imm8:
                buffer_[patch_offset] = static_cast<std::uint8_t>(value);
                break;

            case PatchType::Imm16: {
                auto* ptr = reinterpret_cast<std::uint16_t*>(&buffer_[patch_offset]);
                *ptr = static_cast<std::uint16_t>(value);
                break;
            }

            case PatchType::Imm32: {
                auto* ptr = reinterpret_cast<std::uint32_t*>(&buffer_[patch_offset]);
                *ptr = static_cast<std::uint32_t>(value);
                break;
            }

            case PatchType::Imm64: {
                auto* ptr = reinterpret_cast<std::uint64_t*>(&buffer_[patch_offset]);
                *ptr = value;
                break;
            }

            case PatchType::RegOffset: {
                // Register file offset (scaled by 8 for 64-bit values)
                auto* ptr = reinterpret_cast<std::int32_t*>(&buffer_[patch_offset]);
                *ptr = static_cast<std::int32_t>(value * 8);
                break;
            }

            case PatchType::RelBranch32: {
                // PC-relative branch offset (relative to end of instruction)
                auto* ptr = reinterpret_cast<std::int32_t*>(&buffer_[patch_offset]);
                *ptr = static_cast<std::int32_t>(value);
                break;
            }

            case PatchType::AbsAddr64: {
                auto* ptr = reinterpret_cast<std::uint64_t*>(&buffer_[patch_offset]);
                *ptr = value;
                break;
            }
        }
    }

    /// Emit raw bytes
    void emit_bytes(std::span<const std::uint8_t> bytes) {
        reserve(bytes.size());
        std::memcpy(buffer_.data() + position_, bytes.data(), bytes.size());
        position_ += bytes.size();
    }

    /// Emit a single byte
    void emit_byte(std::uint8_t byte) {
        reserve(1);
        buffer_[position_++] = byte;
    }

    /// Emit a 32-bit value (little-endian)
    void emit_u32(std::uint32_t value) {
        reserve(4);
        std::memcpy(buffer_.data() + position_, &value, 4);
        position_ += 4;
    }

    /// Emit a 64-bit value (little-endian)
    void emit_u64(std::uint64_t value) {
        reserve(8);
        std::memcpy(buffer_.data() + position_, &value, 8);
        position_ += 8;
    }

    /// Get the current position (for calculating offsets)
    [[nodiscard]] auto position() const noexcept -> std::size_t {
        return position_;
    }

    /// Set a label at the current position
    void set_label(std::size_t label_id) {
        if (label_id >= labels_.size()) {
            labels_.resize(label_id + 1, 0);
        }
        labels_[label_id] = position_;
    }

    /// Get the offset of a label
    [[nodiscard]] auto get_label(std::size_t label_id) const noexcept -> std::size_t {
        if (label_id >= labels_.size()) {
            return 0;
        }
        return labels_[label_id];
    }

    /// Clear the buffer
    void clear() noexcept {
        position_ = 0;
        labels_.clear();
    }

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t position_{0};
    std::vector<std::size_t> labels_;
};

// ============================================================================
// x86-64 specific code generation helpers
// ============================================================================

#ifdef DOTVM_JIT_X86_64

namespace x86_64_gen {

/// REX prefix byte
constexpr std::uint8_t REX_W = 0x48;  // 64-bit operand size
constexpr std::uint8_t REX_R = 0x44;  // Extension of ModR/M reg field
constexpr std::uint8_t REX_X = 0x42;  // Extension of SIB index field
constexpr std::uint8_t REX_B = 0x41;  // Extension of ModR/M r/m or SIB base

/// ModR/M byte construction
constexpr auto modrm(std::uint8_t mod, std::uint8_t reg, std::uint8_t rm) -> std::uint8_t {
    return static_cast<std::uint8_t>((mod << 6) | (reg << 3) | rm);
}

/// SIB byte construction
constexpr auto sib(std::uint8_t scale, std::uint8_t index, std::uint8_t base) -> std::uint8_t {
    return static_cast<std::uint8_t>((scale << 6) | (index << 3) | base);
}

/// Common x86-64 opcodes
namespace op {
    constexpr std::uint8_t ADD_RM_R = 0x01;    // ADD r/m64, r64
    constexpr std::uint8_t ADD_R_RM = 0x03;    // ADD r64, r/m64
    constexpr std::uint8_t SUB_RM_R = 0x29;    // SUB r/m64, r64
    constexpr std::uint8_t SUB_R_RM = 0x2B;    // SUB r64, r/m64
    constexpr std::uint8_t AND_RM_R = 0x21;    // AND r/m64, r64
    constexpr std::uint8_t AND_R_RM = 0x23;    // AND r64, r/m64
    constexpr std::uint8_t OR_RM_R = 0x09;     // OR r/m64, r64
    constexpr std::uint8_t OR_R_RM = 0x0B;     // OR r64, r/m64
    constexpr std::uint8_t XOR_RM_R = 0x31;    // XOR r/m64, r64
    constexpr std::uint8_t XOR_R_RM = 0x33;    // XOR r64, r/m64
    constexpr std::uint8_t CMP_RM_R = 0x39;    // CMP r/m64, r64
    constexpr std::uint8_t CMP_R_RM = 0x3B;    // CMP r64, r/m64
    constexpr std::uint8_t MOV_RM_R = 0x89;    // MOV r/m64, r64
    constexpr std::uint8_t MOV_R_RM = 0x8B;    // MOV r64, r/m64
    constexpr std::uint8_t MOV_R_IMM64 = 0xB8; // MOV r64, imm64 (+reg)
    constexpr std::uint8_t IMUL_R_RM = 0xAF;   // IMUL r64, r/m64 (0F prefix)
    constexpr std::uint8_t IDIV_RM = 0xF7;     // IDIV r/m64 (/7)
    constexpr std::uint8_t NEG_RM = 0xF7;      // NEG r/m64 (/3)
    constexpr std::uint8_t NOT_RM = 0xF7;      // NOT r/m64 (/2)
    constexpr std::uint8_t SHL_RM_CL = 0xD3;   // SHL r/m64, cl (/4)
    constexpr std::uint8_t SHR_RM_CL = 0xD3;   // SHR r/m64, cl (/5)
    constexpr std::uint8_t SAR_RM_CL = 0xD3;   // SAR r/m64, cl (/7)
    constexpr std::uint8_t RET = 0xC3;         // RET (near)
    constexpr std::uint8_t PUSH_R = 0x50;      // PUSH r64 (+reg)
    constexpr std::uint8_t POP_R = 0x58;       // POP r64 (+reg)
    constexpr std::uint8_t CALL_REL32 = 0xE8;  // CALL rel32
    constexpr std::uint8_t JMP_REL32 = 0xE9;   // JMP rel32
    constexpr std::uint8_t JMP_REL8 = 0xEB;    // JMP rel8
    constexpr std::uint8_t JZ_REL32 = 0x84;    // JZ rel32 (0F prefix)
    constexpr std::uint8_t JNZ_REL32 = 0x85;   // JNZ rel32 (0F prefix)
    constexpr std::uint8_t JL_REL32 = 0x8C;    // JL rel32 (0F prefix)
    constexpr std::uint8_t JLE_REL32 = 0x8E;   // JLE rel32 (0F prefix)
    constexpr std::uint8_t JG_REL32 = 0x8F;    // JG rel32 (0F prefix)
    constexpr std::uint8_t JGE_REL32 = 0x8D;   // JGE rel32 (0F prefix)
    constexpr std::uint8_t SETZ_RM = 0x94;     // SETZ r/m8 (0F prefix)
    constexpr std::uint8_t SETNZ_RM = 0x95;    // SETNZ r/m8 (0F prefix)
    constexpr std::uint8_t SETL_RM = 0x9C;     // SETL r/m8 (0F prefix)
    constexpr std::uint8_t SETLE_RM = 0x9E;    // SETLE r/m8 (0F prefix)
    constexpr std::uint8_t SETG_RM = 0x9F;     // SETG r/m8 (0F prefix)
    constexpr std::uint8_t SETGE_RM = 0x9D;    // SETGE r/m8 (0F prefix)
    constexpr std::uint8_t PREFIX_0F = 0x0F;   // Two-byte opcode prefix
    constexpr std::uint8_t CQO = 0x99;         // Sign-extend RAX to RDX:RAX
}

/// x86-64 register encoding
namespace reg {
    constexpr std::uint8_t RAX = 0;
    constexpr std::uint8_t RCX = 1;
    constexpr std::uint8_t RDX = 2;
    constexpr std::uint8_t RBX = 3;
    constexpr std::uint8_t RSP = 4;
    constexpr std::uint8_t RBP = 5;
    constexpr std::uint8_t RSI = 6;
    constexpr std::uint8_t RDI = 7;
    constexpr std::uint8_t R8 = 8;
    constexpr std::uint8_t R9 = 9;
    constexpr std::uint8_t R10 = 10;
    constexpr std::uint8_t R11 = 11;
    constexpr std::uint8_t R12 = 12;
    constexpr std::uint8_t R13 = 13;
    constexpr std::uint8_t R14 = 14;
    constexpr std::uint8_t R15 = 15;
}

}  // namespace x86_64_gen

#endif  // DOTVM_JIT_X86_64

// ============================================================================
// ARM64 specific code generation helpers
// ============================================================================

#ifdef DOTVM_JIT_ARM64

namespace arm64_gen {

/// ARM64 instruction encoding helpers
/// ARM64 instructions are 32 bits, little-endian

/// Data processing (register) - ADD/SUB
constexpr auto dp_reg(
    std::uint8_t sf,      // 64-bit (1) or 32-bit (0)
    std::uint8_t op,      // Operation (0=ADD, 1=SUB)
    std::uint8_t S,       // Set flags
    std::uint8_t rm,      // Source register 2
    std::uint8_t rn,      // Source register 1
    std::uint8_t rd       // Destination register
) -> std::uint32_t {
    return (static_cast<std::uint32_t>(sf) << 31) |
           (static_cast<std::uint32_t>(op) << 30) |
           (static_cast<std::uint32_t>(S) << 29) |
           (0b01011u << 24) |  // Data processing (register)
           (static_cast<std::uint32_t>(rm) << 16) |
           (static_cast<std::uint32_t>(rn) << 5) |
           static_cast<std::uint32_t>(rd);
}

/// Load/Store (register offset)
constexpr auto ldst_reg(
    std::uint8_t size,    // Size (3=64-bit)
    std::uint8_t V,       // Vector register
    std::uint8_t opc,     // Opcode (0=STR, 1=LDR)
    std::uint8_t rm,      // Offset register
    std::uint8_t rn,      // Base register
    std::uint8_t rt       // Target register
) -> std::uint32_t {
    return (static_cast<std::uint32_t>(size) << 30) |
           (0b111u << 27) |
           (static_cast<std::uint32_t>(V) << 26) |
           (0b00u << 24) |
           (static_cast<std::uint32_t>(opc) << 22) |
           (1u << 21) |        // Register offset
           (static_cast<std::uint32_t>(rm) << 16) |
           (0b011u << 13) |    // Extend type (LSL)
           (1u << 12) |        // S (scaled)
           (static_cast<std::uint32_t>(rn) << 5) |
           static_cast<std::uint32_t>(rt);
}

/// Branch register (RET, BR, BLR)
constexpr auto branch_reg(
    std::uint8_t opc,     // Operation (0=BR, 1=BLR, 2=RET)
    std::uint8_t rn       // Register
) -> std::uint32_t {
    return (0b1101011u << 25) |
           (static_cast<std::uint32_t>(opc) << 21) |
           (0b11111u << 16) |
           (static_cast<std::uint32_t>(rn) << 5);
}

/// ARM64 register encoding
namespace reg {
    constexpr std::uint8_t X0 = 0;
    constexpr std::uint8_t X1 = 1;
    constexpr std::uint8_t X2 = 2;
    constexpr std::uint8_t X3 = 3;
    constexpr std::uint8_t X19 = 19;  // Callee-saved, used for reg file base
    constexpr std::uint8_t X20 = 20;
    constexpr std::uint8_t X21 = 21;
    constexpr std::uint8_t X22 = 22;
    constexpr std::uint8_t X23 = 23;
    constexpr std::uint8_t X24 = 24;
    constexpr std::uint8_t X25 = 25;
    constexpr std::uint8_t X26 = 26;
    constexpr std::uint8_t X27 = 27;
    constexpr std::uint8_t X28 = 28;
    constexpr std::uint8_t X29 = 29;  // FP
    constexpr std::uint8_t X30 = 30;  // LR
    constexpr std::uint8_t XZR = 31;  // Zero register / SP
}

}  // namespace arm64_gen

#endif  // DOTVM_JIT_ARM64

}  // namespace dotvm::jit

#endif  // DOTVM_JIT_COPY_PATCH_TEMPLATE_HPP
