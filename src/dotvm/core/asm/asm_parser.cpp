/// @file asm_parser.cpp
/// @brief TOOL-004 Assembly parser implementation

#include "dotvm/core/asm/asm_parser.hpp"

#include <charconv>

namespace dotvm::core::asm_ {

// ============================================================================
// Constructor
// ============================================================================

AsmParser::AsmParser(std::string_view source, AsmParserConfig config)
    : lexer_{std::make_unique<AsmLexer>(source)}, config_{std::move(config)}, source_{source} {
    // Initialize current token
    current_ = lexer_->next_token();
}

// ============================================================================
// Public Interface
// ============================================================================

AsmParseResult AsmParser::parse() {
    statements_.clear();

    // Parse statements until EOF
    while (!at_end()) {
        if (!parse_statement()) {
            // Error occurred, synchronize
            synchronize();
        }
    }

    // Copy lexer errors
    for (const auto& err : lexer_->errors()) {
        errors_.add(err);
    }

    // Build result
    AsmParseResult result;
    result.program.statements = std::move(statements_);

    // Calculate program span
    if (!result.program.statements.empty()) {
        result.program.span = SourceSpan::from(
            statement_span(result.program.statements.front()).start,
            statement_span(result.program.statements.back()).end);
    }

    result.errors = errors_;
    return result;
}

void AsmParser::set_file_reader(FileReader reader) {
    file_reader_ = std::move(reader);
}

// ============================================================================
// Parsing Helpers
// ============================================================================

AsmToken AsmParser::advance() {
    previous_ = current_;
    if (!at_end()) {
        current_ = lexer_->next_token();
    }
    return previous_;
}

bool AsmParser::check(AsmTokenType type) const noexcept {
    return current_.type == type;
}

bool AsmParser::match(AsmTokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool AsmParser::expect(AsmTokenType type, AsmError error_code, std::string_view msg) {
    if (check(type)) {
        advance();
        return true;
    }
    report_error(error_code, msg);
    return false;
}

bool AsmParser::at_end() const noexcept {
    return current_.type == AsmTokenType::Eof;
}

void AsmParser::synchronize() {
    // Skip to end of line or EOF
    while (!at_end() && current_.type != AsmTokenType::Newline) {
        advance();
    }
    // Consume the newline
    if (current_.type == AsmTokenType::Newline) {
        advance();
    }
}

// ============================================================================
// Statement Parsing
// ============================================================================

bool AsmParser::parse_statement() {
    // Skip empty lines
    while (match(AsmTokenType::Newline)) {
        // Empty line
    }

    if (at_end()) {
        return false;
    }

    // Check for label
    if (check(AsmTokenType::Label)) {
        parse_label();
        // May have instruction on same line
        if (!check(AsmTokenType::Newline) && !at_end()) {
            parse_statement();  // Continue parsing
        }
        return true;
    }

    // Check for directive
    if (is_directive(current_.type)) {
        parse_directive();
        return true;
    }

    // Check for opcode (instruction)
    if (check(AsmTokenType::Opcode)) {
        parse_instruction();
        return true;
    }

    // Unexpected token
    if (!check(AsmTokenType::Newline) && !at_end()) {
        report_error(AsmError::UnexpectedToken, "Expected label, instruction, or directive");
        return false;
    }

    return true;
}

void AsmParser::parse_label() {
    auto tok = advance();  // Label token
    AsmLabel label;
    label.name = std::string{tok.lexeme};
    label.span = tok.span;

    statements_.push_back(label);
}

void AsmParser::parse_instruction() {
    auto opcode_tok = advance();  // Opcode token

    AsmInstruction instr;
    instr.mnemonic = std::string{opcode_tok.lexeme};
    instr.opcode = opcode_tok.value;
    instr.span = opcode_tok.span;

    // Get opcode info for format
    auto info = lookup_opcode(opcode_tok.lexeme);
    if (info) {
        instr.format = info->format;
    }

    // Parse operands if present
    if (!check(AsmTokenType::Newline) && !at_end()) {
        // Parse first operand
        auto op = parse_operand();
        if (op) {
            instr.operands.push_back(std::move(*op));

            // Parse remaining operands
            while (match(AsmTokenType::Comma)) {
                op = parse_operand();
                if (op) {
                    instr.operands.push_back(std::move(*op));
                } else {
                    break;  // Error already reported
                }
            }
        }
    }

    // Update span to cover all operands
    if (!instr.operands.empty()) {
        instr.span = SourceSpan::from(opcode_tok.span.start, instr.operands.back().span.end);
    }

    // Validate operand count
    if (info) {
        auto count = static_cast<std::uint8_t>(instr.operands.size());
        if (count < info->min_operands || count > info->max_operands) {
            report_error(AsmError::InvalidOperandCount, instr.span,
                         "Invalid number of operands for " + instr.mnemonic);
        }
    }

    statements_.push_back(instr);

    // Expect newline after instruction
    if (!check(AsmTokenType::Newline) && !at_end()) {
        report_error(AsmError::ExpectedNewline, "Expected newline after instruction");
    } else {
        match(AsmTokenType::Newline);
    }
}

void AsmParser::parse_directive() {
    auto dir_tok = advance();  // Directive token
    SourceSpan span = dir_tok.span;

    switch (dir_tok.type) {
        case AsmTokenType::DirSection:
            handle_section_directive(span);
            break;
        case AsmTokenType::DirGlobal:
            handle_global_directive(span);
            break;
        case AsmTokenType::DirData:
            handle_data_directive(span);
            break;
        case AsmTokenType::DirText:
            handle_text_directive(span);
            break;
        case AsmTokenType::DirByte:
            handle_byte_directive(span);
            break;
        case AsmTokenType::DirWord:
            handle_word_directive(span);
            break;
        case AsmTokenType::DirDword:
            handle_dword_directive(span);
            break;
        case AsmTokenType::DirInclude:
            handle_include_directive(span);
            break;
        case AsmTokenType::DirAlign:
            handle_align_directive(span);
            break;
        default:
            report_error(AsmError::UnexpectedToken, "Unknown directive");
            break;
    }

    // Expect newline after directive
    if (!check(AsmTokenType::Newline) && !at_end()) {
        report_error(AsmError::ExpectedNewline, "Expected newline after directive");
    } else {
        match(AsmTokenType::Newline);
    }
}

// ============================================================================
// Operand Parsing
// ============================================================================

std::optional<AsmOperand> AsmParser::parse_operand() {
    // Memory operand: [Rn+offset]
    if (check(AsmTokenType::LBracket)) {
        return parse_memory_operand();
    }

    // Register
    if (check(AsmTokenType::Register)) {
        auto tok = advance();
        return AsmOperand::reg(tok.value, tok.span);
    }

    // Immediate
    if (check(AsmTokenType::Immediate)) {
        auto tok = advance();
        auto val = parse_immediate_value(tok.lexeme);
        if (val) {
            return AsmOperand::imm(*val, tok.span);
        }
        return std::nullopt;
    }

    // Identifier (label reference)
    if (check(AsmTokenType::Identifier)) {
        auto tok = advance();
        return AsmOperand::label_ref(tok.lexeme, tok.span);
    }

    // String (for data directives)
    if (check(AsmTokenType::String)) {
        auto tok = advance();
        return AsmOperand::str(tok.lexeme, tok.span);
    }

    report_error(AsmError::UnexpectedToken, "Expected operand");
    return std::nullopt;
}

std::optional<AsmOperand> AsmParser::parse_memory_operand() {
    auto start_span = current_.span;
    advance();  // Consume '['

    // Expect register
    if (!check(AsmTokenType::Register)) {
        report_error(AsmError::ExpectedRegister, "Expected register in memory operand");
        return std::nullopt;
    }
    auto reg_tok = advance();
    MemoryOperand mem;
    mem.base_reg = reg_tok.value;
    mem.offset = 0;

    // Check for offset
    if (check(AsmTokenType::Plus) || check(AsmTokenType::Minus)) {
        bool negative = current_.type == AsmTokenType::Minus;
        advance();  // Consume '+' or '-'

        // Expect immediate or number
        if (check(AsmTokenType::Immediate)) {
            auto imm_tok = advance();
            auto val = parse_immediate_value(imm_tok.lexeme);
            if (val) {
                mem.offset = static_cast<std::int16_t>(negative ? -*val : *val);
            }
        } else {
            report_error(AsmError::ExpectedImmediate, "Expected offset in memory operand");
            return std::nullopt;
        }
    }

    // Expect ']'
    if (!expect(AsmTokenType::RBracket, AsmError::ExpectedRBracket, "Expected ']'")) {
        return std::nullopt;
    }

    auto end_span = previous_.span;
    return AsmOperand::memory(mem, SourceSpan::from(start_span.start, end_span.end));
}

std::optional<std::int64_t> AsmParser::parse_immediate_value(std::string_view lexeme) {
    // Skip '#' prefix
    if (!lexeme.empty() && lexeme[0] == '#') {
        lexeme = lexeme.substr(1);
    }

    bool negative = false;
    if (!lexeme.empty() && lexeme[0] == '-') {
        negative = true;
        lexeme = lexeme.substr(1);
    } else if (!lexeme.empty() && lexeme[0] == '+') {
        lexeme = lexeme.substr(1);
    }

    std::int64_t value = 0;

    // Check for hex
    if (lexeme.size() >= 2 && lexeme[0] == '0' && (lexeme[1] == 'x' || lexeme[1] == 'X')) {
        lexeme = lexeme.substr(2);
        auto result = std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value, 16);
        if (result.ec != std::errc{}) {
            report_error(AsmError::InvalidHexNumber, "Invalid hexadecimal number");
            return std::nullopt;
        }
    }
    // Check for binary
    else if (lexeme.size() >= 2 && lexeme[0] == '0' && (lexeme[1] == 'b' || lexeme[1] == 'B')) {
        lexeme = lexeme.substr(2);
        auto result = std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value, 2);
        if (result.ec != std::errc{}) {
            report_error(AsmError::InvalidBinaryNumber, "Invalid binary number");
            return std::nullopt;
        }
    }
    // Decimal
    else {
        auto result = std::from_chars(lexeme.data(), lexeme.data() + lexeme.size(), value, 10);
        if (result.ec != std::errc{}) {
            report_error(AsmError::InvalidImmediate, "Invalid immediate value");
            return std::nullopt;
        }
    }

