#pragma once

/// @file instruction.hpp
/// @brief DSL-002 IR instruction definitions
///
/// Defines all instruction types for the SSA IR. Instructions are categorized as:
/// - Arithmetic: Binary and unary operations
/// - State: Load/store from mutable state slots
/// - Comparison: Produce boolean results
/// - Call: Function invocations
/// - Constants: Load constant values
/// - Terminators: Control flow (end basic blocks)

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "dotvm/core/dsl/ast.hpp"
#include "dotvm/core/dsl/ir/types.hpp"
#include "dotvm/core/dsl/source_location.hpp"
#include "dotvm/core/value.hpp"

// Bring AST types into scope for helper functions
namespace dotvm::core::dsl {
// Forward reference to AST binary/unary ops (defined in ast.hpp)
using AstBinaryOp = BinaryOp;
using AstUnaryOp = UnaryOp;
}  // namespace dotvm::core::dsl

namespace dotvm::core::dsl::ir {

// ============================================================================
// Instruction Opcodes
// ============================================================================

/// @brief Binary operation types
enum class BinaryOpKind : std::uint8_t {
    Add,   ///< Addition
    Sub,   ///< Subtraction
    Mul,   ///< Multiplication
    Div,   ///< Division
    Mod,   ///< Modulo
    And,   ///< Logical AND (for booleans)
    Or,    ///< Logical OR (for booleans)
    Shl,   ///< Shift left
    Shr,   ///< Shift right (logical)
    Sar,   ///< Shift right (arithmetic)
    Band,  ///< Bitwise AND
    Bor,   ///< Bitwise OR
    Bxor,  ///< Bitwise XOR
};

/// @brief Convert BinaryOpKind to string
[[nodiscard]] constexpr const char* to_string(BinaryOpKind op) noexcept {
    switch (op) {
        case BinaryOpKind::Add:
            return "add";
        case BinaryOpKind::Sub:
            return "sub";
        case BinaryOpKind::Mul:
            return "mul";
        case BinaryOpKind::Div:
            return "div";
        case BinaryOpKind::Mod:
            return "mod";
        case BinaryOpKind::And:
            return "and";
        case BinaryOpKind::Or:
            return "or";
        case BinaryOpKind::Shl:
            return "shl";
        case BinaryOpKind::Shr:
            return "shr";
        case BinaryOpKind::Sar:
            return "sar";
        case BinaryOpKind::Band:
            return "band";
        case BinaryOpKind::Bor:
            return "bor";
        case BinaryOpKind::Bxor:
            return "bxor";
    }
    return "?";
}

/// @brief Unary operation types
enum class UnaryOpKind : std::uint8_t {
    Neg,   ///< Arithmetic negation
    Not,   ///< Logical NOT
    Bnot,  ///< Bitwise NOT
};

/// @brief Convert UnaryOpKind to string
[[nodiscard]] constexpr const char* to_string(UnaryOpKind op) noexcept {
    switch (op) {
        case UnaryOpKind::Neg:
            return "neg";
        case UnaryOpKind::Not:
            return "not";
        case UnaryOpKind::Bnot:
            return "bnot";
    }
    return "?";
}

/// @brief Comparison operation types
enum class CompareKind : std::uint8_t {
    Eq,   ///< Equal
    Ne,   ///< Not equal
    Lt,   ///< Less than (signed)
    Le,   ///< Less or equal (signed)
    Gt,   ///< Greater than (signed)
    Ge,   ///< Greater or equal (signed)
    Ltu,  ///< Less than (unsigned)
    Leu,  ///< Less or equal (unsigned)
    Gtu,  ///< Greater than (unsigned)
    Geu,  ///< Greater or equal (unsigned)
};

/// @brief Convert CompareKind to string
[[nodiscard]] constexpr const char* to_string(CompareKind op) noexcept {
    switch (op) {
        case CompareKind::Eq:
            return "eq";
        case CompareKind::Ne:
            return "ne";
        case CompareKind::Lt:
            return "lt";
        case CompareKind::Le:
            return "le";
        case CompareKind::Gt:
            return "gt";
        case CompareKind::Ge:
            return "ge";
        case CompareKind::Ltu:
            return "ltu";
        case CompareKind::Leu:
            return "leu";
        case CompareKind::Gtu:
            return "gtu";
        case CompareKind::Geu:
            return "geu";
    }
    return "?";
}

// ============================================================================
// Instruction Types
// ============================================================================

/// @brief Binary operation instruction
/// result = left op right
struct BinaryOp {
    Value result;
    BinaryOpKind op;
    std::uint32_t left_id;   ///< ID of left operand value
    std::uint32_t right_id;  ///< ID of right operand value
};

/// @brief Unary operation instruction
/// result = op operand
struct UnaryOp {
    Value result;
    UnaryOpKind op;
    std::uint32_t operand_id;  ///< ID of operand value
};

/// @brief Comparison instruction (produces bool)
/// result = left cmp right
struct Compare {
    Value result;  ///< Result type is always Bool
    CompareKind op;
    std::uint32_t left_id;
    std::uint32_t right_id;
};

/// @brief Load constant value
/// result = constant
struct LoadConst {
    Value result;
    dotvm::core::Value constant;  ///< The constant value
};

/// @brief Load from state slot
/// result = state[slot_index]
struct StateGet {
    Value result;
    std::uint32_t slot_index;  ///< Index into state slots array
};

/// @brief Store to state slot
/// state[slot_index] = value
struct StatePut {
    std::uint32_t slot_index;
    std::uint32_t value_id;  ///< ID of value to store
};

/// @brief Function call instruction
/// result = callee(args...)
struct Call {
    Value result;  ///< Return value (Void if no return)
    std::string callee;
    std::vector<std::uint32_t> arg_ids;  ///< IDs of argument values
};

/// @brief Type cast instruction
/// result = (target_type)value
struct Cast {
    Value result;
    std::uint32_t value_id;
    ValueType target_type;
};

/// @brief Copy instruction (for SSA construction)
/// result = value
struct Copy {
    Value result;
    std::uint32_t value_id;
};

// ============================================================================
// Terminator Instructions
// ============================================================================

/// @brief Unconditional jump
/// goto target_block
struct Jump {
    std::uint32_t target_block_id;
};

/// @brief Conditional branch
/// if (condition) goto true_block else goto false_block
struct Branch {
    std::uint32_t condition_id;    ///< ID of boolean condition value
    std::uint32_t true_block_id;   ///< Block to jump to if condition is true
    std::uint32_t false_block_id;  ///< Block to jump to if condition is false
};

/// @brief Return from function
/// return value (or return void)
struct Return {
    std::optional<std::uint32_t> value_id;  ///< Optional return value ID
};

/// @brief Halt execution
struct Halt {
    std::optional<std::uint32_t> exit_code_id;  ///< Optional exit code value ID
};

/// @brief Unreachable marker (for dead code)
struct Unreachable {};

// ============================================================================
// Instruction Variant
// ============================================================================

/// @brief All possible instruction types
using InstructionKind = std::variant<
    // Value-producing instructions
    BinaryOp, UnaryOp, Compare, LoadConst, StateGet, Call, Cast, Copy,
    // Side-effect only
    StatePut,
    // Terminators
    Jump, Branch, Return, Halt, Unreachable>;

/// @brief An IR instruction
struct Instruction {
    InstructionKind kind;
    SourceSpan span;

