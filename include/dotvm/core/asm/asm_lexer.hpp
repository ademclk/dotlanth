#pragma once

/// @file asm_lexer.hpp
/// @brief TOOL-004 Line-oriented lexer for DotVM assembly
///
/// Hand-written lexer optimized for assembly syntax:
/// - Zero-copy lexemes via string_view into source
/// - Line-oriented scanning (assembly is line-based)
/// - Opcode validation against known mnemonics
/// - Register parsing (R0-R255)
/// - Immediate parsing (#decimal, #0xHex, #0bBinary)

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "dotvm/core/asm/asm_error.hpp"
#include "dotvm/core/asm/asm_token.hpp"
#include "dotvm/core/instruction.hpp"

namespace dotvm::core::asm_ {

/// @brief Information about an opcode
struct OpcodeInfo {
    /// Opcode value (from opcode.hpp)
    std::uint8_t value{0};

    /// Instruction format/type
    InstructionType format{InstructionType::TypeA};

    /// Minimum number of operands
    std::uint8_t min_operands{0};

    /// Maximum number of operands
    std::uint8_t max_operands{0};
};

/// @brief Lookup opcode by mnemonic
///
/// @param mnemonic The opcode mnemonic (e.g., "ADD", "LOAD64")
/// @return OpcodeInfo if found, nullopt otherwise
[[nodiscard]] std::optional<OpcodeInfo> lookup_opcode(std::string_view mnemonic) noexcept;

/// @brief Get mnemonic name for an opcode value
///
/// @param opcode The opcode value
/// @return Mnemonic string if known, empty string otherwise
[[nodiscard]] std::string_view opcode_name(std::uint8_t opcode) noexcept;

/// @brief Line-oriented lexer for DotVM assembly
///
/// Tokenizes assembly source with line-by-line scanning.
/// Comments start with ';' and continue to end of line.
///
/// @note The lexer operates in a single mode, handling all assembly
/// constructs including labels, opcodes, directives, and operands.
class AsmLexer {
public:
    /// @brief Construct a lexer for the given source
    /// @param source Source code to tokenize (must remain valid during lexing)
    explicit AsmLexer(std::string_view source) noexcept;

    /// @brief Get the next token
    /// @return The next token from the source
    [[nodiscard]] AsmToken next_token();

    /// @brief Peek at the next token without consuming it
    /// @return The next token (will be returned again on next next_token call)
    [[nodiscard]] const AsmToken& peek();

    /// @brief Check if lexer has reached end of file
    [[nodiscard]] bool at_end() const noexcept;

    /// @brief Get the current source location
    [[nodiscard]] SourceLocation location() const noexcept;

    /// @brief Get accumulated errors
    [[nodiscard]] const AsmErrorList& errors() const noexcept { return errors_; }

    /// @brief Check if there are any errors
    [[nodiscard]] bool has_errors() const noexcept { return errors_.has_errors(); }

    /// @brief Get the source being lexed
    [[nodiscard]] std::string_view source() const noexcept { return source_; }

private:
    // ===== Character Classification =====

    /// Check if character is alphabetic or underscore
    [[nodiscard]] static bool is_alpha(char c) noexcept;

    /// Check if character is digit
    [[nodiscard]] static bool is_digit(char c) noexcept;

    /// Check if character is hexadecimal digit
    [[nodiscard]] static bool is_hex_digit(char c) noexcept;

    /// Check if character is binary digit (0 or 1)
    [[nodiscard]] static bool is_binary_digit(char c) noexcept;

    /// Check if character is alphanumeric or underscore
    [[nodiscard]] static bool is_alnum(char c) noexcept;

    /// Check if character is whitespace (excluding newline)
    [[nodiscard]] static bool is_whitespace(char c) noexcept;

    // ===== Basic Scanning =====

    /// Peek at current character
    [[nodiscard]] char peek_char() const noexcept;

    /// Peek at character n positions ahead
    [[nodiscard]] char peek_char(std::size_t n) const noexcept;

    /// Advance to next character
    char advance() noexcept;

    /// Check if current character matches expected
    bool match(char expected) noexcept;

    /// Skip whitespace (not newlines)
    void skip_whitespace() noexcept;

    /// Skip to end of line (for comments)
    void skip_comment() noexcept;

    // ===== Token Scanning =====

    /// Scan an identifier, opcode, or register
    [[nodiscard]] AsmToken scan_identifier();

    /// Scan a register (R0-R255)
    [[nodiscard]] AsmToken scan_register();

    /// Scan an immediate value (#123, #0xFF, #0b1010)
    [[nodiscard]] AsmToken scan_immediate();

    /// Scan a string literal
    [[nodiscard]] AsmToken scan_string();

    /// Scan a directive (.section, .global, etc.)
    [[nodiscard]] AsmToken scan_directive();

    /// Classify identifier as opcode, register, or plain identifier
    [[nodiscard]] AsmToken classify_identifier(std::string_view text, SourceSpan span);

    // ===== Error Handling =====

    /// Record an error
    void report_error(AsmError code, std::string_view msg = "");

    /// Create an error token at current position
    [[nodiscard]] AsmToken error_token(std::string_view msg = "");

    // ===== State =====

    /// Source code being lexed
    std::string_view source_;

    /// Current position in source
    std::size_t pos_{0};

    /// Current line (1-based)
    std::uint32_t line_{1};

    /// Current column (1-based)
    std::uint32_t column_{1};

    /// Token start position
    std::size_t token_start_{0};

    /// Token start location
    SourceLocation token_start_loc_;

    /// Accumulated errors
    AsmErrorList errors_;

    /// Peeked token (for lookahead)
    AsmToken peeked_token_;

    /// Whether we have a peeked token
    bool has_peeked_{false};
};

}  // namespace dotvm::core::asm_