    return negative ? -value : value;
}

// ============================================================================
// Directive Handling
// ============================================================================

void AsmParser::handle_section_directive(SourceSpan span) {
    AsmDirective dir;
    dir.kind = DirectiveKind::Section;
    dir.span = span;

    // Expect section name
    if (check(AsmTokenType::Identifier)) {
        auto tok = advance();
        dir.args.push_back(AsmOperand::label_ref(tok.lexeme, tok.span));
        dir.span = SourceSpan::from(span.start, tok.span.end);
    } else {
        report_error(AsmError::ExpectedDirectiveArg, "Expected section name");
    }

    statements_.push_back(dir);
}

void AsmParser::handle_global_directive(SourceSpan span) {
    AsmDirective dir;
    dir.kind = DirectiveKind::Global;
    dir.span = span;

    // Expect symbol name
    if (check(AsmTokenType::Identifier)) {
        auto tok = advance();
        dir.args.push_back(AsmOperand::label_ref(tok.lexeme, tok.span));
        dir.span = SourceSpan::from(span.start, tok.span.end);
    } else {
        report_error(AsmError::ExpectedDirectiveArg, "Expected symbol name");
    }

    statements_.push_back(dir);
}

void AsmParser::handle_data_directive(SourceSpan span) {
    AsmDirective dir;
    dir.kind = DirectiveKind::Data;
    dir.span = span;
    statements_.push_back(dir);
}

