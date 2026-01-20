/// @file lexer.cpp
/// @brief DSL-001 Indentation-aware lexer implementation

#include "dotvm/core/dsl/lexer.hpp"

#include <cctype>

namespace dotvm::core::dsl {

// ============================================================================
// Constructor
// ============================================================================

Lexer::Lexer(std::string_view source) noexcept : source_{source}, token_start_loc_{1, 1, 0} {
    // Start with base indentation level of 0
    indent_stack_.reserve(MAX_INDENT_DEPTH);
    indent_stack_.push_back(0);
}

// ============================================================================
// Character Classification
// ============================================================================

bool Lexer::is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool Lexer::is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

bool Lexer::is_alnum(char c) noexcept { return is_alpha(c) || is_digit(c); }

bool Lexer::is_whitespace(char c) noexcept { return c == ' ' || c == '\t' || c == '\r'; }

// ============================================================================
// Basic Scanning
// ============================================================================

char Lexer::peek_char() const noexcept {
    if (pos_ >= source_.size()) return '\0';
    return source_[pos_];
}

char Lexer::peek_char(std::size_t n) const noexcept {
    if (pos_ + n >= source_.size()) return '\0';
    return source_[pos_ + n];
}

char Lexer::advance() noexcept {
    if (pos_ >= source_.size()) return '\0';
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

bool Lexer::match(char expected) noexcept {
    if (pos_ >= source_.size() || source_[pos_] != expected) return false;
    advance();
    return true;
}

void Lexer::skip_comment() noexcept {
    while (pos_ < source_.size() && source_[pos_] != '\n') {
        advance();
    }
}

// ============================================================================
// Public Interface
// ============================================================================

bool Lexer::at_end() const noexcept { return pos_ >= source_.size() && pending_dedents_ == 0; }

SourceLocation Lexer::location() const noexcept {
    return SourceLocation{line_, column_, pos_};
}

const Token& Lexer::peek() {
    if (!has_peeked_) {
        peeked_token_ = next_token();
        has_peeked_ = true;
    }
    return peeked_token_;
}

Token Lexer::next_token() {
    // Return peeked token if available
    if (has_peeked_) {
        has_peeked_ = false;
        return peeked_token_;
    }

    // Emit pending DEDENTs
    if (pending_dedents_ > 0) {
        return emit_indent_token();
    }

    // Emit pending INDENT
    if (pending_indent_) {
        pending_indent_ = false;
        return Token::make(TokenType::Indent, SourceSpan::at(token_start_loc_), "");
    }

    // Process indentation at start of line
    if (at_line_start_) {
        process_indentation();
        if (pending_dedents_ > 0 || pending_indent_) {
            return next_token();
        }
    }

    // Skip whitespace (but not newlines)
    while (pos_ < source_.size() && is_whitespace(source_[pos_])) {
        advance();
    }

    // Record token start
    token_start_ = pos_;
    token_start_loc_ = location();

    // Check for end of file
    if (pos_ >= source_.size()) {
        // Emit any remaining DEDENTs at EOF
        while (indent_stack_.size() > 1) {
            indent_stack_.pop_back();
            pending_dedents_++;
        }
        if (pending_dedents_ > 0) {
            return emit_indent_token();
        }
        return Token::eof(token_start_loc_);
    }

    char c = advance();

    // Skip comments
    if (c == '#') {
        skip_comment();
        // Try again after comment
        return next_token();
    }

    // Newline
    if (c == '\n') {
        at_line_start_ = true;
        return Token::make(TokenType::Newline, SourceSpan::from(token_start_loc_, location()), "\n");
    }

    // Identifier or keyword
    if (is_alpha(c)) {
        return scan_identifier();
    }

    // Number
    if (is_digit(c)) {
        return scan_number();
    }

    // String
    if (c == '"') {
        return scan_string();
    }

    // Two-character operators
    switch (c) {
        case '-':
            if (match('>')) {
                return Token::make(TokenType::Arrow, SourceSpan::from(token_start_loc_, location()),
                                   "->");
            }
            if (match('=')) {
                return Token::make(TokenType::MinusEquals,
                                   SourceSpan::from(token_start_loc_, location()), "-=");
            }
            return Token::make(TokenType::Minus, SourceSpan::from(token_start_loc_, location()),
                               "-");

        case '+':
            if (match('=')) {
                return Token::make(TokenType::PlusEquals,
                                   SourceSpan::from(token_start_loc_, location()), "+=");
            }
            return Token::make(TokenType::Plus, SourceSpan::from(token_start_loc_, location()), "+");

        case '*':
            if (match('=')) {
                return Token::make(TokenType::StarEquals,
                                   SourceSpan::from(token_start_loc_, location()), "*=");
            }
            return Token::make(TokenType::Star, SourceSpan::from(token_start_loc_, location()), "*");

        case '/':
            if (match('=')) {
                return Token::make(TokenType::SlashEquals,
                                   SourceSpan::from(token_start_loc_, location()), "/=");
            }
            return Token::make(TokenType::Slash, SourceSpan::from(token_start_loc_, location()),
                               "/");

        case '=':
            if (match('=')) {
                return Token::make(TokenType::EqualEqual,
                                   SourceSpan::from(token_start_loc_, location()), "==");
            }
            return Token::make(TokenType::Equals, SourceSpan::from(token_start_loc_, location()),
                               "=");

        case '!':
            if (match('=')) {
                return Token::make(TokenType::NotEqual,
                                   SourceSpan::from(token_start_loc_, location()), "!=");
            }
            report_error(DslError::UnexpectedCharacter, "Expected '=' after '!'");
            return error_token();

        case '<':
            if (match('=')) {
                return Token::make(TokenType::LessEqual,
                                   SourceSpan::from(token_start_loc_, location()), "<=");
            }
            return Token::make(TokenType::Less, SourceSpan::from(token_start_loc_, location()), "<");

        case '>':
            if (match('=')) {
                return Token::make(TokenType::GreaterEqual,
                                   SourceSpan::from(token_start_loc_, location()), ">=");
            }
            return Token::make(TokenType::Greater, SourceSpan::from(token_start_loc_, location()),
                               ">");

        // Single-character tokens
        case ':':
            return Token::make(TokenType::Colon, SourceSpan::from(token_start_loc_, location()),
                               ":");
        case ',':
            return Token::make(TokenType::Comma, SourceSpan::from(token_start_loc_, location()),
                               ",");
        case '.':
            return Token::make(TokenType::Dot, SourceSpan::from(token_start_loc_, location()), ".");
        case '(':
            return Token::make(TokenType::LParen, SourceSpan::from(token_start_loc_, location()),
                               "(");
        case ')':
            return Token::make(TokenType::RParen, SourceSpan::from(token_start_loc_, location()),
                               ")");
        case '%':
            return Token::make(TokenType::Percent, SourceSpan::from(token_start_loc_, location()),
                               "%");

        case '}':
            // End of interpolation
            if (in_interpolation_ && interpolation_depth_ > 0) {
                interpolation_depth_--;
                if (interpolation_depth_ == 0) {
                    in_interpolation_ = false;
                    return continue_string();
                }
            }
            report_error(DslError::UnexpectedCharacter, "Unexpected '}'");
            return error_token();

        default:
            report_error(DslError::UnexpectedCharacter);
            return error_token();
    }
}

// ============================================================================
// Token Scanning
// ============================================================================

Token Lexer::scan_identifier() {
    while (pos_ < source_.size() && is_alnum(source_[pos_])) {
        advance();
    }

    std::string_view text = source_.substr(token_start_, pos_ - token_start_);
    TokenType type = lookup_keyword(text);

    return Token::make(type, SourceSpan::from(token_start_loc_, location()), text);
}

Token Lexer::scan_number() {
    // Scan integer part
    while (pos_ < source_.size() && is_digit(source_[pos_])) {
        advance();
    }

    // Check for fractional part
    bool is_float = false;
    if (pos_ < source_.size() && source_[pos_] == '.' && pos_ + 1 < source_.size() &&
        is_digit(source_[pos_ + 1])) {
        is_float = true;
        advance();  // consume '.'
        while (pos_ < source_.size() && is_digit(source_[pos_])) {
            advance();
        }
    }

    // Check for exponent
    if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')) {
        is_float = true;
        advance();  // consume 'e' or 'E'
        if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) {
            advance();  // consume sign
        }
        if (pos_ >= source_.size() || !is_digit(source_[pos_])) {
            report_error(DslError::InvalidNumber, "Expected digit after exponent");
            return error_token();
        }
        while (pos_ < source_.size() && is_digit(source_[pos_])) {
            advance();
        }
    }

