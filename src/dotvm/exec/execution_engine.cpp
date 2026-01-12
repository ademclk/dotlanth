/// @file execution_engine.cpp
/// @brief Implementation of computed-goto dispatch loop
///
/// This file contains the core dispatch loop implementation using GCC/Clang's
/// computed-goto extension for high-performance instruction dispatch.
///
/// Uses X-macros from opcode_handlers.hpp to eliminate duplication between
/// switch-based and computed-goto dispatch implementations.

#include <dotvm/exec/execution_engine.hpp>
#include <dotvm/exec/opcode_handlers.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/value.hpp>

namespace dotvm::exec {

// ============================================================================
// ExecutionEngine Implementation
// ============================================================================

ExecutionEngine::ExecutionEngine(core::VmContext& ctx) noexcept
    : vm_ctx_{ctx}
    , exec_ctx_{}
    , const_pool_{} {}

void ExecutionEngine::reset() noexcept {
    exec_ctx_ = ExecutionContext{};
    const_pool_ = {};
}

ExecResult ExecutionEngine::execute(
    const std::uint32_t* code,
    std::size_t code_size,
    std::size_t entry_point,
    std::span<const core::Value> const_pool) noexcept {

    // Initialize execution context
    exec_ctx_.reset(code, code_size, entry_point);
    const_pool_ = const_pool;

    // Run the dispatch loop
    return dispatch_loop();
}

ExecResult ExecutionEngine::step() noexcept {
    if (exec_ctx_.halted) {
        return exec_ctx_.error;
    }

    if (exec_ctx_.pc >= exec_ctx_.code_size) {
        exec_ctx_.halt_with_error(ExecResult::OutOfBounds);
        return ExecResult::OutOfBounds;
    }

    std::uint32_t instr = exec_ctx_.code[exec_ctx_.pc++];
    ++exec_ctx_.instructions_executed;

    if (!execute_instruction(instr)) {
        return exec_ctx_.error;
    }

    return ExecResult::Success;
}

bool ExecutionEngine::execute_instruction(std::uint32_t instr) noexcept {
    // Get references to frequently accessed components
    auto& regs = vm_ctx_.registers();
    auto& alu = vm_ctx_.alu();
    auto& mem = vm_ctx_.memory();

    std::uint8_t opcode_val = static_cast<std::uint8_t>(instr >> 24);

    switch (opcode_val) {
        // =====================================================================
        // ARITHMETIC & BITWISE - Generated via X-macros
        // =====================================================================

        // Type A Binary Operations (rd = op(rs1, rs2))
        DOTVM_TYPE_A_BINARY_OPS(DOTVM_SWITCH_TYPE_A_BINARY)

        // Type A Unary Operations (rd = op(rs1))
        DOTVM_TYPE_A_UNARY_OPS(DOTVM_SWITCH_TYPE_A_UNARY)

        // Type A Comparison Operations (rd = cmp_op(rs1, rs2))
        DOTVM_TYPE_A_COMPARISON_OPS(DOTVM_SWITCH_TYPE_A_BINARY)

        // Type B Immediate Arithmetic (rd = op(rd, sign_extend(imm16)))
        DOTVM_TYPE_B_ARITH_IMM_OPS(DOTVM_SWITCH_TYPE_B_ARITH_IMM)

        // Type B Immediate Bitwise (rd = op(rd, zero_extend(imm16)))
        DOTVM_TYPE_B_BITWISE_IMM_OPS(DOTVM_SWITCH_TYPE_B_BITWISE_IMM)

        // =====================================================================
        // CONTROL FLOW (0x40-0x5F)
        // =====================================================================
        case opcode::JMP: {
            auto d = core::decode_type_c(instr);
            exec_ctx_.jump_relative(d.offset24 - 1);  // -1 because pc already advanced
            return true;
        }
        case opcode::JMPR: {
            auto d = core::decode_type_a(instr);
            auto target = static_cast<std::size_t>(regs.read(d.rs1).as_integer());
            exec_ctx_.jump_to(target);
            return true;
        }
        case opcode::BEQ: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() == regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::BNE: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() != regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::BLT: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() < regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::BGE: {
            auto d = core::decode_type_a(instr);
            if (regs.read(d.rd).as_integer() >= regs.read(d.rs1).as_integer()) {
                auto offset = static_cast<std::int8_t>(d.rs2);
                exec_ctx_.jump_relative(offset - 1);
            }
            return true;
        }
        case opcode::CALL: {
            auto d = core::decode_type_c(instr);
            // Save return address in link register (R1)
            regs.write(1, core::Value::from_int(static_cast<std::int64_t>(exec_ctx_.pc)));
            exec_ctx_.jump_relative(d.offset24 - 1);
            return true;
        }
        case opcode::RET: {
            // Jump to address in link register (R1)
            auto ret_addr = static_cast<std::size_t>(regs.read(1).as_integer());
            exec_ctx_.jump_to(ret_addr);
            return true;
        }

        // =====================================================================
        // MEMORY (0x60-0x7F)
        // =====================================================================
        case opcode::LOAD: {
            auto d = core::decode_type_a(instr);
            auto handle = regs.read(d.rs1).as_handle();
            auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
            auto result = mem.read<std::uint64_t>(handle, offset * sizeof(std::uint64_t));
            if (result) {
                regs.write(d.rd, core::Value::from_raw(*result));
            } else {
                regs.write(d.rd, core::Value::nil());
            }
            return true;
        }
        case opcode::STORE: {
            auto d = core::decode_type_a(instr);
            auto handle = regs.read(d.rd).as_handle();
            auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
            auto value = regs.read(d.rs1).raw_bits();
            (void)mem.write<std::uint64_t>(handle, offset * sizeof(std::uint64_t), value);
            return true;
        }
        case opcode::ALLOC: {
            auto d = core::decode_type_a(instr);
            auto size = static_cast<std::size_t>(regs.read(d.rs1).as_integer());
            auto result = mem.allocate(size);
            if (result) {
                regs.write(d.rd, core::Value::from_handle(*result));
            } else {
                regs.write(d.rd, core::Value::nil());
            }
            return true;
        }
        case opcode::FREE: {
            auto d = core::decode_type_a(instr);
            auto handle = regs.read(d.rs1).as_handle();
            (void)mem.deallocate(handle);
            return true;
        }

        // =====================================================================
        // DATA MOVE (0x80-0x8F)
        // =====================================================================
        case opcode::MOV: {
            auto d = core::decode_type_a(instr);
            regs.write(d.rd, regs.read(d.rs1));
            return true;
        }
        case opcode::MOVI: {
            auto d = core::decode_type_b(instr);
            regs.write(d.rd, core::Value::from_int(static_cast<std::int16_t>(d.imm16)));
            return true;
        }
        case opcode::LOADK: {
            auto d = core::decode_type_b(instr);
            auto idx = static_cast<std::size_t>(d.imm16);
            if (idx < const_pool_.size()) [[likely]] {
                regs.write(d.rd, const_pool_[idx]);
            } else {
                regs.write(d.rd, core::Value::nil());
            }
            return true;
        }
        case opcode::MOVHI: {
            auto d = core::decode_type_b(instr);
            auto current = regs.read(d.rd).as_integer();
            auto upper = static_cast<std::int64_t>(d.imm16) << 16;
            regs.write(d.rd, core::Value::from_int((current & 0xFFFF) | upper));
            return true;
        }

        // =====================================================================
        // SYSTEM (0xF0-0xFF)
        // =====================================================================
        case opcode::NOP: {
            return true;
        }
        case opcode::HALT: {
            exec_ctx_.halt();
            return false;
        }

        default: {
            // Check for reserved opcodes
            if (core::is_reserved_opcode(opcode_val)) {
                exec_ctx_.halt_with_error(ExecResult::CfiViolation);
            } else {
                exec_ctx_.halt_with_error(ExecResult::InvalidOpcode);
            }
            return false;
        }
    }
}

// ============================================================================
// Computed-Goto Dispatch Loop
// ============================================================================

#if DOTVM_HAS_COMPUTED_GOTO

ExecResult ExecutionEngine::dispatch_loop() noexcept {
    // Declare all handler labels using GCC __label__ extension
    // NOTE: __label__ MUST be at the very beginning of the block (GCC requirement)
    __label__
        // Arithmetic (0x00-0x1F)
        op_ADD, op_SUB, op_MUL, op_DIV, op_MOD, op_NEG, op_ABS,
        op_ADDI, op_SUBI, op_MULI,
        // Bitwise (0x20-0x2F)
        op_AND, op_OR, op_XOR, op_NOT, op_SHL, op_SHR, op_SAR,
        op_ANDI, op_ORI, op_XORI,
        // Comparison (0x30-0x3F)
        op_EQ, op_NE, op_LT, op_LE, op_GT, op_GE,
        op_LTU, op_LEU, op_GTU, op_GEU,
        // Control Flow (0x40-0x5F)
        op_JMP, op_JMPR, op_BEQ, op_BNE, op_BLT, op_BGE, op_BLTU, op_BGEU,
        op_CALL, op_RET, op_JMPI,
        // Memory (0x60-0x7F)
        op_LOAD, op_STORE, op_LOADB, op_STOREB,
        op_LOADH, op_STOREH, op_LOADW, op_STOREW,
        op_ALLOC, op_FREE, op_MEMCPY, op_MEMSET,
        // Data Move (0x80-0x8F)
        op_MOV, op_MOVI, op_LOADK, op_MOVHI, op_MOVLO, op_XCHG,
        // System (0xF0-0xFF)
        op_NOP, op_BREAK, op_SYSCALL, op_HALT,
        // Error handlers
        op_INVALID, op_RESERVED, op_OUT_OF_BOUNDS;

    // Local references for hot path (avoid repeated member access)
    auto& regs = vm_ctx_.registers();
    auto& alu = vm_ctx_.alu();
    auto& mem = vm_ctx_.memory();

    // Current instruction being executed
    std::uint32_t instr = 0;

    // Build dispatch table with label addresses
    // Note: This is computed at runtime when the function is first called
    // and cached in static storage for subsequent calls
    static void* dispatch_table[OPCODE_COUNT];
    static bool table_initialized = false;

    if (!table_initialized) [[unlikely]] {
        // Initialize all entries to invalid handler
        for (auto& entry : dispatch_table) {
            entry = &&op_INVALID;
        }

        // Arithmetic handlers (0x00-0x1F)
        dispatch_table[opcode::ADD]  = &&op_ADD;
        dispatch_table[opcode::SUB]  = &&op_SUB;
        dispatch_table[opcode::MUL]  = &&op_MUL;
        dispatch_table[opcode::DIV]  = &&op_DIV;
        dispatch_table[opcode::MOD]  = &&op_MOD;
        dispatch_table[opcode::NEG]  = &&op_NEG;
        dispatch_table[opcode::ABS]  = &&op_ABS;
        dispatch_table[opcode::ADDI] = &&op_ADDI;
        dispatch_table[opcode::SUBI] = &&op_SUBI;
        dispatch_table[opcode::MULI] = &&op_MULI;

        // Bitwise handlers (0x20-0x2F)
        dispatch_table[opcode::AND]  = &&op_AND;
        dispatch_table[opcode::OR]   = &&op_OR;
        dispatch_table[opcode::XOR]  = &&op_XOR;
        dispatch_table[opcode::NOT]  = &&op_NOT;
        dispatch_table[opcode::SHL]  = &&op_SHL;
        dispatch_table[opcode::SHR]  = &&op_SHR;
        dispatch_table[opcode::SAR]  = &&op_SAR;
        dispatch_table[opcode::ANDI] = &&op_ANDI;
        dispatch_table[opcode::ORI]  = &&op_ORI;
        dispatch_table[opcode::XORI] = &&op_XORI;

        // Comparison handlers (0x30-0x3F)
        dispatch_table[opcode::EQ]  = &&op_EQ;
        dispatch_table[opcode::NE]  = &&op_NE;
        dispatch_table[opcode::LT]  = &&op_LT;
        dispatch_table[opcode::LE]  = &&op_LE;
        dispatch_table[opcode::GT]  = &&op_GT;
        dispatch_table[opcode::GE]  = &&op_GE;
        dispatch_table[opcode::LTU] = &&op_LTU;
        dispatch_table[opcode::LEU] = &&op_LEU;
        dispatch_table[opcode::GTU] = &&op_GTU;
        dispatch_table[opcode::GEU] = &&op_GEU;

        // Control flow handlers (0x40-0x5F)
        dispatch_table[opcode::JMP]  = &&op_JMP;
        dispatch_table[opcode::JMPR] = &&op_JMPR;
        dispatch_table[opcode::BEQ]  = &&op_BEQ;
        dispatch_table[opcode::BNE]  = &&op_BNE;
        dispatch_table[opcode::BLT]  = &&op_BLT;
        dispatch_table[opcode::BGE]  = &&op_BGE;
        dispatch_table[opcode::BLTU] = &&op_BLTU;
        dispatch_table[opcode::BGEU] = &&op_BGEU;
        dispatch_table[opcode::CALL] = &&op_CALL;
        dispatch_table[opcode::RET]  = &&op_RET;
        dispatch_table[opcode::JMPI] = &&op_JMPI;

        // Memory handlers (0x60-0x7F)
        dispatch_table[opcode::LOAD]   = &&op_LOAD;
        dispatch_table[opcode::STORE]  = &&op_STORE;
        dispatch_table[opcode::LOADB]  = &&op_LOADB;
        dispatch_table[opcode::STOREB] = &&op_STOREB;
        dispatch_table[opcode::LOADH]  = &&op_LOADH;
        dispatch_table[opcode::STOREH] = &&op_STOREH;
        dispatch_table[opcode::LOADW]  = &&op_LOADW;
        dispatch_table[opcode::STOREW] = &&op_STOREW;
        dispatch_table[opcode::ALLOC]  = &&op_ALLOC;
        dispatch_table[opcode::FREE]   = &&op_FREE;
        dispatch_table[opcode::MEMCPY] = &&op_MEMCPY;
        dispatch_table[opcode::MEMSET] = &&op_MEMSET;

        // Data move handlers (0x80-0x8F)
        dispatch_table[opcode::MOV]   = &&op_MOV;
        dispatch_table[opcode::MOVI]  = &&op_MOVI;
        dispatch_table[opcode::LOADK] = &&op_LOADK;
        dispatch_table[opcode::MOVHI] = &&op_MOVHI;
        dispatch_table[opcode::MOVLO] = &&op_MOVLO;
        dispatch_table[opcode::XCHG]  = &&op_XCHG;

        // System handlers (0xF0-0xFF)
        dispatch_table[opcode::NOP]     = &&op_NOP;
        dispatch_table[opcode::BREAK]   = &&op_BREAK;
        dispatch_table[opcode::SYSCALL] = &&op_SYSCALL;
        dispatch_table[opcode::HALT]    = &&op_HALT;

        // Mark reserved ranges
        for (std::size_t i = 0x90; i <= 0x9F; ++i) {
            dispatch_table[i] = &&op_RESERVED;
        }
        for (std::size_t i = 0xD0; i <= 0xEF; ++i) {
            dispatch_table[i] = &&op_RESERVED;
        }

        table_initialized = true;
    }

    // Start execution - fetch first instruction
    DOTVM_NEXT();

    // =========================================================================
    // ARITHMETIC HANDLERS (0x00-0x1F)
    // =========================================================================

    op_ADD: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.add(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_SUB: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.sub(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_MUL: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.mul(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_DIV: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.div(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_MOD: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.mod(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_NEG: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.neg(regs.read(d.rs1)));
        DOTVM_NEXT();
    }

    op_ABS: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.abs(regs.read(d.rs1)));
        DOTVM_NEXT();
    }

    op_ADDI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.add(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_SUBI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.sub(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_MULI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16));
        regs.write(d.rd, alu.mul(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    // =========================================================================
    // BITWISE HANDLERS (0x20-0x2F)
    // =========================================================================

    op_AND: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.bit_and(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_OR: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.bit_or(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_XOR: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.bit_xor(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_NOT: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.bit_not(regs.read(d.rs1)));
        DOTVM_NEXT();
    }

    op_SHL: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.shl(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_SHR: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.shr(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_SAR: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.sar(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_ANDI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(d.imm16);
        regs.write(d.rd, alu.bit_and(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_ORI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(d.imm16);
        regs.write(d.rd, alu.bit_or(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    op_XORI: {
        auto d = core::decode_type_b(instr);
        auto imm = core::Value::from_int(d.imm16);
        regs.write(d.rd, alu.bit_xor(regs.read(d.rd), imm));
        DOTVM_NEXT();
    }

    // =========================================================================
    // COMPARISON HANDLERS (0x30-0x3F)
    // =========================================================================

    op_EQ: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_eq(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_NE: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_ne(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_LT: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_lt(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_LE: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_le(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_GT: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_gt(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_GE: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_ge(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_LTU: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_ltu(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_LEU: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_leu(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_GTU: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_gtu(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    op_GEU: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, alu.cmp_geu(regs.read(d.rs1), regs.read(d.rs2)));
        DOTVM_NEXT();
    }

    // =========================================================================
    // CONTROL FLOW HANDLERS (0x40-0x5F)
    // =========================================================================

    op_JMP: {
        auto d = core::decode_type_c(instr);
        exec_ctx_.jump_relative(d.offset24 - 1);
        DOTVM_NEXT();
    }

    op_JMPR: {
        auto d = core::decode_type_a(instr);
        auto target = static_cast<std::size_t>(regs.read(d.rs1).as_integer());
        exec_ctx_.jump_to(target);
        DOTVM_NEXT();
    }

    op_BEQ: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() == regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BNE: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() != regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BLT: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() < regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BGE: {
        auto d = core::decode_type_a(instr);
        if (regs.read(d.rd).as_integer() >= regs.read(d.rs1).as_integer()) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BLTU: {
        auto d = core::decode_type_a(instr);
        auto v1 = static_cast<std::uint64_t>(regs.read(d.rd).as_integer());
        auto v2 = static_cast<std::uint64_t>(regs.read(d.rs1).as_integer());
        if (v1 < v2) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_BGEU: {
        auto d = core::decode_type_a(instr);
        auto v1 = static_cast<std::uint64_t>(regs.read(d.rd).as_integer());
        auto v2 = static_cast<std::uint64_t>(regs.read(d.rs1).as_integer());
        if (v1 >= v2) {
            auto offset = static_cast<std::int8_t>(d.rs2);
            exec_ctx_.jump_relative(offset - 1);
        }
        DOTVM_NEXT();
    }

    op_CALL: {
        auto d = core::decode_type_c(instr);
        // Push return address to link register (R1)
        regs.write(1, core::Value::from_int(static_cast<std::int64_t>(exec_ctx_.pc)));
        exec_ctx_.jump_relative(d.offset24 - 1);
        DOTVM_NEXT();
    }

    op_RET: {
        // Return to address in link register (R1)
        auto ret_addr = static_cast<std::size_t>(regs.read(1).as_integer());
        exec_ctx_.jump_to(ret_addr);
        DOTVM_NEXT();
    }

    op_JMPI: {
        auto d = core::decode_type_c(instr);
        // Indirect jump (treated as regular jump for now)
        exec_ctx_.jump_relative(d.offset24 - 1);
        DOTVM_NEXT();
    }

    // =========================================================================
    // MEMORY HANDLERS (0x60-0x7F)
    // =========================================================================

    op_LOAD: {
        auto d = core::decode_type_a(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
        auto result = mem.read<std::uint64_t>(handle, offset * sizeof(std::uint64_t));
        if (result) {
            regs.write(d.rd, core::Value::from_raw(*result));
        } else {
            regs.write(d.rd, core::Value::nil());
        }
        DOTVM_NEXT();
    }

    op_STORE: {
        auto d = core::decode_type_a(instr);
        auto handle = regs.read(d.rd).as_handle();
        auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
        auto val = regs.read(d.rs1).raw_bits();
        (void)mem.write<std::uint64_t>(handle, offset * sizeof(std::uint64_t), val);
        DOTVM_NEXT();
    }

    op_LOADB: {
        auto d = core::decode_type_a(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
        auto result = mem.read<std::uint8_t>(handle, offset);
        if (result) {
            regs.write(d.rd, core::Value::from_int(*result));
        } else {
            regs.write(d.rd, core::Value::nil());
        }
        DOTVM_NEXT();
    }

    op_STOREB: {
        auto d = core::decode_type_a(instr);
        auto handle = regs.read(d.rd).as_handle();
        auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
        auto val = static_cast<std::uint8_t>(regs.read(d.rs1).as_integer());
        (void)mem.write<std::uint8_t>(handle, offset, val);
        DOTVM_NEXT();
    }

    op_LOADH: {
        auto d = core::decode_type_a(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
        auto result = mem.read<std::uint16_t>(handle, offset * sizeof(std::uint16_t));
        if (result) {
            regs.write(d.rd, core::Value::from_int(*result));
        } else {
            regs.write(d.rd, core::Value::nil());
        }
        DOTVM_NEXT();
    }

    op_STOREH: {
        auto d = core::decode_type_a(instr);
        auto handle = regs.read(d.rd).as_handle();
        auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
        auto val = static_cast<std::uint16_t>(regs.read(d.rs1).as_integer());
        (void)mem.write<std::uint16_t>(handle, offset * sizeof(std::uint16_t), val);
        DOTVM_NEXT();
    }

    op_LOADW: {
        auto d = core::decode_type_a(instr);
        auto handle = regs.read(d.rs1).as_handle();
        auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
        auto result = mem.read<std::uint32_t>(handle, offset * sizeof(std::uint32_t));
        if (result) {
            regs.write(d.rd, core::Value::from_int(*result));
        } else {
            regs.write(d.rd, core::Value::nil());
        }
        DOTVM_NEXT();
    }

    op_STOREW: {
        auto d = core::decode_type_a(instr);
        auto handle = regs.read(d.rd).as_handle();
        auto offset = static_cast<std::size_t>(regs.read(d.rs2).as_integer());
        auto val = static_cast<std::uint32_t>(regs.read(d.rs1).as_integer());
        (void)mem.write<std::uint32_t>(handle, offset * sizeof(std::uint32_t), val);
        DOTVM_NEXT();
    }

    op_ALLOC: {
        auto d = core::decode_type_a(instr);
        auto size = static_cast<std::size_t>(regs.read(d.rs1).as_integer());
        auto result = mem.allocate(size);
        if (result) {
            regs.write(d.rd, core::Value::from_handle(*result));
        } else {
            regs.write(d.rd, core::Value::nil());
        }
        DOTVM_NEXT();
    }

    op_FREE: {
        auto d = core::decode_type_a(instr);
        auto handle = regs.read(d.rs1).as_handle();
        (void)mem.deallocate(handle);
        DOTVM_NEXT();
    }

    op_MEMCPY: {
        // MEMCPY rd(dest), rs1(src), rs2(size)
        // For now, this is a stub
        DOTVM_NEXT();
    }

    op_MEMSET: {
        // MEMSET rd(dest), rs1(value), rs2(size)
        // For now, this is a stub
        DOTVM_NEXT();
    }

    // =========================================================================
    // DATA MOVE HANDLERS (0x80-0x8F)
    // =========================================================================

    op_MOV: {
        auto d = core::decode_type_a(instr);
        regs.write(d.rd, regs.read(d.rs1));
        DOTVM_NEXT();
    }

    op_MOVI: {
        auto d = core::decode_type_b(instr);
        regs.write(d.rd, core::Value::from_int(static_cast<std::int16_t>(d.imm16)));
        DOTVM_NEXT();
    }

    op_LOADK: {
        auto d = core::decode_type_b(instr);
        auto idx = static_cast<std::size_t>(d.imm16);
        if (idx < const_pool_.size()) [[likely]] {
            regs.write(d.rd, const_pool_[idx]);
        } else {
            regs.write(d.rd, core::Value::nil());
        }
        DOTVM_NEXT();
    }

    op_MOVHI: {
        auto d = core::decode_type_b(instr);
        auto current = regs.read(d.rd).as_integer();
        auto upper = static_cast<std::int64_t>(d.imm16) << 16;
        regs.write(d.rd, core::Value::from_int((current & 0xFFFF) | upper));
        DOTVM_NEXT();
    }

    op_MOVLO: {
        auto d = core::decode_type_b(instr);
        auto current = regs.read(d.rd).as_integer();
        regs.write(d.rd, core::Value::from_int((current & ~0xFFFFLL) | d.imm16));
        DOTVM_NEXT();
    }

    op_XCHG: {
        auto d = core::decode_type_a(instr);
        auto tmp = regs.read(d.rd);
        regs.write(d.rd, regs.read(d.rs1));
        regs.write(d.rs1, tmp);
        DOTVM_NEXT();
    }

    // =========================================================================
    // SYSTEM HANDLERS (0xF0-0xFF)
    // =========================================================================

    op_NOP: {
        DOTVM_NEXT();
    }

    op_BREAK: {
        // Breakpoint - return to allow debugging
        exec_ctx_.halt_with_error(ExecResult::Interrupted);
        return ExecResult::Interrupted;
    }

    op_SYSCALL: {
        // System call - not implemented yet
        DOTVM_NEXT();
    }

    op_HALT: {
        exec_ctx_.halt();
        return ExecResult::Success;
    }

    // =========================================================================
    // ERROR HANDLERS
    // =========================================================================

    op_INVALID: {
        exec_ctx_.halt_with_error(ExecResult::InvalidOpcode);
        return ExecResult::InvalidOpcode;
    }

    op_RESERVED: {
        exec_ctx_.halt_with_error(ExecResult::CfiViolation);
        return ExecResult::CfiViolation;
    }

    op_OUT_OF_BOUNDS: {
        exec_ctx_.halt_with_error(ExecResult::OutOfBounds);
        return ExecResult::OutOfBounds;
    }
}

#else // !DOTVM_HAS_COMPUTED_GOTO

// Fallback switch-based dispatch for non-GCC/Clang compilers
ExecResult ExecutionEngine::dispatch_loop() noexcept {
    while (exec_ctx_.should_continue()) {
        std::uint32_t instr = exec_ctx_.code[exec_ctx_.pc++];
        ++exec_ctx_.instructions_executed;

        if (!execute_instruction(instr)) {
            return exec_ctx_.error;
        }
    }

    if (exec_ctx_.pc >= exec_ctx_.code_size && !exec_ctx_.halted) {
        exec_ctx_.halt_with_error(ExecResult::OutOfBounds);
        return ExecResult::OutOfBounds;
    }

    return exec_ctx_.error;
}

#endif // DOTVM_HAS_COMPUTED_GOTO

}  // namespace dotvm::exec
