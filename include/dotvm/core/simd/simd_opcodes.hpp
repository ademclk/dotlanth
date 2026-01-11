#pragma once

/// @file simd_opcodes.hpp
/// @brief SIMD opcode definitions and instruction encoding for the ParaDot extension
///
/// This header defines the SIMD opcodes in the reserved range 0xC0-0xCF and provides
/// utilities for encoding/decoding SIMD instructions. The ParaDot SIMD extension
/// supports vector operations across multiple widths (128, 256, 512 bits) with
/// runtime dispatch based on CPU capabilities.

#include <cstdint>
#include <optional>
#include <string_view>

namespace dotvm::core::simd {

// ============================================================================
// SIMD Opcode Definitions (0xC0 - 0xCF)
// ============================================================================

namespace opcode {

/// Vector addition: vd = vs1 + vs2
inline constexpr std::uint8_t VADD = 0xC0;

/// Vector subtraction: vd = vs1 - vs2
inline constexpr std::uint8_t VSUB = 0xC1;

/// Vector multiplication: vd = vs1 * vs2
inline constexpr std::uint8_t VMUL = 0xC2;

/// Vector division: vd = vs1 / vs2
inline constexpr std::uint8_t VDIV = 0xC3;

/// Dot product: scalar = sum(vs1[i] * vs2[i])
inline constexpr std::uint8_t VDOT = 0xC4;

/// Fused multiply-add: vd = vs1 * vs2 + vs3
inline constexpr std::uint8_t VFMA = 0xC5;

/// Vector minimum: vd = min(vs1, vs2) element-wise
inline constexpr std::uint8_t VMIN = 0xC6;

/// Vector maximum: vd = max(vs1, vs2) element-wise
inline constexpr std::uint8_t VMAX = 0xC7;

/// Load vector from memory: vd = mem[addr]
inline constexpr std::uint8_t VLOAD = 0xC8;

/// Store vector to memory: mem[addr] = vs
inline constexpr std::uint8_t VSTORE = 0xC9;

/// Broadcast scalar to all lanes: vd = {scalar, scalar, ...}
inline constexpr std::uint8_t VBCAST = 0xCA;

/// Extract single lane: scalar = vs[lane]
inline constexpr std::uint8_t VEXTRACT = 0xCB;

/// Compare equal: vd = (vs1 == vs2) ? all_ones : 0
inline constexpr std::uint8_t VCMPEQ = 0xCC;

/// Compare less than: vd = (vs1 < vs2) ? all_ones : 0
inline constexpr std::uint8_t VCMPLT = 0xCD;

/// Blend/conditional select: vd = mask ? vs1 : vs2
inline constexpr std::uint8_t VBLEND = 0xCE;

/// Shuffle/permute lanes: vd = shuffle(vs, indices)
inline constexpr std::uint8_t VSHUFFLE = 0xCF;

/// Minimum valid SIMD opcode
inline constexpr std::uint8_t SIMD_OPCODE_MIN = 0xC0;

/// Maximum valid SIMD opcode
inline constexpr std::uint8_t SIMD_OPCODE_MAX = 0xCF;

/// Check if an opcode is a SIMD instruction
[[nodiscard]] constexpr bool is_simd_opcode(std::uint8_t op) noexcept {
    return op >= SIMD_OPCODE_MIN && op <= SIMD_OPCODE_MAX;
}

/// Get the mnemonic name for a SIMD opcode
[[nodiscard]] constexpr std::string_view opcode_name(std::uint8_t op) noexcept {
    switch (op) {
        case VADD:     return "VADD";
        case VSUB:     return "VSUB";
        case VMUL:     return "VMUL";
        case VDIV:     return "VDIV";
        case VDOT:     return "VDOT";
        case VFMA:     return "VFMA";
        case VMIN:     return "VMIN";
        case VMAX:     return "VMAX";
        case VLOAD:    return "VLOAD";
        case VSTORE:   return "VSTORE";
        case VBCAST:   return "VBCAST";
        case VEXTRACT: return "VEXTRACT";
        case VCMPEQ:   return "VCMPEQ";
        case VCMPLT:   return "VCMPLT";
        case VBLEND:   return "VBLEND";
        case VSHUFFLE: return "VSHUFFLE";
        default:       return "UNKNOWN";
    }
}

/// Get the number of source operands for an opcode
[[nodiscard]] constexpr std::uint8_t source_operand_count(std::uint8_t op) noexcept {
    switch (op) {
        case VBCAST:
        case VEXTRACT:
        case VLOAD:
            return 1;
        case VADD:
        case VSUB:
        case VMUL:
        case VDIV:
        case VDOT:
        case VMIN:
        case VMAX:
        case VSTORE:
        case VCMPEQ:
        case VCMPLT:
        case VSHUFFLE:
            return 2;
        case VFMA:
        case VBLEND:
            return 3;
        default:
            return 0;
    }
}

/// Check if opcode produces a scalar result (vs vector)
[[nodiscard]] constexpr bool produces_scalar(std::uint8_t op) noexcept {
    return op == VDOT || op == VEXTRACT;
}

/// Check if opcode is a memory operation
[[nodiscard]] constexpr bool is_memory_op(std::uint8_t op) noexcept {
    return op == VLOAD || op == VSTORE;
}

}  // namespace opcode

// ============================================================================
// Element Size Encoding
// ============================================================================

/// Element size/type encoding for SIMD instructions
///
/// Packed into 3 bits in the instruction encoding:
/// - Bits 0-2: element size code
enum class ElementSize : std::uint8_t {
    Int8    = 0,  ///< 8-bit signed integer
    Int16   = 1,  ///< 16-bit signed integer
    Int32   = 2,  ///< 32-bit signed integer
    Int64   = 3,  ///< 64-bit signed integer
    Float32 = 4,  ///< 32-bit IEEE 754 float
    Float64 = 5   ///< 64-bit IEEE 754 double
};

/// Maximum valid element size value
inline constexpr std::uint8_t ELEMENT_SIZE_MAX = 5;

/// Check if element size value is valid
[[nodiscard]] constexpr bool is_valid_element_size(ElementSize es) noexcept {
    return static_cast<std::uint8_t>(es) <= ELEMENT_SIZE_MAX;
}

/// Get the byte count for an element size
[[nodiscard]] constexpr std::size_t element_byte_size(ElementSize es) noexcept {
    switch (es) {
        case ElementSize::Int8:    return 1;
        case ElementSize::Int16:   return 2;
        case ElementSize::Int32:   return 4;
        case ElementSize::Int64:   return 8;
        case ElementSize::Float32: return 4;
        case ElementSize::Float64: return 8;
    }
    return 0;
}

/// Check if element size represents a floating-point type
[[nodiscard]] constexpr bool is_float_element(ElementSize es) noexcept {
    return es == ElementSize::Float32 || es == ElementSize::Float64;
}

/// Check if element size represents an integer type
[[nodiscard]] constexpr bool is_integer_element(ElementSize es) noexcept {
    return static_cast<std::uint8_t>(es) <= 3;
}

/// Get the element size name
[[nodiscard]] constexpr std::string_view element_size_name(ElementSize es) noexcept {
    switch (es) {
        case ElementSize::Int8:    return "i8";
        case ElementSize::Int16:   return "i16";
        case ElementSize::Int32:   return "i32";
        case ElementSize::Int64:   return "i64";
        case ElementSize::Float32: return "f32";
        case ElementSize::Float64: return "f64";
    }
    return "???";
}

// ============================================================================
// Vector Width Encoding
// ============================================================================

/// Vector width encoding for SIMD instructions
enum class VectorWidth : std::uint8_t {
    Width128 = 0,  ///< 128-bit vectors (SSE/NEON)
    Width256 = 1,  ///< 256-bit vectors (AVX2)
    Width512 = 2   ///< 512-bit vectors (AVX-512)
};

/// Get the bit count for a vector width
[[nodiscard]] constexpr std::size_t vector_bit_width(VectorWidth vw) noexcept {
    switch (vw) {
        case VectorWidth::Width128: return 128;
        case VectorWidth::Width256: return 256;
        case VectorWidth::Width512: return 512;
    }
    return 128;
}

/// Get the lane count for a given vector width and element size
[[nodiscard]] constexpr std::size_t lane_count(VectorWidth vw, ElementSize es) noexcept {
    return vector_bit_width(vw) / (element_byte_size(es) * 8);
}

// ============================================================================
// Instruction Flags
// ============================================================================

/// Instruction flags byte
namespace flags {

/// Mask for saturating arithmetic (for integer ops)
inline constexpr std::uint8_t SATURATING = 0x01;

/// Mask for unsigned element interpretation
inline constexpr std::uint8_t UNSIGNED = 0x02;

/// Mask for rounding mode (for float ops)
inline constexpr std::uint8_t ROUND_MASK = 0x0C;
inline constexpr std::uint8_t ROUND_NEAREST = 0x00;
inline constexpr std::uint8_t ROUND_DOWN    = 0x04;
inline constexpr std::uint8_t ROUND_UP      = 0x08;
inline constexpr std::uint8_t ROUND_TRUNC   = 0x0C;

/// Mask for memory alignment requirement
inline constexpr std::uint8_t ALIGNED = 0x10;

/// Mask for fault suppression (masked loads/stores)
inline constexpr std::uint8_t FAULT_SUPPRESS = 0x20;

}  // namespace flags

// ============================================================================
// SIMD Instruction Encoding
// ============================================================================

/// Encoded SIMD instruction format
///
/// Layout (32 bits):
/// ```
/// [31:24] opcode      (8 bits)
/// [23:21] element_size (3 bits)
/// [20:19] vector_width (2 bits)
/// [18:14] vd          (5 bits) - destination register
/// [13:9]  vs1         (5 bits) - source register 1
/// [8:4]   vs2         (5 bits) - source register 2
/// [3:0]   flags       (4 bits) - lower 4 bits of flags
/// ```
///
/// Extended format (64 bits) for VFMA/VBLEND adds:
/// ```
/// [63:32] extended word
/// [36:32] vs3         (5 bits) - source register 3
/// [47:40] imm8        (8 bits) - immediate (for VSHUFFLE, VEXTRACT)
/// ```
struct SimdInstruction {
    std::uint8_t opcode;          ///< SIMD opcode (0xC0-0xCF)
    ElementSize element_size;     ///< Element type
    VectorWidth vector_width;     ///< Vector width
    std::uint8_t vd;              ///< Destination register (0-31)
    std::uint8_t vs1;             ///< Source register 1 (0-31)
    std::uint8_t vs2;             ///< Source register 2 (0-31)
    std::uint8_t vs3;             ///< Source register 3 (0-31, for FMA/BLEND)
    std::uint8_t flags;           ///< Instruction flags
    std::uint8_t imm8;            ///< 8-bit immediate (for shuffle/extract)