    std::string_view text = source_.substr(token_start_, pos_ - token_start_);
    TokenType type = is_float ? TokenType::Float : TokenType::Integer;

    return Token::make(type, SourceSpan::from(token_start_loc_, location()), text);
}

Token Lexer::scan_string() {
    std::size_t content_start = pos_;
    bool has_interpolation = false;

    while (pos_ < source_.size()) {
        char c = source_[pos_];

        if (c == '"') {
            // End of string
            std::string_view text = source_.substr(content_start, pos_ - content_start);
            advance();  // consume closing quote

            if (has_interpolation) {
                return Token::make(TokenType::StringEnd,
                                   SourceSpan::from(token_start_loc_, location()), text);
            } else {
                return Token::make(TokenType::String,
                                   SourceSpan::from(token_start_loc_, location()), text);
            }
        }

        if (c == '\n') {
            report_error(DslError::UnterminatedString, "Newline in string literal");
            return error_token();
        }

        if (c == '\\') {
            // Escape sequence
            advance();
            if (pos_ >= source_.size()) {
                report_error(DslError::UnterminatedString, "Unexpected end of string");
                return error_token();
            }
            char next = source_[pos_];
            if (next != 'n' && next != 't' && next != 'r' && next != '\\' && next != '"' &&
                next != '$' && next != '{' && next != '}') {
                report_error(DslError::InvalidEscapeSequence);
                return error_token();
            }
            advance();
            continue;
        }

        if (c == '$' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '{') {
            // Start of interpolation
            std::string_view text = source_.substr(content_start, pos_ - content_start);
            advance();  // consume '$'
            advance();  // consume '{'
            in_interpolation_ = true;
            interpolation_depth_ = 1;

            if (has_interpolation) {
                return Token::make(TokenType::StringMiddle,
                                   SourceSpan::from(token_start_loc_, location()), text);
            } else {
                has_interpolation = true;
                return Token::make(TokenType::StringStart,
                                   SourceSpan::from(token_start_loc_, location()), text);
            }
        }

        advance();
    }

    report_error(DslError::UnterminatedString);
    return error_token();
}

