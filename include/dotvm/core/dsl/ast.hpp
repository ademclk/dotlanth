#pragma once

/// @file ast.hpp
/// @brief DSL-001 Abstract Syntax Tree nodes for the DotLanth DSL
///
/// Defines all AST node types representing the parsed DSL structure.
/// Uses std::variant for expressions to enable type-safe pattern matching.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "dotvm/core/dsl/source_location.hpp"
#include "dotvm/core/dsl/token.hpp"

namespace dotvm::core::dsl {

// Forward declarations for recursive types
struct Expression;
struct BinaryExpr;
struct UnaryExpr;
struct InterpolatedString;

// ============================================================================
// Expression Nodes
// ============================================================================

/// @brief Identifier expression (variable reference)
struct IdentifierExpr {
    std::string name;
    SourceSpan span;
};

/// @brief Member access expression (a.b.c)
struct MemberExpr {
    std::unique_ptr<Expression> object;
    std::string member;
    SourceSpan span;
};

/// @brief String literal expression
struct StringExpr {
    std::string value;  // Unescaped content
    SourceSpan span;
};

/// @brief Integer literal expression
struct IntegerExpr {
    std::int64_t value;
    SourceSpan span;
};

/// @brief Floating-point literal expression
struct FloatExpr {
    double value;
    SourceSpan span;
};

/// @brief Boolean literal expression
struct BoolExpr {
    bool value;
    SourceSpan span;
};

/// @brief Binary operator types
enum class BinaryOp : std::uint8_t {
    // Arithmetic
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    // Comparison
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge,
    // Logical
    And,
    Or,
};

/// @brief Convert BinaryOp to string
[[nodiscard]] constexpr const char* to_string(BinaryOp op) noexcept {
    switch (op) {
        case BinaryOp::Add:
            return "+";
        case BinaryOp::Sub:
            return "-";
        case BinaryOp::Mul:
            return "*";
        case BinaryOp::Div:
            return "/";
        case BinaryOp::Mod:
            return "%";
        case BinaryOp::Eq:
            return "==";
        case BinaryOp::Ne:
            return "!=";
        case BinaryOp::Lt:
            return "<";
        case BinaryOp::Le:
            return "<=";
        case BinaryOp::Gt:
            return ">";
        case BinaryOp::Ge:
            return ">=";
        case BinaryOp::And:
            return "and";
        case BinaryOp::Or:
            return "or";
    }
    return "?";
}

/// @brief Unary operator types
enum class UnaryOp : std::uint8_t {
    Neg,  // -
    Not,  // not
};

/// @brief Convert UnaryOp to string
[[nodiscard]] constexpr const char* to_string(UnaryOp op) noexcept {
    switch (op) {
        case UnaryOp::Neg:
            return "-";
        case UnaryOp::Not:
            return "not";
    }
    return "?";
}

/// @brief Binary expression (a op b)
struct BinaryExpr {
    std::unique_ptr<Expression> left;
    BinaryOp op;
    std::unique_ptr<Expression> right;
    SourceSpan span;
};

/// @brief Unary expression (op a)
struct UnaryExpr {
    UnaryOp op;
    std::unique_ptr<Expression> operand;
    SourceSpan span;
};

/// @brief Interpolation segment in a string
struct InterpolationSegment {
    /// Text before the interpolation (or entire string for last segment)
    std::string text;

    /// Interpolated expression (empty for last segment after final text)
    std::unique_ptr<Expression> expr;
};

/// @brief Interpolated string expression "text ${expr} more"
struct InterpolatedString {
    std::vector<InterpolationSegment> segments;
    SourceSpan span;
};

/// @brief Call expression (for actions)
struct CallExpr {
    std::string callee;
    std::vector<std::unique_ptr<Expression>> arguments;
    SourceSpan span;
};

/// @brief Grouped expression with parentheses
struct GroupExpr {
    std::unique_ptr<Expression> inner;
    SourceSpan span;
};

/// @brief Expression variant type
using ExpressionVariant = std::variant<IdentifierExpr, MemberExpr, StringExpr, IntegerExpr,
                                       FloatExpr, BoolExpr, BinaryExpr, UnaryExpr,
                                       InterpolatedString, CallExpr, GroupExpr>;

/// @brief Expression node wrapper
struct Expression {
    ExpressionVariant value;
    SourceSpan span;

