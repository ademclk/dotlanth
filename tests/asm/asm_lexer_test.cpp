/// @file asm_lexer_test.cpp
/// @brief TOOL-004 Assembly lexer unit tests

#include "dotvm/core/asm/asm_lexer.hpp"

#include <gtest/gtest.h>

#include "dotvm/core/opcode.hpp"

namespace dotvm::core::asm_ {
namespace {

class AsmLexerTest : public ::testing::Test {
protected:
    // Helper to collect all tokens from source
    std::vector<AsmToken> lex_all(std::string_view source) {
        AsmLexer lexer{source};
        std::vector<AsmToken> tokens;
        for (;;) {
            auto tok = lexer.next_token();
            tokens.push_back(tok);
            if (tok.is_eof()) {
                break;
            }
        }
        return tokens;
    }

    // Helper to get single token (first non-newline)
    AsmToken lex_single(std::string_view source) {
        AsmLexer lexer{source};
        return lexer.next_token();
    }
};

// ============================================================================
// Basic Token Tests
// ============================================================================

TEST_F(AsmLexerTest, EmptySource) {
    auto tokens = lex_all("");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, AsmTokenType::Eof);
}

TEST_F(AsmLexerTest, WhitespaceOnly) {
    auto tokens = lex_all("   \t  ");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, AsmTokenType::Eof);
}

TEST_F(AsmLexerTest, NewlineToken) {
    auto tok = lex_single("\n");
    EXPECT_EQ(tok.type, AsmTokenType::Newline);
}

TEST_F(AsmLexerTest, CommentSkipped) {
    auto tokens = lex_all("; this is a comment\n");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].type, AsmTokenType::Newline);
    EXPECT_EQ(tokens[1].type, AsmTokenType::Eof);
}

TEST_F(AsmLexerTest, InlineComment) {
    auto tokens = lex_all("ADD R0, R1, R2 ; add registers\n");
    // Should get: Opcode, Register, Comma, Register, Comma, Register, Newline, Eof
    EXPECT_EQ(tokens[0].type, AsmTokenType::Opcode);
    EXPECT_EQ(tokens[1].type, AsmTokenType::Register);
    EXPECT_EQ(tokens[2].type, AsmTokenType::Comma);
}

// ============================================================================
// Register Tests
// ============================================================================

TEST_F(AsmLexerTest, RegisterR0) {
    auto tok = lex_single("R0");
    EXPECT_EQ(tok.type, AsmTokenType::Register);
    EXPECT_EQ(tok.value, 0);
    EXPECT_EQ(tok.lexeme, "R0");
}

TEST_F(AsmLexerTest, RegisterR255) {
    auto tok = lex_single("R255");
    EXPECT_EQ(tok.type, AsmTokenType::Register);
    EXPECT_EQ(tok.value, 255);
}

TEST_F(AsmLexerTest, RegisterLowercase) {
    auto tok = lex_single("r42");
    EXPECT_EQ(tok.type, AsmTokenType::Register);
    EXPECT_EQ(tok.value, 42);
}

TEST_F(AsmLexerTest, RegisterOutOfRange) {
    AsmLexer lexer{"R256"};
    auto tok = lexer.next_token();
    EXPECT_EQ(tok.type, AsmTokenType::Error);
    EXPECT_TRUE(lexer.has_errors());
}

// ============================================================================
// Opcode Tests
// ============================================================================

TEST_F(AsmLexerTest, OpcodeADD) {
    auto tok = lex_single("ADD");
    EXPECT_EQ(tok.type, AsmTokenType::Opcode);
    EXPECT_EQ(tok.value, opcode::ADD);
    EXPECT_EQ(tok.lexeme, "ADD");
}

TEST_F(AsmLexerTest, OpcodeLowercase) {
    auto tok = lex_single("add");
    EXPECT_EQ(tok.type, AsmTokenType::Opcode);
    EXPECT_EQ(tok.value, opcode::ADD);
}

TEST_F(AsmLexerTest, OpcodeMixedCase) {
    auto tok = lex_single("Add");
    EXPECT_EQ(tok.type, AsmTokenType::Opcode);
    EXPECT_EQ(tok.value, opcode::ADD);
}

