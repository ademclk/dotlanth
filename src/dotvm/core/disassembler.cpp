/// @file disassembler.cpp
/// @brief TOOL-008 Disassembler implementation

#include "dotvm/core/disassembler.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "dotvm/core/asm/asm_lexer.hpp"
#include "dotvm/core/opcode.hpp"

namespace dotvm::core {

// ============================================================================
// Instruction Type Lookup
// ============================================================================

InstructionType get_instruction_type(std::uint8_t op) noexcept {
    // TypeD: JZ, JNZ (jump with register test)
    if (op == opcode::JZ || op == opcode::JNZ) {
        return InstructionType::TypeD;
    }

    // TypeS: Shift-immediate operations
    if (op >= opcode::SHLI && op <= opcode::SARI) {
        return InstructionType::TypeS;
    }

    // TypeM: Memory load/store operations
    if (is_typed_memory_op(op)) {
        return InstructionType::TypeM;
    }

    // TypeC: Control flow without register operands
    // JMP, CALL, RET, HALT, NOP, BREAK, SYSCALL, DEBUG, CATCH, ENDTRY
    if (op == opcode::JMP || op == opcode::CALL || op == opcode::RET || op == opcode::HALT ||
        op == opcode::NOP || op == opcode::BREAK || op == opcode::SYSCALL || op == opcode::DEBUG ||
        op == opcode::CATCH || op == opcode::ENDTRY) {
        return InstructionType::TypeC;
    }

    // TypeB: Immediate operations
    // Arithmetic immediate
    if (op >= opcode::ADDI && op <= opcode::MULI) {
        return InstructionType::TypeB;
    }
    // Bitwise immediate
    if (op >= opcode::ANDI && op <= opcode::XORI) {
        return InstructionType::TypeB;
    }
    // Comparison immediate
    if (op >= opcode::CMPI_EQ && op <= opcode::CMPI_GE) {
        return InstructionType::TypeB;
    }
    // Exception: TRY is TypeB
    if (op == opcode::TRY) {
        return InstructionType::TypeB;
    }

    // TypeA: Everything else (register-register operations)
    return InstructionType::TypeA;
}

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

/// @brief Check if opcode is a branch (conditional)
[[nodiscard]] bool is_branch_opcode(std::uint8_t op) noexcept {
    // TypeD branches
    if (op == opcode::JZ || op == opcode::JNZ) {
        return true;
    }
    // TypeA branches (BEQ, BNE, BLT, BLE, BGT, BGE)
    if (op >= opcode::BEQ && op <= opcode::BGE) {
        return true;
    }
    return false;
}

/// @brief Check if opcode is a jump (unconditional transfer)
[[nodiscard]] bool is_jump_opcode(std::uint8_t op) noexcept {
    return op == opcode::JMP || op == opcode::CALL;
}

/// @brief Check if opcode terminates a basic block
[[nodiscard]] bool is_terminator_opcode(std::uint8_t op) noexcept {
    return op == opcode::HALT || op == opcode::RET;
}

/// @brief Sign-extend 8-bit value to 32-bit
[[nodiscard]] constexpr std::int32_t sign_extend_8(std::uint8_t value) noexcept {
    return static_cast<std::int32_t>(static_cast<std::int8_t>(value));
}

/// @brief Sign-extend 16-bit value to 32-bit
[[nodiscard]] constexpr std::int32_t sign_extend_16(std::uint16_t value) noexcept {
    return static_cast<std::int32_t>(static_cast<std::int16_t>(value));
}

/// @brief Compute branch/jump target address
[[nodiscard]] std::optional<std::uint32_t> compute_target(std::uint8_t op, std::uint32_t pc,
                                                          std::int32_t offset) noexcept {
    // Terminators don't have targets (RET pops from stack, HALT stops)
    if (is_terminator_opcode(op)) {
        return std::nullopt;
    }

    // For branches and jumps, target = pc + offset
    if (is_branch_opcode(op) || is_jump_opcode(op)) {
        std::int64_t target = static_cast<std::int64_t>(pc) + offset;
        if (target < 0) {
            return std::nullopt;  // Invalid target
        }
        return static_cast<std::uint32_t>(target);
    }

    return std::nullopt;
}

/// @brief Format a register operand
[[nodiscard]] std::string format_register(std::uint8_t reg) {
    return "R" + std::to_string(reg);
}

/// @brief Format an immediate value
[[nodiscard]] std::string format_immediate(std::int32_t imm) {
    return "#" + std::to_string(imm);
}

/// @brief Format hex address
[[nodiscard]] std::string format_hex_addr(std::uint32_t addr) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0') << std::setw(4) << addr;
    return oss.str();
}