    /// @brief Create from expression variant
    template <typename T>
    static std::unique_ptr<Expression> make(T&& expr) {
        auto e = std::make_unique<Expression>();
        e->span = get_span(expr);
        e->value = std::forward<T>(expr);
        return e;
    }

private:
    /// Get span from any expression variant type
    template <typename T>
    static SourceSpan get_span(const T& expr) {
        return expr.span;
    }
};

// ============================================================================
// Statement/Definition Nodes
// ============================================================================

/// @brief Import definition: import "path/to/module"
struct ImportDef {
    std::string path;
    SourceSpan span;
};

/// @brief State variable definition: name: value
struct StateVar {
    std::string name;
    std::unique_ptr<Expression> value;
    SourceSpan span;
};

/// @brief State block definition
struct StateDef {
    std::vector<StateVar> variables;
    SourceSpan span;
};

/// @brief Assignment operator types
enum class AssignOp : std::uint8_t {
    Assign,     // =
    AddAssign,  // +=
    SubAssign,  // -=
    MulAssign,  // *=
    DivAssign,  // /=
};

/// @brief Convert AssignOp to string
[[nodiscard]] constexpr const char* to_string(AssignOp op) noexcept {
    switch (op) {
        case AssignOp::Assign:
            return "=";
        case AssignOp::AddAssign:
            return "+=";
        case AssignOp::SubAssign:
            return "-=";
        case AssignOp::MulAssign:
            return "*=";
        case AssignOp::DivAssign:
            return "/=";
    }
    return "?";
}

/// @brief Action statement within a do block
struct ActionStmt {
    /// Action type: either a call or an assignment
    enum class Type : std::uint8_t { Call, Assignment };

    Type type;

    // For Call type: callee "arg1" "arg2"
    std::string callee;
    std::vector<std::unique_ptr<Expression>> arguments;

    // For Assignment type: target op value
    std::unique_ptr<Expression> target;
    AssignOp assign_op;
    std::unique_ptr<Expression> value;

    SourceSpan span;

    /// Create a call action
    static ActionStmt call(std::string callee, std::vector<std::unique_ptr<Expression>> args,
                           SourceSpan span) {
        ActionStmt stmt;
        stmt.type = Type::Call;
        stmt.callee = std::move(callee);
        stmt.arguments = std::move(args);
        stmt.span = span;
        return stmt;
    }

    /// Create an assignment action
    static ActionStmt assignment(std::unique_ptr<Expression> target, AssignOp op,
                                 std::unique_ptr<Expression> value, SourceSpan span) {
        ActionStmt stmt;
        stmt.type = Type::Assignment;
        stmt.target = std::move(target);
        stmt.assign_op = op;
        stmt.value = std::move(value);
        stmt.span = span;
        return stmt;
    }
};

/// @brief Do block containing actions
struct DoBlock {
    std::vector<ActionStmt> actions;
    SourceSpan span;
};

/// @brief Trigger definition: when condition: do: actions
struct TriggerDef {
    std::unique_ptr<Expression> condition;
    DoBlock do_block;
    SourceSpan span;
};

/// @brief Dot (agent) definition
struct DotDef {
    std::string name;
    std::optional<StateDef> state;
    std::vector<TriggerDef> triggers;
    SourceSpan span;
};

/// @brief Link definition: link agent1 -> agent2
struct LinkDef {
    std::string source;
    std::string target;
    SourceSpan span;
};

/// @brief Root module containing all definitions
struct DslModule {
    /// Import statements
    std::vector<ImportDef> imports;

    /// Dot (agent) definitions
    std::vector<DotDef> dots;

    /// Link definitions
    std::vector<LinkDef> links;

    /// Full span of the module
    SourceSpan span;
};

// ============================================================================
// AST Visitor Pattern (optional, for future use)
// ============================================================================

/// @brief Base visitor interface for expression traversal
template <typename R = void>
struct ExprVisitor {
    virtual ~ExprVisitor() = default;

    virtual R visit(const IdentifierExpr& expr) = 0;
    virtual R visit(const MemberExpr& expr) = 0;
    virtual R visit(const StringExpr& expr) = 0;
    virtual R visit(const IntegerExpr& expr) = 0;
    virtual R visit(const FloatExpr& expr) = 0;
    virtual R visit(const BoolExpr& expr) = 0;
    virtual R visit(const BinaryExpr& expr) = 0;
    virtual R visit(const UnaryExpr& expr) = 0;
    virtual R visit(const InterpolatedString& expr) = 0;
    virtual R visit(const CallExpr& expr) = 0;
    virtual R visit(const GroupExpr& expr) = 0;
};

/// @brief Apply visitor to expression
template <typename R>
R visit_expr(ExprVisitor<R>& visitor, const Expression& expr) {
    return std::visit([&visitor](const auto& e) -> R { return visitor.visit(e); }, expr.value);
}

}  // namespace dotvm::core::dsl