    // ========================================================================
    // Construction
    // ========================================================================

    /// Default constructor - creates a NOP-like instruction
    constexpr SimdInstruction() noexcept
        : opcode{0}, element_size{ElementSize::Int32}, vector_width{VectorWidth::Width128},
          vd{0}, vs1{0}, vs2{0}, vs3{0}, flags{0}, imm8{0} {}

    /// Full constructor
    constexpr SimdInstruction(
        std::uint8_t op,
        ElementSize es,
        VectorWidth vw,
        std::uint8_t dst,
        std::uint8_t src1,
        std::uint8_t src2 = 0,
        std::uint8_t src3 = 0,
        std::uint8_t flg = 0,
        std::uint8_t imm = 0
    ) noexcept
        : opcode{op}, element_size{es}, vector_width{vw},
          vd{static_cast<std::uint8_t>(dst & 0x1F)},
          vs1{static_cast<std::uint8_t>(src1 & 0x1F)},
          vs2{static_cast<std::uint8_t>(src2 & 0x1F)},
          vs3{static_cast<std::uint8_t>(src3 & 0x1F)},
          flags{flg}, imm8{imm} {}

    // ========================================================================
    // Encoding/Decoding
    // ========================================================================

    /// Encode instruction to 32-bit word
    ///
    /// @return Encoded instruction
    [[nodiscard]] constexpr std::uint32_t encode() const noexcept {
        std::uint32_t encoded = 0;
        encoded |= static_cast<std::uint32_t>(opcode) << 24;
        encoded |= static_cast<std::uint32_t>(element_size) << 21;
        encoded |= static_cast<std::uint32_t>(vector_width) << 19;
        encoded |= static_cast<std::uint32_t>(vd & 0x1F) << 14;
        encoded |= static_cast<std::uint32_t>(vs1 & 0x1F) << 9;
        encoded |= static_cast<std::uint32_t>(vs2 & 0x1F) << 4;
        encoded |= static_cast<std::uint32_t>(flags & 0x0F);
        return encoded;
    }

