#pragma once

/// @file asm_parser.hpp
/// @brief TOOL-004 Assembly parser interface
///
/// Parses assembly source into an AST. Uses the AsmLexer for tokenization
/// and produces an AsmProgram containing all parsed statements.

#include <expected>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include "dotvm/core/asm/asm_ast.hpp"
#include "dotvm/core/asm/asm_error.hpp"
#include "dotvm/core/asm/asm_lexer.hpp"
#include "dotvm/core/asm/asm_token.hpp"

namespace dotvm::core::asm_ {

/// @brief Configuration for the assembly parser
struct AsmParserConfig {
    /// Base directory for resolving .include directives
    std::string include_base_dir;

    /// Maximum include nesting depth (to prevent circular includes)
    std::size_t max_include_depth{10};

    /// Whether to process .include directives
    bool process_includes{true};
};

/// @brief Result of parsing assembly source
///
/// Contains either a successfully parsed program or error information.
struct AsmParseResult {
    /// The parsed program (may be partially complete even with errors)
    AsmProgram program;

    /// Collected errors during parsing
    AsmErrorList errors;

    /// @brief Check if parsing was successful (no errors)
    [[nodiscard]] bool success() const noexcept { return errors.empty(); }
};

/// @brief File reader callback type for .include directive
///
/// Given a file path, returns the file contents or an error message.
using FileReader = std::function<std::expected<std::string, std::string>(std::string_view path)>;

/// @brief Assembly parser producing AsmProgram from source
///
/// Grammar:
/// @code
/// program     → statement* EOF
/// statement   → (label? instruction | directive) NEWLINE
/// label       → IDENTIFIER ':'
/// instruction → OPCODE operand_list?
/// operand_list→ operand (',' operand)*
/// operand     → REGISTER | IMMEDIATE | memory | IDENTIFIER | STRING
/// memory      → '[' REGISTER ('+' | '-') number ']'
/// directive   → '.' directive_name args
/// @endcode
class AsmParser {
public:
    /// @brief Construct a parser for the given source
    /// @param source Source code to parse (must remain valid during parsing)
    /// @param config Parser configuration
    explicit AsmParser(std::string_view source, AsmParserConfig config = {});

    /// @brief Parse the source into an AsmProgram
    /// @return Parse result containing the program and any errors
    [[nodiscard]] AsmParseResult parse();

    /// @brief Set the file reader for .include directive
    /// @param reader Callback to read included files
    void set_file_reader(FileReader reader);

    /// @brief Get accumulated errors
    [[nodiscard]] const AsmErrorList& errors() const noexcept { return errors_; }

    /// @brief Check if there are any errors
    [[nodiscard]] bool has_errors() const noexcept { return errors_.has_errors(); }

private:
    // ===== Parsing Helpers =====

    /// Advance to next token
    AsmToken advance();

    /// Check if current token matches type
    [[nodiscard]] bool check(AsmTokenType type) const noexcept;

    /// Check if current token matches any of given types
    template <typename... Types>
    [[nodiscard]] bool check_any(Types... types) const noexcept {
        return ((current_.type == types) || ...);
    }

    /// Match and consume token if it matches
    bool match(AsmTokenType type);

    /// Expect a specific token type, report error if not found
    bool expect(AsmTokenType type, AsmError error_code, std::string_view msg = "");

    /// Check if at end of file
    [[nodiscard]] bool at_end() const noexcept;

    /// Skip tokens until newline or EOF (error recovery)
    void synchronize();

    // ===== Statement Parsing =====

    /// Parse a single statement (label, instruction, or directive)
    /// @return true if statement was parsed, false if at EOF or error
    bool parse_statement();

    /// Parse a label definition
    void parse_label();

    /// Parse an instruction
    void parse_instruction();

    /// Parse a directive
    void parse_directive();

    // ===== Operand Parsing =====

    /// Parse an operand
    [[nodiscard]] std::optional<AsmOperand> parse_operand();

    /// Parse a memory operand [Rn+offset]
    [[nodiscard]] std::optional<AsmOperand> parse_memory_operand();

    /// Parse an immediate value from token
    [[nodiscard]] std::optional<std::int64_t> parse_immediate_value(std::string_view lexeme);

    // ===== Directive Handling =====

    /// Handle .section directive
    void handle_section_directive(SourceSpan span);

    /// Handle .global directive
    void handle_global_directive(SourceSpan span);

    /// Handle .data directive
    void handle_data_directive(SourceSpan span);

    /// Handle .text directive
    void handle_text_directive(SourceSpan span);

    /// Handle .byte directive
    void handle_byte_directive(SourceSpan span);

    /// Handle .word directive
    void handle_word_directive(SourceSpan span);

    /// Handle .dword directive
    void handle_dword_directive(SourceSpan span);

    /// Handle .include directive
    void handle_include_directive(SourceSpan span);

    /// Handle .align directive
    void handle_align_directive(SourceSpan span);

    // ===== Include Processing =====

    /// Process an included file
    /// @param path Path to the file to include
    /// @param span Source span for error reporting
    void process_include(std::string_view path, SourceSpan span);

    // ===== Error Handling =====

    /// Report an error
    void report_error(AsmError code, std::string_view msg = "");

    /// Report an error at a specific span
    void report_error(AsmError code, SourceSpan span, std::string_view msg = "");

    // ===== State =====

    /// Lexer for tokenization
    std::unique_ptr<AsmLexer> lexer_;

    /// Parser configuration
    AsmParserConfig config_;

    /// File reader for includes
    FileReader file_reader_;

    /// Current token
    AsmToken current_;

    /// Previous token
    AsmToken previous_;

    /// Accumulated errors
    AsmErrorList errors_;

    /// Parsed statements
    std::vector<AsmStatement> statements_;

    /// Current include depth
    std::size_t include_depth_{0};

    /// Set of included file paths (for circular detection)
    std::set<std::string> included_files_;

    /// Source text (needed for span calculation)
    std::string_view source_;
};

}  // namespace dotvm::core::asm_
