#include "dotvm/core/dsl/compiler/lowerer.hpp"

#include <algorithm>
#include <unordered_set>

namespace dotvm::core::dsl::compiler {

using namespace ir;

// Register 0 is hardwired to zero in DotVM
constexpr std::uint8_t REG_ZERO = 0;
constexpr std::uint8_t REG_FIRST_ALLOCATABLE = 1;
constexpr std::uint8_t REG_MAX = 255;

// ============================================================================
// Lowerer Implementation
// ============================================================================

LinearIR Lowerer::lower(const DotIR& dot) {
    LinearIR result;
    result.name = dot.name;
    result.state_slots = dot.state_slots;
    result.entry_block_id = dot.entry_block_id;

    // Allocate registers
    auto alloc = allocate_registers(dot);
    result.max_register_used = alloc.max_reg_used;

    // Lower each block
    for (const auto& block : dot.blocks) {
        result.blocks.push_back(lower_block(*block, alloc));
    }

    return result;
}

RegAllocation Lowerer::allocate_registers(const DotIR& dot) {
    RegAllocation alloc;
    std::uint8_t next_reg = REG_FIRST_ALLOCATABLE;

    // Simple linear scan: assign registers in order of first definition
    for (const auto& block : dot.blocks) {
        // Phi nodes
        for (const auto& phi : block->phis) {
            if (alloc.value_to_reg.find(phi->result.id) == alloc.value_to_reg.end()) {
                alloc.value_to_reg[phi->result.id] = next_reg++;
                if (next_reg > REG_MAX)
                    next_reg = REG_FIRST_ALLOCATABLE;  // Wrap (spill would be needed)
            }
        }

        // Instructions
        for (const auto& instr : block->instructions) {
            if (auto* result = instr->get_result()) {
                if (alloc.value_to_reg.find(result->id) == alloc.value_to_reg.end()) {
                    alloc.value_to_reg[result->id] = next_reg++;
                    if (next_reg > REG_MAX)
                        next_reg = REG_FIRST_ALLOCATABLE;
                }
            }
        }
    }

    alloc.max_reg_used = next_reg > 0 ? next_reg - 1 : 0;
    return alloc;
}

void Lowerer::eliminate_phis(DotIR& /*dot*/) {
    // Phi elimination would insert copy instructions at predecessor block ends
    // For now, we handle phis during lowering by generating moves
}

LinearBlock Lowerer::lower_block(const BasicBlock& block, const RegAllocation& alloc) {
    LinearBlock result;
    result.id = block.id;
    // Always use bb<id> format for consistent label resolution
    result.label = "bb" + std::to_string(block.id);

    // Lower phi nodes as parallel copies (simplified: sequential copies)
    // In a real implementation, we'd insert copies at predecessor block ends
    // For now, skip phi lowering (handled by SSA construction)
    (void)block.phis;  // Suppress unused warning

    // Lower instructions
    for (const auto& instr : block.instructions) {
        auto lowered = lower_instruction(*instr, alloc);
        for (auto& li : lowered) {
            result.instructions.push_back(std::move(li));
        }
    }

    // Lower terminator
    if (block.terminator) {
        auto lowered = lower_instruction(*block.terminator, alloc);
        for (auto& li : lowered) {
            result.instructions.push_back(std::move(li));
        }
    }

    return result;
}

std::vector<LinearInstr> Lowerer::lower_instruction(const Instruction& instr,
                                                    const RegAllocation& alloc) {
    std::vector<LinearInstr> result;

    std::visit(
        [&](const auto& inst) {
            using T = std::decay_t<decltype(inst)>;

            if constexpr (std::is_same_v<T, ir::BinaryOp>) {
                LinearInstr li;
                li.kind = inst;
                li.dest_reg = get_reg(inst.result.id, alloc);
                li.src1_reg = get_reg(inst.left_id, alloc);
                li.src2_reg = get_reg(inst.right_id, alloc);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, ir::UnaryOp>) {
                LinearInstr li;
                li.kind = inst;
                li.dest_reg = get_reg(inst.result.id, alloc);
                li.src1_reg = get_reg(inst.operand_id, alloc);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, Compare>) {
                LinearInstr li;
                li.kind = inst;
                li.dest_reg = get_reg(inst.result.id, alloc);
                li.src1_reg = get_reg(inst.left_id, alloc);
                li.src2_reg = get_reg(inst.right_id, alloc);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, LoadConst>) {
                LinearInstr li;
                li.kind = inst;
                li.dest_reg = get_reg(inst.result.id, alloc);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, StateGet>) {
                LinearInstr li;
                li.kind = inst;
                li.dest_reg = get_reg(inst.result.id, alloc);
                li.immediate = static_cast<std::int32_t>(inst.slot_index);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, StatePut>) {
                LinearInstr li;
                li.kind = inst;
                li.src1_reg = get_reg(inst.value_id, alloc);
                li.immediate = static_cast<std::int32_t>(inst.slot_index);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, Call>) {
                LinearInstr li;
                li.kind = inst;
                if (inst.result.type != ValueType::Void) {
                    li.dest_reg = get_reg(inst.result.id, alloc);
                }
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, Cast>) {
                LinearInstr li;
                li.kind = inst;
                li.dest_reg = get_reg(inst.result.id, alloc);
                li.src1_reg = get_reg(inst.value_id, alloc);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, Copy>) {
                LinearInstr li;
                li.kind = inst;
                li.dest_reg = get_reg(inst.result.id, alloc);
                li.src1_reg = get_reg(inst.value_id, alloc);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, Jump>) {
                LinearInstr li;
                li.kind = inst;
                li.label = "bb" + std::to_string(inst.target_block_id);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, Branch>) {
                LinearInstr li;
                li.kind = inst;
                li.src1_reg = get_reg(inst.condition_id, alloc);
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, Return>) {
                LinearInstr li;
                li.kind = inst;
                if (inst.value_id) {
                    li.src1_reg = get_reg(*inst.value_id, alloc);
                }
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, Halt>) {
                LinearInstr li;
                li.kind = inst;
                if (inst.exit_code_id) {
                    li.src1_reg = get_reg(*inst.exit_code_id, alloc);
                }
                li.span = instr.span;
                result.push_back(li);
            } else if constexpr (std::is_same_v<T, Unreachable>) {
                LinearInstr li;
                li.kind = inst;
                li.span = instr.span;
                result.push_back(li);
            }
        },
        instr.kind);

    return result;
}

std::uint8_t Lowerer::get_reg(std::uint32_t value_id, const RegAllocation& alloc) {
    auto it = alloc.value_to_reg.find(value_id);
    if (it != alloc.value_to_reg.end()) {
        return it->second;
    }
    // Fallback to R0 (zero register) for missing values
    return REG_ZERO;
}

}  // namespace dotvm::core::dsl::compiler