TEST_F(AsmLexerTest, OpcodeHALT) {
    auto tok = lex_single("HALT");
    EXPECT_EQ(tok.type, AsmTokenType::Opcode);
    EXPECT_EQ(tok.value, opcode::HALT);
}

TEST_F(AsmLexerTest, OpcodeLOAD64) {
    auto tok = lex_single("LOAD64");
    EXPECT_EQ(tok.type, AsmTokenType::Opcode);
    EXPECT_EQ(tok.value, opcode::LOAD64);
}

TEST_F(AsmLexerTest, OpcodeJMP) {
    auto tok = lex_single("JMP");
    EXPECT_EQ(tok.type, AsmTokenType::Opcode);
    EXPECT_EQ(tok.value, opcode::JMP);
}

TEST_F(AsmLexerTest, OpcodeJZ) {
    auto tok = lex_single("JZ");
    EXPECT_EQ(tok.type, AsmTokenType::Opcode);
    EXPECT_EQ(tok.value, opcode::JZ);
}

TEST_F(AsmLexerTest, AllOpcodes) {
    // Test a sampling of opcodes from each category
    EXPECT_TRUE(lookup_opcode("ADD").has_value());
    EXPECT_TRUE(lookup_opcode("SUB").has_value());
    EXPECT_TRUE(lookup_opcode("MUL").has_value());
    EXPECT_TRUE(lookup_opcode("DIV").has_value());
    EXPECT_TRUE(lookup_opcode("ADDI").has_value());
    EXPECT_TRUE(lookup_opcode("AND").has_value());
    EXPECT_TRUE(lookup_opcode("OR").has_value());
    EXPECT_TRUE(lookup_opcode("XOR").has_value());
    EXPECT_TRUE(lookup_opcode("SHLI").has_value());
    EXPECT_TRUE(lookup_opcode("EQ").has_value());
    EXPECT_TRUE(lookup_opcode("LT").has_value());
    EXPECT_TRUE(lookup_opcode("JMP").has_value());
    EXPECT_TRUE(lookup_opcode("JZ").has_value());
    EXPECT_TRUE(lookup_opcode("CALL").has_value());
    EXPECT_TRUE(lookup_opcode("RET").has_value());
    EXPECT_TRUE(lookup_opcode("LOAD64").has_value());
    EXPECT_TRUE(lookup_opcode("STORE64").has_value());
    EXPECT_TRUE(lookup_opcode("NOP").has_value());
    EXPECT_TRUE(lookup_opcode("HALT").has_value());
}

// ============================================================================
// Immediate Tests
// ============================================================================

TEST_F(AsmLexerTest, ImmediateDecimal) {
    auto tok = lex_single("#123");
    EXPECT_EQ(tok.type, AsmTokenType::Immediate);
    EXPECT_EQ(tok.lexeme, "#123");
}

TEST_F(AsmLexerTest, ImmediateNegative) {
    auto tok = lex_single("#-45");
    EXPECT_EQ(tok.type, AsmTokenType::Immediate);
    EXPECT_EQ(tok.lexeme, "#-45");
}

TEST_F(AsmLexerTest, ImmediateHex) {
    auto tok = lex_single("#0xFF");
    EXPECT_EQ(tok.type, AsmTokenType::Immediate);
    EXPECT_EQ(tok.lexeme, "#0xFF");
}

TEST_F(AsmLexerTest, ImmediateBinary) {
    auto tok = lex_single("#0b1010");
    EXPECT_EQ(tok.type, AsmTokenType::Immediate);
    EXPECT_EQ(tok.lexeme, "#0b1010");
}

TEST_F(AsmLexerTest, ImmediateZero) {
    auto tok = lex_single("#0");
    EXPECT_EQ(tok.type, AsmTokenType::Immediate);
    EXPECT_EQ(tok.lexeme, "#0");
}

// ============================================================================
// Label Tests
// ============================================================================

TEST_F(AsmLexerTest, LabelDefinition) {
    auto tok = lex_single("main:");
    EXPECT_EQ(tok.type, AsmTokenType::Label);
    EXPECT_EQ(tok.lexeme, "main");
}

TEST_F(AsmLexerTest, LabelWithNumbers) {
    auto tok = lex_single("loop1:");
    EXPECT_EQ(tok.type, AsmTokenType::Label);
    EXPECT_EQ(tok.lexeme, "loop1");
}