void AsmParser::handle_text_directive(SourceSpan span) {
    AsmDirective dir;
    dir.kind = DirectiveKind::Text;
    dir.span = span;
    statements_.push_back(dir);
}

void AsmParser::handle_byte_directive(SourceSpan span) {
    AsmDirective dir;
    dir.kind = DirectiveKind::Byte;
    dir.span = span;

    // Parse data values
    do {
        auto op = parse_operand();
        if (op) {
            dir.args.push_back(std::move(*op));
        } else {
            break;
        }
    } while (match(AsmTokenType::Comma));

    if (!dir.args.empty()) {
        dir.span = SourceSpan::from(span.start, dir.args.back().span.end);
    }

    statements_.push_back(dir);
}

void AsmParser::handle_word_directive(SourceSpan span) {
    AsmDirective dir;
    dir.kind = DirectiveKind::Word;
    dir.span = span;

    // Parse data values
    do {
        auto op = parse_operand();
        if (op) {
            dir.args.push_back(std::move(*op));
        } else {
            break;
        }
    } while (match(AsmTokenType::Comma));

    if (!dir.args.empty()) {
        dir.span = SourceSpan::from(span.start, dir.args.back().span.end);
    }

    statements_.push_back(dir);
}

void AsmParser::handle_dword_directive(SourceSpan span) {
    AsmDirective dir;
    dir.kind = DirectiveKind::Dword;
    dir.span = span;

    // Parse data values
    do {
        auto op = parse_operand();
        if (op) {
            dir.args.push_back(std::move(*op));
        } else {
            break;
        }
    } while (match(AsmTokenType::Comma));

    if (!dir.args.empty()) {
        dir.span = SourceSpan::from(span.start, dir.args.back().span.end);
    }

    statements_.push_back(dir);
}

