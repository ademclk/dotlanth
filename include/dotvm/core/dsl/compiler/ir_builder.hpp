#pragma once

/// @file ir_builder.hpp
/// @brief DSL-002/004 IR Builder - AST to SSA IR conversion
///
/// Converts the parsed DslModule AST into typed SSA IR. Handles:
/// - Type inference from literals
/// - State slot allocation
/// - Control flow graph construction
/// - Expression lowering to SSA values
/// - Stdlib import resolution and capability checking (DSL-004)

#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "dotvm/core/capabilities/capability.hpp"
#include "dotvm/core/dsl/ast.hpp"
#include "dotvm/core/dsl/dsl_error.hpp"
#include "dotvm/core/dsl/ir/instruction.hpp"
#include "dotvm/core/dsl/ir/types.hpp"

// Forward declarations
namespace dotvm::core::dsl::stdlib {
struct ModuleDef;
struct FunctionDef;
enum class StdlibType : std::uint8_t;
}  // namespace dotvm::core::dsl::stdlib

namespace dotvm::core::dsl::compiler {

/// @brief Error type for IR building
struct IRBuildError {
    enum class Kind {
        UnknownIdentifier,
        TypeMismatch,
        InvalidAssignmentTarget,
        UnsupportedExpression,
        InternalError,
        // DSL-004: Stdlib errors
        UnknownModule,
        CapabilityRequired,
        UnknownFunction,
        ArgumentCountMismatch,
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

    // DSL-004: Stdlib error factories
    static IRBuildError unknown_module(const std::string& path, SourceSpan span) {
        return IRBuildError{Kind::UnknownModule, "Unknown module: " + path, span};
    }

    static IRBuildError capability_required(const std::string& module,
                                            capabilities::Permission required, SourceSpan span) {
        return IRBuildError{Kind::CapabilityRequired,
                            "Module '" + module +
                                "' requires capability: " + capabilities::to_string(required),
                            span};
    }

    static IRBuildError unknown_function(const std::string& name, SourceSpan span) {
        return IRBuildError{Kind::UnknownFunction, "Unknown function: " + name, span};
    }

    static IRBuildError argument_count(const std::string& fn, std::size_t expected, std::size_t got,
                                       SourceSpan span) {
        return IRBuildError{Kind::ArgumentCountMismatch,
                            "Function '" + fn + "' expects " + std::to_string(expected) +
                                " arguments, got " + std::to_string(got),
                            span};
    }
};

/// @brief Compilation context with granted capabilities
struct CompileContext {
    /// Capabilities granted for this compilation
    capabilities::Permission granted_caps{capabilities::Permission::None};

    /// Constructor with no capabilities (default safe mode)
    CompileContext() = default;

    /// Constructor with specific capabilities
    explicit CompileContext(capabilities::Permission caps) : granted_caps(caps) {}

    /// Check if a permission is granted
    [[nodiscard]] bool has_permission(capabilities::Permission perm) const noexcept {
        return capabilities::has_permission(granted_caps, perm);
    }
};

/// @brief Result type for IR building operations
template <typename T>
using IRBuildResult = std::expected<T, IRBuildError>;

/// @brief Builds SSA IR from AST
class IRBuilder {
public:
    IRBuilder() = default;

    /// @brief Construct with compilation context (capabilities)
    explicit IRBuilder(CompileContext context) : context_(std::move(context)) {}

    /// @brief Build IR for a complete module
    [[nodiscard]] IRBuildResult<ir::CompiledModule> build(const DslModule& module);

    /// @brief Build IR for a single dot definition
    [[nodiscard]] IRBuildResult<ir::DotIR> build_dot(const DotDef& dot);

    /// @brief Get the compilation context
    [[nodiscard]] const CompileContext& context() const noexcept { return context_; }

    /// @brief Set the compilation context
    void set_context(CompileContext context) { context_ = std::move(context); }

private:
    // Compilation context (capabilities)
    CompileContext context_;

    // Current state during building
    ir::DotIR* current_dot_{nullptr};
    ir::BasicBlock* current_block_{nullptr};
    std::unordered_map<std::string, std::uint32_t> state_name_to_slot_;
    std::unordered_map<std::string, std::uint32_t> local_values_;

    // DSL-004: Imported modules (alias -> module path)
    std::unordered_map<std::string, std::string> imported_modules_;
    // DSL-004: Imported module pointers (path -> ModuleDef*)
    std::unordered_map<std::string, const stdlib::ModuleDef*> module_cache_;

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

    // DSL-004: Import processing
    IRBuildResult<void> process_imports(const std::vector<ImportDef>& imports);
    IRBuildResult<void> process_import(const ImportDef& import_def);

    // Expression building
    IRBuildResult<std::uint32_t> build_expression(const Expression& expr);
    IRBuildResult<std::uint32_t> build_binary_expr(const BinaryExpr& expr);
    IRBuildResult<std::uint32_t> build_unary_expr(const UnaryExpr& expr);
    IRBuildResult<std::uint32_t> build_identifier(const IdentifierExpr& expr);
    IRBuildResult<std::uint32_t> build_member_expr(const MemberExpr& expr);
    IRBuildResult<std::uint32_t> build_call_expr(const CallExpr& expr);
    IRBuildResult<std::uint32_t> build_literal(const Expression& expr);
    IRBuildResult<std::uint32_t> build_interpolated_string(const InterpolatedString& expr);

    // DSL-004: Stdlib call resolution
    IRBuildResult<std::uint32_t> build_stdlib_call(const stdlib::FunctionDef* fn,
                                                   const std::vector<std::uint32_t>& arg_ids,
                                                   SourceSpan span);
    IRBuildResult<std::uint32_t> build_inline_stdlib_call(const stdlib::FunctionDef* fn,
                                                          const std::vector<std::uint32_t>& arg_ids,
                                                          SourceSpan span);
    const stdlib::FunctionDef* resolve_function(const std::string& callee) const;

    // Trigger building
    IRBuildResult<void> build_trigger(const TriggerDef& trigger);
    IRBuildResult<void> build_action(const ActionStmt& action);
    IRBuildResult<void> build_assignment(const ActionStmt& action);
    IRBuildResult<void> build_call_action(const ActionStmt& action);

    // Type inference
    ir::ValueType infer_type(const Expression& expr);
    ir::ValueType infer_binary_result_type(ir::ValueType left, ir::ValueType right,
                                           ir::BinaryOpKind op);
    static ir::ValueType stdlib_type_to_ir(stdlib::StdlibType type);

    // Emit instructions
    void emit(std::unique_ptr<ir::Instruction> instr);
    void emit_terminator(std::unique_ptr<ir::Instruction> term);
};

}  // namespace dotvm::core::dsl::compiler