/// @brief Format raw bytes as hex
[[nodiscard]] std::string format_raw_bytes(std::uint32_t bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(2) << ((bytes >> 24) & 0xFF) << " "
        << std::setw(2) << ((bytes >> 16) & 0xFF) << " " << std::setw(2) << ((bytes >> 8) & 0xFF)
        << " " << std::setw(2) << (bytes & 0xFF);
    return oss.str();
}

/// @brief Generate label name for address
[[nodiscard]] std::string generate_label(std::uint32_t addr, const ControlFlowGraph& cfg,
                                         std::uint32_t entry_point) {
    if (addr == entry_point) {
        return "_entry";
    }
    if (cfg.call_targets.contains(addr)) {
        std::ostringstream oss;
        oss << "_func_" << std::hex << std::setfill('0') << std::setw(4) << addr;
        return oss.str();
    }
    // Find label index (sorted order)
    std::vector<std::uint32_t> sorted_targets(cfg.branch_targets.begin(), cfg.branch_targets.end());
    std::sort(sorted_targets.begin(), sorted_targets.end());
    auto it = std::find(sorted_targets.begin(), sorted_targets.end(), addr);
    if (it != sorted_targets.end()) {
        return ".L" + std::to_string(std::distance(sorted_targets.begin(), it));
    }
    // Fallback
    std::ostringstream oss;
    oss << ".L" << std::hex << addr;
    return oss.str();
}

}  // namespace

// ============================================================================
// Core Disassembly Functions
// ============================================================================

DisasmInstruction decode_instruction(std::span<const std::uint8_t> code,
                                     std::uint32_t pc) noexcept {
    DisasmInstruction instr{};
    instr.address = pc;

    // Read 4 bytes as little-endian u32
    instr.raw_bytes = endian::read_u32_le(code.data());
    instr.opcode = extract_opcode(instr.raw_bytes);
    instr.type = get_instruction_type(instr.opcode);

    // Get mnemonic from assembler lexer
    instr.mnemonic = asm_::opcode_name(instr.opcode);

    // Decode based on instruction type
    switch (instr.type) {
        case InstructionType::TypeA: {
            auto decoded = decode_type_a(instr.raw_bytes);
            instr.rd = decoded.rd;
            instr.rs1 = decoded.rs1;
            instr.rs2 = decoded.rs2;
            // TypeA branches have offset in rs2 (sign-extended 8-bit)
            if (is_branch_opcode(instr.opcode)) {
                instr.immediate = sign_extend_8(decoded.rs2);
            }
            break;
        }
        case InstructionType::TypeB: {
            auto decoded = decode_type_b(instr.raw_bytes);
            instr.rd = decoded.rd;
            instr.immediate = sign_extend_16(decoded.imm16);
            break;
        }
        case InstructionType::TypeC: {
            auto decoded = decode_type_c(instr.raw_bytes);
            instr.immediate = decoded.offset24;  // Already sign-extended by decoder
            break;
        }
        case InstructionType::TypeD: {
            auto decoded = decode_type_d(instr.raw_bytes);
            instr.rs1 = decoded.rs;  // Register to test
            instr.immediate = sign_extend_16(static_cast<std::uint16_t>(decoded.offset16));
            break;
        }
        case InstructionType::TypeM: {
            auto decoded = decode_type_m(instr.raw_bytes);
            instr.rd = decoded.rd_rs2;  // Destination or source value
            instr.rs1 = decoded.rs1;    // Base register
            instr.immediate = decoded.offset8;
            break;
        }
        case InstructionType::TypeS: {
            auto decoded = decode_type_s(instr.raw_bytes);
            instr.rd = decoded.rd;
            instr.rs1 = decoded.rs1;
            instr.immediate = decoded.shamt6;
            break;
        }
    }

    // Set control flow flags
    instr.is_branch = is_branch_opcode(instr.opcode);
    instr.is_jump = is_jump_opcode(instr.opcode);
    instr.is_terminator = is_terminator_opcode(instr.opcode);

    // Compute target address for control flow instructions
    instr.target = compute_target(instr.opcode, pc, instr.immediate);

    return instr;
}

std::vector<DisasmInstruction> disassemble(std::span<const std::uint8_t> code) noexcept {
    std::vector<DisasmInstruction> instructions;

    // Reserve based on expected instruction count
    const std::size_t max_instrs = code.size() / 4;
    instructions.reserve(max_instrs);

    // Decode each 4-byte instruction
    for (std::size_t offset = 0; offset + 4 <= code.size(); offset += 4) {
        auto instr = decode_instruction(code.subspan(offset), static_cast<std::uint32_t>(offset));
        instructions.push_back(instr);
    }

    return instructions;
}