void AsmParser::handle_include_directive(SourceSpan span) {
    // Expect string path
    if (!check(AsmTokenType::String)) {
        report_error(AsmError::ExpectedString, "Expected file path string");
        return;
    }
    auto path_tok = advance();

    if (config_.process_includes) {
        process_include(path_tok.lexeme, SourceSpan::from(span.start, path_tok.span.end));
    } else {
        // Just record directive without processing
        AsmDirective dir;
        dir.kind = DirectiveKind::Include;
        dir.span = SourceSpan::from(span.start, path_tok.span.end);
        dir.args.push_back(AsmOperand::str(path_tok.lexeme, path_tok.span));
        statements_.push_back(dir);
    }
}

void AsmParser::handle_align_directive(SourceSpan span) {
    AsmDirective dir;
    dir.kind = DirectiveKind::Align;
    dir.span = span;

    // Expect alignment value
    if (check(AsmTokenType::Immediate)) {
        auto tok = advance();
        auto val = parse_immediate_value(tok.lexeme);
        if (val) {
            dir.args.push_back(AsmOperand::imm(*val, tok.span));
            dir.span = SourceSpan::from(span.start, tok.span.end);
        }
    } else {
        report_error(AsmError::ExpectedImmediate, "Expected alignment value");
    }

    statements_.push_back(dir);
}

// ============================================================================
// Include Processing
// ============================================================================

void AsmParser::process_include(std::string_view path, SourceSpan span) {
    // Check include depth
    if (include_depth_ >= config_.max_include_depth) {
        report_error(AsmError::IncludeDepthExceeded, span, "Maximum include depth exceeded");
        return;
    }

    // Build full path
    std::string full_path;
    if (!config_.include_base_dir.empty()) {
        full_path = config_.include_base_dir;
        if (full_path.back() != '/') {
            full_path += '/';
        }
    }
    full_path += path;

    // Check for circular include
    if (included_files_.count(full_path) > 0) {
        report_error(AsmError::CircularInclude, span, "Circular include detected: " + full_path);
        return;
    }

    // Read file
    if (!file_reader_) {
        report_error(AsmError::FileNotFound, span, "No file reader configured");
        return;
    }

    auto content = file_reader_(full_path);
    if (!content) {
        report_error(AsmError::FileNotFound, span, content.error());
        return;
    }

    // Mark as included
    included_files_.insert(full_path);
    include_depth_++;

    // Parse included content
    AsmParser include_parser{*content, config_};
    include_parser.file_reader_ = file_reader_;
    include_parser.included_files_ = included_files_;
    include_parser.include_depth_ = include_depth_;

    auto result = include_parser.parse();

    // Merge statements
    for (auto& stmt : result.program.statements) {
        statements_.push_back(std::move(stmt));
    }

    // Merge errors
    for (const auto& err : result.errors) {
        errors_.add(err);
    }

    include_depth_--;
}

// ============================================================================
// Error Handling
// ============================================================================

void AsmParser::report_error(AsmError code, std::string_view msg) {
    errors_.add(code, current_.span, msg);
}

void AsmParser::report_error(AsmError code, SourceSpan span, std::string_view msg) {
    errors_.add(code, span, msg);
}

}  // namespace dotvm::core::asm_
