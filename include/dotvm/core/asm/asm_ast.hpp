#pragma once

/// @file asm_ast.hpp
/// @brief TOOL-004 AST definitions for the assembly parser
///
/// Defines the Abstract Syntax Tree nodes for parsed assembly programs.
/// The AST closely mirrors the assembly source structure.

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "dotvm/core/dsl/source_location.hpp"
#include "dotvm/core/instruction.hpp"

namespace dotvm::core::asm_ {

// Reuse SourceLocation and SourceSpan from DSL
using dsl::SourceLocation;
using dsl::SourceSpan;

/// @brief Memory operand with base register and offset
///
/// Represents addressing like [R0+4] or [R1-8]
struct MemoryOperand {
    /// Base register (0-255)
    std::uint8_t base_reg{0};

    /// Signed offset (-128 to +127 for 8-bit, larger for labels)
    std::int16_t offset{0};

    [[nodiscard]] constexpr bool operator==(const MemoryOperand&) const noexcept = default;
};

/// @brief Operand kinds
enum class OperandKind : std::uint8_t {
    /// Register operand (R0-R255)
    Register = 0,

    /// Immediate value (#123, #0xFF)
    Immediate = 1,

    /// Label reference (forward or backward)
    LabelRef = 2,

    /// Memory operand [Rn+offset]
    Memory = 3,

    /// String operand (for .byte directive)
    String = 4,
};

/// @brief Convert OperandKind to human-readable string
[[nodiscard]] constexpr const char* to_string(OperandKind kind) noexcept {
    switch (kind) {
        case OperandKind::Register:
            return "Register";
        case OperandKind::Immediate:
            return "Immediate";
        case OperandKind::LabelRef:
            return "LabelRef";
        case OperandKind::Memory:
            return "Memory";
        case OperandKind::String:
            return "String";
    }
    return "Unknown";
}

/// @brief An instruction operand
///
/// Can be a register, immediate, label reference, memory operand, or string.
struct AsmOperand {
    /// Kind of operand
    OperandKind kind{OperandKind::Register};

    /// Source span for error reporting
    SourceSpan span;

    /// Value storage
    /// - Register: uint8_t (0-255)
    /// - Immediate: int64_t
    /// - LabelRef: string (label name)
    /// - Memory: MemoryOperand
    /// - String: string
    std::variant<std::uint8_t, std::int64_t, std::string, MemoryOperand> value;

    /// @brief Create a register operand
    [[nodiscard]] static AsmOperand reg(std::uint8_t reg_num, SourceSpan span) noexcept {
        return AsmOperand{OperandKind::Register, span, reg_num};
    }

    /// @brief Create an immediate operand
    [[nodiscard]] static AsmOperand imm(std::int64_t val, SourceSpan span) noexcept {
        return AsmOperand{OperandKind::Immediate, span, val};
    }

    /// @brief Create a label reference operand
    [[nodiscard]] static AsmOperand label_ref(std::string_view name, SourceSpan span) {
        return AsmOperand{OperandKind::LabelRef, span, std::string{name}};
    }

    /// @brief Create a memory operand
    [[nodiscard]] static AsmOperand memory(MemoryOperand mem, SourceSpan span) noexcept {
        return AsmOperand{OperandKind::Memory, span, mem};
    }

    /// @brief Create a string operand
    [[nodiscard]] static AsmOperand str(std::string_view s, SourceSpan span) {
        return AsmOperand{OperandKind::String, span, std::string{s}};
    }

    /// @brief Get register number (only valid for Register kind)
    [[nodiscard]] std::uint8_t as_register() const { return std::get<std::uint8_t>(value); }

    /// @brief Get immediate value (only valid for Immediate kind)
    [[nodiscard]] std::int64_t as_immediate() const { return std::get<std::int64_t>(value); }

    /// @brief Get label name (only valid for LabelRef kind)
    [[nodiscard]] const std::string& as_label_ref() const { return std::get<std::string>(value); }

    /// @brief Get memory operand (only valid for Memory kind)
    [[nodiscard]] const MemoryOperand& as_memory() const { return std::get<MemoryOperand>(value); }