    /// Check if this is a terminator instruction
    [[nodiscard]] bool is_terminator() const noexcept {
        return std::holds_alternative<Jump>(kind) || std::holds_alternative<Branch>(kind) ||
               std::holds_alternative<Return>(kind) || std::holds_alternative<Halt>(kind) ||
               std::holds_alternative<Unreachable>(kind);
    }

    /// Check if this instruction produces a value
    [[nodiscard]] bool produces_value() const noexcept {
        return std::holds_alternative<BinaryOp>(kind) || std::holds_alternative<UnaryOp>(kind) ||
               std::holds_alternative<Compare>(kind) || std::holds_alternative<LoadConst>(kind) ||
               std::holds_alternative<StateGet>(kind) || std::holds_alternative<Call>(kind) ||
               std::holds_alternative<Cast>(kind) || std::holds_alternative<Copy>(kind);
    }

    /// Get the result value if this instruction produces one
    [[nodiscard]] const Value* get_result() const noexcept {
        return std::visit(
            [](const auto& inst) -> const Value* {
                if constexpr (requires { inst.result; }) {
                    return &inst.result;
                }
                return nullptr;
            },
            kind);
    }

    /// Get mutable result value if this instruction produces one
    [[nodiscard]] Value* get_result_mut() noexcept {
        return std::visit(
            [](auto& inst) -> Value* {
                if constexpr (requires { inst.result; }) {
                    return &inst.result;
                }
                return nullptr;
            },
            kind);
    }