    /// Encode extended word for 3-operand instructions
    ///
    /// @return Extended word (upper 32 bits)
    [[nodiscard]] constexpr std::uint32_t encode_extended() const noexcept {
        std::uint32_t extended = 0;
        extended |= static_cast<std::uint32_t>(vs3 & 0x1F);
        extended |= static_cast<std::uint32_t>(imm8) << 8;
        extended |= static_cast<std::uint32_t>(flags & 0xF0) << 12;
        return extended;
    }

    /// Encode as 64-bit instruction (for extended format)
    [[nodiscard]] constexpr std::uint64_t encode64() const noexcept {
        return (static_cast<std::uint64_t>(encode_extended()) << 32) |
               static_cast<std::uint64_t>(encode());
    }

    /// Decode from 32-bit word
    ///
    /// @param encoded The encoded instruction
    /// @return Decoded instruction
    [[nodiscard]] static constexpr SimdInstruction decode(std::uint32_t encoded) noexcept {
        SimdInstruction inst;
        inst.opcode = static_cast<std::uint8_t>((encoded >> 24) & 0xFF);
        inst.element_size = static_cast<ElementSize>((encoded >> 21) & 0x07);
        inst.vector_width = static_cast<VectorWidth>((encoded >> 19) & 0x03);
        inst.vd = static_cast<std::uint8_t>((encoded >> 14) & 0x1F);
        inst.vs1 = static_cast<std::uint8_t>((encoded >> 9) & 0x1F);
        inst.vs2 = static_cast<std::uint8_t>((encoded >> 4) & 0x1F);
        inst.flags = static_cast<std::uint8_t>(encoded & 0x0F);
        inst.vs3 = 0;
        inst.imm8 = 0;
        return inst;
    }

