#pragma once

/// @file ir_builder.hpp
/// @brief DSL-002 IR Builder - AST to SSA IR conversion
///
/// Converts the parsed DslModule AST into typed SSA IR. Handles:
/// - Type inference from literals
/// - State slot allocation
/// - Control flow graph construction
/// - Expression lowering to SSA values

#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dotvm/core/dsl/ast.hpp"
#include "dotvm/core/dsl/dsl_error.hpp"
#include "dotvm/core/dsl/ir/instruction.hpp"
#include "dotvm/core/dsl/ir/types.hpp"

namespace dotvm::core::dsl::compiler {

/// @brief Error type for IR building
struct IRBuildError {
    enum class Kind {
        UnknownIdentifier,
        TypeMismatch,
        InvalidAssignmentTarget,
        UnsupportedExpression,
        InternalError,
    };

    Kind kind;
    std::string message;
    SourceSpan span;

    static IRBuildError unknown_identifier(const std::string& name, SourceSpan span) {
        return IRBuildError{Kind::UnknownIdentifier, "Unknown identifier: " + name, span};
    }

    static IRBuildError type_mismatch(const std::string& msg, SourceSpan span) {
        return IRBuildError{Kind::TypeMismatch, msg, span};
    }

    static IRBuildError invalid_target(SourceSpan span) {
        return IRBuildError{Kind::InvalidAssignmentTarget, "Invalid assignment target", span};
    }

    static IRBuildError unsupported(const std::string& msg, SourceSpan span) {
        return IRBuildError{Kind::UnsupportedExpression, msg, span};
    }

    static IRBuildError internal(const std::string& msg) {
        return IRBuildError{Kind::InternalError, msg, {}};
    }
};

/// @brief Result type for IR building operations
template <typename T>
using IRBuildResult = std::expected<T, IRBuildError>;

/// @brief Builds SSA IR from AST
class IRBuilder {
public:
    IRBuilder() = default;

    /// @brief Build IR for a complete module
    [[nodiscard]] IRBuildResult<ir::CompiledModule> build(const DslModule& module);

    /// @brief Build IR for a single dot definition
    [[nodiscard]] IRBuildResult<ir::DotIR> build_dot(const DotDef& dot);

private:
    // Current state during building
    ir::DotIR* current_dot_{nullptr};
    ir::BasicBlock* current_block_{nullptr};
    std::unordered_map<std::string, std::uint32_t> state_name_to_slot_;
    std::unordered_map<std::string, std::uint32_t> local_values_;

    // Block management
    ir::BasicBlock* create_block(const std::string& label = "");
    void set_current_block(ir::BasicBlock* block);
    void seal_block(ir::BasicBlock* block);

    // Value management
    ir::Value create_value(ir::ValueType type, const std::string& name = "");
    ir::Value create_const_value(ir::ValueType type, dotvm::core::Value val,
                                 const std::string& name = "");

    // State slot management
    IRBuildResult<void> build_state(const StateDef& state);
    std::uint32_t get_or_create_state_slot(const std::string& name, ir::ValueType type);

    // Expression building
    IRBuildResult<std::uint32_t> build_expression(const Expression& expr);
    IRBuildResult<std::uint32_t> build_binary_expr(const BinaryExpr& expr);
    IRBuildResult<std::uint32_t> build_unary_expr(const UnaryExpr& expr);
    IRBuildResult<std::uint32_t> build_identifier(const IdentifierExpr& expr);
    IRBuildResult<std::uint32_t> build_member_expr(const MemberExpr& expr);
    IRBuildResult<std::uint32_t> build_call_expr(const CallExpr& expr);
    IRBuildResult<std::uint32_t> build_literal(const Expression& expr);

    // Trigger building
    IRBuildResult<void> build_trigger(const TriggerDef& trigger);
    IRBuildResult<void> build_action(const ActionStmt& action);
    IRBuildResult<void> build_assignment(const ActionStmt& action);
    IRBuildResult<void> build_call_action(const ActionStmt& action);

    // Type inference
    ir::ValueType infer_type(const Expression& expr);
    ir::ValueType infer_binary_result_type(ir::ValueType left, ir::ValueType right,
                                           ir::BinaryOpKind op);

    // Emit instructions
    void emit(std::unique_ptr<ir::Instruction> instr);
    void emit_terminator(std::unique_ptr<ir::Instruction> term);
};

}  // namespace dotvm::core::dsl::compiler