TEST_F(AsmLexerTest, LabelReference) {
    auto tok = lex_single("loop");
    EXPECT_EQ(tok.type, AsmTokenType::Identifier);
    EXPECT_EQ(tok.lexeme, "loop");
}

// ============================================================================
// Directive Tests
// ============================================================================

TEST_F(AsmLexerTest, DirectiveSection) {
    auto tok = lex_single(".section");
    EXPECT_EQ(tok.type, AsmTokenType::DirSection);
}

TEST_F(AsmLexerTest, DirectiveGlobal) {
    auto tok = lex_single(".global");
    EXPECT_EQ(tok.type, AsmTokenType::DirGlobal);
}

TEST_F(AsmLexerTest, DirectiveData) {
    auto tok = lex_single(".data");
    EXPECT_EQ(tok.type, AsmTokenType::DirData);
}

TEST_F(AsmLexerTest, DirectiveText) {
    auto tok = lex_single(".text");
    EXPECT_EQ(tok.type, AsmTokenType::DirText);
}

TEST_F(AsmLexerTest, DirectiveByte) {
    auto tok = lex_single(".byte");
    EXPECT_EQ(tok.type, AsmTokenType::DirByte);
}

TEST_F(AsmLexerTest, DirectiveWord) {
    auto tok = lex_single(".word");
    EXPECT_EQ(tok.type, AsmTokenType::DirWord);
}

TEST_F(AsmLexerTest, DirectiveDword) {
    auto tok = lex_single(".dword");
    EXPECT_EQ(tok.type, AsmTokenType::DirDword);
}

TEST_F(AsmLexerTest, DirectiveInclude) {
    auto tok = lex_single(".include");
    EXPECT_EQ(tok.type, AsmTokenType::DirInclude);
}

TEST_F(AsmLexerTest, DirectiveAlign) {
    auto tok = lex_single(".align");
    EXPECT_EQ(tok.type, AsmTokenType::DirAlign);
}

TEST_F(AsmLexerTest, InvalidDirective) {
    AsmLexer lexer{".unknown"};
    auto tok = lexer.next_token();
    EXPECT_EQ(tok.type, AsmTokenType::Error);
    EXPECT_TRUE(lexer.has_errors());
}

// ============================================================================
// String Tests
// ============================================================================

TEST_F(AsmLexerTest, SimpleString) {
    auto tok = lex_single("\"hello\"");
    EXPECT_EQ(tok.type, AsmTokenType::String);
    EXPECT_EQ(tok.lexeme, "hello");
}

TEST_F(AsmLexerTest, StringWithEscape) {
    auto tok = lex_single("\"hello\\nworld\"");
    EXPECT_EQ(tok.type, AsmTokenType::String);
    EXPECT_EQ(tok.lexeme, "hello\\nworld");
}

TEST_F(AsmLexerTest, EmptyString) {
    auto tok = lex_single("\"\"");
    EXPECT_EQ(tok.type, AsmTokenType::String);
    EXPECT_EQ(tok.lexeme, "");
}

TEST_F(AsmLexerTest, UnterminatedString) {
    AsmLexer lexer{"\"hello"};
    auto tok = lexer.next_token();
    EXPECT_EQ(tok.type, AsmTokenType::Error);
    EXPECT_TRUE(lexer.has_errors());
}

// ============================================================================
// Punctuation Tests
// ============================================================================

TEST_F(AsmLexerTest, Comma) {
    auto tok = lex_single(",");
    EXPECT_EQ(tok.type, AsmTokenType::Comma);
}

TEST_F(AsmLexerTest, LeftBracket) {
    auto tok = lex_single("[");
    EXPECT_EQ(tok.type, AsmTokenType::LBracket);
}

TEST_F(AsmLexerTest, RightBracket) {
    auto tok = lex_single("]");
    EXPECT_EQ(tok.type, AsmTokenType::RBracket);
}

TEST_F(AsmLexerTest, Plus) {
    auto tok = lex_single("+");
    EXPECT_EQ(tok.type, AsmTokenType::Plus);
}

TEST_F(AsmLexerTest, Minus) {
    auto tok = lex_single("-");
    EXPECT_EQ(tok.type, AsmTokenType::Minus);
}

// ============================================================================
// Full Line Tests
// ============================================================================