Token Lexer::continue_string() {
    token_start_ = pos_;
    token_start_loc_ = location();

    std::size_t content_start = pos_;

    while (pos_ < source_.size()) {
        char c = source_[pos_];

        if (c == '"') {
            std::string_view text = source_.substr(content_start, pos_ - content_start);
            advance();  // consume closing quote
            return Token::make(TokenType::StringEnd, SourceSpan::from(token_start_loc_, location()),
                               text);
        }

        if (c == '\n') {
            report_error(DslError::UnterminatedString, "Newline in string literal");
            return error_token();
        }

        if (c == '\\') {
            advance();
            if (pos_ >= source_.size()) {
                report_error(DslError::UnterminatedString);
                return error_token();
            }
            advance();
            continue;
        }

        if (c == '$' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '{') {
            std::string_view text = source_.substr(content_start, pos_ - content_start);
            advance();  // consume '$'
            advance();  // consume '{'
            in_interpolation_ = true;
            interpolation_depth_ = 1;
            return Token::make(TokenType::StringMiddle,
                               SourceSpan::from(token_start_loc_, location()), text);
        }

        advance();
    }

    report_error(DslError::UnterminatedString);
    return error_token();
}

TokenType Lexer::lookup_keyword(std::string_view text) const noexcept {
    // Fast keyword lookup using switch on (length << 8) | first_char
    // This provides O(1) lookup without hash tables
    if (text.empty()) return TokenType::Identifier;

    std::size_t len = text.size();
    char first = text[0];

    // Compute key: (length << 8) | first_char
    std::uint32_t key = (static_cast<std::uint32_t>(len) << 8) | static_cast<std::uint8_t>(first);

    switch (key) {
        // Length 2
        case (2 << 8) | 'd':  // "do"
            if (text == "do") return TokenType::KwDo;
            break;
        case (2 << 8) | 'o':  // "or"
            if (text == "or") return TokenType::KwOr;
            break;

        // Length 3
        case (3 << 8) | 'd':  // "dot"
            if (text == "dot") return TokenType::KwDot;
            break;
        case (3 << 8) | 'a':  // "and"
            if (text == "and") return TokenType::KwAnd;
            break;
        case (3 << 8) | 'n':  // "not"
            if (text == "not") return TokenType::KwNot;
            break;

        // Length 4
        case (4 << 8) | 'w':  // "when"
            if (text == "when") return TokenType::KwWhen;
            break;
        case (4 << 8) | 'l':  // "link"
            if (text == "link") return TokenType::KwLink;
            break;
        case (4 << 8) | 't':  // "true"
            if (text == "true") return TokenType::KwTrue;
            break;

        // Length 5
        case (5 << 8) | 's':  // "state"
            if (text == "state") return TokenType::KwState;
            break;
        case (5 << 8) | 'f':  // "false"
            if (text == "false") return TokenType::KwFalse;
            break;

        // Length 6
        case (6 << 8) | 'i':  // "import"
            if (text == "import") return TokenType::KwImport;
            break;

        default:
            break;
    }

    return TokenType::Identifier;
}

