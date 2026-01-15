#pragma once

/// @file opcode_handlers.hpp
/// @brief X-macro based opcode handler definitions for DRY instruction dispatch
///
/// This header uses X-macros to define opcode handlers once and generate:
/// 1. Computed-goto handler labels
/// 2. Dispatch table entries
/// 3. Handler implementation code
///
/// This eliminates the duplication between switch-based and computed-goto dispatch.

#include <dotvm/exec/dispatch_macros.hpp>
#include <dotvm/core/instruction.hpp>
#include <dotvm/core/value.hpp>

namespace dotvm::exec {

// ============================================================================
// X-Macro Definitions for Opcode Handlers
// ============================================================================

/// Type A Binary Operations (rd = op(rs1, rs2))
/// Format: X(OPCODE_NAME, alu_method)
#define DOTVM_TYPE_A_BINARY_OPS(X) \
    X(ADD, add)     \
    X(SUB, sub)     \
    X(MUL, mul)     \
    X(DIV, div)     \
    X(MOD, mod)     \
    X(AND, bit_and) \
    X(OR,  bit_or)  \
    X(XOR, bit_xor) \
    X(SHL, shl)     \
    X(SHR, shr)     \
    X(SAR, sar)

/// Type A Unary Operations (rd = op(rs1))
/// Format: X(OPCODE_NAME, alu_method)
#define DOTVM_TYPE_A_UNARY_OPS(X) \
    X(NEG, neg)     \
    X(ABS, abs)     \
    X(NOT, bit_not)

/// Type A Comparison Operations (rd = cmp_op(rs1, rs2)) - EXEC-009
/// Format: X(OPCODE_NAME, alu_method)
#define DOTVM_TYPE_A_COMPARISON_OPS(X) \
    X(EQ,  cmp_eq)  \
    X(NE,  cmp_ne)  \
    X(LT,  cmp_lt)  \
    X(LE,  cmp_le)  \
    X(GT,  cmp_gt)  \
    X(GE,  cmp_ge)  \
    X(LTU, cmp_ltu) \
    X(LEU, cmp_leu) \
    X(GTU, cmp_gtu) \
    X(GEU, cmp_geu) \
    X(TEST, test)

/// Type B Immediate Comparison (rd = cmp_op(rd, sign_extend(imm16))) - EXEC-009
/// Format: X(OPCODE_NAME, alu_method)
#define DOTVM_TYPE_B_CMP_IMM_OPS(X) \
    X(CMPI_EQ, cmp_eq) \
    X(CMPI_NE, cmp_ne) \
    X(CMPI_LT, cmp_lt) \
    X(CMPI_GE, cmp_ge)

/// Type B Immediate Arithmetic (rd = op(rd, sign_extend(imm16)))
/// Format: X(OPCODE_NAME, alu_method)
#define DOTVM_TYPE_B_ARITH_IMM_OPS(X) \
    X(ADDI, add)    \
    X(SUBI, sub)    \
    X(MULI, mul)

/// Type B Immediate Bitwise (rd = op(rd, zero_extend(imm16)))
/// Format: X(OPCODE_NAME, alu_method)
#define DOTVM_TYPE_B_BITWISE_IMM_OPS(X) \
    X(ANDI, bit_and) \
    X(ORI,  bit_or)  \
    X(XORI, bit_xor)

// ============================================================================
// Handler Code Generation Macros
// ============================================================================

/// Generate a Type A binary operation handler for computed-goto dispatch
#define DOTVM_DEFINE_TYPE_A_BINARY_HANDLER(NAME, ALU_OP) \
    op_##NAME: {                                          \
        auto d = core::decode_type_a(instr);              \
        regs.write(d.rd, alu.ALU_OP(regs.read(d.rs1), regs.read(d.rs2))); \
        DOTVM_NEXT();                                     \
    }