TEST_F(AsmLexerTest, SimpleInstruction) {
    auto tokens = lex_all("ADD R0, R1, R2\n");
    ASSERT_GE(tokens.size(), 7u);
    EXPECT_EQ(tokens[0].type, AsmTokenType::Opcode);
    EXPECT_EQ(tokens[1].type, AsmTokenType::Register);
    EXPECT_EQ(tokens[2].type, AsmTokenType::Comma);
    EXPECT_EQ(tokens[3].type, AsmTokenType::Register);
    EXPECT_EQ(tokens[4].type, AsmTokenType::Comma);
    EXPECT_EQ(tokens[5].type, AsmTokenType::Register);
    EXPECT_EQ(tokens[6].type, AsmTokenType::Newline);
}

TEST_F(AsmLexerTest, ImmediateInstruction) {
    auto tokens = lex_all("ADDI R0, #10\n");
    ASSERT_GE(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].type, AsmTokenType::Opcode);
    EXPECT_EQ(tokens[1].type, AsmTokenType::Register);
    EXPECT_EQ(tokens[2].type, AsmTokenType::Comma);
    EXPECT_EQ(tokens[3].type, AsmTokenType::Immediate);
    EXPECT_EQ(tokens[4].type, AsmTokenType::Newline);
}

TEST_F(AsmLexerTest, LabeledInstruction) {
    auto tokens = lex_all("loop: ADD R0, R1, R2\n");
    ASSERT_GE(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].type, AsmTokenType::Label);
    EXPECT_EQ(tokens[0].lexeme, "loop");
    EXPECT_EQ(tokens[1].type, AsmTokenType::Opcode);
}

TEST_F(AsmLexerTest, JumpWithLabel) {
    auto tokens = lex_all("JMP loop\n");
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, AsmTokenType::Opcode);
    EXPECT_EQ(tokens[1].type, AsmTokenType::Identifier);
    EXPECT_EQ(tokens[1].lexeme, "loop");
}

TEST_F(AsmLexerTest, MemoryOperand) {
    auto tokens = lex_all("LOAD64 R0, [R1+8]\n");
    ASSERT_GE(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].type, AsmTokenType::Opcode);
    EXPECT_EQ(tokens[1].type, AsmTokenType::Register);
    EXPECT_EQ(tokens[2].type, AsmTokenType::Comma);
    EXPECT_EQ(tokens[3].type, AsmTokenType::LBracket);
    EXPECT_EQ(tokens[4].type, AsmTokenType::Register);
    EXPECT_EQ(tokens[5].type, AsmTokenType::Plus);
}

TEST_F(AsmLexerTest, DirectiveWithArg) {
    auto tokens = lex_all(".global main\n");
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, AsmTokenType::DirGlobal);
    EXPECT_EQ(tokens[1].type, AsmTokenType::Identifier);
    EXPECT_EQ(tokens[1].lexeme, "main");
}

TEST_F(AsmLexerTest, DataDirective) {
    auto tokens = lex_all(".byte \"Hello\", 0\n");
    ASSERT_GE(tokens.size(), 5u);
    EXPECT_EQ(tokens[0].type, AsmTokenType::DirByte);
    EXPECT_EQ(tokens[1].type, AsmTokenType::String);
    EXPECT_EQ(tokens[2].type, AsmTokenType::Comma);
}

// ============================================================================
// Multi-line Program Test
// ============================================================================

TEST_F(AsmLexerTest, SimpleProgram) {
    const char* source = R"(
; Counter program
.section text
.global main

main:
    ADDI R0, #0          ; counter = 0
    ADDI R1, #10         ; limit = 10
loop:
    LT R2, R0, R1        ; R2 = (counter < limit)
    JZ R2, end           ; if false, exit
    ADDI R0, #1          ; counter++
    JMP loop
end:
    HALT
)";

    AsmLexer lexer{source};
    std::vector<AsmToken> tokens;
    while (!lexer.at_end()) {
        auto tok = lexer.next_token();
        if (tok.is_eof()) {
            break;
        }
        tokens.push_back(tok);
    }

    // Verify no errors
    EXPECT_FALSE(lexer.has_errors());

    // Count specific token types
    int opcodes = 0, labels = 0, directives = 0;
    for (const auto& tok : tokens) {
        if (tok.type == AsmTokenType::Opcode) {
            opcodes++;
        }
        if (tok.type == AsmTokenType::Label) {
            labels++;
        }
        if (is_directive(tok.type)) {
            directives++;
        }
    }

    EXPECT_EQ(labels, 3);  // main, loop, end
    EXPECT_EQ(directives, 2);  // .section, .global
    EXPECT_GE(opcodes, 6);  // ADDI, ADDI, LT, JZ, ADDI, JMP, HALT
}