    /// Create an instruction from any instruction kind
    template <typename T>
    static std::unique_ptr<Instruction> make(T&& inst, SourceSpan span = {}) {
        auto instr = std::make_unique<Instruction>();
        instr->kind = std::forward<T>(inst);
        instr->span = span;
        return instr;
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Map DSL BinaryOp to IR BinaryOpKind
[[nodiscard]] inline std::optional<BinaryOpKind> map_binary_op(dsl::AstBinaryOp op) noexcept {
    switch (op) {
        case dsl::AstBinaryOp::Add:
            return BinaryOpKind::Add;
        case dsl::AstBinaryOp::Sub:
            return BinaryOpKind::Sub;
        case dsl::AstBinaryOp::Mul:
            return BinaryOpKind::Mul;
        case dsl::AstBinaryOp::Div:
            return BinaryOpKind::Div;
        case dsl::AstBinaryOp::Mod:
            return BinaryOpKind::Mod;
        case dsl::AstBinaryOp::And:
            return BinaryOpKind::And;
        case dsl::AstBinaryOp::Or:
            return BinaryOpKind::Or;
        default:
            return std::nullopt;
    }
}

/// @brief Map DSL comparison BinaryOp to IR CompareKind
[[nodiscard]] inline std::optional<CompareKind> map_compare_op(dsl::AstBinaryOp op) noexcept {
    switch (op) {
        case dsl::AstBinaryOp::Eq:
            return CompareKind::Eq;
        case dsl::AstBinaryOp::Ne:
            return CompareKind::Ne;
        case dsl::AstBinaryOp::Lt:
            return CompareKind::Lt;
        case dsl::AstBinaryOp::Le:
            return CompareKind::Le;
        case dsl::AstBinaryOp::Gt:
            return CompareKind::Gt;
        case dsl::AstBinaryOp::Ge:
            return CompareKind::Ge;
        default:
            return std::nullopt;
    }
}

/// @brief Map DSL UnaryOp to IR UnaryOpKind
[[nodiscard]] inline std::optional<UnaryOpKind> map_unary_op(dsl::AstUnaryOp op) noexcept {
    switch (op) {
        case dsl::AstUnaryOp::Neg:
            return UnaryOpKind::Neg;
        case dsl::AstUnaryOp::Not:
            return UnaryOpKind::Not;
    }
    return std::nullopt;
}

/// @brief Check if a DSL BinaryOp is a comparison operator
[[nodiscard]] inline bool is_comparison_op(dsl::AstBinaryOp op) noexcept {
    switch (op) {
        case dsl::AstBinaryOp::Eq:
        case dsl::AstBinaryOp::Ne:
        case dsl::AstBinaryOp::Lt:
        case dsl::AstBinaryOp::Le:
        case dsl::AstBinaryOp::Gt:
        case dsl::AstBinaryOp::Ge:
            return true;
        default:
            return false;
    }
}

}  // namespace dotvm::core::dsl::ir
