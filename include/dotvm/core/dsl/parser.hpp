#pragma once

/// @file parser.hpp
/// @brief DSL-001 Recursive descent parser for the DotLanth DSL
///
/// Parses token stream into AST with panic-mode error recovery.
/// Grammar:
///   module → (import | dot_def | link_def)* EOF
///   import → 'import' STRING NEWLINE
///   dot_def → 'dot' IDENTIFIER ':' NEWLINE INDENT (state_def | trigger_def)* DEDENT
///   state_def → 'state' ':' NEWLINE INDENT state_var* DEDENT
///   state_var → IDENTIFIER ':' expression NEWLINE
///   trigger_def → 'when' expression ':' NEWLINE INDENT do_block DEDENT
///   do_block → 'do' ':' NEWLINE INDENT action* DEDENT
///   action → IDENTIFIER expression* NEWLINE | assignment NEWLINE
///   assignment → member_expr assign_op expression
///   link_def → 'link' IDENTIFIER '->' IDENTIFIER NEWLINE
///
/// Expression precedence (lowest to highest):
///   or → and → equality → comparison → term → factor → unary → primary

#include <string_view>

#include "dotvm/core/dsl/ast.hpp"
#include "dotvm/core/dsl/dsl_error.hpp"
#include "dotvm/core/dsl/lexer.hpp"
#include "dotvm/core/result.hpp"

namespace dotvm::core::dsl {

/// @brief Recursive descent parser for the DotLanth DSL
///
/// Parses a token stream into an AST. Uses panic-mode error recovery
/// to collect multiple errors in a single parse pass.
class DslParser {
public:
    /// @brief Parse DSL source code
    /// @param source DSL source text
    /// @return Parsed module or error list
    [[nodiscard]] static Result<DslModule, DslErrorList> parse(std::string_view source);

    /// @brief Parse from an existing lexer
    /// @param lexer Lexer to consume tokens from
    /// @return Parsed module or error list
    [[nodiscard]] static Result<DslModule, DslErrorList> parse(Lexer& lexer);

private:
    explicit DslParser(Lexer& lexer) noexcept;

    // ===== Module Parsing =====

    /// Parse the complete module
    [[nodiscard]] DslModule parse_module();

    /// Parse an import statement
    [[nodiscard]] std::optional<ImportDef> parse_import();

    /// Parse an include directive
    [[nodiscard]] std::optional<IncludeDef> parse_include();

    /// Parse a dot definition
    [[nodiscard]] std::optional<DotDef> parse_dot_def();

    /// Parse a link definition
    [[nodiscard]] std::optional<LinkDef> parse_link_def();

    // ===== Dot Body Parsing =====

    /// Parse a state block
    [[nodiscard]] std::optional<StateDef> parse_state_def();

    /// Parse a state variable
    [[nodiscard]] std::optional<StateVar> parse_state_var();

    /// Parse a trigger definition
    [[nodiscard]] std::optional<TriggerDef> parse_trigger_def();

    /// Parse a do block
    [[nodiscard]] std::optional<DoBlock> parse_do_block();

    /// Parse an action statement
    [[nodiscard]] std::optional<ActionStmt> parse_action();

    // ===== Expression Parsing (precedence climbing) =====

    /// Parse any expression
    [[nodiscard]] std::unique_ptr<Expression> parse_expression();

    /// Parse 'or' expression (lowest precedence)
    [[nodiscard]] std::unique_ptr<Expression> parse_or();

    /// Parse 'and' expression
    [[nodiscard]] std::unique_ptr<Expression> parse_and();

    /// Parse equality expression (== !=)
    [[nodiscard]] std::unique_ptr<Expression> parse_equality();

    /// Parse comparison expression (< <= > >=)
    [[nodiscard]] std::unique_ptr<Expression> parse_comparison();

    /// Parse term expression (+ -)
    [[nodiscard]] std::unique_ptr<Expression> parse_term();

    /// Parse factor expression (* / %)
    [[nodiscard]] std::unique_ptr<Expression> parse_factor();

    /// Parse unary expression (- not)
    [[nodiscard]] std::unique_ptr<Expression> parse_unary();

    /// Parse primary expression (literals, identifiers, grouped)
    [[nodiscard]] std::unique_ptr<Expression> parse_primary();

    /// Parse member access (a.b.c)
    [[nodiscard]] std::unique_ptr<Expression> parse_postfix(std::unique_ptr<Expression> left);

    /// Parse interpolated string
    [[nodiscard]] std::unique_ptr<Expression> parse_interpolated_string();

    // ===== Token Helpers =====

    /// Get current token
    [[nodiscard]] const Token& current() const noexcept;

    /// Peek at current token type
    [[nodiscard]] TokenType peek() const noexcept;

    /// Advance to next token
    Token advance();

    /// Check if current token matches type
    [[nodiscard]] bool check(TokenType type) const noexcept;

    /// Check if current token matches any of the types
    template <typename... Types>
    [[nodiscard]] bool check_any(Types... types) const noexcept;

    /// Consume token if it matches, return true
    bool match(TokenType type);

    /// Consume token if it matches any, return true
    template <typename... Types>
    bool match_any(Types... types);

    /// Expect a specific token type, record error if not
    bool expect(TokenType type, std::string_view error_msg = "");

    /// Check if at end of file
    [[nodiscard]] bool at_end() const noexcept;

    // ===== Error Handling =====

    /// Record an error
    void error(DslError code, std::string_view msg = "");

    /// Record an error at specific location
    void error_at(const Token& token, DslError code, std::string_view msg = "");

    /// Synchronize after error (panic mode recovery)
    void synchronize();

    /// Skip newlines (for error recovery)
    void skip_newlines();

    // ===== State =====

    /// Lexer providing tokens
    Lexer& lexer_;

    /// Current token
    Token current_;

    /// Previous token (for span tracking)
    Token previous_;

    /// Error list
    DslErrorList errors_;

    /// Whether we're in panic mode (recovering from error)
    bool panic_mode_{false};
};

// ===== Template Implementation =====

template <typename... Types>
bool DslParser::check_any(Types... types) const noexcept {
    return ((current_.type == types) || ...);
}

template <typename... Types>
bool DslParser::match_any(Types... types) {
    if (check_any(types...)) {
        advance();
        return true;
    }
    return false;
}

}  // namespace dotvm::core::dsl
