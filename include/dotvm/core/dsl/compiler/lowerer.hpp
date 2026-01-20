#pragma once

/// @file lowerer.hpp
/// @brief DSL-002 IR Lowerer - SSA to Linear IR conversion
///
/// Converts SSA IR to linear IR suitable for bytecode generation:
/// - Phi elimination via critical edge splitting
/// - Linear scan register allocation
/// - Convert to linear instruction sequence

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "dotvm/core/dsl/ir/instruction.hpp"
#include "dotvm/core/dsl/ir/types.hpp"
#include "dotvm/core/dsl/source_location.hpp"

namespace dotvm::core::dsl::compiler {

/// @brief Linear IR instruction (post-lowering)
struct LinearInstr {
    ir::InstructionKind kind;
    std::uint8_t dest_reg{0};      ///< Destination register (if applicable)
    std::uint8_t src1_reg{0};      ///< First source register
    std::uint8_t src2_reg{0};      ///< Second source register
    std::int32_t immediate{0};     ///< Immediate value or offset
    std::string label;             ///< For jumps: target label
    SourceSpan span;
};

/// @brief Linear basic block
struct LinearBlock {
    std::uint32_t id;
    std::string label;
    std::vector<LinearInstr> instructions;
    std::int32_t offset{-1};  ///< Byte offset in code section (-1 = not yet assigned)
};

/// @brief Linear IR representation (ready for codegen)
struct LinearIR {
    std::string name;
    std::vector<LinearBlock> blocks;
    std::vector<ir::StateSlot> state_slots;
    std::uint32_t entry_block_id{0};
    std::uint8_t max_register_used{0};
};

/// @brief Register allocation result
struct RegAllocation {
    std::unordered_map<std::uint32_t, std::uint8_t> value_to_reg;
    std::uint8_t max_reg_used{0};
};

/// @brief SSA to Linear IR lowering pass
class Lowerer {
public:
    /// @brief Lower a DotIR to LinearIR
    [[nodiscard]] LinearIR lower(const ir::DotIR& dot);

private:
    // Register allocation (simple linear scan)
    RegAllocation allocate_registers(const ir::DotIR& dot);

    // Phi elimination
    void eliminate_phis(ir::DotIR& dot);

    // Convert SSA block to linear block
    LinearBlock lower_block(const ir::BasicBlock& block, const RegAllocation& alloc);

    // Convert SSA instruction to linear instruction(s)
    std::vector<LinearInstr> lower_instruction(const ir::Instruction& instr,
                                                const RegAllocation& alloc);

    // Register lookup
    std::uint8_t get_reg(std::uint32_t value_id, const RegAllocation& alloc);
};

}  // namespace dotvm::core::dsl::compiler
