/// @file jit_compiler.cpp
/// @brief Implementation of copy-and-patch JIT compiler

#include "dotvm/jit/jit_compiler.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <ranges>
#include <span>
#include <vector>

#include "dotvm/jit/jit_config.hpp"
#include "dotvm/jit/jit_profiler.hpp"
#include "dotvm/jit/stencil.hpp"

namespace dotvm::jit {

JitCompiler::JitCompiler(const JitConfig& config, JitCodeBuffer& buffer,
                         const StencilRegistry& stencils) noexcept
    : config_(config), buffer_(buffer), stencils_(stencils) {}

CompileResult<CompiledFunction> JitCompiler::compile(FunctionId func_id,
                                                     std::span<const BytecodeInstr> instructions) {
    if (!config_.enabled) [[unlikely]] {
        return std::unexpected(JitStatus::Disabled);
    }

    if (instructions.empty()) [[unlikely]] {
        return std::unexpected(JitStatus::InvalidFunction);
    }

    // Estimate code size and check buffer space
    const std::size_t estimated_size = estimate_code_size(instructions);
    if (!buffer_.has_space(estimated_size)) [[unlikely]] {
        return std::unexpected(JitStatus::CacheFull);
    }

    // Allocate space in the buffer
    auto alloc_result = buffer_.allocate(estimated_size);
    if (!alloc_result) [[unlikely]] {
        return std::unexpected(JitStatus::AllocationFailed);
    }

    auto output = *alloc_result;
    std::size_t offset = 0;

    // Calculate frame size (for now, fixed size)
    // NOLINTNEXTLINE(readability-identifier-naming)
    constexpr std::size_t frame_size = 64;

    // Emit prologue
    auto prologue_result = emit_prologue(output.subspan(offset), frame_size);
    if (!prologue_result) [[unlikely]] {
        return std::unexpected(prologue_result.error());
    }
    offset += *prologue_result;

    // Emit each instruction
    for (const auto& instr : instructions) {
        auto instr_result = emit_instruction(output.subspan(offset), instr);
        if (!instr_result) [[unlikely]] {
            // If we can't compile an instruction, emit fallback
            ++stats_.fallbacks_emitted;
            // For now, just skip unsupported instructions
            // In a real implementation, we'd emit interpreter fallback
            continue;
        }
        offset += *instr_result;
        ++stats_.instructions_compiled;
    }

    // Emit epilogue
    auto epilogue_result = emit_epilogue(output.subspan(offset), frame_size);
    if (!epilogue_result) [[unlikely]] {
        return std::unexpected(epilogue_result.error());
    }
    offset += *epilogue_result;

    // Build result
    CompiledFunction result;
    result.code = output.data();
    result.code_size = offset;
    result.function_id = func_id;
    result.entry_pc = instructions.front().pc;
    result.end_pc = instructions.back().pc + 4;  // Assume 4-byte instructions

    ++stats_.functions_compiled;
    stats_.bytes_generated += offset;

    return result;
}

bool JitCompiler::supports_opcode(std::uint8_t opcode) const noexcept {
    return stencils_.has_stencil(opcode);
}

bool JitCompiler::can_compile(std::span<const BytecodeInstr> instructions) const noexcept {
    return std::ranges::all_of(instructions,
                               [this](const auto& instr) { return supports_opcode(instr.opcode); });
}

std::size_t
JitCompiler::estimate_code_size(std::span<const BytecodeInstr> instructions) const noexcept {
    // Start with prologue/epilogue overhead
    std::size_t size = 64;  // Conservative estimate for prologue + epilogue

    for (const auto& instr : instructions) {
        const auto* stencil = stencils_.get(instr.opcode);
        if (stencil != nullptr) {
            size += stencil->code_size;
        } else {
            // Estimate for fallback
            size += 32;
        }
    }

    // Add padding for alignment (16-byte aligned)
    size = (size + 15) & ~static_cast<std::size_t>(15);

    return size;
}

CompileResult<std::size_t> JitCompiler::emit_prologue(std::span<std::uint8_t> output,
                                                      std::size_t frame_size) {
#if defined(__x86_64__) || defined(_M_X64)
    const auto& prologue = stencils::x86_64::prologue;

    if (output.size() < prologue.code_size) [[unlikely]] {
        return std::unexpected(JitStatus::AllocationFailed);
    }

    // Copy prologue code
    std::memcpy(output.data(), prologue.code, prologue.code_size);

    // Patch frame size hole
    const std::int64_t operands[] = {static_cast<std::int64_t>(frame_size)};
    for (std::size_t i = 0; i < prologue.hole_count; ++i) {
        patch_hole(output, prologue.holes[i], operands[prologue.holes[i].operand_index]);
    }

    return prologue.code_size;
#else
    return std::unexpected(JitStatus::UnsupportedOpcode);
#endif
}

CompileResult<std::size_t> JitCompiler::emit_epilogue(std::span<std::uint8_t> output,
                                                      std::size_t frame_size) {
#if defined(__x86_64__) || defined(_M_X64)
    const auto& epilogue = stencils::x86_64::epilogue;

    if (output.size() < epilogue.code_size) [[unlikely]] {
        return std::unexpected(JitStatus::AllocationFailed);
    }

    // Copy epilogue code
    std::memcpy(output.data(), epilogue.code, epilogue.code_size);

    // Patch frame size hole
    const std::int64_t operands[] = {static_cast<std::int64_t>(frame_size)};
    for (std::size_t i = 0; i < epilogue.hole_count; ++i) {
        patch_hole(output, epilogue.holes[i], operands[epilogue.holes[i].operand_index]);
    }

    return epilogue.code_size;
#else
    return std::unexpected(JitStatus::UnsupportedOpcode);
#endif
}

CompileResult<std::size_t> JitCompiler::emit_instruction(std::span<std::uint8_t> output,
                                                         const BytecodeInstr& instr) {
    const auto* stencil = stencils_.get(instr.opcode);
    if (stencil == nullptr) [[unlikely]] {
        return std::unexpected(JitStatus::UnsupportedOpcode);
    }

    // Build operands array
    // Most stencils expect: [dst_offset, src1_offset, src2_offset] or variations
    std::array<std::int64_t, MAX_STENCIL_HOLES> operands{};

    // NOLINTBEGIN(bugprone-branch-clone) - cases intentionally share code paths
    for (std::size_t i = 0; i < stencil->hole_count; ++i) {
        const auto& hole = stencil->holes[i];
        switch (hole.operand_index) {
            case 0:  // dst
                if (hole.type == HoleType::Immediate32) {
                    operands[i] = reg_offset(instr.dst);
                } else if (hole.type == HoleType::Immediate64) {
                    operands[i] = instr.immediate;
                } else if (hole.type == HoleType::RelativeOffset32) {
                    operands[i] = instr.immediate;  // Jump target
                } else {
                    operands[i] = instr.dst;
                }
                break;
            case 1:  // src1
                if (hole.type == HoleType::Immediate32) {
                    operands[i] = reg_offset(instr.src1);
                } else if (hole.type == HoleType::Immediate64) {
                    operands[i] = instr.immediate;
                } else if (hole.type == HoleType::RelativeOffset32) {
                    operands[i] = instr.immediate;
                } else {
                    operands[i] = instr.src1;
                }
                break;
            case 2:  // src2
                if (hole.type == HoleType::Immediate32) {
                    operands[i] = reg_offset(instr.src2);
                } else {
                    operands[i] = instr.src2;
                }
                break;
            default:
                operands[i] = 0;
                break;
        }
    }
    // NOLINTEND(bugprone-branch-clone)

    return emit_stencil(output, *stencil, std::span(operands.data(), stencil->hole_count));
}

CompileResult<std::size_t> JitCompiler::emit_stencil(std::span<std::uint8_t> output,
                                                     const Stencil& stencil,
                                                     std::span<const std::int64_t> operands) {
    if (output.size() < stencil.code_size) [[unlikely]] {
        return std::unexpected(JitStatus::AllocationFailed);
    }

    // Copy stencil code
    std::memcpy(output.data(), stencil.code, stencil.code_size);

    // Patch holes
    for (std::size_t i = 0; i < stencil.hole_count && i < operands.size(); ++i) {
        patch_hole(output.subspan(0, stencil.code_size), stencil.holes[i], operands[i]);
    }

    return stencil.code_size;
}

void JitCompiler::patch_hole(std::span<std::uint8_t> code, const StencilHole& hole,
                             std::int64_t value) noexcept {
    if (hole.offset >= code.size()) [[unlikely]] {
        return;
    }

    std::uint8_t* ptr = code.data() + hole.offset;

    switch (hole.type) {
        case HoleType::Immediate32:
        case HoleType::RelativeOffset32: {
            auto val32 = static_cast<std::int32_t>(value + hole.adjustment);
            std::memcpy(ptr, &val32, sizeof(val32));
            break;
        }
        case HoleType::Immediate64:
        case HoleType::AbsoluteAddress: {
            auto val64 = static_cast<std::int64_t>(value + hole.adjustment);
            std::memcpy(ptr, &val64, sizeof(val64));
            break;
        }
        case HoleType::RegisterIndex: {
            *ptr = static_cast<std::uint8_t>(value);
            break;
        }
        case HoleType::PcRelative: {
            // PC-relative addressing is tricky - would need to know
            // the actual address the code will be at
            auto val32 = static_cast<std::int32_t>(value + hole.adjustment);
            std::memcpy(ptr, &val32, sizeof(val32));
            break;
        }
    }
}

// ============================================================================
// Bytecode Parsing
// ============================================================================

std::vector<BytecodeInstr> parse_bytecode(std::span<const std::uint8_t> bytecode,
                                          std::size_t start_pc, std::size_t end_pc) {
    std::vector<BytecodeInstr> instructions;

    // Simple 4-byte instruction format for now:
    // [opcode][dst][src1][src2]
    // Or for immediates:
    // [opcode][dst][imm_low][imm_high] followed by more immediate bytes

    for (std::size_t pc = start_pc; pc < end_pc && pc + 4 <= bytecode.size(); pc += 4) {
        BytecodeInstr instr;
        instr.pc = pc;
        instr.opcode = bytecode[pc];
        instr.dst = bytecode[pc + 1];
        instr.src1 = bytecode[pc + 2];
        instr.src2 = bytecode[pc + 3];

        // For instructions with immediates, combine src1/src2 or look ahead
        if (instr.opcode == static_cast<std::uint8_t>(JitOpcode::LOAD_IMM)) {
            // Immediate follows in next 8 bytes
            if (pc + 12 <= bytecode.size()) {
                std::memcpy(&instr.immediate, &bytecode[pc + 4], 8);
                pc += 8;  // Skip the immediate bytes
            }
        } else if (instr.opcode == static_cast<std::uint8_t>(JitOpcode::JMP) ||
                   instr.opcode == static_cast<std::uint8_t>(JitOpcode::JMP_Z) ||
                   instr.opcode == static_cast<std::uint8_t>(JitOpcode::JMP_NZ)) {
            // Jump target is signed 16-bit offset in src1:src2
            auto offset = static_cast<std::int16_t>((static_cast<std::uint16_t>(instr.src2) << 8) |
                                                    instr.src1);
            instr.immediate = offset;
        }

        instructions.push_back(instr);
    }

    return instructions;
}

}  // namespace dotvm::jit
