/// @file formatter.cpp
/// @brief DSL-003 Source code formatter implementation

#include "dotvm/cli/formatter.hpp"

#include <iomanip>
#include <sstream>
#include <variant>

namespace dotvm::cli {

// ============================================================================
// Formatter (backward compatibility)
// ============================================================================

Formatter::Formatter(FormatConfig config) : config_(std::move(config)) {}

std::string Formatter::format(std::string_view source) const {
    // The simple Formatter just returns source unchanged.
    // For real formatting, use AstFormatter with a parsed AST.
    return std::string(source);
}

// ============================================================================
// AstFormatter
// ============================================================================

AstFormatter::AstFormatter(FormatConfig config) : config_(std::move(config)) {}

std::string AstFormatter::format(const core::dsl::DslModule& module) {
    // Reset state
    output_.str("");
    output_.clear();
    indent_level_ = 0;
    at_line_start_ = true;

    // Compute indent string from config
    if (config_.use_tabs) {
        indent_str_ = "\t";
    } else {
        indent_str_ = std::string(config_.indent_size, ' ');
    }

    // Format the module
    format_module(module);

    std::string result = output_.str();

    // Ensure final newline if configured
    if (config_.ensure_final_newline && !result.empty() && result.back() != '\n') {
        result += '\n';
    }

    // Trim trailing whitespace from lines if configured
    if (config_.trim_trailing) {
        std::string trimmed;
        std::istringstream stream(result);
        std::string line;
        bool first = true;
        while (std::getline(stream, line)) {
            // Trim trailing spaces/tabs
            auto end = line.find_last_not_of(" \t");
            if (end != std::string::npos) {
                line = line.substr(0, end + 1);
            } else if (!line.empty()) {
                // Line was all whitespace
                line.clear();
            }

            if (!first) {
                trimmed += '\n';
            }
            trimmed += line;
            first = false;
        }

        // Preserve final newline if original had it
        if (!result.empty() && result.back() == '\n') {
            trimmed += '\n';
        }

        result = trimmed;
    }

    return result;
}

void AstFormatter::format_module(const core::dsl::DslModule& module) {
    bool need_blank = false;

    // Format includes first
    for (const auto& include : module.includes) {
        format_include(include);
        need_blank = true;
    }

    // Format imports
    if (!module.imports.empty()) {
        if (need_blank) {
            blank_line();
        }
        for (const auto& import : module.imports) {
            format_import(import);
        }
        need_blank = true;
    }

    // Format dots
    for (const auto& dot : module.dots) {
        if (need_blank) {
            blank_line();
        }
        format_dot(dot);
        need_blank = true;
    }

    // Format links
    for (const auto& link : module.links) {
        if (need_blank) {
            blank_line();
        }
        format_link(link);
        need_blank = true;
    }
}

void AstFormatter::format_include(const core::dsl::IncludeDef& include) {
    write_indent();
    output_ << "include: \"" << escape_string(include.path) << "\"";
    newline();
}

void AstFormatter::format_import(const core::dsl::ImportDef& import) {
    write_indent();
    output_ << "import \"" << escape_string(import.path) << "\"";
    newline();
}

void AstFormatter::format_dot(const core::dsl::DotDef& dot) {
    write_indent();
    output_ << "dot " << dot.name << ":";
    newline();

    indent();

    // Format state if present
    if (dot.state.has_value()) {
        format_state(*dot.state);
    }

    // Format triggers
    for (const auto& trigger : dot.triggers) {
        format_trigger(trigger);
    }

    dedent();
}

void AstFormatter::format_state(const core::dsl::StateDef& state) {
    write_indent();
    output_ << "state:";
    newline();

    indent();
    for (const auto& var : state.variables) {
        format_state_var(var);
    }
    dedent();
}

void AstFormatter::format_state_var(const core::dsl::StateVar& var) {
    write_indent();
    output_ << var.name << ": ";
    if (var.value) {
        output_ << format_expr(*var.value);
    }
    newline();
}

void AstFormatter::format_trigger(const core::dsl::TriggerDef& trigger) {
    write_indent();
    output_ << "when ";
    if (trigger.condition) {
        output_ << format_expr(*trigger.condition);
    }
    output_ << ":";
    newline();

    indent();
    format_do_block(trigger.do_block);
    dedent();
}

void AstFormatter::format_do_block(const core::dsl::DoBlock& do_block) {
    write_indent();
    output_ << "do:";
    newline();

    indent();
    for (const auto& action : do_block.actions) {
        format_action(action);
    }
    dedent();
}

void AstFormatter::format_action(const core::dsl::ActionStmt& action) {
    write_indent();

    if (action.type == core::dsl::ActionStmt::Type::Call) {
        output_ << action.callee;
        for (const auto& arg : action.arguments) {
            output_ << " " << format_expr(*arg);
        }
    } else {
        // Assignment
        if (action.target) {
            output_ << format_expr(*action.target);
        }
        output_ << " " << core::dsl::to_string(action.assign_op) << " ";
        if (action.value) {
            output_ << format_expr(*action.value);
        }
    }

    newline();
}

void AstFormatter::format_link(const core::dsl::LinkDef& link) {
    write_indent();
    output_ << "link " << link.source << " -> " << link.target;
    newline();
}

// ============================================================================
// Expression Formatting
// ============================================================================

std::string AstFormatter::format_expr(const core::dsl::Expression& expr, Precedence parent_prec) {
    return std::visit(
        [this, parent_prec](const auto& e) -> std::string {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, core::dsl::IdentifierExpr>) {
                return format_identifier(e);
            } else if constexpr (std::is_same_v<T, core::dsl::MemberExpr>) {
                return format_member(e);
            } else if constexpr (std::is_same_v<T, core::dsl::StringExpr>) {
                return format_string(e);
            } else if constexpr (std::is_same_v<T, core::dsl::IntegerExpr>) {
                return format_integer(e);
            } else if constexpr (std::is_same_v<T, core::dsl::FloatExpr>) {
                return format_float(e);
            } else if constexpr (std::is_same_v<T, core::dsl::BoolExpr>) {
                return format_bool(e);
            } else if constexpr (std::is_same_v<T, core::dsl::BinaryExpr>) {
                return format_binary(e, parent_prec);
            } else if constexpr (std::is_same_v<T, core::dsl::UnaryExpr>) {
                return format_unary(e, parent_prec);
            } else if constexpr (std::is_same_v<T, core::dsl::InterpolatedString>) {
                return format_interpolated(e);
            } else if constexpr (std::is_same_v<T, core::dsl::CallExpr>) {
                return format_call(e);
            } else if constexpr (std::is_same_v<T, core::dsl::GroupExpr>) {
                return format_group(e);
            } else {
                return "/* unknown expression */";
            }
        },
        expr.value);
}

