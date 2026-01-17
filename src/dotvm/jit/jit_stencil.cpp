/// @file jit_stencil.cpp
/// @brief Implementation of x86-64 code stencils for JIT compilation
///
/// Contains the actual machine code bytes for each stencil. These are
/// hand-crafted x86-64 instructions with placeholder bytes that get
/// patched during compilation.

#include "dotvm/jit/jit_stencil.hpp"

namespace dotvm::jit {

#if defined(__x86_64__) || defined(_M_X64)

namespace stencils::x86_64 {

// ============================================================================
// Machine Code Conventions
// ============================================================================
//
// Register allocation for JIT code:
//   rdi = pointer to register file (Value* regs)
//   rsi = pointer to VmContext (for fallback calls)
//   rbx = preserved across stencils (callee-saved)
//
// Register file access pattern:
//   Value at reg[i] is at offset i * 8 from regs base
//   Load:  mov rax, [rdi + reg*8]
//   Store: mov [rdi + reg*8], rax
//
// Placeholder bytes:
//   0xCC = int3 (used as placeholder for single byte)
//   0xCCCCCCCC = placeholder for 32-bit value
//   0xCCCCCCCCCCCCCCCC = placeholder for 64-bit value
//
// ============================================================================

// ============================================================================
// Prologue/Epilogue
// ============================================================================

/// @brief Function prologue code
/// push rbp             ; 55
/// mov rbp, rsp         ; 48 89 e5
/// sub rsp, 0x40        ; 48 83 ec 40  (reserve 64 bytes, patched)
static constexpr std::uint8_t prologue_code[] = {
    0x55,                    // push rbp
    0x48, 0x89, 0xe5,        // mov rbp, rsp
    0x48, 0x83, 0xec, 0x40,  // sub rsp, 0x40 (placeholder for frame size)
    0x48, 0x89, 0x7d, 0xf8,  // mov [rbp-8], rdi  (save regs ptr)
    0x48, 0x89, 0x75, 0xf0,  // mov [rbp-16], rsi (save ctx ptr)
};

const Stencil prologue = {
    .code = prologue_code,
    .code_size = sizeof(prologue_code),
    .holes = {{
        {.offset = 7, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 1,
    .opcode = 0,
    .name = "prologue",
};

/// @brief Function epilogue code
/// mov rdi, [rbp-8]     ; restore regs
/// add rsp, 0x40        ; 48 83 c4 40
/// pop rbp              ; 5d
/// ret                  ; c3
static constexpr std::uint8_t epilogue_code[] = {
    0x48, 0x8b, 0x7d, 0xf8,  // mov rdi, [rbp-8]  (restore regs ptr)
    0x48, 0x83, 0xc4, 0x40,  // add rsp, 0x40 (placeholder)
    0x5d,                    // pop rbp
    0xc3,                    // ret
};

const Stencil epilogue = {
    .code = epilogue_code,
    .code_size = sizeof(epilogue_code),
    .holes = {{
        {.offset = 7, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 1,
    .opcode = 0,
    .name = "epilogue",
};

// ============================================================================
// Arithmetic Stencils
// ============================================================================

/// @brief ADD r[dst], r[src1], r[src2]
///
/// mov rax, [rdi + src1*8]   ; 48 8b 87 XX XX XX XX
/// add rax, [rdi + src2*8]   ; 48 03 87 XX XX XX XX
/// mov [rdi + dst*8], rax    ; 48 89 87 XX XX XX XX
static constexpr std::uint8_t add_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x03, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // add rax, [rdi + src2_offset]
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil add = {
    .code = add_code,
    .code_size = sizeof(add_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},   // src1
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},  // src2
        {.offset = 17, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},  // dst
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::ADD),
    .name = "add",
};

/// @brief SUB r[dst], r[src1], r[src2]
static constexpr std::uint8_t sub_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x2b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // sub rax, [rdi + src2_offset]
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil sub = {
    .code = sub_code,
    .code_size = sizeof(sub_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 17, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::SUB),
    .name = "sub",
};

/// @brief MUL r[dst], r[src1], r[src2]
/// Uses imul for signed multiplication (matches VM semantics)
static constexpr std::uint8_t mul_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,        // mov rax, [rdi + src1_offset]
    0x48, 0x0f, 0xaf, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // imul rax, [rdi + src2_offset]
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,        // mov [rdi + dst_offset], rax
};

const Stencil mul = {
    .code = mul_code,
    .code_size = sizeof(mul_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 11, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 18, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::MUL),
    .name = "mul",
};

/// @brief DIV r[dst], r[src1], r[src2]
/// Includes zero check - jumps to error handler if divisor is zero
static constexpr std::uint8_t div_code[] = {
    // Check for division by zero
    0x48, 0x8b, 0x8f, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rcx, [rdi + src2_offset]
    0x48, 0x85, 0xc9,                          // test rcx, rcx
    0x74, 0x14,                                // jz error (skip 20 bytes)
    // Perform division
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x99,                                // cqo (sign extend rax to rdx:rax)
    0x48, 0xf7, 0xf9,                          // idiv rcx
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
    0xeb, 0x07,                                // jmp done
    // Error path: set dst to 0
    0x48, 0xc7, 0x87, 0xCC, 0xCC, 0xCC, 0xCC, 0x00, 0x00, 0x00,
    0x00,  // mov qword [rdi + dst_offset], 0
};

const Stencil div = {
    .code = div_code,
    .code_size = sizeof(div_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},   // src2
        {.offset = 16, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},  // src1
        {.offset = 27, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},  // dst
        {.offset = 34,
         .type = HoleType::Immediate32,
         .operand_index = 0,
         .adjustment = 0},  // dst (error path)
    }},
    .hole_count = 4,
    .opcode = static_cast<std::uint8_t>(JitOpcode::DIV),
    .name = "div",
};

/// @brief MOD r[dst], r[src1], r[src2]
/// Same as DIV but stores remainder (from rdx after idiv)
static constexpr std::uint8_t mod_code[] = {
    // Check for division by zero
    0x48,
    0x8b,
    0x8f,
    0xCC,
    0xCC,
    0xCC,
    0xCC,  // mov rcx, [rdi + src2_offset]
    0x48,
    0x85,
    0xc9,  // test rcx, rcx
    0x74,
    0x14,  // jz error
    // Perform division
    0x48,
    0x8b,
    0x87,
    0xCC,
    0xCC,
    0xCC,
    0xCC,  // mov rax, [rdi + src1_offset]
    0x48,
    0x99,  // cqo
    0x48,
    0xf7,
    0xf9,  // idiv rcx
    0x48,
    0x89,
    0x97,
    0xCC,
    0xCC,
    0xCC,
    0xCC,  // mov [rdi + dst_offset], rdx (remainder)
    0xeb,
    0x07,  // jmp done
    // Error path
    0x48,
    0xc7,
    0x87,
    0xCC,
    0xCC,
    0xCC,
    0xCC,
    0x00,
    0x00,
    0x00,
    0x00,
};

const Stencil mod = {
    .code = mod_code,
    .code_size = sizeof(mod_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 16, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 27, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
        {.offset = 34, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 4,
    .opcode = static_cast<std::uint8_t>(JitOpcode::MOD),
    .name = "mod",
};

// ============================================================================
// Bitwise Stencils
// ============================================================================

/// @brief AND r[dst], r[src1], r[src2]
static constexpr std::uint8_t and_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x23, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // and rax, [rdi + src2_offset]
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil and_op = {
    .code = and_code,
    .code_size = sizeof(and_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 17, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::AND),
    .name = "and",
};

/// @brief OR r[dst], r[src1], r[src2]
static constexpr std::uint8_t or_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x0b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // or rax, [rdi + src2_offset]
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil or_op = {
    .code = or_code,
    .code_size = sizeof(or_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 17, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::OR),
    .name = "or",
};

/// @brief XOR r[dst], r[src1], r[src2]
static constexpr std::uint8_t xor_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x33, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // xor rax, [rdi + src2_offset]
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil xor_op = {
    .code = xor_code,
    .code_size = sizeof(xor_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 17, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::XOR),
    .name = "xor",
};

/// @brief NOT r[dst], r[src]
static constexpr std::uint8_t not_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src_offset]
    0x48, 0xf7, 0xd0,                          // not rax
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil not_op = {
    .code = not_code,
    .code_size = sizeof(not_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 13, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 2,
    .opcode = static_cast<std::uint8_t>(JitOpcode::NOT),
    .name = "not",
};

/// @brief SHL r[dst], r[src1], r[src2]
static constexpr std::uint8_t shl_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x8b, 0x8f, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rcx, [rdi + src2_offset]
    0x48, 0xd3, 0xe0,                          // shl rax, cl
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil shl = {
    .code = shl_code,
    .code_size = sizeof(shl_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 20, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::SHL),
    .name = "shl",
};

/// @brief SHR r[dst], r[src1], r[src2] (logical shift right)
static constexpr std::uint8_t shr_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x8b, 0x8f, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rcx, [rdi + src2_offset]
    0x48, 0xd3, 0xe8,                          // shr rax, cl
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil shr = {
    .code = shr_code,
    .code_size = sizeof(shr_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 20, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::SHR),
    .name = "shr",
};

// ============================================================================
// Comparison Stencils
// ============================================================================

/// @brief CMP_EQ r[dst], r[src1], r[src2]
/// Sets dst to 1 if equal, 0 otherwise
static constexpr std::uint8_t cmp_eq_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x3b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // cmp rax, [rdi + src2_offset]
    0x0f, 0x94, 0xc0,                          // sete al
    0x48, 0x0f, 0xb6, 0xc0,                    // movzx rax, al
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil cmp_eq = {
    .code = cmp_eq_code,
    .code_size = sizeof(cmp_eq_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 24, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::CMP_EQ),
    .name = "cmp_eq",
};

/// @brief CMP_LT r[dst], r[src1], r[src2]
/// Sets dst to 1 if less than, 0 otherwise
static constexpr std::uint8_t cmp_lt_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src1_offset]
    0x48, 0x3b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // cmp rax, [rdi + src2_offset]
    0x0f, 0x9c, 0xc0,                          // setl al
    0x48, 0x0f, 0xb6, 0xc0,                    // movzx rax, al
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil cmp_lt = {
    .code = cmp_lt_code,
    .code_size = sizeof(cmp_lt_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 2, .adjustment = 0},
        {.offset = 24, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 3,
    .opcode = static_cast<std::uint8_t>(JitOpcode::CMP_LT),
    .name = "cmp_lt",
};

// ============================================================================
// Control Flow Stencils
// ============================================================================

/// @brief JMP offset
/// Unconditional relative jump
static constexpr std::uint8_t jmp_code[] = {
    0xe9, 0xCC, 0xCC, 0xCC, 0xCC,  // jmp rel32
};

const Stencil jmp = {
    .code = jmp_code,
    .code_size = sizeof(jmp_code),
    .holes = {{
        {.offset = 1, .type = HoleType::RelativeOffset32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 1,
    .opcode = static_cast<std::uint8_t>(JitOpcode::JMP),
    .name = "jmp",
};

/// @brief JMP_Z r[cond], offset
/// Jump if register is zero
static constexpr std::uint8_t jmp_z_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + cond_offset]
    0x48, 0x85, 0xc0,                          // test rax, rax
    0x0f, 0x84, 0xCC, 0xCC, 0xCC, 0xCC,        // jz rel32
};

const Stencil jmp_z = {
    .code = jmp_z_code,
    .code_size = sizeof(jmp_z_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
        {.offset = 12, .type = HoleType::RelativeOffset32, .operand_index = 1, .adjustment = 0},
    }},
    .hole_count = 2,
    .opcode = static_cast<std::uint8_t>(JitOpcode::JMP_Z),
    .name = "jmp_z",
};

/// @brief JMP_NZ r[cond], offset
/// Jump if register is not zero
static constexpr std::uint8_t jmp_nz_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + cond_offset]
    0x48, 0x85, 0xc0,                          // test rax, rax
    0x0f, 0x85, 0xCC, 0xCC, 0xCC, 0xCC,        // jnz rel32
};

const Stencil jmp_nz = {
    .code = jmp_nz_code,
    .code_size = sizeof(jmp_nz_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
        {.offset = 12, .type = HoleType::RelativeOffset32, .operand_index = 1, .adjustment = 0},
    }},
    .hole_count = 2,
    .opcode = static_cast<std::uint8_t>(JitOpcode::JMP_NZ),
    .name = "jmp_nz",
};

// ============================================================================
// Memory Stencils
// ============================================================================

/// @brief LOAD r[dst], r[addr]
/// Note: This is simplified - real impl would need memory bounds checking
static constexpr std::uint8_t load_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + addr_offset]
    0x48, 0x8b, 0x00,                          // mov rax, [rax]
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil load = {
    .code = load_code,
    .code_size = sizeof(load_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 13, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 2,
    .opcode = static_cast<std::uint8_t>(JitOpcode::LOAD),
    .name = "load",
};

/// @brief STORE r[val], r[addr]
static constexpr std::uint8_t store_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + val_offset]
    0x48, 0x8b, 0x8f, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rcx, [rdi + addr_offset]
    0x48, 0x89, 0x01,                          // mov [rcx], rax
};