ControlFlowGraph build_cfg(const std::vector<DisasmInstruction>& instrs,
                           std::uint32_t entry_point) noexcept {
    ControlFlowGraph cfg;

    // Entry point is always a branch target
    cfg.branch_targets.insert(entry_point);

    // Scan for branch and call targets
    for (const auto& instr : instrs) {
        if (instr.target.has_value()) {
            if (instr.opcode == opcode::CALL) {
                cfg.call_targets.insert(instr.target.value());
            } else {
                cfg.branch_targets.insert(instr.target.value());
            }
        }
    }

    return cfg;
}

// ============================================================================
// Output Formatting Functions
// ============================================================================

std::string format_instruction(const DisasmInstruction& instr, const ControlFlowGraph* cfg,
                               const DisasmOptions& opts) {
    std::ostringstream oss;

    // Address
    oss << format_hex_addr(instr.address) << ":  ";

    // Raw bytes (optional)
    if (opts.show_bytes) {
        oss << format_raw_bytes(instr.raw_bytes) << "    ";
    }

    // Mnemonic (left-aligned, padded)
    std::string mnem{instr.mnemonic};
    if (mnem.empty()) {
        mnem = "???";
    }
    oss << std::left << std::setw(8) << mnem;

    // Operands based on instruction type
    switch (instr.type) {
        case InstructionType::TypeA:
            if (instr.opcode == opcode::NEG || instr.opcode == opcode::NOT ||
                instr.opcode == opcode::FNEG || instr.opcode == opcode::FSQRT ||
                instr.opcode == opcode::F2I || instr.opcode == opcode::I2F) {
                // Unary operations: Rd, Rs1
                oss << format_register(instr.rd) << ", " << format_register(instr.rs1);
            } else if (is_branch_opcode(instr.opcode)) {
                // TypeA branches: Rs1, Rs2, offset
                oss << format_register(instr.rs1) << ", " << format_register(instr.rs2) << ", ";
                if (opts.show_labels && cfg && instr.target.has_value() &&
                    cfg->branch_targets.contains(instr.target.value())) {
                    oss << generate_label(instr.target.value(), *cfg, 0);
                } else {
                    oss << format_immediate(instr.immediate);
                }
            } else if (instr.opcode == opcode::THROW) {
                // THROW: Rtype, Rpayload
                oss << format_register(instr.rd) << ", " << format_register(instr.rs1);
            } else {
                // Standard: Rd, Rs1, Rs2
                oss << format_register(instr.rd) << ", " << format_register(instr.rs1) << ", "
                    << format_register(instr.rs2);
            }
            break;

        case InstructionType::TypeB:
            oss << format_register(instr.rd) << ", " << format_immediate(instr.immediate);
            break;

        case InstructionType::TypeC:
            if (instr.is_jump && instr.target.has_value()) {
                if (opts.show_labels && cfg) {
                    if (instr.opcode == opcode::CALL &&
                        cfg->call_targets.contains(instr.target.value())) {
                        oss << generate_label(instr.target.value(), *cfg, 0);
                    } else if (cfg->branch_targets.contains(instr.target.value())) {
                        oss << generate_label(instr.target.value(), *cfg, 0);
                    } else {
                        oss << format_immediate(instr.immediate);
                    }
                } else {
                    oss << format_immediate(instr.immediate);
                }
            }
            // NOP, HALT, RET, etc. have no operands
            break;

        case InstructionType::TypeD:
            oss << format_register(instr.rs1) << ", ";
            if (opts.show_labels && cfg && instr.target.has_value() &&
                cfg->branch_targets.contains(instr.target.value())) {
                oss << generate_label(instr.target.value(), *cfg, 0);
            } else {
                oss << format_immediate(instr.immediate);
            }
            break;

        case InstructionType::TypeM:
            if (is_store_op(instr.opcode)) {
                // STORE: Rs2, [Rs1+offset]
                oss << format_register(instr.rd) << ", [" << format_register(instr.rs1);
                if (instr.immediate >= 0) {
                    oss << "+" << instr.immediate;
                } else {
                    oss << instr.immediate;
                }
                oss << "]";
            } else {
                // LOAD/LEA: Rd, [Rs1+offset]
                oss << format_register(instr.rd) << ", [" << format_register(instr.rs1);
                if (instr.immediate >= 0) {
                    oss << "+" << instr.immediate;
                } else {
                    oss << instr.immediate;
                }
                oss << "]";
            }
            break;

        case InstructionType::TypeS:
            oss << format_register(instr.rd) << ", " << format_register(instr.rs1) << ", "
                << format_immediate(instr.immediate);
            break;
    }

    return oss.str();
}