    /// Decode from 64-bit word (extended format)
    ///
    /// @param encoded The encoded 64-bit instruction
    /// @return Decoded instruction
    [[nodiscard]] static constexpr SimdInstruction decode64(std::uint64_t encoded) noexcept {
        SimdInstruction inst = decode(static_cast<std::uint32_t>(encoded & 0xFFFFFFFF));
        std::uint32_t extended = static_cast<std::uint32_t>(encoded >> 32);
        inst.vs3 = static_cast<std::uint8_t>(extended & 0x1F);
        inst.imm8 = static_cast<std::uint8_t>((extended >> 8) & 0xFF);
        inst.flags |= static_cast<std::uint8_t>((extended >> 12) & 0xF0);
        return inst;
    }

    // ========================================================================
    // Query Methods
    // ========================================================================

    /// Check if this is a valid SIMD instruction
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return opcode::is_simd_opcode(opcode) &&
               is_valid_element_size(element_size) &&
               vd < 32 && vs1 < 32 && vs2 < 32 && vs3 < 32;
    }

    /// Check if this instruction needs the extended format
    [[nodiscard]] constexpr bool needs_extended() const noexcept {
        return opcode == opcode::VFMA ||
               opcode == opcode::VBLEND ||
               opcode == opcode::VSHUFFLE ||
               opcode == opcode::VEXTRACT;
    }

    /// Get the byte size of this instruction
    [[nodiscard]] constexpr std::size_t byte_size() const noexcept {
        return needs_extended() ? 8 : 4;
    }

    /// Get the number of lanes for this instruction's configuration
    [[nodiscard]] constexpr std::size_t num_lanes() const noexcept {
        return lane_count(vector_width, element_size);
    }

    /// Check if saturating arithmetic is enabled
    [[nodiscard]] constexpr bool is_saturating() const noexcept {
        return (flags & flags::SATURATING) != 0;
    }

    /// Check if unsigned interpretation is enabled
    [[nodiscard]] constexpr bool is_unsigned() const noexcept {
        return (flags & flags::UNSIGNED) != 0;
    }

    /// Check if aligned memory access is required
    [[nodiscard]] constexpr bool requires_alignment() const noexcept {
        return (flags & flags::ALIGNED) != 0;
    }
};

// ============================================================================
// Instruction Builder Helpers
// ============================================================================

/// Create a vector add instruction
[[nodiscard]] constexpr SimdInstruction make_vadd(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2,
    std::uint8_t flg = 0
) noexcept {
    return SimdInstruction{opcode::VADD, es, vw, vd, vs1, vs2, 0, flg, 0};
}

/// Create a vector subtract instruction
[[nodiscard]] constexpr SimdInstruction make_vsub(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2,
    std::uint8_t flg = 0
) noexcept {
    return SimdInstruction{opcode::VSUB, es, vw, vd, vs1, vs2, 0, flg, 0};
}

