/// @file asm_parser_test.cpp
/// @brief TOOL-004 Assembly parser unit tests

#include <gtest/gtest.h>

#include "dotvm/core/asm/asm_parser.hpp"
#include "dotvm/core/opcode.hpp"

namespace dotvm::core::asm_ {
namespace {

class AsmParserTest : public ::testing::Test {
protected:
    // Helper to parse source and check for success
    AsmParseResult parse(std::string_view source, AsmParserConfig config = {}) {
        AsmParser parser{source, config};
        return parser.parse();
    }

    // Get instruction from statement
    const AsmInstruction& as_instruction(const AsmStatement& stmt) {
        return std::get<AsmInstruction>(stmt);
    }

    // Get label from statement
    const AsmLabel& as_label(const AsmStatement& stmt) { return std::get<AsmLabel>(stmt); }

    // Get directive from statement
    const AsmDirective& as_directive(const AsmStatement& stmt) {
        return std::get<AsmDirective>(stmt);
    }
};

// ============================================================================
// Basic Parsing Tests
// ============================================================================

TEST_F(AsmParserTest, EmptySource) {
    auto result = parse("");
    EXPECT_TRUE(result.success());
    EXPECT_TRUE(result.program.empty());
}

TEST_F(AsmParserTest, WhitespaceOnly) {
    auto result = parse("   \n\n  \t  \n");
    EXPECT_TRUE(result.success());
    EXPECT_TRUE(result.program.empty());
}

TEST_F(AsmParserTest, CommentOnly) {
    auto result = parse("; this is a comment\n; another comment\n");
    EXPECT_TRUE(result.success());
    EXPECT_TRUE(result.program.empty());
}

// ============================================================================
// Label Tests
// ============================================================================

TEST_F(AsmParserTest, SimpleLabel) {
    auto result = parse("main:\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    EXPECT_TRUE(is_label(result.program.statements[0]));
    EXPECT_EQ(as_label(result.program.statements[0]).name, "main");
}

TEST_F(AsmParserTest, MultipleLabels) {
    auto result = parse("start:\nloop:\nend:\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 3);
    EXPECT_EQ(as_label(result.program.statements[0]).name, "start");
    EXPECT_EQ(as_label(result.program.statements[1]).name, "loop");
    EXPECT_EQ(as_label(result.program.statements[2]).name, "end");
}

// ============================================================================
// Instruction Tests
// ============================================================================

TEST_F(AsmParserTest, NoOperandInstruction) {
    auto result = parse("HALT\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    EXPECT_TRUE(is_instruction(result.program.statements[0]));
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.mnemonic, "HALT");
    EXPECT_EQ(instr.opcode, opcode::HALT);
    EXPECT_TRUE(instr.operands.empty());
}

TEST_F(AsmParserTest, TwoOperandInstruction) {
    auto result = parse("ADDI R0, #10\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.mnemonic, "ADDI");
    EXPECT_EQ(instr.opcode, opcode::ADDI);
    ASSERT_EQ(instr.operands.size(), 2);
    EXPECT_EQ(instr.operands[0].kind, OperandKind::Register);
    EXPECT_EQ(instr.operands[0].as_register(), 0);
    EXPECT_EQ(instr.operands[1].kind, OperandKind::Immediate);
    EXPECT_EQ(instr.operands[1].as_immediate(), 10);
}

TEST_F(AsmParserTest, ThreeOperandInstruction) {
    auto result = parse("ADD R0, R1, R2\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.mnemonic, "ADD");
    EXPECT_EQ(instr.opcode, opcode::ADD);
    ASSERT_EQ(instr.operands.size(), 3);
    EXPECT_EQ(instr.operands[0].as_register(), 0);
    EXPECT_EQ(instr.operands[1].as_register(), 1);
    EXPECT_EQ(instr.operands[2].as_register(), 2);
}

TEST_F(AsmParserTest, JumpWithLabelReference) {
    auto result = parse("JMP loop\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.mnemonic, "JMP");
    ASSERT_EQ(instr.operands.size(), 1);
    EXPECT_EQ(instr.operands[0].kind, OperandKind::LabelRef);
    EXPECT_EQ(instr.operands[0].as_label_ref(), "loop");
}

TEST_F(AsmParserTest, ConditionalJump) {
    auto result = parse("JZ R0, end\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.mnemonic, "JZ");
    ASSERT_EQ(instr.operands.size(), 2);
    EXPECT_EQ(instr.operands[0].kind, OperandKind::Register);
    EXPECT_EQ(instr.operands[1].kind, OperandKind::LabelRef);
}

// ============================================================================
// Memory Operand Tests
// ============================================================================

TEST_F(AsmParserTest, MemoryOperandSimple) {
    auto result = parse("LOAD64 R0, [R1]\n");
    EXPECT_TRUE(result.success()) << "Errors: " << result.errors.size();
    ASSERT_EQ(result.program.size(), 1);
    auto& instr = as_instruction(result.program.statements[0]);
    ASSERT_EQ(instr.operands.size(), 2);
    EXPECT_EQ(instr.operands[1].kind, OperandKind::Memory);
    auto& mem = instr.operands[1].as_memory();
    EXPECT_EQ(mem.base_reg, 1);
    EXPECT_EQ(mem.offset, 0);
}

TEST_F(AsmParserTest, MemoryOperandPositiveOffset) {
    auto result = parse("LOAD64 R0, [R1+#8]\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    auto& instr = as_instruction(result.program.statements[0]);
    ASSERT_EQ(instr.operands.size(), 2);
    auto& mem = instr.operands[1].as_memory();
    EXPECT_EQ(mem.base_reg, 1);
    EXPECT_EQ(mem.offset, 8);
}

TEST_F(AsmParserTest, MemoryOperandNegativeOffset) {
    auto result = parse("LOAD64 R0, [R1-#4]\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    auto& instr = as_instruction(result.program.statements[0]);
    auto& mem = instr.operands[1].as_memory();
    EXPECT_EQ(mem.offset, -4);
}

TEST_F(AsmParserTest, StoreInstruction) {
    auto result = parse("STORE64 R0, [R1+#0]\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.opcode, opcode::STORE64);
}

// ============================================================================
// Immediate Value Tests
// ============================================================================

TEST_F(AsmParserTest, DecimalImmediate) {
    auto result = parse("ADDI R0, #123\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.operands[1].as_immediate(), 123);
}

TEST_F(AsmParserTest, NegativeImmediate) {
    auto result = parse("ADDI R0, #-45\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.operands[1].as_immediate(), -45);
}

TEST_F(AsmParserTest, HexImmediate) {
    auto result = parse("ADDI R0, #0xFF\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.operands[1].as_immediate(), 255);
}

TEST_F(AsmParserTest, BinaryImmediate) {
    auto result = parse("ADDI R0, #0b1010\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.operands[1].as_immediate(), 10);
}

// ============================================================================
// Directive Tests
// ============================================================================

TEST_F(AsmParserTest, SectionDirective) {
    auto result = parse(".section text\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    EXPECT_TRUE(is_directive(result.program.statements[0]));
    auto& dir = as_directive(result.program.statements[0]);
    EXPECT_EQ(dir.kind, DirectiveKind::Section);
    ASSERT_EQ(dir.args.size(), 1);
    EXPECT_EQ(dir.args[0].as_label_ref(), "text");
}

TEST_F(AsmParserTest, GlobalDirective) {
    auto result = parse(".global main\n");
    EXPECT_TRUE(result.success());
    auto& dir = as_directive(result.program.statements[0]);
    EXPECT_EQ(dir.kind, DirectiveKind::Global);
    EXPECT_EQ(dir.args[0].as_label_ref(), "main");
}

TEST_F(AsmParserTest, DataDirective) {
    auto result = parse(".data\n");
    EXPECT_TRUE(result.success());
    auto& dir = as_directive(result.program.statements[0]);
    EXPECT_EQ(dir.kind, DirectiveKind::Data);
}

TEST_F(AsmParserTest, TextDirective) {
    auto result = parse(".text\n");
    EXPECT_TRUE(result.success());
    auto& dir = as_directive(result.program.statements[0]);
    EXPECT_EQ(dir.kind, DirectiveKind::Text);
}

TEST_F(AsmParserTest, ByteDirectiveWithString) {
    auto result = parse(".byte \"Hello\", #0\n");
    EXPECT_TRUE(result.success());
    auto& dir = as_directive(result.program.statements[0]);
    EXPECT_EQ(dir.kind, DirectiveKind::Byte);
    ASSERT_EQ(dir.args.size(), 2);
    EXPECT_EQ(dir.args[0].kind, OperandKind::String);
    EXPECT_EQ(dir.args[0].as_string(), "Hello");
    EXPECT_EQ(dir.args[1].kind, OperandKind::Immediate);
}

TEST_F(AsmParserTest, WordDirective) {
    auto result = parse(".word #0x1234, #0x5678\n");
    EXPECT_TRUE(result.success());
    auto& dir = as_directive(result.program.statements[0]);
    EXPECT_EQ(dir.kind, DirectiveKind::Word);
    ASSERT_EQ(dir.args.size(), 2);
    EXPECT_EQ(dir.args[0].as_immediate(), 0x1234);
    EXPECT_EQ(dir.args[1].as_immediate(), 0x5678);
}

TEST_F(AsmParserTest, DwordDirective) {
    auto result = parse(".dword #0x123456789ABC\n");
    EXPECT_TRUE(result.success());
    auto& dir = as_directive(result.program.statements[0]);
    EXPECT_EQ(dir.kind, DirectiveKind::Dword);
    ASSERT_EQ(dir.args.size(), 1);
}

TEST_F(AsmParserTest, AlignDirective) {
    auto result = parse(".align #8\n");
    EXPECT_TRUE(result.success());
    auto& dir = as_directive(result.program.statements[0]);
    EXPECT_EQ(dir.kind, DirectiveKind::Align);
    ASSERT_EQ(dir.args.size(), 1);
    EXPECT_EQ(dir.args[0].as_immediate(), 8);
}

// ============================================================================
// Label + Instruction Tests
// ============================================================================

TEST_F(AsmParserTest, LabelWithInstruction) {
    auto result = parse("main: HALT\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 2);
    EXPECT_TRUE(is_label(result.program.statements[0]));
    EXPECT_TRUE(is_instruction(result.program.statements[1]));
}

TEST_F(AsmParserTest, LabelOnSeparateLine) {
    auto result = parse("main:\n    HALT\n");
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 2);
    EXPECT_TRUE(is_label(result.program.statements[0]));
    EXPECT_TRUE(is_instruction(result.program.statements[1]));
}

// ============================================================================
// Complete Program Tests
// ============================================================================

TEST_F(AsmParserTest, SimpleProgram) {
    const char* source = R"(
.section text
.global main

main:
    ADDI R0, #0
    ADDI R1, #10
loop:
    LT R2, R0, R1
    JZ R2, end
    ADDI R0, #1
    JMP loop
end:
    HALT
)";

    auto result = parse(source);
    EXPECT_TRUE(result.success()) << "Errors: " << result.errors.size();

    // Count statement types
    int labels = 0, instructions = 0, directives = 0;
    for (const auto& stmt : result.program.statements) {
        if (is_label(stmt)) {
            labels++;
        }
        if (is_instruction(stmt)) {
            instructions++;
        }
        if (is_directive(stmt)) {
            directives++;
        }
    }

    EXPECT_EQ(labels, 3);        // main, loop, end
    EXPECT_EQ(directives, 2);    // .section, .global
    EXPECT_EQ(instructions, 7);  // ADDI x2, LT, JZ, ADDI, JMP, HALT
}

TEST_F(AsmParserTest, DataSection) {
    const char* source = R"(
.section data
msg:
    .byte "Hello, World!", #0
count:
    .dword #0
)";

    auto result = parse(source);
    EXPECT_TRUE(result.success());

    // Find the .byte directive
    bool found_byte = false;
    for (const auto& stmt : result.program.statements) {
        if (is_directive(stmt)) {
            auto& dir = as_directive(stmt);
            if (dir.kind == DirectiveKind::Byte) {
                found_byte = true;
                EXPECT_EQ(dir.args.size(), 2);  // string + null terminator
            }
        }
    }
    EXPECT_TRUE(found_byte);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(AsmParserTest, InvalidOperandCount) {
    auto result = parse("ADD R0\n");  // ADD needs 3 operands
    EXPECT_FALSE(result.success());
    EXPECT_TRUE(result.errors.has_errors());
}

TEST_F(AsmParserTest, UnexpectedToken) {
    auto result = parse("!@#\n");
    EXPECT_FALSE(result.success());
}

TEST_F(AsmParserTest, MissingComma) {
    auto result = parse("ADD R0 R1 R2\n");  // Missing commas
    EXPECT_FALSE(result.success());
}

TEST_F(AsmParserTest, UnterminatedMemoryOperand) {
    auto result = parse("LOAD64 R0, [R1\n");  // Missing ]
    EXPECT_FALSE(result.success());
}

TEST_F(AsmParserTest, ErrorRecovery) {
    const char* source = R"(
ADD R0
HALT
)";
    auto result = parse(source);
    // Should have error for first line but parse HALT successfully
    EXPECT_FALSE(result.success());

    // Should still have parsed the HALT instruction
    bool found_halt = false;
    for (const auto& stmt : result.program.statements) {
        if (is_instruction(stmt)) {
            auto& instr = as_instruction(stmt);
            if (instr.opcode == opcode::HALT) {
                found_halt = true;
            }
        }
    }
    EXPECT_TRUE(found_halt);
}

// ============================================================================
// Include Directive Tests (without file reader)
// ============================================================================

TEST_F(AsmParserTest, IncludeDirectiveNoProcessing) {
    AsmParserConfig config;
    config.process_includes = false;

    auto result = parse(".include \"test.asm\"\n", config);
    EXPECT_TRUE(result.success());
    ASSERT_EQ(result.program.size(), 1);
    auto& dir = as_directive(result.program.statements[0]);
    EXPECT_EQ(dir.kind, DirectiveKind::Include);
    ASSERT_EQ(dir.args.size(), 1);
    EXPECT_EQ(dir.args[0].as_string(), "test.asm");
}

TEST_F(AsmParserTest, IncludeWithFileReader) {
    AsmParserConfig config;
    config.process_includes = true;

    AsmParser parser{".include \"test.asm\"\n", config};

    // Set up file reader that returns a simple program
    parser.set_file_reader([](std::string_view path) -> std::expected<std::string, std::string> {
        if (path == "test.asm") {
            return "HALT\n";
        }
        return std::unexpected{"File not found: " + std::string{path}};
    });

    auto result = parser.parse();
    EXPECT_TRUE(result.success());

    // Should have included the HALT instruction
    bool found_halt = false;
    for (const auto& stmt : result.program.statements) {
        if (is_instruction(stmt)) {
            auto& instr = as_instruction(stmt);
            if (instr.opcode == opcode::HALT) {
                found_halt = true;
            }
        }
    }
    EXPECT_TRUE(found_halt);
}

TEST_F(AsmParserTest, CircularIncludeDetection) {
    AsmParserConfig config;
    config.process_includes = true;

    AsmParser parser{".include \"a.asm\"\n", config};

    // Set up file reader that creates circular dependency
    parser.set_file_reader([](std::string_view path) -> std::expected<std::string, std::string> {
        if (path == "a.asm") {
            return ".include \"b.asm\"\n";
        }
        if (path == "b.asm") {
            return ".include \"a.asm\"\n";  // Circular!
        }
        return std::unexpected{"File not found"};
    });

    auto result = parser.parse();
    EXPECT_FALSE(result.success());

    // Should have circular include error
    bool found_error = false;
    for (const auto& err : result.errors) {
        if (err.error == AsmError::CircularInclude) {
            found_error = true;
        }
    }
    EXPECT_TRUE(found_error);
}

TEST_F(AsmParserTest, IncludeDepthExceeded) {
    AsmParserConfig config;
    config.process_includes = true;
    config.max_include_depth = 2;

    AsmParser parser{".include \"a.asm\"\n", config};

    // Create deep include chain (but not circular)
    parser.set_file_reader([](std::string_view path) -> std::expected<std::string, std::string> {
        if (path == "a.asm") {
            return ".include \"b.asm\"\n";
        }
        if (path == "b.asm") {
            return ".include \"c.asm\"\n";  // depth = 2
        }
        if (path == "c.asm") {
            return ".include \"d.asm\"\n";  // depth = 3, exceeds max
        }
        if (path == "d.asm") {
            return "HALT\n";
        }
        return std::unexpected{"File not found"};
    });

    auto result = parser.parse();
    EXPECT_FALSE(result.success());

    // Should have depth exceeded error
    bool found_error = false;
    for (const auto& err : result.errors) {
        if (err.error == AsmError::IncludeDepthExceeded) {
            found_error = true;
        }
    }
    EXPECT_TRUE(found_error);
}

// ============================================================================
// Instruction Format Tests
// ============================================================================

TEST_F(AsmParserTest, TypeAFormat) {
    auto result = parse("ADD R0, R1, R2\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.format, InstructionType::TypeA);
}

TEST_F(AsmParserTest, TypeBFormat) {
    auto result = parse("ADDI R0, #10\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.format, InstructionType::TypeB);
}

TEST_F(AsmParserTest, TypeCFormat) {
    auto result = parse("JMP end\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.format, InstructionType::TypeC);
}

TEST_F(AsmParserTest, TypeSFormat) {
    auto result = parse("SHLI R0, R1, #5\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.format, InstructionType::TypeS);
}

TEST_F(AsmParserTest, TypeDFormat) {
    auto result = parse("JZ R0, end\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.format, InstructionType::TypeD);
}

TEST_F(AsmParserTest, TypeMFormat) {
    auto result = parse("LOAD64 R0, [R1+#8]\n");
    EXPECT_TRUE(result.success());
    auto& instr = as_instruction(result.program.statements[0]);
    EXPECT_EQ(instr.format, InstructionType::TypeM);
}

}  // namespace
}  // namespace dotvm::core::asm_