std::string AstFormatter::format_identifier(const core::dsl::IdentifierExpr& expr) {
    return expr.name;
}

std::string AstFormatter::format_member(const core::dsl::MemberExpr& expr) {
    std::string result;
    if (expr.object) {
        result = format_expr(*expr.object, Precedence::Primary);
    }
    result += "." + expr.member;
    return result;
}

std::string AstFormatter::format_string(const core::dsl::StringExpr& expr) {
    return "\"" + escape_string(expr.value) + "\"";
}

std::string AstFormatter::format_integer(const core::dsl::IntegerExpr& expr) {
    return std::to_string(expr.value);
}

std::string AstFormatter::format_float(const core::dsl::FloatExpr& expr) {
    std::ostringstream ss;
    // Use enough precision to round-trip the value
    ss << std::setprecision(17) << expr.value;
    std::string result = ss.str();

    // Ensure there's a decimal point for floats
    if (result.find('.') == std::string::npos && result.find('e') == std::string::npos) {
        result += ".0";
    }

    return result;
}

std::string AstFormatter::format_bool(const core::dsl::BoolExpr& expr) {
    return expr.value ? "true" : "false";
}

std::string AstFormatter::format_binary(const core::dsl::BinaryExpr& expr, Precedence parent_prec) {
    Precedence my_prec = get_precedence(expr.op);

    std::string left_str;
    std::string right_str;

    if (expr.left) {
        // Left associativity: left child needs parens only if strictly lower precedence
        left_str = format_expr(*expr.left, my_prec);
    }

    if (expr.right) {
        // For right child, we need parens if lower OR equal precedence (to preserve associativity)
        // But for commutative ops at same precedence, no parens needed
        // Actually, let's use a slightly higher precedence for right to handle associativity
        Precedence right_context = my_prec;
        // Only increment for non-commutative or to preserve left-to-right semantics
        if (expr.op == core::dsl::BinaryOp::Sub || expr.op == core::dsl::BinaryOp::Div ||
            expr.op == core::dsl::BinaryOp::Mod) {
            // For subtraction/division/mod, right needs higher context to add parens at same level
            right_context = static_cast<Precedence>(static_cast<int>(my_prec) + 1);
        }
        right_str = format_expr(*expr.right, right_context);
    }

    // Determine the operator string with spacing
    std::string op_str;
    switch (expr.op) {
        case core::dsl::BinaryOp::And:
        case core::dsl::BinaryOp::Or:
            // 'and' and 'or' need spaces around them
            op_str = std::string(" ") + core::dsl::to_string(expr.op) + " ";
            break;
        default:
            // All other operators get spaces around them
            op_str = std::string(" ") + core::dsl::to_string(expr.op) + " ";
            break;
    }

    std::string result = left_str + op_str + right_str;

    // Add parentheses if our precedence is lower than parent
    if (needs_parens(my_prec, parent_prec)) {
        result = "(" + result + ")";
    }

    return result;
}

