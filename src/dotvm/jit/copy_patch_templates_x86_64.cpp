// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2025 DotLanth Project

#include "dotvm/jit/copy_patch_template.hpp"

#ifdef DOTVM_JIT_X86_64

#include <array>
#include <unordered_map>

namespace dotvm::jit {

// ============================================================================
// x86-64 Pre-compiled Templates
// ============================================================================
//
// Register conventions:
// - rbx: RegisterFile base pointer (pinned)
// - rax, rcx, rdx: Scratch registers for operations
//
// Template holes use placeholder values that are patched at JIT time.
// Each template loads from register file, performs operation, stores back.
// Values are kept NaN-boxed; type checking happens at template boundaries.

namespace {

// ----------------------------------------------------------------------------
// ADD_RRR: Rd = Rs1 + Rs2 (integer, simplified)
// ----------------------------------------------------------------------------
// Simplified template: loads values, adds, stores (no NaN-boxing for now)

constexpr std::uint8_t add_rrr_code_data[] = {
    // mov rax, [rbx + Rs1*8] - 7 bytes
    0x48, 0x8B, 0x83, 0x00, 0x00, 0x00, 0x00,  // Rs1 offset at [3]
    // add rax, [rbx + Rs2*8] - 7 bytes
    0x48, 0x03, 0x83, 0x00, 0x00, 0x00, 0x00,  // Rs2 offset at [10]
    // mov [rbx + Rd*8], rax - 7 bytes
    0x48, 0x89, 0x83, 0x00, 0x00, 0x00, 0x00,  // Rd offset at [17]
};

constexpr PatchLocation add_rrr_patches_data[] = {
    {.offset = 3,  .type = PatchType::RegOffset, .operand_index = 1, .size = 4},  // Rs1
    {.offset = 10, .type = PatchType::RegOffset, .operand_index = 2, .size = 4},  // Rs2
    {.offset = 17, .type = PatchType::RegOffset, .operand_index = 0, .size = 4},  // Rd
};

// ----------------------------------------------------------------------------
// SUB_RRR: Rd = Rs1 - Rs2 (integer, simplified)
// ----------------------------------------------------------------------------

constexpr std::uint8_t sub_rrr_code_data[] = {
    // mov rax, [rbx + Rs1*8] - 7 bytes
    0x48, 0x8B, 0x83, 0x00, 0x00, 0x00, 0x00,
    // sub rax, [rbx + Rs2*8] - 7 bytes
    0x48, 0x2B, 0x83, 0x00, 0x00, 0x00, 0x00,
    // mov [rbx + Rd*8], rax - 7 bytes
    0x48, 0x89, 0x83, 0x00, 0x00, 0x00, 0x00,
};

constexpr PatchLocation sub_rrr_patches_data[] = {
    {.offset = 3,  .type = PatchType::RegOffset, .operand_index = 1, .size = 4},
    {.offset = 10, .type = PatchType::RegOffset, .operand_index = 2, .size = 4},
    {.offset = 17, .type = PatchType::RegOffset, .operand_index = 0, .size = 4},
};

// ----------------------------------------------------------------------------
// MUL_RRR: Rd = Rs1 * Rs2 (integer, simplified)
// ----------------------------------------------------------------------------

constexpr std::uint8_t mul_rrr_code_data[] = {
    // mov rax, [rbx + Rs1*8] - 7 bytes
    0x48, 0x8B, 0x83, 0x00, 0x00, 0x00, 0x00,
    // mov rcx, [rbx + Rs2*8] - 7 bytes
    0x48, 0x8B, 0x8B, 0x00, 0x00, 0x00, 0x00,
    // imul rax, rcx - 4 bytes (REX.W + 0F AF /r)
    0x48, 0x0F, 0xAF, 0xC1,
    // mov [rbx + Rd*8], rax - 7 bytes
    0x48, 0x89, 0x83, 0x00, 0x00, 0x00, 0x00,
};

constexpr PatchLocation mul_rrr_patches_data[] = {
    {.offset = 3,  .type = PatchType::RegOffset, .operand_index = 1, .size = 4},
    {.offset = 10, .type = PatchType::RegOffset, .operand_index = 2, .size = 4},
    {.offset = 21, .type = PatchType::RegOffset, .operand_index = 0, .size = 4},
};

// ----------------------------------------------------------------------------
// NEG_RR: Rd = -Rs1 (integer)
// ----------------------------------------------------------------------------

constexpr std::uint8_t neg_rr_code_data[] = {
    // mov rax, [rbx + Rs1*8] - 7 bytes
    0x48, 0x8B, 0x83, 0x00, 0x00, 0x00, 0x00,
    // neg rax - 3 bytes (REX.W + F7 /3)
    0x48, 0xF7, 0xD8,
    // mov [rbx + Rd*8], rax - 7 bytes
    0x48, 0x89, 0x83, 0x00, 0x00, 0x00, 0x00,
};

constexpr PatchLocation neg_rr_patches_data[] = {
    {.offset = 3,  .type = PatchType::RegOffset, .operand_index = 1, .size = 4},
    {.offset = 13, .type = PatchType::RegOffset, .operand_index = 0, .size = 4},
};

// ----------------------------------------------------------------------------
// AND_RRR: Rd = Rs1 & Rs2 (bitwise)
// ----------------------------------------------------------------------------

constexpr std::uint8_t and_rrr_code_data[] = {
    // mov rax, [rbx + Rs1*8]
    0x48, 0x8B, 0x83, 0x00, 0x00, 0x00, 0x00,
    // and rax, [rbx + Rs2*8]
    0x48, 0x23, 0x83, 0x00, 0x00, 0x00, 0x00,
    // mov [rbx + Rd*8], rax
    0x48, 0x89, 0x83, 0x00, 0x00, 0x00, 0x00,
};

constexpr PatchLocation and_rrr_patches_data[] = {
    {.offset = 3,  .type = PatchType::RegOffset, .operand_index = 1, .size = 4},
    {.offset = 10, .type = PatchType::RegOffset, .operand_index = 2, .size = 4},
    {.offset = 17, .type = PatchType::RegOffset, .operand_index = 0, .size = 4},
};

// ----------------------------------------------------------------------------
// OR_RRR: Rd = Rs1 | Rs2 (bitwise)
// ----------------------------------------------------------------------------

constexpr std::uint8_t or_rrr_code_data[] = {
    // mov rax, [rbx + Rs1*8]
    0x48, 0x8B, 0x83, 0x00, 0x00, 0x00, 0x00,
    // or rax, [rbx + Rs2*8]
    0x48, 0x0B, 0x83, 0x00, 0x00, 0x00, 0x00,
    // mov [rbx + Rd*8], rax
    0x48, 0x89, 0x83, 0x00, 0x00, 0x00, 0x00,
};

constexpr PatchLocation or_rrr_patches_data[] = {
    {.offset = 3,  .type = PatchType::RegOffset, .operand_index = 1, .size = 4},
    {.offset = 10, .type = PatchType::RegOffset, .operand_index = 2, .size = 4},
    {.offset = 17, .type = PatchType::RegOffset, .operand_index = 0, .size = 4},
};

// ----------------------------------------------------------------------------
// XOR_RRR: Rd = Rs1 ^ Rs2 (bitwise)
// ----------------------------------------------------------------------------

constexpr std::uint8_t xor_rrr_code_data[] = {
    // mov rax, [rbx + Rs1*8]
    0x48, 0x8B, 0x83, 0x00, 0x00, 0x00, 0x00,
    // xor rax, [rbx + Rs2*8]
    0x48, 0x33, 0x83, 0x00, 0x00, 0x00, 0x00,
    // mov [rbx + Rd*8], rax
    0x48, 0x89, 0x83, 0x00, 0x00, 0x00, 0x00,
};

constexpr PatchLocation xor_rrr_patches_data[] = {
    {.offset = 3,  .type = PatchType::RegOffset, .operand_index = 1, .size = 4},
    {.offset = 10, .type = PatchType::RegOffset, .operand_index = 2, .size = 4},
    {.offset = 17, .type = PatchType::RegOffset, .operand_index = 0, .size = 4},
};

// ----------------------------------------------------------------------------
// Function prologue template
// ----------------------------------------------------------------------------
// Saves callee-saved registers and sets up rbx as register file base

constexpr std::uint8_t prologue_code_data[] = {
    // push rbx - 1 byte
    0x53,
    // push r12 - 2 bytes
    0x41, 0x54,
    // push r13 - 2 bytes
    0x41, 0x55,
    // push r14 - 2 bytes
    0x41, 0x56,
    // push r15 - 2 bytes
    0x41, 0x57,
    // mov rbx, rdi - 3 bytes (rdi = first arg = register file ptr)
    0x48, 0x89, 0xFB,
};

// No patches for prologue
constexpr PatchLocation* prologue_patches_data = nullptr;

// ----------------------------------------------------------------------------
// Function epilogue template
// ----------------------------------------------------------------------------
// Restores callee-saved registers and returns

constexpr std::uint8_t epilogue_code_data[] = {
    // pop r15 - 2 bytes
    0x41, 0x5F,
    // pop r14 - 2 bytes
    0x41, 0x5E,
    // pop r13 - 2 bytes
    0x41, 0x5D,
    // pop r12 - 2 bytes
    0x41, 0x5C,
    // pop rbx - 1 byte
    0x5B,
    // ret - 1 byte
    0xC3,
};

// No patches for epilogue
constexpr PatchLocation* epilogue_patches_data = nullptr;

// ----------------------------------------------------------------------------
// Template table
// ----------------------------------------------------------------------------

struct TemplateEntry {
    TemplateCategory category;
    const std::uint8_t* code;
    std::size_t code_size;
    const PatchLocation* patches;
    std::size_t patch_count;
};

constexpr TemplateEntry template_table[] = {
    {TemplateCategory::ADD_RRR, add_rrr_code_data, sizeof(add_rrr_code_data),
     add_rrr_patches_data, std::size(add_rrr_patches_data)},
    {TemplateCategory::SUB_RRR, sub_rrr_code_data, sizeof(sub_rrr_code_data),
     sub_rrr_patches_data, std::size(sub_rrr_patches_data)},
    {TemplateCategory::MUL_RRR, mul_rrr_code_data, sizeof(mul_rrr_code_data),
     mul_rrr_patches_data, std::size(mul_rrr_patches_data)},
    {TemplateCategory::NEG_RR, neg_rr_code_data, sizeof(neg_rr_code_data),
     neg_rr_patches_data, std::size(neg_rr_patches_data)},
    {TemplateCategory::AND_RRR, and_rrr_code_data, sizeof(and_rrr_code_data),
     and_rrr_patches_data, std::size(and_rrr_patches_data)},
    {TemplateCategory::OR_RRR, or_rrr_code_data, sizeof(or_rrr_code_data),
     or_rrr_patches_data, std::size(or_rrr_patches_data)},
    {TemplateCategory::XOR_RRR, xor_rrr_code_data, sizeof(xor_rrr_code_data),
     xor_rrr_patches_data, std::size(xor_rrr_patches_data)},
    {TemplateCategory::PROLOGUE, prologue_code_data, sizeof(prologue_code_data),
     prologue_patches_data, 0},
    {TemplateCategory::EPILOGUE, epilogue_code_data, sizeof(epilogue_code_data),
     epilogue_patches_data, 0},
};

}  // anonymous namespace

// ============================================================================
// x86-64 Template Registry Implementation
// ============================================================================

class X86_64TemplateRegistry final : public TemplateRegistry {
public:
    X86_64TemplateRegistry() {
        // Build lookup table from static data
        for (const auto& entry : template_table) {
            CodeTemplate tmpl{
                .category = entry.category,
                .code = std::span<const std::uint8_t>(entry.code, entry.code_size),
                .patches = std::span<const PatchLocation>(entry.patches, entry.patch_count),
                .stack_frame_size = 0,
                .has_slow_path = false,
                .slow_path_offset = 0
            };
            templates_[entry.category] = tmpl;
        }
    }

    [[nodiscard]] auto get_template(TemplateCategory category) const noexcept
        -> const CodeTemplate* override {
        auto it = templates_.find(category);
        if (it == templates_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    [[nodiscard]] auto has_template(TemplateCategory category) const noexcept
        -> bool override {
        return templates_.contains(category);
    }

    [[nodiscard]] auto target_arch() const noexcept -> TargetArch override {
        return TargetArch::X86_64;
    }

private:
    std::unordered_map<TemplateCategory, CodeTemplate> templates_;
};

// Global instance accessor
auto get_x86_64_template_registry() -> const TemplateRegistry& {
    static X86_64TemplateRegistry registry;
    return registry;
}

}  // namespace dotvm::jit

#endif  // DOTVM_JIT_X86_64
