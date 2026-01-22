/// @file asm_lexer.cpp
/// @brief TOOL-004 Line-oriented assembly lexer implementation

#include "dotvm/core/asm/asm_lexer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

#include "dotvm/core/opcode.hpp"

namespace dotvm::core::asm_ {

// ============================================================================
// Opcode Lookup Table
// ============================================================================

namespace {

/// Entry in the opcode table
struct OpcodeEntry {
    std::string_view mnemonic;
    OpcodeInfo info;
};

// clang-format off
/// Complete opcode table mapping mnemonics to opcode info
/// MUST be sorted alphabetically for binary search (std::lower_bound)
constexpr std::array<OpcodeEntry, 77> OPCODE_TABLE = {{
    {"ADD",     {opcode::ADD,     InstructionType::TypeA, 3, 3}},
    {"ADDI",    {opcode::ADDI,    InstructionType::TypeB, 2, 2}},
    {"AND",     {opcode::AND,     InstructionType::TypeA, 3, 3}},
    {"ANDI",    {opcode::ANDI,    InstructionType::TypeB, 2, 2}},
    {"BEQ",     {opcode::BEQ,     InstructionType::TypeA, 3, 3}},
    {"BGE",     {opcode::BGE,     InstructionType::TypeA, 3, 3}},
    {"BGT",     {opcode::BGT,     InstructionType::TypeA, 3, 3}},
    {"BLE",     {opcode::BLE,     InstructionType::TypeA, 3, 3}},
    {"BLT",     {opcode::BLT,     InstructionType::TypeA, 3, 3}},
    {"BNE",     {opcode::BNE,     InstructionType::TypeA, 3, 3}},
    {"BREAK",   {opcode::BREAK,   InstructionType::TypeC, 0, 0}},
    {"CALL",    {opcode::CALL,    InstructionType::TypeC, 1, 1}},
    {"CATCH",   {opcode::CATCH,   InstructionType::TypeC, 0, 0}},
    {"CMPI_EQ", {opcode::CMPI_EQ, InstructionType::TypeB, 2, 2}},
    {"CMPI_GE", {opcode::CMPI_GE, InstructionType::TypeB, 2, 2}},
    {"CMPI_LT", {opcode::CMPI_LT, InstructionType::TypeB, 2, 2}},
    {"CMPI_NE", {opcode::CMPI_NE, InstructionType::TypeB, 2, 2}},
    {"DEBUG",   {opcode::DEBUG,   InstructionType::TypeC, 0, 0}},
    {"DIV",     {opcode::DIV,     InstructionType::TypeA, 3, 3}},
    {"ENDTRY",  {opcode::ENDTRY,  InstructionType::TypeC, 0, 0}},
    {"EQ",      {opcode::EQ,      InstructionType::TypeA, 3, 3}},
    {"F2I",     {opcode::F2I,     InstructionType::TypeA, 2, 2}},
    {"FADD",    {opcode::FADD,    InstructionType::TypeA, 3, 3}},
    {"FCMP",    {opcode::FCMP,    InstructionType::TypeA, 2, 2}},
    {"FDIV",    {opcode::FDIV,    InstructionType::TypeA, 3, 3}},
    {"FMUL",    {opcode::FMUL,    InstructionType::TypeA, 3, 3}},
    {"FNEG",    {opcode::FNEG,    InstructionType::TypeA, 2, 2}},
    {"FSQRT",   {opcode::FSQRT,   InstructionType::TypeA, 2, 2}},
    {"FSUB",    {opcode::FSUB,    InstructionType::TypeA, 3, 3}},
    {"GE",      {opcode::GE,      InstructionType::TypeA, 3, 3}},
    {"GEU",     {opcode::GEU,     InstructionType::TypeA, 3, 3}},
    {"GT",      {opcode::GT,      InstructionType::TypeA, 3, 3}},
    {"GTU",     {opcode::GTU,     InstructionType::TypeA, 3, 3}},
    {"HALT",    {opcode::HALT,    InstructionType::TypeC, 0, 0}},
    {"I2F",     {opcode::I2F,     InstructionType::TypeA, 2, 2}},
    {"JMP",     {opcode::JMP,     InstructionType::TypeC, 1, 1}},
    {"JNZ",     {opcode::JNZ,     InstructionType::TypeD, 2, 2}},
    {"JZ",      {opcode::JZ,      InstructionType::TypeD, 2, 2}},
    {"LE",      {opcode::LE,      InstructionType::TypeA, 3, 3}},
    {"LEA",     {opcode::LEA,     InstructionType::TypeM, 2, 3}},
    {"LEU",     {opcode::LEU,     InstructionType::TypeA, 3, 3}},
    {"LOAD16",  {opcode::LOAD16,  InstructionType::TypeM, 2, 3}},
    {"LOAD32",  {opcode::LOAD32,  InstructionType::TypeM, 2, 3}},
    {"LOAD64",  {opcode::LOAD64,  InstructionType::TypeM, 2, 3}},
    {"LOAD8",   {opcode::LOAD8,   InstructionType::TypeM, 2, 3}},
    {"LT",      {opcode::LT,      InstructionType::TypeA, 3, 3}},
    {"LTU",     {opcode::LTU,     InstructionType::TypeA, 3, 3}},
    {"MOD",     {opcode::MOD,     InstructionType::TypeA, 3, 3}},
    {"MUL",     {opcode::MUL,     InstructionType::TypeA, 3, 3}},
    {"MULI",    {opcode::MULI,    InstructionType::TypeB, 2, 2}},
    {"NE",      {opcode::NE,      InstructionType::TypeA, 3, 3}},
    {"NEG",     {opcode::NEG,     InstructionType::TypeA, 2, 2}},
    {"NOP",     {opcode::NOP,     InstructionType::TypeC, 0, 0}},
    {"NOT",     {opcode::NOT,     InstructionType::TypeA, 2, 2}},
    {"OR",      {opcode::OR,      InstructionType::TypeA, 3, 3}},
    {"ORI",     {opcode::ORI,     InstructionType::TypeB, 2, 2}},
    {"RET",     {opcode::RET,     InstructionType::TypeC, 0, 0}},
    {"ROL",     {opcode::ROL,     InstructionType::TypeA, 3, 3}},
    {"ROR",     {opcode::ROR,     InstructionType::TypeA, 3, 3}},
    {"SAR",     {opcode::SAR,     InstructionType::TypeA, 3, 3}},
    {"SARI",    {opcode::SARI,    InstructionType::TypeS, 3, 3}},
    {"SHL",     {opcode::SHL,     InstructionType::TypeA, 3, 3}},
    {"SHLI",    {opcode::SHLI,    InstructionType::TypeS, 3, 3}},
    {"SHR",     {opcode::SHR,     InstructionType::TypeA, 3, 3}},
    {"SHRI",    {opcode::SHRI,    InstructionType::TypeS, 3, 3}},
    {"STORE16", {opcode::STORE16, InstructionType::TypeM, 2, 3}},
    {"STORE32", {opcode::STORE32, InstructionType::TypeM, 2, 3}},
    {"STORE64", {opcode::STORE64, InstructionType::TypeM, 2, 3}},
    {"STORE8",  {opcode::STORE8,  InstructionType::TypeM, 2, 3}},
    {"SUB",     {opcode::SUB,     InstructionType::TypeA, 3, 3}},
    {"SUBI",    {opcode::SUBI,    InstructionType::TypeB, 2, 2}},
    {"SYSCALL", {opcode::SYSCALL, InstructionType::TypeC, 0, 0}},
    {"TEST",    {opcode::TEST,    InstructionType::TypeA, 3, 3}},
    {"THROW",   {opcode::THROW,   InstructionType::TypeA, 2, 2}},
    {"TRY",     {opcode::TRY,     InstructionType::TypeB, 2, 2}},
    {"XOR",     {opcode::XOR,     InstructionType::TypeA, 3, 3}},
    {"XORI",    {opcode::XORI,    InstructionType::TypeB, 2, 2}},
}};
// clang-format on

/// Compare function for binary search
constexpr auto opcode_compare = [](const OpcodeEntry& a, std::string_view b) {
    return a.mnemonic < b;
};

}  // namespace

std::optional<OpcodeInfo> lookup_opcode(std::string_view mnemonic) noexcept {
    // Convert to uppercase for comparison
    std::string upper;
    upper.reserve(mnemonic.size());
    for (char c : mnemonic) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    // Binary search in sorted table
    auto it = std::lower_bound(OPCODE_TABLE.begin(), OPCODE_TABLE.end(), std::string_view{upper},
                               opcode_compare);

    if (it != OPCODE_TABLE.end() && it->mnemonic == upper) {
        return it->info;
    }
    return std::nullopt;
}

std::string_view opcode_name(std::uint8_t opcode) noexcept {
    // Linear search since we're looking up by value
    for (const auto& entry : OPCODE_TABLE) {
        if (entry.info.value == opcode) {
            return entry.mnemonic;
        }
    }
    return "";
}

// ============================================================================
// Constructor
// ============================================================================

AsmLexer::AsmLexer(std::string_view source) noexcept : source_{source}, token_start_loc_{1, 1, 0} {}

// ============================================================================
// Character Classification
// ============================================================================

bool AsmLexer::is_alpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool AsmLexer::is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool AsmLexer::is_hex_digit(char c) noexcept {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool AsmLexer::is_binary_digit(char c) noexcept {
    return c == '0' || c == '1';
}

bool AsmLexer::is_alnum(char c) noexcept {
    return is_alpha(c) || is_digit(c);
}

bool AsmLexer::is_whitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r';
}

// ============================================================================
// Basic Scanning
// ============================================================================

char AsmLexer::peek_char() const noexcept {
    if (pos_ >= source_.size()) {
        return '\0';
    }
    return source_[pos_];
}

char AsmLexer::peek_char(std::size_t n) const noexcept {
    if (pos_ + n >= source_.size()) {
        return '\0';
    }
    return source_[pos_ + n];
}

char AsmLexer::advance() noexcept {
    if (pos_ >= source_.size()) {
        return '\0';
    }
    char c = source_[pos_++];
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

bool AsmLexer::match(char expected) noexcept {
    if (pos_ >= source_.size() || source_[pos_] != expected) {
        return false;
    }
    advance();
    return true;
}

void AsmLexer::skip_whitespace() noexcept {
    while (pos_ < source_.size() && is_whitespace(source_[pos_])) {
        advance();
    }
}

void AsmLexer::skip_comment() noexcept {
    while (pos_ < source_.size() && source_[pos_] != '\n') {
        advance();
    }
}

// ============================================================================
// Public Interface
// ============================================================================

bool AsmLexer::at_end() const noexcept {
    return pos_ >= source_.size();
}

SourceLocation AsmLexer::location() const noexcept {
    return SourceLocation{line_, column_, pos_};
}

const AsmToken& AsmLexer::peek() {
    if (!has_peeked_) {
        peeked_token_ = next_token();
        has_peeked_ = true;
    }
    return peeked_token_;
}

AsmToken AsmLexer::next_token() {
    // Return peeked token if available
    if (has_peeked_) {
        has_peeked_ = false;
        return peeked_token_;
    }

    // Skip whitespace (but not newlines)
    skip_whitespace();

    // Record token start
    token_start_ = pos_;
    token_start_loc_ = location();

    // Check for end of file
    if (pos_ >= source_.size()) {
        return AsmToken::eof(token_start_loc_);
    }

    char c = advance();

    // Skip comments
    if (c == ';') {
        skip_comment();
        // Try again after comment
        return next_token();
    }

    // Newline
    if (c == '\n') {
        return AsmToken::make(AsmTokenType::Newline, SourceSpan::from(token_start_loc_, location()),
                              "\n");
    }

    // Directive (starts with '.')
    if (c == '.') {
        return scan_directive();
    }

    // Immediate value (starts with '#')
    if (c == '#') {
        return scan_immediate();
    }

    // String literal
    if (c == '"') {
        return scan_string();
    }

    // Identifier, opcode, or register
    if (is_alpha(c)) {
        return scan_identifier();
    }

    // Number at start of line could be a label (but we require labels to start with alpha)
    // For now, numbers without # are errors
    if (is_digit(c)) {
        // Could be part of a label like "loop1:"
        // Back up and try as identifier
        pos_--;
        column_--;
        return scan_identifier();
    }

    // Single-character tokens
    switch (c) {
        case ',':
            return AsmToken::make(AsmTokenType::Comma, SourceSpan::from(token_start_loc_, location()),
                                  ",");
        case ':':
            return AsmToken::make(AsmTokenType::Colon, SourceSpan::from(token_start_loc_, location()),
                                  ":");
        case '[':
            return AsmToken::make(AsmTokenType::LBracket,
                                  SourceSpan::from(token_start_loc_, location()), "[");
        case ']':
            return AsmToken::make(AsmTokenType::RBracket,
                                  SourceSpan::from(token_start_loc_, location()), "]");
        case '+':
            return AsmToken::make(AsmTokenType::Plus, SourceSpan::from(token_start_loc_, location()),
                                  "+");
        case '-':
            return AsmToken::make(AsmTokenType::Minus, SourceSpan::from(token_start_loc_, location()),
                                  "-");

        default:
            report_error(AsmError::UnexpectedCharacter);
            return error_token();
    }
}

// ============================================================================
// Token Scanning
// ============================================================================

AsmToken AsmLexer::scan_identifier() {
    // Scan alphanumeric characters
    while (pos_ < source_.size() && is_alnum(source_[pos_])) {
        advance();
    }

    std::string_view text = source_.substr(token_start_, pos_ - token_start_);
    SourceSpan span = SourceSpan::from(token_start_loc_, location());

    // Check if followed by colon (label definition)
    if (pos_ < source_.size() && source_[pos_] == ':') {
        advance();  // consume ':'
        // Return as Label (include the colon in span, but not in lexeme)
        return AsmToken::make(AsmTokenType::Label,
                              SourceSpan::from(token_start_loc_, location()), text);
    }

    // Classify as opcode, register, or identifier
    return classify_identifier(text, span);
}

AsmToken AsmLexer::classify_identifier(std::string_view text, SourceSpan span) {
    // Check for register (R0-R255 or r0-r255)
    if ((text[0] == 'R' || text[0] == 'r') && text.size() >= 2) {
        // Parse register number
        std::string_view num_part = text.substr(1);
        bool all_digits = true;
        for (char c : num_part) {
            if (!is_digit(c)) {
                all_digits = false;
                break;
            }
        }
        if (all_digits && !num_part.empty()) {
            int reg_num = 0;
            auto result = std::from_chars(num_part.data(), num_part.data() + num_part.size(),
                                          reg_num);
            if (result.ec == std::errc{} && reg_num >= 0 && reg_num <= 255) {
                return AsmToken::make(AsmTokenType::Register, span, text,
                                      static_cast<std::uint8_t>(reg_num));
            }
            // Invalid register number
            report_error(AsmError::InvalidRegister, "Register number must be 0-255");
            return error_token();
        }
    }

    // Check for opcode
    auto opcode_info = lookup_opcode(text);
    if (opcode_info) {
        return AsmToken::make(AsmTokenType::Opcode, span, text, opcode_info->value);
    }

    // Plain identifier (label reference)
    return AsmToken::make(AsmTokenType::Identifier, span, text);
}

AsmToken AsmLexer::scan_immediate() {
    // Already consumed '#'
    // Check for sign (included in lexeme, parsed later)
    if (pos_ < source_.size() && (source_[pos_] == '-' || source_[pos_] == '+')) {
        advance();
    }

    if (pos_ >= source_.size() || (!is_digit(source_[pos_]) && source_[pos_] != '0')) {
        report_error(AsmError::InvalidImmediate, "Expected number after '#'");
        return error_token();
    }

    // Check for hex (0x) or binary (0b)
    if (source_[pos_] == '0' && pos_ + 1 < source_.size()) {
        char prefix = source_[pos_ + 1];
        if (prefix == 'x' || prefix == 'X') {
            advance();  // '0'
            advance();  // 'x'
            // Scan hex digits
            std::size_t hex_start = pos_;
            while (pos_ < source_.size() && is_hex_digit(source_[pos_])) {
                advance();
            }
            if (pos_ == hex_start) {
                report_error(AsmError::InvalidHexNumber, "Expected hex digits after '0x'");
                return error_token();
            }
            std::string_view text = source_.substr(token_start_, pos_ - token_start_);
            return AsmToken::make(AsmTokenType::Immediate,
                                  SourceSpan::from(token_start_loc_, location()), text);
        } else if (prefix == 'b' || prefix == 'B') {
            advance();  // '0'
            advance();  // 'b'
            // Scan binary digits
            std::size_t bin_start = pos_;
            while (pos_ < source_.size() && is_binary_digit(source_[pos_])) {
                advance();
            }
            if (pos_ == bin_start) {
                report_error(AsmError::InvalidBinaryNumber, "Expected binary digits after '0b'");
                return error_token();
            }
            std::string_view text = source_.substr(token_start_, pos_ - token_start_);
            return AsmToken::make(AsmTokenType::Immediate,
                                  SourceSpan::from(token_start_loc_, location()), text);
        }
    }

    // Decimal number
    while (pos_ < source_.size() && is_digit(source_[pos_])) {
        advance();
    }

    std::string_view text = source_.substr(token_start_, pos_ - token_start_);
    return AsmToken::make(AsmTokenType::Immediate, SourceSpan::from(token_start_loc_, location()),
                          text);
}

AsmToken AsmLexer::scan_string() {
    // Already consumed opening quote
    std::size_t content_start = pos_;

    while (pos_ < source_.size()) {
        char c = source_[pos_];

        if (c == '"') {
            // End of string
            std::string_view text = source_.substr(content_start, pos_ - content_start);
            advance();  // consume closing quote
            return AsmToken::make(AsmTokenType::String,
                                  SourceSpan::from(token_start_loc_, location()), text);
        }

        if (c == '\n') {
            report_error(AsmError::UnterminatedString, "Newline in string literal");
            return error_token();
        }

        if (c == '\\') {
            // Escape sequence
            advance();
            if (pos_ >= source_.size()) {
                report_error(AsmError::UnterminatedString, "Unexpected end of string");
                return error_token();
            }
            char next = source_[pos_];
            if (next != 'n' && next != 't' && next != 'r' && next != '\\' && next != '"' &&
                next != '0') {
                report_error(AsmError::InvalidEscapeSequence);
                return error_token();
            }
            advance();
            continue;
        }

        advance();
    }

    report_error(AsmError::UnterminatedString);
    return error_token();
}

AsmToken AsmLexer::scan_directive() {
    // Already consumed '.'
    // Scan directive name
    while (pos_ < source_.size() && is_alpha(source_[pos_])) {
        advance();
    }

    std::string_view text = source_.substr(token_start_, pos_ - token_start_);
    SourceSpan span = SourceSpan::from(token_start_loc_, location());

    // Match directive
    if (text == ".section") {
        return AsmToken::make(AsmTokenType::DirSection, span, text);
    } else if (text == ".global") {
        return AsmToken::make(AsmTokenType::DirGlobal, span, text);
    } else if (text == ".data") {
        return AsmToken::make(AsmTokenType::DirData, span, text);
    } else if (text == ".text") {
        return AsmToken::make(AsmTokenType::DirText, span, text);
    } else if (text == ".byte") {
        return AsmToken::make(AsmTokenType::DirByte, span, text);
    } else if (text == ".word") {
        return AsmToken::make(AsmTokenType::DirWord, span, text);
    } else if (text == ".dword") {
        return AsmToken::make(AsmTokenType::DirDword, span, text);
    } else if (text == ".include") {
        return AsmToken::make(AsmTokenType::DirInclude, span, text);
    } else if (text == ".align") {
        return AsmToken::make(AsmTokenType::DirAlign, span, text);
    }

    report_error(AsmError::InvalidDirective, "Unknown directive");
    return error_token();
}

// ============================================================================
// Error Handling
// ============================================================================

void AsmLexer::report_error(AsmError code, std::string_view msg) {
    errors_.add(code, SourceSpan::from(token_start_loc_, location()), msg);
}

AsmToken AsmLexer::error_token(std::string_view msg) {
    std::string_view lexeme = source_.substr(token_start_, pos_ - token_start_);
    return AsmToken::error(SourceSpan::from(token_start_loc_, location()), lexeme);
}

}  // namespace dotvm::core::asm_