/// Generate a Type A unary operation handler
#define DOTVM_DEFINE_TYPE_A_UNARY_HANDLER(NAME, ALU_OP) \
    op_##NAME: {                                        \
        auto d = core::decode_type_a(instr);            \
        regs.write(d.rd, alu.ALU_OP(regs.read(d.rs1))); \
        DOTVM_NEXT();                                   \
    }

/// Generate a Type B immediate arithmetic handler (sign-extend imm16)
#define DOTVM_DEFINE_TYPE_B_ARITH_IMM_HANDLER(NAME, ALU_OP) \
    op_##NAME: {                                           \
        auto d = core::decode_type_b(instr);               \
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16)); \
        regs.write(d.rd, alu.ALU_OP(regs.read(d.rd), imm)); \
        DOTVM_NEXT();                                      \
    }

/// Generate a Type B immediate bitwise handler (zero-extend imm16)
#define DOTVM_DEFINE_TYPE_B_BITWISE_IMM_HANDLER(NAME, ALU_OP) \
    op_##NAME: {                                             \
        auto d = core::decode_type_b(instr);                 \
        auto imm = core::Value::from_int(d.imm16);           \
        regs.write(d.rd, alu.ALU_OP(regs.read(d.rd), imm));  \
        DOTVM_NEXT();                                        \
    }

// ============================================================================
// Dispatch Table Entry Generation Macros
// ============================================================================

/// Generate dispatch table entry for an opcode
#define DOTVM_DISPATCH_TABLE_ENTRY(NAME, ALU_OP) \
    dispatch_table[opcode::NAME] = &&op_##NAME;

// ============================================================================
// Label Declaration Macros
// ============================================================================

/// Declare a handler label for __label__ extension
#define DOTVM_DECLARE_LABEL(NAME, ALU_OP) op_##NAME,

// ============================================================================
// Switch Case Generation Macros (for fallback dispatch)
// ============================================================================

/// Generate a switch case for Type A binary operation
#define DOTVM_SWITCH_TYPE_A_BINARY(NAME, ALU_OP) \
    case opcode::NAME: {                         \
        auto d = core::decode_type_a(instr);     \
        regs.write(d.rd, alu.ALU_OP(regs.read(d.rs1), regs.read(d.rs2))); \
        return true;                             \
    }

/// Generate a switch case for Type A unary operation
#define DOTVM_SWITCH_TYPE_A_UNARY(NAME, ALU_OP) \
    case opcode::NAME: {                        \
        auto d = core::decode_type_a(instr);    \
        regs.write(d.rd, alu.ALU_OP(regs.read(d.rs1))); \
        return true;                            \
    }

/// Generate a switch case for Type B immediate arithmetic
#define DOTVM_SWITCH_TYPE_B_ARITH_IMM(NAME, ALU_OP) \
    case opcode::NAME: {                           \
        auto d = core::decode_type_b(instr);       \
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16)); \
        regs.write(d.rd, alu.ALU_OP(regs.read(d.rd), imm)); \
        return true;                               \
    }

/// Generate a switch case for Type B immediate bitwise
#define DOTVM_SWITCH_TYPE_B_BITWISE_IMM(NAME, ALU_OP) \
    case opcode::NAME: {                             \
        auto d = core::decode_type_b(instr);         \
        auto imm = core::Value::from_int(d.imm16);   \
        regs.write(d.rd, alu.ALU_OP(regs.read(d.rd), imm)); \
        return true;                                 \
    }

/// Generate a switch case for Type B immediate comparison (sign-extend imm16) - EXEC-009
#define DOTVM_SWITCH_TYPE_B_CMP_IMM(NAME, ALU_OP) \
    case opcode::NAME: {                           \
        auto d = core::decode_type_b(instr);       \
        auto imm = core::Value::from_int(static_cast<std::int16_t>(d.imm16)); \
        regs.write(d.rd, alu.ALU_OP(regs.read(d.rd), imm)); \
        return true;                               \
    }

}  // namespace dotvm::exec