const Stencil store = {
    .code = store_code,
    .code_size = sizeof(store_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
    }},
    .hole_count = 2,
    .opcode = static_cast<std::uint8_t>(JitOpcode::STORE),
    .name = "store",
};

/// @brief LOAD_IMM r[dst], imm64
static constexpr std::uint8_t load_imm_code[] = {
    0x48, 0xb8, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, imm64
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,                    // mov [rdi + dst_offset], rax
};

const Stencil load_imm = {
    .code = load_imm_code,
    .code_size = sizeof(load_imm_code),
    .holes = {{
        {.offset = 2, .type = HoleType::Immediate64, .operand_index = 1, .adjustment = 0},
        {.offset = 13, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 2,
    .opcode = static_cast<std::uint8_t>(JitOpcode::LOAD_IMM),
    .name = "load_imm",
};

/// @brief MOV r[dst], r[src]
static constexpr std::uint8_t mov_code[] = {
    0x48, 0x8b, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, [rdi + src_offset]
    0x48, 0x89, 0x87, 0xCC, 0xCC, 0xCC, 0xCC,  // mov [rdi + dst_offset], rax
};

const Stencil mov = {
    .code = mov_code,
    .code_size = sizeof(mov_code),
    .holes = {{
        {.offset = 3, .type = HoleType::Immediate32, .operand_index = 1, .adjustment = 0},
        {.offset = 10, .type = HoleType::Immediate32, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 2,
    .opcode = static_cast<std::uint8_t>(JitOpcode::MOV),
    .name = "mov",
};

// ============================================================================
// Interpreter Fallback
// ============================================================================

/// @brief Fallback to interpreter for unsupported opcodes
/// Calls interpreter_single_step(ctx, pc)
static constexpr std::uint8_t interpreter_fallback_code[] = {
    // Arguments already in rdi (ctx) and rsi (pc)
    // Call the fallback function (address patched)
    0x48, 0xb8, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,  // mov rax, fallback_addr
    0xff, 0xd0,                                                  // call rax
};

const Stencil interpreter_fallback = {
    .code = interpreter_fallback_code,
    .code_size = sizeof(interpreter_fallback_code),
    .holes = {{
        {.offset = 2, .type = HoleType::AbsoluteAddress, .operand_index = 0, .adjustment = 0},
    }},
    .hole_count = 1,
    .opcode = 0xFF,
    .name = "interpreter_fallback",
};

}  // namespace stencils::x86_64

#endif  // __x86_64__

// ============================================================================
// StencilRegistry Implementation
// ============================================================================

StencilRegistry StencilRegistry::create_default() {
    StencilRegistry registry;

#if defined(__x86_64__) || defined(_M_X64)
    // Register all x86-64 stencils
    registry.register_stencil(stencils::x86_64::add);
    registry.register_stencil(stencils::x86_64::sub);
    registry.register_stencil(stencils::x86_64::mul);
    registry.register_stencil(stencils::x86_64::div);
    registry.register_stencil(stencils::x86_64::mod);
    registry.register_stencil(stencils::x86_64::and_op);
    registry.register_stencil(stencils::x86_64::or_op);
    registry.register_stencil(stencils::x86_64::xor_op);
    registry.register_stencil(stencils::x86_64::not_op);
    registry.register_stencil(stencils::x86_64::shl);
    registry.register_stencil(stencils::x86_64::shr);
    registry.register_stencil(stencils::x86_64::cmp_eq);
    registry.register_stencil(stencils::x86_64::cmp_lt);
    registry.register_stencil(stencils::x86_64::jmp);
    registry.register_stencil(stencils::x86_64::jmp_z);
    registry.register_stencil(stencils::x86_64::jmp_nz);
    registry.register_stencil(stencils::x86_64::load);
    registry.register_stencil(stencils::x86_64::store);
    registry.register_stencil(stencils::x86_64::load_imm);
    registry.register_stencil(stencils::x86_64::mov);
#endif

    return registry;
}

}  // namespace dotvm::jit