/// Create a vector multiply instruction
[[nodiscard]] constexpr SimdInstruction make_vmul(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2,
    std::uint8_t flg = 0
) noexcept {
    return SimdInstruction{opcode::VMUL, es, vw, vd, vs1, vs2, 0, flg, 0};
}

/// Create a vector divide instruction
[[nodiscard]] constexpr SimdInstruction make_vdiv(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2,
    std::uint8_t flg = 0
) noexcept {
    return SimdInstruction{opcode::VDIV, es, vw, vd, vs1, vs2, 0, flg, 0};
}

/// Create a dot product instruction
[[nodiscard]] constexpr SimdInstruction make_vdot(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2
) noexcept {
    return SimdInstruction{opcode::VDOT, es, vw, vd, vs1, vs2, 0, 0, 0};
}

/// Create a fused multiply-add instruction
[[nodiscard]] constexpr SimdInstruction make_vfma(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2, std::uint8_t vs3
) noexcept {
    return SimdInstruction{opcode::VFMA, es, vw, vd, vs1, vs2, vs3, 0, 0};
}

/// Create a vector minimum instruction
[[nodiscard]] constexpr SimdInstruction make_vmin(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2
) noexcept {
    return SimdInstruction{opcode::VMIN, es, vw, vd, vs1, vs2, 0, 0, 0};
}

/// Create a vector maximum instruction
[[nodiscard]] constexpr SimdInstruction make_vmax(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2
) noexcept {
    return SimdInstruction{opcode::VMAX, es, vw, vd, vs1, vs2, 0, 0, 0};
}

/// Create a vector load instruction
[[nodiscard]] constexpr SimdInstruction make_vload(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t addr_reg,
    bool aligned = false
) noexcept {
    std::uint8_t flg = aligned ? flags::ALIGNED : 0;
    return SimdInstruction{opcode::VLOAD, es, vw, vd, addr_reg, 0, 0, flg, 0};
}

/// Create a vector store instruction
[[nodiscard]] constexpr SimdInstruction make_vstore(
    ElementSize es, VectorWidth vw,
    std::uint8_t vs, std::uint8_t addr_reg,
    bool aligned = false
) noexcept {
    std::uint8_t flg = aligned ? flags::ALIGNED : 0;
    return SimdInstruction{opcode::VSTORE, es, vw, 0, addr_reg, vs, 0, flg, 0};
}

/// Create a broadcast instruction
[[nodiscard]] constexpr SimdInstruction make_vbcast(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t scalar_reg
) noexcept {
    return SimdInstruction{opcode::VBCAST, es, vw, vd, scalar_reg, 0, 0, 0, 0};
}

/// Create an extract instruction
[[nodiscard]] constexpr SimdInstruction make_vextract(
    ElementSize es, VectorWidth vw,
    std::uint8_t scalar_dst, std::uint8_t vs, std::uint8_t lane_idx
) noexcept {
    return SimdInstruction{opcode::VEXTRACT, es, vw, scalar_dst, vs, 0, 0, 0, lane_idx};
}

/// Create a compare equal instruction
[[nodiscard]] constexpr SimdInstruction make_vcmpeq(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2
) noexcept {
    return SimdInstruction{opcode::VCMPEQ, es, vw, vd, vs1, vs2, 0, 0, 0};
}

/// Create a compare less than instruction
[[nodiscard]] constexpr SimdInstruction make_vcmplt(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs1, std::uint8_t vs2,
    bool is_unsigned = false
) noexcept {
    std::uint8_t flg = is_unsigned ? flags::UNSIGNED : 0;
    return SimdInstruction{opcode::VCMPLT, es, vw, vd, vs1, vs2, 0, flg, 0};
}

/// Create a blend instruction
[[nodiscard]] constexpr SimdInstruction make_vblend(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vmask, std::uint8_t vs1, std::uint8_t vs2
) noexcept {
    return SimdInstruction{opcode::VBLEND, es, vw, vd, vmask, vs1, vs2, 0, 0};
}

/// Create a shuffle instruction
[[nodiscard]] constexpr SimdInstruction make_vshuffle(
    ElementSize es, VectorWidth vw,
    std::uint8_t vd, std::uint8_t vs, std::uint8_t indices
) noexcept {
    return SimdInstruction{opcode::VSHUFFLE, es, vw, vd, vs, 0, 0, 0, indices};
}

}  // namespace dotvm::core::simd