// ============================================================================
// Peek Tests
// ============================================================================

TEST_F(AsmLexerTest, PeekDoesNotConsume) {
    AsmLexer lexer{"ADD R0"};

    const auto& peeked = lexer.peek();
    EXPECT_EQ(peeked.type, AsmTokenType::Opcode);

    auto tok = lexer.next_token();
    EXPECT_EQ(tok.type, AsmTokenType::Opcode);

    tok = lexer.next_token();
    EXPECT_EQ(tok.type, AsmTokenType::Register);
}

TEST_F(AsmLexerTest, MultiplePeeks) {
    AsmLexer lexer{"ADD"};

    const auto& peek1 = lexer.peek();
    const auto& peek2 = lexer.peek();

    EXPECT_EQ(peek1.type, peek2.type);
    EXPECT_EQ(peek1.lexeme, peek2.lexeme);
}

// ============================================================================
// Opcode Lookup Tests
// ============================================================================

TEST_F(AsmLexerTest, OpcodeLookupInfo) {
    auto info = lookup_opcode("ADD");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->value, opcode::ADD);
    EXPECT_EQ(info->format, InstructionType::TypeA);
    EXPECT_EQ(info->min_operands, 3);
    EXPECT_EQ(info->max_operands, 3);
}

TEST_F(AsmLexerTest, OpcodeLookupTypeB) {
    auto info = lookup_opcode("ADDI");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, InstructionType::TypeB);
    EXPECT_EQ(info->min_operands, 2);
    EXPECT_EQ(info->max_operands, 2);
}

TEST_F(AsmLexerTest, OpcodeLookupTypeC) {
    auto info = lookup_opcode("JMP");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, InstructionType::TypeC);
    EXPECT_EQ(info->min_operands, 1);
    EXPECT_EQ(info->max_operands, 1);
}

TEST_F(AsmLexerTest, OpcodeLookupTypeS) {
    auto info = lookup_opcode("SHLI");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, InstructionType::TypeS);
    EXPECT_EQ(info->min_operands, 3);
    EXPECT_EQ(info->max_operands, 3);
}

TEST_F(AsmLexerTest, OpcodeLookupTypeD) {
    auto info = lookup_opcode("JZ");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, InstructionType::TypeD);
    EXPECT_EQ(info->min_operands, 2);
    EXPECT_EQ(info->max_operands, 2);
}

TEST_F(AsmLexerTest, OpcodeLookupTypeM) {
    auto info = lookup_opcode("LOAD64");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->format, InstructionType::TypeM);
    EXPECT_EQ(info->min_operands, 2);
    EXPECT_EQ(info->max_operands, 3);
}

TEST_F(AsmLexerTest, OpcodeLookupUnknown) {
    auto info = lookup_opcode("UNKNOWN");
    EXPECT_FALSE(info.has_value());
}

TEST_F(AsmLexerTest, OpcodeNameLookup) {
    EXPECT_EQ(opcode_name(opcode::ADD), "ADD");
    EXPECT_EQ(opcode_name(opcode::HALT), "HALT");
    EXPECT_EQ(opcode_name(0xFF), "");  // Unknown
}

// ============================================================================
// Source Location Tests
// ============================================================================

TEST_F(AsmLexerTest, LocationTracking) {
    AsmLexer lexer{"ADD\nSUB\n"};

    auto tok1 = lexer.next_token();  // ADD
    EXPECT_EQ(tok1.span.start.line, 1);
    EXPECT_EQ(tok1.span.start.column, 1);

    (void)lexer.next_token();  // Newline

    auto tok2 = lexer.next_token();  // SUB
    EXPECT_EQ(tok2.span.start.line, 2);
    EXPECT_EQ(tok2.span.start.column, 1);
}

}  // namespace
}  // namespace dotvm::core::asm_