// ============================================================================
// Indentation Handling
// ============================================================================

void Lexer::process_indentation() {
    at_line_start_ = false;

    // Count leading whitespace
    std::size_t indent = 0;
    std::size_t start_pos = pos_;

    while (pos_ < source_.size()) {
        char c = source_[pos_];
        if (c == ' ') {
            indent++;
            advance();
        } else if (c == '\t') {
            // Tab counts as 4 spaces (configurable in the future)
            indent += 4;
            advance();
        } else {
            break;
        }
    }

    // Skip blank lines and EOF - don't process indentation changes
    if (pos_ >= source_.size()) {
        return;
    }
    char c = source_[pos_];
    if (c == '\n') {
        // Blank line - don't process indentation changes
        return;
    }

    // Compare with current indentation level
    // Note: We still process indentation for comment-only lines so that
    // INDENT/DEDENT tokens are properly emitted before the comment.
    std::size_t current = current_indent();

    if (indent > current) {
        // Indentation increased - push new level and emit INDENT
        if (indent_stack_.size() >= MAX_INDENT_DEPTH) {
            report_error(DslError::NestingTooDeep, "Maximum indentation depth exceeded");
            return;
        }
        indent_stack_.push_back(indent);
        pending_indent_ = true;
        token_start_loc_ = SourceLocation::at(line_, 1, start_pos);
    } else if (indent < current) {
        // Indentation decreased - pop levels and emit DEDENTs
        token_start_loc_ = SourceLocation::at(line_, 1, start_pos);
        while (indent_stack_.size() > 1 && indent < indent_stack_.back()) {
            indent_stack_.pop_back();
            pending_dedents_++;
        }

        // Check for inconsistent indentation
        if (indent != indent_stack_.back()) {
            report_error(DslError::InvalidIndentation,
                         "Indentation does not match any outer level");
        }
    }
    // If indent == current, no change needed
}

std::size_t Lexer::current_indent() const noexcept {
    return indent_stack_.empty() ? 0 : indent_stack_.back();
}

Token Lexer::emit_indent_token() {
    if (pending_dedents_ > 0) {
        pending_dedents_--;
        return Token::make(TokenType::Dedent, SourceSpan::at(token_start_loc_), "");
    }
    return Token::eof(location());
}

// ============================================================================
// Error Handling
// ============================================================================

void Lexer::report_error(DslError code, std::string_view msg) {
    errors_.add(code, SourceSpan::from(token_start_loc_, location()), msg);
}

Token Lexer::error_token(std::string_view msg) {
    std::string_view lexeme = source_.substr(token_start_, pos_ - token_start_);
    return Token::error(SourceSpan::from(token_start_loc_, location()), lexeme);
}

}  // namespace dotvm::core::dsl
