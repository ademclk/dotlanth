#pragma once

/// @file disassembler.hpp
/// @brief TOOL-008 Disassembler API for DotVM bytecode
///
/// Provides functionality to convert DotVM bytecode back into human-readable
/// assembly format with control flow analysis and multiple output options.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/instruction.hpp"

namespace dotvm::core {

// ============================================================================
// Disassembly Types
// ============================================================================

/// @brief Decoded instruction with metadata for disassembly output
struct DisasmInstruction {
    std::uint32_t address;      ///< Byte offset in code section
    std::uint32_t raw_bytes;    ///< Raw 4-byte instruction (little-endian)
    std::uint8_t opcode;        ///< Opcode byte
    std::string_view mnemonic;  ///< Mnemonic from opcode_name()
    InstructionType type;       ///< Instruction format type

    // Operands (interpretation depends on type)
    std::uint8_t rd;         ///< Destination register
    std::uint8_t rs1;        ///< Source register 1 / base register
    std::uint8_t rs2;        ///< Source register 2 / value register
    std::int32_t immediate;  ///< Immediate/offset (sign-extended)

    // Control flow metadata
    bool is_branch;                       ///< Conditional branch instruction
    bool is_jump;                         ///< Unconditional jump/call
    bool is_terminator;                   ///< HALT, RET - ends basic block
    std::optional<std::uint32_t> target;  ///< Branch/jump target address
};

/// @brief Control flow graph for label generation
struct ControlFlowGraph {
    std::unordered_set<std::uint32_t> branch_targets;  ///< Addresses that are branch targets
    std::unordered_set<std::uint32_t>
        call_targets;  ///< Addresses that are call targets (functions)
};

/// @brief Options for disassembly output formatting
struct DisasmOptions {
    bool show_bytes = false;   ///< Show raw instruction bytes
    bool show_labels = false;  ///< Mark branch targets with labels
    bool annotate = false;     ///< Add comments for patterns (entry, loops)
};

/// @brief Output format selection
enum class OutputFormat {
    Text,  ///< Human-readable assembly text
    Json   ///< Structured JSON output
};

// ============================================================================
// Instruction Type Lookup
// ============================================================================

/// @brief Get the instruction type for an opcode
///
/// Maps opcodes to their instruction format (TypeA, TypeB, TypeC, TypeD, TypeM, TypeS).
/// Used to determine how to decode instruction operands.
///
/// @param opcode The opcode byte
/// @return The instruction type for the opcode
[[nodiscard]] InstructionType get_instruction_type(std::uint8_t opcode) noexcept;

// ============================================================================
// Core Disassembly Functions
// ============================================================================

/// @brief Decode a single instruction from bytecode
///
/// Decodes 4 bytes at the given offset into a DisasmInstruction structure.
/// Handles all instruction types and computes branch targets.
///
/// @param code Bytecode span (must contain at least 4 bytes from offset)
/// @param pc Program counter (byte offset) for this instruction
/// @return Decoded instruction with all metadata populated
[[nodiscard]] DisasmInstruction decode_instruction(std::span<const std::uint8_t> code,
                                                   std::uint32_t pc) noexcept;

/// @brief Disassemble entire code section
///
/// Decodes all instructions in the code section sequentially.
/// Each instruction is 4 bytes aligned.
///
/// @param code Bytecode span for the code section
/// @return Vector of decoded instructions
[[nodiscard]] std::vector<DisasmInstruction>
disassemble(std::span<const std::uint8_t> code) noexcept;

/// @brief Build control flow graph from decoded instructions
///
/// Analyzes instructions to identify branch and call targets for label generation.
/// The entry point is always added to branch_targets.
///
/// @param instrs Decoded instructions
/// @param entry_point Entry point address (byte offset)
/// @return Control flow graph with target sets populated
[[nodiscard]] ControlFlowGraph build_cfg(const std::vector<DisasmInstruction>& instrs,
                                         std::uint32_t entry_point) noexcept;

// ============================================================================
// Output Formatting Functions
// ============================================================================

/// @brief Format a single instruction as text
///
/// Produces output like: "0x0004:  ADD     R1, R2, R3"
/// With show_bytes: "0x0004:  00 01 02 03    ADD     R1, R2, R3"
/// With show_labels: Uses label names for branch targets
///
/// @param instr Decoded instruction
/// @param cfg Control flow graph (optional, for label lookup)
/// @param opts Formatting options
/// @return Formatted instruction line
[[nodiscard]] std::string format_instruction(const DisasmInstruction& instr,
                                             const ControlFlowGraph* cfg,
                                             const DisasmOptions& opts);

/// @brief Format complete disassembly as text
///
/// Produces a complete assembly listing with header comment, labels, and instructions.
///
/// @param instrs Decoded instructions
/// @param cfg Control flow graph for labels
/// @param header Bytecode header for metadata
/// @param constants Constant pool values
/// @param opts Formatting options
/// @return Complete formatted disassembly text
[[nodiscard]] std::string format_disassembly(const std::vector<DisasmInstruction>& instrs,
                                             const ControlFlowGraph& cfg,
                                             const BytecodeHeader& header,
                                             const std::vector<Value>& constants,
                                             const DisasmOptions& opts);

/// @brief Format disassembly as JSON
///
/// Produces structured JSON output with header, labels, and instructions.
///
/// @param instrs Decoded instructions
/// @param cfg Control flow graph for labels
/// @param header Bytecode header for metadata
/// @param constants Constant pool values
/// @return JSON string
[[nodiscard]] std::string format_json(const std::vector<DisasmInstruction>& instrs,
                                      const ControlFlowGraph& cfg, const BytecodeHeader& header,
                                      const std::vector<Value>& constants);

}  // namespace dotvm::core
