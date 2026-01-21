#pragma once

/// @file formatter.hpp
/// @brief DSL-003 Source code formatter
///
/// Auto-formats DSL source code with consistent indentation and style.
/// Uses AST-based formatting for semantic awareness.

#include <sstream>
#include <string>
#include <string_view>

#include "dotvm/core/dsl/ast.hpp"

namespace dotvm::cli {

/// @brief Formatting options
struct FormatConfig {
    std::size_t indent_size = 4;       ///< Number of spaces per indent level
    bool use_tabs = false;             ///< Use tabs instead of spaces
    bool trim_trailing = true;         ///< Trim trailing whitespace
    bool ensure_final_newline = true;  ///< Ensure file ends with newline
};

/// @brief DSL source code formatter
///
/// Formats DSL source code with consistent style.
/// NOTE: This class is kept for backward compatibility but the primary
/// formatting is done through AstFormatter.
class Formatter {
public:
    /// @brief Construct formatter with default config
    Formatter() = default;

    /// @brief Construct formatter with custom config
    explicit Formatter(FormatConfig config);

    /// @brief Format DSL source code
    /// @param source Input source code
    /// @return Formatted source code
    [[nodiscard]] std::string format(std::string_view source) const;

    /// @brief Get the current configuration
    [[nodiscard]] const FormatConfig& config() const noexcept { return config_; }

    /// @brief Set the configuration
    void set_config(FormatConfig config) { config_ = std::move(config); }

private:
    FormatConfig config_;
};

/// @brief Expression precedence levels for minimal parentheses
///
/// Higher numbers = higher precedence (bind tighter)
enum class Precedence : int {
    None = 0,
    Or = 1,              // or
    And = 2,             // and
    Equality = 3,        // == !=
    Relational = 4,      // < <= > >=
    Additive = 5,        // + -
    Multiplicative = 6,  // * / %
    Unary = 7,           // not -
    Primary = 8          // identifiers, literals, grouped
};

/// @brief AST-based source code formatter
///
/// Traverses the AST and regenerates formatted source code with:
/// - Consistent 4-space indentation
/// - One blank line between top-level definitions
/// - Minimal parentheses (only where needed for precedence)
/// - Normalized spacing around operators
/// - Double quotes for strings
class AstFormatter {
public:
    /// @brief Construct formatter with default config
    AstFormatter() = default;

    /// @brief Construct formatter with custom config
    explicit AstFormatter(FormatConfig config);

    /// @brief Format a parsed module back to source code
    /// @param module The parsed AST module
    /// @return Formatted source code
    [[nodiscard]] std::string format(const core::dsl::DslModule& module);

    /// @brief Get the current configuration
    [[nodiscard]] const FormatConfig& config() const noexcept { return config_; }

    /// @brief Set the configuration
    void set_config(FormatConfig config) { config_ = std::move(config); }

private:
    // Node formatters
    void format_module(const core::dsl::DslModule& module);
    void format_include(const core::dsl::IncludeDef& include);
    void format_import(const core::dsl::ImportDef& import);
    void format_dot(const core::dsl::DotDef& dot);
    void format_state(const core::dsl::StateDef& state);
    void format_state_var(const core::dsl::StateVar& var);
    void format_trigger(const core::dsl::TriggerDef& trigger);
    void format_do_block(const core::dsl::DoBlock& do_block);
    void format_action(const core::dsl::ActionStmt& action);
    void format_link(const core::dsl::LinkDef& link);

    // Expression formatter with precedence handling
    [[nodiscard]] std::string format_expr(const core::dsl::Expression& expr,
                                          Precedence parent_prec = Precedence::None);

    // Expression variant formatters
    [[nodiscard]] std::string format_identifier(const core::dsl::IdentifierExpr& expr);
    [[nodiscard]] std::string format_member(const core::dsl::MemberExpr& expr);
    [[nodiscard]] std::string format_string(const core::dsl::StringExpr& expr);
    [[nodiscard]] std::string format_integer(const core::dsl::IntegerExpr& expr);
    [[nodiscard]] std::string format_float(const core::dsl::FloatExpr& expr);
    [[nodiscard]] std::string format_bool(const core::dsl::BoolExpr& expr);
    [[nodiscard]] std::string format_binary(const core::dsl::BinaryExpr& expr,
                                            Precedence parent_prec);
    [[nodiscard]] std::string format_unary(const core::dsl::UnaryExpr& expr,
                                           Precedence parent_prec);
    [[nodiscard]] std::string format_interpolated(const core::dsl::InterpolatedString& expr);
    [[nodiscard]] std::string format_call(const core::dsl::CallExpr& expr);
    [[nodiscard]] std::string format_group(const core::dsl::GroupExpr& expr);

    // Helpers
    void write_indent();
    void indent();
    void dedent();
    void newline();
    void blank_line();

    // Precedence helpers
    [[nodiscard]] static Precedence get_precedence(core::dsl::BinaryOp op);
    [[nodiscard]] static bool needs_parens(Precedence child_prec, Precedence parent_prec);

    // String escape helper
    [[nodiscard]] static std::string escape_string(std::string_view str);

    std::ostringstream output_;
    int indent_level_ = 0;
    std::string indent_str_;  // Computed from config
    FormatConfig config_;
    bool at_line_start_ = true;  // Track if we're at start of a line
};

}  // namespace dotvm::cli