    /// @brief Get string value (only valid for String kind)
    [[nodiscard]] const std::string& as_string() const { return std::get<std::string>(value); }
};

/// @brief A label definition
///
/// Labels are defined as "name:" and can be referenced from instructions.
struct AsmLabel {
    /// Label name (without the colon)
    std::string name;

    /// Source span for error reporting
    SourceSpan span;
};

/// @brief An assembly instruction
///
/// Represents a parsed instruction with opcode and operands.
struct AsmInstruction {
    /// Opcode value (from opcode.hpp)
    std::uint8_t opcode{0};

    /// Mnemonic text (for error messages)
    std::string mnemonic;

    /// Instruction operands
    std::vector<AsmOperand> operands;

    /// Instruction format/type determined from opcode
    InstructionType format{InstructionType::TypeA};

    /// Source span for error reporting
    SourceSpan span;
};

/// @brief Assembler directive kinds
enum class DirectiveKind : std::uint8_t {
    Section = 0,  // .section name
    Global = 1,   // .global label
    Data = 2,     // .data
    Text = 3,     // .text
    Byte = 4,     // .byte values...
    Word = 5,     // .word values...
    Dword = 6,    // .dword values...
    Include = 7,  // .include "file"
    Align = 8,    // .align n
};

/// @brief Convert DirectiveKind to human-readable string
[[nodiscard]] constexpr const char* to_string(DirectiveKind kind) noexcept {
    switch (kind) {
        case DirectiveKind::Section:
            return "Section";
        case DirectiveKind::Global:
            return "Global";
        case DirectiveKind::Data:
            return "Data";
        case DirectiveKind::Text:
            return "Text";
        case DirectiveKind::Byte:
            return "Byte";
        case DirectiveKind::Word:
            return "Word";
        case DirectiveKind::Dword:
            return "Dword";
        case DirectiveKind::Include:
            return "Include";
        case DirectiveKind::Align:
            return "Align";
    }
    return "Unknown";
}

/// @brief An assembler directive
///
/// Directives control assembly behavior (sections, data, includes).
struct AsmDirective {
    /// Directive kind
    DirectiveKind kind{DirectiveKind::Section};

    /// Directive arguments (operands)
    std::vector<AsmOperand> args;

    /// Source span for error reporting
    SourceSpan span;
};

/// @brief A statement in assembly source
///
/// Can be a label, instruction, or directive.
using AsmStatement = std::variant<AsmLabel, AsmInstruction, AsmDirective>;

/// @brief Check if statement is a label
[[nodiscard]] inline bool is_label(const AsmStatement& stmt) noexcept {
    return std::holds_alternative<AsmLabel>(stmt);
}

/// @brief Check if statement is an instruction
[[nodiscard]] inline bool is_instruction(const AsmStatement& stmt) noexcept {
    return std::holds_alternative<AsmInstruction>(stmt);
}

/// @brief Check if statement is a directive
[[nodiscard]] inline bool is_directive(const AsmStatement& stmt) noexcept {
    return std::holds_alternative<AsmDirective>(stmt);
}

/// @brief Get statement span
[[nodiscard]] inline SourceSpan statement_span(const AsmStatement& stmt) noexcept {
    return std::visit(
        [](const auto& s) -> SourceSpan {
            if constexpr (std::is_same_v<std::decay_t<decltype(s)>, AsmLabel>) {
                return s.span;
            } else if constexpr (std::is_same_v<std::decay_t<decltype(s)>, AsmInstruction>) {
                return s.span;
            } else {
                return s.span;
            }
        },
        stmt);
}

/// @brief A complete assembly program
///
/// Contains all parsed statements from the source.
struct AsmProgram {
    /// All statements in program order
    std::vector<AsmStatement> statements;

    /// Source span covering the entire program
    SourceSpan span;

    /// @brief Check if program is empty
    [[nodiscard]] bool empty() const noexcept { return statements.empty(); }

    /// @brief Get number of statements
    [[nodiscard]] std::size_t size() const noexcept { return statements.size(); }
};

}  // namespace dotvm::core::asm_
