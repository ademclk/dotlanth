/// @file parser.cpp
/// @brief DSL-001 Recursive descent parser implementation

#include "dotvm/core/dsl/parser.hpp"

#include <charconv>

namespace dotvm::core::dsl {

// ============================================================================
// Public Interface
// ============================================================================

Result<DslModule, DslErrorList> DslParser::parse(std::string_view source) {
    Lexer lexer{source};
    return parse(lexer);
}

Result<DslModule, DslErrorList> DslParser::parse(Lexer& lexer) {
    DslParser parser{lexer};
    DslModule module = parser.parse_module();

    // Combine lexer and parser errors
    for (const auto& err : lexer.errors()) {
        parser.errors_.add(err);
    }

    if (parser.errors_.has_errors()) {
        return parser.errors_;
    }
    return module;
}

DslParser::DslParser(Lexer& lexer) noexcept : lexer_{lexer} {
    // Prime the parser with first token
    current_ = lexer_.next_token();
}

// ============================================================================
// Module Parsing
// ============================================================================

DslModule DslParser::parse_module() {
    DslModule module;
    SourceLocation start = current_.span.start;

    // Skip leading newlines
    skip_newlines();

    while (!at_end()) {
        if (check(TokenType::KwInclude)) {
            if (auto include_def = parse_include()) {
                module.includes.push_back(std::move(*include_def));
            }
        } else if (check(TokenType::KwImport)) {
            if (auto import_def = parse_import()) {
                module.imports.push_back(std::move(*import_def));
            }
        } else if (check(TokenType::KwDot)) {
            if (auto dot_def = parse_dot_def()) {
                module.dots.push_back(std::move(*dot_def));
            }
        } else if (check(TokenType::KwLink)) {
            if (auto link_def = parse_link_def()) {
                module.links.push_back(std::move(*link_def));
            }
        } else if (check_any(TokenType::Newline, TokenType::Indent, TokenType::Dedent)) {
            // Skip structural tokens at module level (blank lines, orphan indentation)
            advance();
        } else {
            error(DslError::UnexpectedToken, "Expected 'include', 'import', 'dot', or 'link'");
            // Advance at least once to make progress
            advance();
            synchronize();
        }
    }

    module.span = SourceSpan::from(start, previous_.span.end);
    return module;
}

std::optional<ImportDef> DslParser::parse_import() {
    Token start = current_;
    advance();  // consume 'import'

    if (!check(TokenType::String)) {
        error(DslError::ExpectedString, "Expected string after 'import'");
        synchronize();
        return std::nullopt;
    }

    ImportDef import_def;
    import_def.path = std::string{current_.lexeme};
    advance();  // consume string

    if (!match(TokenType::Newline) && !at_end()) {
        error(DslError::ExpectedNewline, "Expected newline after import");
    }

    import_def.span = SourceSpan::from(start.span.start, previous_.span.end);
    return import_def;
}

std::optional<IncludeDef> DslParser::parse_include() {
    Token start = current_;
    advance();  // consume 'include'

    if (!expect(TokenType::Colon, "Expected ':' after 'include'")) {
        synchronize();
        return std::nullopt;
    }

    if (!check(TokenType::String)) {
        error(DslError::ExpectedString, "Expected string path after 'include:'");
        synchronize();
        return std::nullopt;
    }

    IncludeDef include_def;
    include_def.path = std::string{current_.lexeme};
    advance();  // consume string

    if (!match(TokenType::Newline) && !at_end()) {
        error(DslError::ExpectedNewline, "Expected newline after include");
    }

    include_def.span = SourceSpan::from(start.span.start, previous_.span.end);
    return include_def;
}

std::optional<DotDef> DslParser::parse_dot_def() {
    Token start = current_;
    advance();  // consume 'dot'

    if (!check(TokenType::Identifier)) {
        error(DslError::ExpectedIdentifier, "Expected identifier after 'dot'");
        synchronize();
        return std::nullopt;
    }

    DotDef dot_def;
    dot_def.name = std::string{current_.lexeme};
    advance();  // consume identifier

    if (!expect(TokenType::Colon, "Expected ':' after dot name")) {
        synchronize();
        return std::nullopt;
    }

    if (!expect(TokenType::Newline, "Expected newline after ':'")) {
        synchronize();
        return std::nullopt;
    }

    if (!expect(TokenType::Indent, "Expected indented block")) {
        synchronize();
        return std::nullopt;
    }

    // Parse dot body (state and triggers)
    while (!check(TokenType::Dedent) && !at_end()) {
        skip_newlines();

        if (check(TokenType::KwState)) {
            if (auto state_def = parse_state_def()) {
                if (dot_def.state.has_value()) {
                    error(DslError::DuplicateState, "Duplicate state block");
                } else {
                    dot_def.state = std::move(*state_def);
                }
            }
        } else if (check(TokenType::KwWhen)) {
            if (auto trigger_def = parse_trigger_def()) {
                dot_def.triggers.push_back(std::move(*trigger_def));
            }
        } else if (check(TokenType::Dedent)) {
            break;
        } else {
            error(DslError::UnexpectedToken, "Expected 'state' or 'when'");
            synchronize();
        }
    }

    if (!match(TokenType::Dedent) && !at_end()) {
        error(DslError::ExpectedDedent, "Expected dedent after dot body");
    }

    dot_def.span = SourceSpan::from(start.span.start, previous_.span.end);
    return dot_def;
}

std::optional<LinkDef> DslParser::parse_link_def() {
    Token start = current_;
    advance();  // consume 'link'

    if (!check(TokenType::Identifier)) {
        error(DslError::ExpectedIdentifier, "Expected source agent name");
        synchronize();
        return std::nullopt;
    }

    LinkDef link_def;
    link_def.source = std::string{current_.lexeme};
    advance();  // consume source identifier

    if (!expect(TokenType::Arrow, "Expected '->' in link")) {
        synchronize();
        return std::nullopt;
    }

    if (!check(TokenType::Identifier)) {
        error(DslError::ExpectedIdentifier, "Expected target agent name");
        synchronize();
        return std::nullopt;
    }

    link_def.target = std::string{current_.lexeme};
    advance();  // consume target identifier

    if (!match(TokenType::Newline) && !at_end()) {
        error(DslError::ExpectedNewline, "Expected newline after link");
    }

    link_def.span = SourceSpan::from(start.span.start, previous_.span.end);
    return link_def;
}

// ============================================================================
// Dot Body Parsing
// ============================================================================

std::optional<StateDef> DslParser::parse_state_def() {
    Token start = current_;
    advance();  // consume 'state'

    if (!expect(TokenType::Colon, "Expected ':' after 'state'")) {
        synchronize();
        return std::nullopt;
    }

    if (!expect(TokenType::Newline, "Expected newline after ':'")) {
        synchronize();
        return std::nullopt;
    }

    if (!expect(TokenType::Indent, "Expected indented block")) {
        synchronize();
        return std::nullopt;
    }

    StateDef state_def;

    while (!check(TokenType::Dedent) && !at_end()) {
        skip_newlines();

        if (check(TokenType::Identifier)) {
            if (auto var = parse_state_var()) {
                state_def.variables.push_back(std::move(*var));
            }
        } else if (check(TokenType::Dedent)) {
            break;
        } else {
            error(DslError::ExpectedIdentifier, "Expected variable name");
            synchronize();
        }
    }

    if (!match(TokenType::Dedent) && !at_end()) {
        error(DslError::ExpectedDedent, "Expected dedent after state block");
    }

    state_def.span = SourceSpan::from(start.span.start, previous_.span.end);
    return state_def;
}

std::optional<StateVar> DslParser::parse_state_var() {
    Token start = current_;

    StateVar var;
    var.name = std::string{current_.lexeme};
    advance();  // consume identifier

    if (!expect(TokenType::Colon, "Expected ':' after variable name")) {
        synchronize();
        return std::nullopt;
    }

    var.value = parse_expression();
    if (!var.value) {
        error(DslError::ExpectedExpression, "Expected expression after ':'");
        synchronize();
        return std::nullopt;
    }

    if (!match(TokenType::Newline) && !at_end()) {
        error(DslError::ExpectedNewline, "Expected newline after state variable");
    }

    var.span = SourceSpan::from(start.span.start, previous_.span.end);
    return var;
}

std::optional<TriggerDef> DslParser::parse_trigger_def() {
    Token start = current_;
    advance();  // consume 'when'

    auto condition = parse_expression();
    if (!condition) {
        error(DslError::ExpectedExpression, "Expected condition after 'when'");
        synchronize();
        return std::nullopt;
    }

    if (!expect(TokenType::Colon, "Expected ':' after condition")) {
        synchronize();
        return std::nullopt;
    }

    if (!expect(TokenType::Newline, "Expected newline after ':'")) {
        synchronize();
        return std::nullopt;
    }

    if (!expect(TokenType::Indent, "Expected indented block")) {
        synchronize();
        return std::nullopt;
    }

    TriggerDef trigger_def;
    trigger_def.condition = std::move(condition);

    skip_newlines();

    if (check(TokenType::KwDo)) {
        if (auto do_block = parse_do_block()) {
            trigger_def.do_block = std::move(*do_block);
        }
    } else {
        error(DslError::ExpectedKeyword, "Expected 'do' in trigger");
        synchronize();
    }

    skip_newlines();

    if (!match(TokenType::Dedent) && !at_end()) {
        error(DslError::ExpectedDedent, "Expected dedent after trigger");
    }

    trigger_def.span = SourceSpan::from(start.span.start, previous_.span.end);
    return trigger_def;
}

std::optional<DoBlock> DslParser::parse_do_block() {
    Token start = current_;
    advance();  // consume 'do'

    if (!expect(TokenType::Colon, "Expected ':' after 'do'")) {
        synchronize();
        return std::nullopt;
    }

    if (!expect(TokenType::Newline, "Expected newline after ':'")) {
        synchronize();
        return std::nullopt;
    }

    if (!expect(TokenType::Indent, "Expected indented block")) {
        synchronize();
        return std::nullopt;
    }

    DoBlock do_block;

    while (!check(TokenType::Dedent) && !at_end()) {
        skip_newlines();

        if (check(TokenType::Dedent)) {
            break;
        }

        if (auto action = parse_action()) {
            do_block.actions.push_back(std::move(*action));
        }
    }

    if (!match(TokenType::Dedent) && !at_end()) {
        error(DslError::ExpectedDedent, "Expected dedent after do block");
    }

    do_block.span = SourceSpan::from(start.span.start, previous_.span.end);
    return do_block;
}

std::optional<ActionStmt> DslParser::parse_action() {
    Token start = current_;

    // Could be: identifier args (call) or identifier.path op= expr (assignment)
    auto expr = parse_expression();
    if (!expr) {
        error(DslError::ExpectedExpression, "Expected action");
        synchronize();
        return std::nullopt;
    }

    // Check for assignment operator
    if (check_any(TokenType::Equals, TokenType::PlusEquals, TokenType::MinusEquals,
                  TokenType::StarEquals, TokenType::SlashEquals)) {
        AssignOp op;
        switch (current_.type) {
            case TokenType::Equals:
                op = AssignOp::Assign;
                break;
            case TokenType::PlusEquals:
                op = AssignOp::AddAssign;
                break;
            case TokenType::MinusEquals:
                op = AssignOp::SubAssign;
                break;
            case TokenType::StarEquals:
                op = AssignOp::MulAssign;
                break;
            case TokenType::SlashEquals:
                op = AssignOp::DivAssign;
                break;
            default:
                op = AssignOp::Assign;
        }
        advance();  // consume operator

        auto value = parse_expression();
        if (!value) {
            error(DslError::ExpectedExpression, "Expected expression after assignment operator");
            synchronize();
            return std::nullopt;
        }

        if (!match(TokenType::Newline) && !at_end()) {
            error(DslError::ExpectedNewline, "Expected newline after assignment");
        }

        return ActionStmt::assignment(std::move(expr), op, std::move(value),
                                      SourceSpan::from(start.span.start, previous_.span.end));
    }

    // It's a call - the first expression should be an identifier
    // and following expressions are arguments
    if (!std::holds_alternative<IdentifierExpr>(expr->value)) {
        // Not an identifier - just a bare expression (like state.counter)
        // This could be valid as an action in some contexts
        if (!match(TokenType::Newline) && !at_end()) {
            error(DslError::ExpectedNewline, "Expected newline after action");
        }

        // Convert to a "call" with the identifier being the expression stringified
        // Actually, this is likely invalid - report error
        error(DslError::InvalidAction, "Invalid action - expected call or assignment");
        return std::nullopt;
    }

    std::string callee = std::get<IdentifierExpr>(expr->value).name;
    std::vector<std::unique_ptr<Expression>> args;

    // Parse arguments until newline
    while (!check(TokenType::Newline) && !at_end()) {
        auto arg = parse_expression();
        if (!arg) {
            break;
        }
        args.push_back(std::move(arg));
    }

    if (!match(TokenType::Newline) && !at_end()) {
        error(DslError::ExpectedNewline, "Expected newline after action");
    }

    return ActionStmt::call(std::move(callee), std::move(args),
                            SourceSpan::from(start.span.start, previous_.span.end));
}

// ============================================================================
// Expression Parsing
// ============================================================================

std::unique_ptr<Expression> DslParser::parse_expression() {
    return parse_or();
}

std::unique_ptr<Expression> DslParser::parse_or() {
    auto left = parse_and();
    if (!left)
        return nullptr;

    while (match(TokenType::KwOr)) {
        auto right = parse_and();
        if (!right) {
            error(DslError::ExpectedExpression, "Expected expression after 'or'");
            return nullptr;
        }

        BinaryExpr bin;
        bin.left = std::move(left);
        bin.op = BinaryOp::Or;
        bin.right = std::move(right);
        bin.span = SourceSpan::from(bin.left->span.start, bin.right->span.end);
        left = Expression::make(std::move(bin));
    }

    return left;
}

std::unique_ptr<Expression> DslParser::parse_and() {
    auto left = parse_equality();
    if (!left)
        return nullptr;

    while (match(TokenType::KwAnd)) {
        auto right = parse_equality();
        if (!right) {
            error(DslError::ExpectedExpression, "Expected expression after 'and'");
            return nullptr;
        }

        BinaryExpr bin;
        bin.left = std::move(left);
        bin.op = BinaryOp::And;
        bin.right = std::move(right);
        bin.span = SourceSpan::from(bin.left->span.start, bin.right->span.end);
        left = Expression::make(std::move(bin));
    }

    return left;
}

std::unique_ptr<Expression> DslParser::parse_equality() {
    auto left = parse_comparison();
    if (!left)
        return nullptr;

    while (match_any(TokenType::EqualEqual, TokenType::NotEqual)) {
        BinaryOp op = (previous_.type == TokenType::EqualEqual) ? BinaryOp::Eq : BinaryOp::Ne;
        auto right = parse_comparison();
        if (!right) {
            error(DslError::ExpectedExpression, "Expected expression after comparison operator");
            return nullptr;
        }

        BinaryExpr bin;
        bin.left = std::move(left);
        bin.op = op;
        bin.right = std::move(right);
        bin.span = SourceSpan::from(bin.left->span.start, bin.right->span.end);
        left = Expression::make(std::move(bin));
    }

    return left;
}

std::unique_ptr<Expression> DslParser::parse_comparison() {
    auto left = parse_term();
    if (!left)
        return nullptr;

    while (match_any(TokenType::Less, TokenType::LessEqual, TokenType::Greater,
                     TokenType::GreaterEqual)) {
        BinaryOp op;
        switch (previous_.type) {
            case TokenType::Less:
                op = BinaryOp::Lt;
                break;
            case TokenType::LessEqual:
                op = BinaryOp::Le;
                break;
            case TokenType::Greater:
                op = BinaryOp::Gt;
                break;
            case TokenType::GreaterEqual:
                op = BinaryOp::Ge;
                break;
            default:
                op = BinaryOp::Lt;
        }

        auto right = parse_term();
        if (!right) {
            error(DslError::ExpectedExpression, "Expected expression after comparison operator");
            return nullptr;
        }

        BinaryExpr bin;
        bin.left = std::move(left);
        bin.op = op;
        bin.right = std::move(right);
        bin.span = SourceSpan::from(bin.left->span.start, bin.right->span.end);
        left = Expression::make(std::move(bin));
    }

    return left;
}

std::unique_ptr<Expression> DslParser::parse_term() {
    auto left = parse_factor();
    if (!left)
        return nullptr;

    while (match_any(TokenType::Plus, TokenType::Minus)) {
        BinaryOp op = (previous_.type == TokenType::Plus) ? BinaryOp::Add : BinaryOp::Sub;
        auto right = parse_factor();
        if (!right) {
            error(DslError::ExpectedExpression, "Expected expression after '+' or '-'");
            return nullptr;
        }

        BinaryExpr bin;
        bin.left = std::move(left);
        bin.op = op;
        bin.right = std::move(right);
        bin.span = SourceSpan::from(bin.left->span.start, bin.right->span.end);
        left = Expression::make(std::move(bin));
    }

    return left;
}

std::unique_ptr<Expression> DslParser::parse_factor() {
    auto left = parse_unary();
    if (!left)
        return nullptr;

    while (match_any(TokenType::Star, TokenType::Slash, TokenType::Percent)) {
        BinaryOp op;
        switch (previous_.type) {
            case TokenType::Star:
                op = BinaryOp::Mul;
                break;
            case TokenType::Slash:
                op = BinaryOp::Div;
                break;
            case TokenType::Percent:
                op = BinaryOp::Mod;
                break;
            default:
                op = BinaryOp::Mul;
        }

        auto right = parse_unary();
        if (!right) {
            error(DslError::ExpectedExpression, "Expected expression after '*', '/', or '%'");
            return nullptr;
        }

        BinaryExpr bin;
        bin.left = std::move(left);
        bin.op = op;
        bin.right = std::move(right);
        bin.span = SourceSpan::from(bin.left->span.start, bin.right->span.end);
        left = Expression::make(std::move(bin));
    }

    return left;
}

std::unique_ptr<Expression> DslParser::parse_unary() {
    if (match(TokenType::Minus)) {
        Token op_token = previous_;
        auto operand = parse_unary();
        if (!operand) {
            error(DslError::ExpectedExpression, "Expected expression after '-'");
            return nullptr;
        }

        UnaryExpr un;
        un.op = UnaryOp::Neg;
        un.operand = std::move(operand);
        un.span = SourceSpan::from(op_token.span.start, un.operand->span.end);
        return Expression::make(std::move(un));
    }

    if (match(TokenType::KwNot)) {
        Token op_token = previous_;
        auto operand = parse_unary();
        if (!operand) {
            error(DslError::ExpectedExpression, "Expected expression after 'not'");
            return nullptr;
        }

        UnaryExpr un;
        un.op = UnaryOp::Not;
        un.operand = std::move(operand);
        un.span = SourceSpan::from(op_token.span.start, un.operand->span.end);
        return Expression::make(std::move(un));
    }

    return parse_primary();
}

std::unique_ptr<Expression> DslParser::parse_primary() {
    // Boolean literals
    if (match(TokenType::KwTrue)) {
        BoolExpr b;
        b.value = true;
        b.span = previous_.span;
        return Expression::make(std::move(b));
    }

    if (match(TokenType::KwFalse)) {
        BoolExpr b;
        b.value = false;
        b.span = previous_.span;
        return Expression::make(std::move(b));
    }

    // Integer literal
    if (match(TokenType::Integer)) {
        IntegerExpr i;
        i.span = previous_.span;
        auto result = std::from_chars(previous_.lexeme.data(),
                                      previous_.lexeme.data() + previous_.lexeme.size(), i.value);
        if (result.ec != std::errc{}) {
            error(DslError::InvalidNumber, "Invalid integer literal");
            return nullptr;
        }
        return Expression::make(std::move(i));
    }

    // Float literal
    if (match(TokenType::Float)) {
        FloatExpr f;
        f.span = previous_.span;
        // std::from_chars for float may not be available on all platforms
        // Use a simple conversion
        std::string num_str{previous_.lexeme};
        try {
            f.value = std::stod(num_str);
        } catch (...) {
            error(DslError::InvalidNumber, "Invalid float literal");
            return nullptr;
        }
        return Expression::make(std::move(f));
    }

    // String literal
    if (match(TokenType::String)) {
        StringExpr s;
        s.value = std::string{previous_.lexeme};
        s.span = previous_.span;
        return Expression::make(std::move(s));
    }

    // Interpolated string
    if (check(TokenType::StringStart)) {
        return parse_interpolated_string();
    }

    // Identifier (and member access)
    // Also allow 'state' keyword as identifier in expressions (for state.xxx access)
    if (match_any(TokenType::Identifier, TokenType::KwState)) {
        IdentifierExpr id;
        id.name = std::string{previous_.lexeme};
        id.span = previous_.span;
        auto expr = Expression::make(std::move(id));
        return parse_postfix(std::move(expr));
    }

    // Grouped expression
    if (match(TokenType::LParen)) {
        Token start = previous_;
        auto inner = parse_expression();
        if (!inner) {
            error(DslError::ExpectedExpression, "Expected expression after '('");
            return nullptr;
        }

        if (!expect(TokenType::RParen, "Expected ')' after expression")) {
            return nullptr;
        }

        GroupExpr g;
        g.inner = std::move(inner);
        g.span = SourceSpan::from(start.span.start, previous_.span.end);
        return Expression::make(std::move(g));
    }

    // No valid primary expression found
    return nullptr;
}

std::unique_ptr<Expression> DslParser::parse_postfix(std::unique_ptr<Expression> left) {
    while (match(TokenType::Dot)) {
        if (!check(TokenType::Identifier)) {
            error(DslError::ExpectedIdentifier, "Expected identifier after '.'");
            return nullptr;
        }

        MemberExpr mem;
        mem.object = std::move(left);
        mem.member = std::string{current_.lexeme};
        advance();  // consume identifier
        mem.span = SourceSpan::from(mem.object->span.start, previous_.span.end);
        left = Expression::make(std::move(mem));
    }

    return left;
}

std::unique_ptr<Expression> DslParser::parse_interpolated_string() {
    Token start = current_;
    advance();  // consume StringStart

    InterpolatedString interp;
    interp.span.start = start.span.start;

    // First segment
    InterpolationSegment seg;
    seg.text = std::string{previous_.lexeme};

    // Parse the interpolated expression
    seg.expr = parse_expression();
    if (!seg.expr) {
        error(DslError::ExpectedExpression, "Expected expression in interpolation");
        return nullptr;
    }

    interp.segments.push_back(std::move(seg));

    // Continue parsing middle and end parts
    while (check_any(TokenType::StringMiddle, TokenType::StringEnd)) {
        if (match(TokenType::StringEnd)) {
            InterpolationSegment end_seg;
            end_seg.text = std::string{previous_.lexeme};
            // No expression for final segment
            interp.segments.push_back(std::move(end_seg));
            break;
        }

        if (match(TokenType::StringMiddle)) {
            InterpolationSegment mid_seg;
            mid_seg.text = std::string{previous_.lexeme};
            mid_seg.expr = parse_expression();
            if (!mid_seg.expr) {
                error(DslError::ExpectedExpression, "Expected expression in interpolation");
                return nullptr;
            }
            interp.segments.push_back(std::move(mid_seg));
        }
    }

    interp.span.end = previous_.span.end;
    return Expression::make(std::move(interp));
}

// ============================================================================
// Token Helpers
// ============================================================================

const Token& DslParser::current() const noexcept {
    return current_;
}

TokenType DslParser::peek() const noexcept {
    return current_.type;
}

Token DslParser::advance() {
    previous_ = current_;
    if (!at_end()) {
        current_ = lexer_.next_token();
    }
    return previous_;
}

bool DslParser::check(TokenType type) const noexcept {
    return current_.type == type;
}

bool DslParser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool DslParser::expect(TokenType type, std::string_view error_msg) {
    if (check(type)) {
        advance();
        return true;
    }
    error(DslError::UnexpectedToken, error_msg);
    return false;
}

bool DslParser::at_end() const noexcept {
    return current_.type == TokenType::Eof;
}

// ============================================================================
// Error Handling
// ============================================================================

void DslParser::error(DslError code, std::string_view msg) {
    error_at(current_, code, msg);
}

void DslParser::error_at(const Token& token, DslError code, std::string_view msg) {
    if (panic_mode_)
        return;  // Suppress cascading errors
    panic_mode_ = true;
    errors_.add(code, token.span, msg);
}

void DslParser::synchronize() {
    panic_mode_ = false;

    while (!at_end()) {
        // Stop at statement boundaries
        if (previous_.type == TokenType::Newline) {
            return;
        }

        // Stop at keywords that start statements
        switch (current_.type) {
            case TokenType::KwDot:
            case TokenType::KwWhen:
            case TokenType::KwDo:
            case TokenType::KwState:
            case TokenType::KwLink:
            case TokenType::KwImport:
            case TokenType::KwInclude:
            case TokenType::Dedent:
                return;
            default:
                advance();
        }
    }
}

void DslParser::skip_newlines() {
    while (match(TokenType::Newline)) {
        // Skip
    }
}

}  // namespace dotvm::core::dsl