std::string AstFormatter::format_unary(const core::dsl::UnaryExpr& expr, Precedence parent_prec) {
    std::string operand_str;
    if (expr.operand) {
        operand_str = format_expr(*expr.operand, Precedence::Unary);
    }

    std::string result;
    switch (expr.op) {
        case core::dsl::UnaryOp::Not:
            result = "not " + operand_str;
            break;
        case core::dsl::UnaryOp::Neg:
            result = "-" + operand_str;
            break;
    }

    // Unary expressions rarely need parens, but check anyway
    if (needs_parens(Precedence::Unary, parent_prec)) {
        result = "(" + result + ")";
    }

    return result;
}

std::string AstFormatter::format_interpolated(const core::dsl::InterpolatedString& expr) {
    std::string result = "\"";

    for (const auto& segment : expr.segments) {
        // Add text part (escaped)
        result += escape_string(segment.text);

        // Add interpolated expression if present
        if (segment.expr) {
            result += "${" + format_expr(*segment.expr) + "}";
        }
    }

    result += "\"";
    return result;
}

std::string AstFormatter::format_call(const core::dsl::CallExpr& expr) {
    std::string result = expr.callee + "(";

    bool first = true;
    for (const auto& arg : expr.arguments) {
        if (!first) {
            result += ", ";
        }
        result += format_expr(*arg);
        first = false;
    }

    result += ")";
    return result;
}

std::string AstFormatter::format_group(const core::dsl::GroupExpr& expr) {
    // A grouped expression (explicit parentheses in source)
    // We can format the inner expression without additional context
    // since the parens are explicit
    std::string inner;
    if (expr.inner) {
        inner = format_expr(*expr.inner, Precedence::None);
    }
    return "(" + inner + ")";
}

// ============================================================================
// Helpers
// ============================================================================

void AstFormatter::write_indent() {
    if (at_line_start_) {
        for (int i = 0; i < indent_level_; ++i) {
            output_ << indent_str_;
        }
        at_line_start_ = false;
    }
}

void AstFormatter::indent() {
    ++indent_level_;
}

void AstFormatter::dedent() {
    if (indent_level_ > 0) {
        --indent_level_;
    }
}

void AstFormatter::newline() {
    output_ << '\n';
    at_line_start_ = true;
}

void AstFormatter::blank_line() {
    output_ << '\n';
    at_line_start_ = true;
}

Precedence AstFormatter::get_precedence(core::dsl::BinaryOp op) {
    switch (op) {
        case core::dsl::BinaryOp::Or:
            return Precedence::Or;
        case core::dsl::BinaryOp::And:
            return Precedence::And;
        case core::dsl::BinaryOp::Eq:
        case core::dsl::BinaryOp::Ne:
            return Precedence::Equality;
        case core::dsl::BinaryOp::Lt:
        case core::dsl::BinaryOp::Le:
        case core::dsl::BinaryOp::Gt:
        case core::dsl::BinaryOp::Ge:
            return Precedence::Relational;
        case core::dsl::BinaryOp::Add:
        case core::dsl::BinaryOp::Sub:
            return Precedence::Additive;
        case core::dsl::BinaryOp::Mul:
        case core::dsl::BinaryOp::Div:
        case core::dsl::BinaryOp::Mod:
            return Precedence::Multiplicative;
    }
    return Precedence::None;
}

bool AstFormatter::needs_parens(Precedence child_prec, Precedence parent_prec) {
    // Need parentheses if child binds less tightly than parent expects
    return static_cast<int>(child_prec) < static_cast<int>(parent_prec);
}

std::string AstFormatter::escape_string(std::string_view str) {
    std::string result;
    result.reserve(str.size());

    for (char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
                break;
        }
    }

    return result;
}

}  // namespace dotvm::cli
