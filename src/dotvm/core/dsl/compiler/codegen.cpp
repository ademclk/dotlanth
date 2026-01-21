#include "dotvm/core/dsl/compiler/codegen.hpp"

#include <bit>

namespace dotvm::core::dsl::compiler {

using namespace ir;

// ============================================================================
// Code Generator Implementation
// ============================================================================

CodegenResult<GeneratedCode> CodeGenerator::generate(const LinearIR& ir) {
    GeneratedCode result;
    result.arch = arch_;

    pending_labels_.clear();
    label_offsets_.clear();

    // First pass: generate code and record labels
    for (const auto& block : ir.blocks) {
        // Record block label
        record_label(block.label, result.code.size());

        // Generate instructions
        for (const auto& instr : block.instructions) {
            auto gen_result = gen_instruction(instr, result.code, result.constants);
            if (!gen_result) {
                return std::unexpected(gen_result.error());
            }
        }
    }

    // Second pass: resolve label references
    auto resolve_result = resolve_labels(result.code);
    if (!resolve_result) {
        return std::unexpected(resolve_result.error());
    }

    // Find entry point
    auto entry_label = "bb" + std::to_string(ir.entry_block_id);
    auto it = label_offsets_.find(entry_label);
    if (it != label_offsets_.end()) {
        result.entry_point = it->second;
    }

    return result;
}

std::vector<std::uint8_t> CodeGenerator::assemble(const GeneratedCode& code) {
    std::vector<std::uint8_t> output;

    // Build constant pool
    std::vector<std::uint8_t> const_pool_data;
    if (!code.constants.empty()) {
        // Header: entry count
        auto count = static_cast<std::uint32_t>(code.constants.size());
        const_pool_data.resize(4);
        endian::write_u32_le(const_pool_data.data(), count);

        // Entries
        for (const auto& c : code.constants) {
            if (c.is_integer()) {
                const_pool_data.push_back(bytecode::CONST_TYPE_I64);
                std::array<std::uint8_t, 8> buf{};
                endian::write_i64_le(buf.data(), c.as_integer());
                const_pool_data.insert(const_pool_data.end(), buf.begin(), buf.end());
            } else if (c.is_float()) {
                const_pool_data.push_back(bytecode::CONST_TYPE_F64);
                std::array<std::uint8_t, 8> buf{};
                endian::write_f64_le(buf.data(), c.as_float());
                const_pool_data.insert(const_pool_data.end(), buf.begin(), buf.end());
            }
        }
    }

    // Calculate offsets
    std::uint64_t const_pool_offset = bytecode::HEADER_SIZE;
    std::uint64_t const_pool_size = const_pool_data.size();
    std::uint64_t code_offset = const_pool_offset + const_pool_size;
    // Align code section to 4 bytes
    while (code_offset % bytecode::INSTRUCTION_ALIGNMENT != 0) {
        ++code_offset;
        const_pool_data.push_back(0);  // Padding
        ++const_pool_size;
    }
    std::uint64_t code_size = code.code.size();

    // Create header
    auto header = make_header(code.arch, bytecode::FLAG_NONE, code.entry_point, const_pool_offset,
                              const_pool_size, code_offset, code_size);

    auto header_bytes = write_header(header);
    output.insert(output.end(), header_bytes.begin(), header_bytes.end());

    // Append constant pool
    output.insert(output.end(), const_pool_data.begin(), const_pool_data.end());

    // Append code
    output.insert(output.end(), code.code.begin(), code.code.end());

    return output;
}

// ============================================================================
// Instruction Encoding
// ============================================================================

void CodeGenerator::emit_type_a(std::vector<std::uint8_t>& code, std::uint8_t op, std::uint8_t rd,
                                std::uint8_t rs1, std::uint8_t rs2) {
    code.push_back(op);
    code.push_back(rd);
    code.push_back(rs1);
    code.push_back(rs2);
}

void CodeGenerator::emit_type_b(std::vector<std::uint8_t>& code, std::uint8_t op, std::uint8_t rd,
                                std::int16_t imm) {
    code.push_back(op);
    code.push_back(rd);
    code.push_back(static_cast<std::uint8_t>(imm & 0xFF));
    code.push_back(static_cast<std::uint8_t>((imm >> 8) & 0xFF));
}

void CodeGenerator::emit_type_c(std::vector<std::uint8_t>& code, std::uint8_t op,
                                std::int32_t offset24) {
    code.push_back(op);
    code.push_back(static_cast<std::uint8_t>(offset24 & 0xFF));
    code.push_back(static_cast<std::uint8_t>((offset24 >> 8) & 0xFF));
    code.push_back(static_cast<std::uint8_t>((offset24 >> 16) & 0xFF));
}

void CodeGenerator::emit_type_m(std::vector<std::uint8_t>& code, std::uint8_t op,
                                std::uint8_t rd_rs2, std::uint8_t rs1, std::int8_t offset) {
    code.push_back(op);
    code.push_back(rd_rs2);
    code.push_back(rs1);
    code.push_back(static_cast<std::uint8_t>(offset));
}

// ============================================================================
// Constant Pool
// ============================================================================

std::uint32_t CodeGenerator::add_constant(std::vector<dotvm::core::Value>& pool,
                                          dotvm::core::Value val) {
    // Check if constant already exists
    for (std::uint32_t i = 0; i < pool.size(); ++i) {
        if (pool[i] == val) {
            return i;
        }
    }
    auto idx = static_cast<std::uint32_t>(pool.size());
    pool.push_back(val);
    return idx;
}

// ============================================================================
// Label Resolution
// ============================================================================

void CodeGenerator::record_label(const std::string& label, std::size_t offset) {
    label_offsets_[label] = offset;
}

void CodeGenerator::add_label_ref(std::size_t offset, const std::string& label, bool relative) {
    pending_labels_.push_back(LabelRef{offset, label, relative});
}

CodegenResult<void> CodeGenerator::resolve_labels(std::vector<std::uint8_t>& code) {
    for (const auto& ref : pending_labels_) {
        auto it = label_offsets_.find(ref.label);
        if (it == label_offsets_.end()) {
            return std::unexpected(CodegenError::internal("Unresolved label: " + ref.label));
        }

        std::int32_t target = static_cast<std::int32_t>(it->second);
        std::int32_t value;

        if (ref.is_relative) {
            // Relative offset from instruction
            value = target - static_cast<std::int32_t>(ref.code_offset);
        } else {
            value = target;
        }

        // Check range for 24-bit offset
        if (value < -(1 << 23) || value >= (1 << 23)) {
            return std::unexpected(
                CodegenError::jump_range("Jump offset out of range: " + ref.label));
        }

        // Patch the instruction (bytes 1-3 for Type C)
        code[ref.code_offset + 1] = static_cast<std::uint8_t>(value & 0xFF);
        code[ref.code_offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        code[ref.code_offset + 3] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    }

    return {};
}

// ============================================================================
// Instruction Generation
// ============================================================================

CodegenResult<void> CodeGenerator::gen_instruction(const LinearInstr& instr,
                                                   std::vector<std::uint8_t>& code,
                                                   std::vector<dotvm::core::Value>& constants) {
    return std::visit(
        [&](const auto& inst) -> CodegenResult<void> {
            using T = std::decay_t<decltype(inst)>;

            if constexpr (std::is_same_v<T, ir::BinaryOp>) {
                std::uint8_t op = opcode::NOP;
                switch (inst.op) {
                    case BinaryOpKind::Add:
                        op = opcode::ADD;
                        break;
                    case BinaryOpKind::Sub:
                        op = opcode::SUB;
                        break;
                    case BinaryOpKind::Mul:
                        op = opcode::MUL;
                        break;
                    case BinaryOpKind::Div:
                        op = opcode::DIV;
                        break;
                    case BinaryOpKind::Mod:
                        op = opcode::MOD;
                        break;
                    case BinaryOpKind::Band:
                        op = opcode::AND;
                        break;
                    case BinaryOpKind::Bor:
                        op = opcode::OR;
                        break;
                    case BinaryOpKind::Bxor:
                        op = opcode::XOR;
                        break;
                    case BinaryOpKind::Shl:
                        op = opcode::SHL;
                        break;
                    case BinaryOpKind::Shr:
                        op = opcode::SHR;
                        break;
                    case BinaryOpKind::Sar:
                        op = opcode::SAR;
                        break;
                    case BinaryOpKind::And:
                    case BinaryOpKind::Or:
                        // Logical ops: use bitwise AND/OR on booleans
                        op = (inst.op == BinaryOpKind::And) ? opcode::AND : opcode::OR;
                        break;
                    default:
                        return std::unexpected(
                            CodegenError::unsupported("Unsupported binary operation"));
                }
                emit_type_a(code, op, instr.dest_reg, instr.src1_reg, instr.src2_reg);
                return {};
            } else if constexpr (std::is_same_v<T, ir::UnaryOp>) {
                std::uint8_t op = opcode::NOP;
                switch (inst.op) {
                    case UnaryOpKind::Neg:
                        op = opcode::NEG;
                        break;
                    case UnaryOpKind::Not:
                    case UnaryOpKind::Bnot:
                        op = opcode::NOT;
                        break;
                    default:
                        return std::unexpected(
                            CodegenError::unsupported("Unsupported unary operation"));
                }
                emit_type_a(code, op, instr.dest_reg, instr.src1_reg, 0);
                return {};
            } else if constexpr (std::is_same_v<T, Compare>) {
                std::uint8_t op = opcode::NOP;
                switch (inst.op) {
                    case CompareKind::Eq:
                        op = opcode::EQ;
                        break;
                    case CompareKind::Ne:
                        op = opcode::NE;
                        break;
                    case CompareKind::Lt:
                        op = opcode::LT;
                        break;
                    case CompareKind::Le:
                        op = opcode::LE;
                        break;
                    case CompareKind::Gt:
                        op = opcode::GT;
                        break;
                    case CompareKind::Ge:
                        op = opcode::GE;
                        break;
                    case CompareKind::Ltu:
                        op = opcode::LTU;
                        break;
                    case CompareKind::Leu:
                        op = opcode::LEU;
                        break;
                    case CompareKind::Gtu:
                        op = opcode::GTU;
                        break;
                    case CompareKind::Geu:
                        op = opcode::GEU;
                        break;
                    default:
                        return std::unexpected(
                            CodegenError::unsupported("Unsupported compare operation"));
                }
                emit_type_a(code, op, instr.dest_reg, instr.src1_reg, instr.src2_reg);
                return {};
            } else if constexpr (std::is_same_v<T, LoadConst>) {
                // Load constant: use ADDI Rd, R0, imm for small values
                // or load from constant pool for larger values
                const auto& c = inst.constant;
                if (c.is_integer()) {
                    auto val = c.as_integer();
                    if (val >= -32768 && val <= 32767) {
                        // Small immediate: ADDI Rd, R0, imm
                        emit_type_b(code, opcode::ADDI, instr.dest_reg,
                                    static_cast<std::int16_t>(val));
                        return {};
                    }
                } else if (c.is_bool()) {
                    // Bool: 0 or 1
                    emit_type_b(code, opcode::ADDI, instr.dest_reg, c.as_bool() ? 1 : 0);
                    return {};
                }

                // For larger constants, we'd need a LOAD_CONST instruction
                // For now, use multiple ADDI operations or constant pool
                // This is a simplification
                if (c.is_integer()) {
                    auto val = c.as_integer();
                    // Load lower 16 bits, then shift and add upper bits
                    emit_type_b(code, opcode::ADDI, instr.dest_reg,
                                static_cast<std::int16_t>(val & 0xFFFF));
                    // Note: Full implementation would need more instructions
                }
                return {};
            } else if constexpr (std::is_same_v<T, StateGet>) {
                // State get: LOAD64 from state memory region
                // Slot index in immediate
                emit_type_m(code, opcode::LOAD64, instr.dest_reg, 0,
                            static_cast<std::int8_t>(instr.immediate));
                return {};
            } else if constexpr (std::is_same_v<T, StatePut>) {
                // State put: STORE64 to state memory region
                emit_type_m(code, opcode::STORE64, instr.src1_reg, 0,
                            static_cast<std::int8_t>(instr.immediate));
                return {};
            } else if constexpr (std::is_same_v<T, Call>) {
                // DSL-004: Check if this is a stdlib syscall
                if (inst.syscall_id != 0) {
                    // Emit SYSCALL instruction (Type B format)
                    // Arguments are passed in registers R1-R6 (calling convention)
                    // The lowerer should have arranged arguments in src registers
                    // SYSCALL: [0xFE][Rd][syscall_id_lo][syscall_id_hi]
                    emit_type_b(code, opcode::SYSCALL, instr.dest_reg,
                                static_cast<std::int16_t>(inst.syscall_id));
                    return {};
                }
                // Regular function call - emit NOP as placeholder
                // Real implementation would resolve function addresses and emit proper CALL
                emit_type_c(code, opcode::NOP, 0);
                return {};
            } else if constexpr (std::is_same_v<T, Cast>) {
                // Type conversion
                if (inst.target_type == ValueType::Float64) {
                    emit_type_a(code, opcode::I2F, instr.dest_reg, instr.src1_reg, 0);
                } else if (inst.target_type == ValueType::Int64) {
                    emit_type_a(code, opcode::F2I, instr.dest_reg, instr.src1_reg, 0);
                } else {
                    // Copy for other casts
                    emit_type_a(code, opcode::ADD, instr.dest_reg, instr.src1_reg, 0);
                }
                return {};
            } else if constexpr (std::is_same_v<T, Copy>) {
                // Copy: ADD Rd, Rs, R0
                emit_type_a(code, opcode::ADD, instr.dest_reg, instr.src1_reg, 0);
                return {};
            } else if constexpr (std::is_same_v<T, Jump>) {
                add_label_ref(code.size(), instr.label, true);
                emit_type_c(code, opcode::JMP, 0);
                return {};
            } else if constexpr (std::is_same_v<T, Branch>) {
                // Branch: JNZ cond, true_block; JMP false_block
                auto true_label = "bb" + std::to_string(inst.true_block_id);
                auto false_label = "bb" + std::to_string(inst.false_block_id);

                // JNZ condition, true_block
                add_label_ref(code.size(), true_label, true);
                emit_type_b(code, opcode::JNZ, instr.src1_reg, 0);

                // JMP false_block
                add_label_ref(code.size(), false_label, true);
                emit_type_c(code, opcode::JMP, 0);
                return {};
            } else if constexpr (std::is_same_v<T, Return>) {
                emit_type_c(code, opcode::RET, 0);
                return {};
            } else if constexpr (std::is_same_v<T, Halt>) {
                emit_type_c(code, opcode::HALT, 0);
                return {};
            } else if constexpr (std::is_same_v<T, Unreachable>) {
                // Emit HALT as unreachable marker
                emit_type_c(code, opcode::HALT, 0);
                return {};
            } else {
                return std::unexpected(CodegenError::unsupported("Unsupported instruction type"));
            }
        },
        instr.kind);
}

}  // namespace dotvm::core::dsl::compiler