std::string format_disassembly(const std::vector<DisasmInstruction>& instrs,
                               const ControlFlowGraph& cfg, const BytecodeHeader& header,
                               const std::vector<Value>& constants, const DisasmOptions& opts) {
    std::ostringstream oss;

    // Header comment
    oss << "; DotVM Bytecode (v" << static_cast<int>(header.version) << ", "
        << (header.arch == Architecture::Arch64 ? "64-bit" : "32-bit") << ")\n";
    oss << "; Entry: " << format_hex_addr(static_cast<std::uint32_t>(header.entry_point))
        << ", Code: " << header.code_size << " bytes\n";

    if (!constants.empty()) {
        oss << "; Constants: " << constants.size() << " entries\n";
    }
    oss << "\n";

    // Build label map for addresses
    std::unordered_set<std::uint32_t> all_labels;
    all_labels.insert(cfg.branch_targets.begin(), cfg.branch_targets.end());
    all_labels.insert(cfg.call_targets.begin(), cfg.call_targets.end());

    // Output instructions with labels
    for (const auto& instr : instrs) {
        // Check if this address needs a label
        if (opts.show_labels && all_labels.contains(instr.address)) {
            std::string label =
                generate_label(instr.address, cfg, static_cast<std::uint32_t>(header.entry_point));
            oss << label << ":\n";
        }

        // Output instruction
        oss << format_instruction(instr, &cfg, opts) << "\n";
    }

    return oss.str();
}

std::string format_json(const std::vector<DisasmInstruction>& instrs, const ControlFlowGraph& cfg,
                        const BytecodeHeader& header, const std::vector<Value>& constants) {
    std::ostringstream oss;

    oss << "{\n";

    // Header
    oss << "  \"header\": {\n";
    oss << "    \"version\": " << static_cast<int>(header.version) << ",\n";
    oss << "    \"arch\": \"" << (header.arch == Architecture::Arch64 ? "64-bit" : "32-bit")
        << "\",\n";
    oss << "    \"entry_point\": " << header.entry_point << ",\n";
    oss << "    \"code_size\": " << header.code_size << "\n";
    oss << "  },\n";

    // Labels
    oss << "  \"labels\": {\n";
    std::vector<std::uint32_t> all_targets;
    all_targets.insert(all_targets.end(), cfg.branch_targets.begin(), cfg.branch_targets.end());
    all_targets.insert(all_targets.end(), cfg.call_targets.begin(), cfg.call_targets.end());
    std::sort(all_targets.begin(), all_targets.end());
    all_targets.erase(std::unique(all_targets.begin(), all_targets.end()), all_targets.end());

    for (std::size_t i = 0; i < all_targets.size(); ++i) {
        std::uint32_t addr = all_targets[i];
        std::string label =
            generate_label(addr, cfg, static_cast<std::uint32_t>(header.entry_point));
        oss << "    \"" << addr << "\": \"" << label << "\"";
        if (i + 1 < all_targets.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "  },\n";

    // Instructions
    oss << "  \"instructions\": [\n";
    for (std::size_t i = 0; i < instrs.size(); ++i) {
        const auto& instr = instrs[i];
        oss << "    {\n";
        oss << "      \"address\": " << instr.address << ",\n";
        oss << "      \"raw\": \"0x" << std::hex << std::setfill('0') << std::setw(8)
            << instr.raw_bytes << std::dec << "\",\n";
        oss << "      \"mnemonic\": \"" << (instr.mnemonic.empty() ? "???" : instr.mnemonic)
            << "\",\n";

        // Format operands based on type
        oss << "      \"operands\": \"";
        DisasmOptions opts;  // Default options for JSON
        std::string full = format_instruction(instr, nullptr, opts);
        // Extract operands part (after mnemonic)
        auto mnem_pos = full.find(instr.mnemonic.empty() ? "???" : std::string{instr.mnemonic});
        if (mnem_pos != std::string::npos) {
            auto operands_start = mnem_pos + instr.mnemonic.size();
            while (operands_start < full.size() && full[operands_start] == ' ') {
                operands_start++;
            }
            oss << full.substr(operands_start);
        }
        oss << "\"\n";

        oss << "    }";
        if (i + 1 < instrs.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "  ]\n";

    oss << "}\n";

    return oss.str();
}

}  // namespace dotvm::core
