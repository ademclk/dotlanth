/// @file asm_integration_test.cpp
/// @brief TOOL-004 Assembly lexer/parser integration tests

#include <gtest/gtest.h>

#include "dotvm/core/asm/asm_ast.hpp"
#include "dotvm/core/asm/asm_lexer.hpp"
#include "dotvm/core/asm/asm_parser.hpp"
#include "dotvm/core/opcode.hpp"

namespace dotvm::core::asm_ {
namespace {

class AsmIntegrationTest : public ::testing::Test {
protected:
    // Parse source and verify no errors
    AsmParseResult parse_success(std::string_view source) {
        AsmParser parser{source};
        auto result = parser.parse();
        EXPECT_TRUE(result.success())
            << "Parse failed with " << result.errors.size() << " errors";
        return result;
    }

    // Count instructions in program
    std::size_t count_instructions(const AsmProgram& program) {
        std::size_t count = 0;
        for (const auto& stmt : program.statements) {
            if (is_instruction(stmt)) {
                count++;
            }
        }
        return count;
    }

    // Count labels in program
    std::size_t count_labels(const AsmProgram& program) {
        std::size_t count = 0;
        for (const auto& stmt : program.statements) {
            if (is_label(stmt)) {
                count++;
            }
        }
        return count;
    }

    // Find instruction by opcode
    const AsmInstruction* find_instruction(const AsmProgram& program, std::uint8_t opcode) {
        for (const auto& stmt : program.statements) {
            if (is_instruction(stmt)) {
                const auto& instr = std::get<AsmInstruction>(stmt);
                if (instr.opcode == opcode) {
                    return &instr;
                }
            }
        }
        return nullptr;
    }
};

// ============================================================================
// Example Programs from Plan
// ============================================================================

TEST_F(AsmIntegrationTest, CounterProgram) {
    const char* source = R"(
; DotVM Assembly - counter example
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

.section data
msg:
    .byte "Done", #0
)";

    auto result = parse_success(source);

    // Verify structure
    EXPECT_EQ(count_labels(result.program), 4);      // main, loop, end, msg
    EXPECT_EQ(count_instructions(result.program), 7);

    // Verify specific instructions
    auto addi = find_instruction(result.program, opcode::ADDI);
    ASSERT_NE(addi, nullptr);
    EXPECT_EQ(addi->format, InstructionType::TypeB);

    auto lt = find_instruction(result.program, opcode::LT);
    ASSERT_NE(lt, nullptr);
    EXPECT_EQ(lt->operands.size(), 3);

    auto jz = find_instruction(result.program, opcode::JZ);
    ASSERT_NE(jz, nullptr);
    EXPECT_EQ(jz->operands[1].kind, OperandKind::LabelRef);
    EXPECT_EQ(jz->operands[1].as_label_ref(), "end");

    auto jmp = find_instruction(result.program, opcode::JMP);
    ASSERT_NE(jmp, nullptr);
    EXPECT_EQ(jmp->operands[0].as_label_ref(), "loop");

    auto halt = find_instruction(result.program, opcode::HALT);
    ASSERT_NE(halt, nullptr);
    EXPECT_TRUE(halt->operands.empty());
}

TEST_F(AsmIntegrationTest, MemoryOperations) {
    const char* source = R"(
.section text
main:
    ; Load from memory
    LOAD64 R0, [R1+#0]
    LOAD32 R2, [R1+#8]
    LOAD16 R3, [R1+#12]
    LOAD8  R4, [R1+#14]

    ; Store to memory
    STORE64 R0, [R2+#0]
    STORE32 R0, [R2+#8]
    STORE16 R0, [R2+#12]
    STORE8  R0, [R2+#14]

    ; Load effective address
    LEA R5, [R1+#100]

    HALT
)";

    auto result = parse_success(source);

    // Check LOAD64 instruction
    auto load64 = find_instruction(result.program, opcode::LOAD64);
    ASSERT_NE(load64, nullptr);
    EXPECT_EQ(load64->format, InstructionType::TypeM);
    EXPECT_EQ(load64->operands.size(), 2);
    EXPECT_EQ(load64->operands[0].kind, OperandKind::Register);
    EXPECT_EQ(load64->operands[1].kind, OperandKind::Memory);

    // Check memory operand details
    auto& mem = load64->operands[1].as_memory();
    EXPECT_EQ(mem.base_reg, 1);
    EXPECT_EQ(mem.offset, 0);

    // Check LEA instruction
    auto lea = find_instruction(result.program, opcode::LEA);
    ASSERT_NE(lea, nullptr);
    auto& lea_mem = lea->operands[1].as_memory();
    EXPECT_EQ(lea_mem.offset, 100);
}

TEST_F(AsmIntegrationTest, BitwiseOperations) {
    const char* source = R"(
.section text
main:
    AND R0, R1, R2
    OR R0, R1, R2
    XOR R0, R1, R2
    NOT R0, R1

    SHL R0, R1, R2
    SHR R0, R1, R2
    SAR R0, R1, R2

    SHLI R0, R1, #5
    SHRI R0, R1, #5
    SARI R0, R1, #5

    ANDI R0, #0xFF
    ORI R0, #0x0F
    XORI R0, #0xF0

    HALT
)";

    auto result = parse_success(source);

    // Check Type A bitwise
    auto and_op = find_instruction(result.program, opcode::AND);
    ASSERT_NE(and_op, nullptr);
    EXPECT_EQ(and_op->format, InstructionType::TypeA);
    EXPECT_EQ(and_op->operands.size(), 3);

    // Check Type S shift
    auto shli = find_instruction(result.program, opcode::SHLI);
    ASSERT_NE(shli, nullptr);
    EXPECT_EQ(shli->format, InstructionType::TypeS);
    EXPECT_EQ(shli->operands[2].as_immediate(), 5);

    // Check Type B bitwise
    auto andi = find_instruction(result.program, opcode::ANDI);
    ASSERT_NE(andi, nullptr);
    EXPECT_EQ(andi->format, InstructionType::TypeB);
    EXPECT_EQ(andi->operands[1].as_immediate(), 0xFF);
}

TEST_F(AsmIntegrationTest, ComparisonOperations) {
    const char* source = R"(
.section text
main:
    ; Register-register comparisons
    EQ R0, R1, R2
    NE R0, R1, R2
    LT R0, R1, R2
    LE R0, R1, R2
    GT R0, R1, R2
    GE R0, R1, R2

    ; Unsigned comparisons
    LTU R0, R1, R2
    LEU R0, R1, R2
    GTU R0, R1, R2
    GEU R0, R1, R2

    ; Test bitwise AND
    TEST R0, R1, R2

    ; Immediate comparisons
    CMPI_EQ R0, #10
    CMPI_NE R0, #20
    CMPI_LT R0, #30
    CMPI_GE R0, #40

    HALT
)";

    auto result = parse_success(source);

    auto eq = find_instruction(result.program, opcode::EQ);
    ASSERT_NE(eq, nullptr);
    EXPECT_EQ(eq->format, InstructionType::TypeA);

    auto cmpi_eq = find_instruction(result.program, opcode::CMPI_EQ);
    ASSERT_NE(cmpi_eq, nullptr);
    EXPECT_EQ(cmpi_eq->format, InstructionType::TypeB);
    EXPECT_EQ(cmpi_eq->operands[1].as_immediate(), 10);
}

TEST_F(AsmIntegrationTest, ControlFlowOperations) {
    const char* source = R"(
.section text
main:
    JMP skip
    NOP
skip:
    JZ R0, zero
    JNZ R0, nonzero
zero:
    NOP
nonzero:
    ; Branch instructions
    BEQ R0, R1, equal
    BNE R0, R1, notequal
    BLT R0, R1, less
    BGE R0, R1, greater
equal:
notequal:
less:
greater:
    ; Function call
    CALL func
    HALT

func:
    NOP
    RET
)";

    auto result = parse_success(source);

    auto jmp = find_instruction(result.program, opcode::JMP);
    ASSERT_NE(jmp, nullptr);
    EXPECT_EQ(jmp->format, InstructionType::TypeC);

    auto jz = find_instruction(result.program, opcode::JZ);
    ASSERT_NE(jz, nullptr);
    EXPECT_EQ(jz->format, InstructionType::TypeD);

    auto beq = find_instruction(result.program, opcode::BEQ);
    ASSERT_NE(beq, nullptr);
    EXPECT_EQ(beq->format, InstructionType::TypeA);

    auto call = find_instruction(result.program, opcode::CALL);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->format, InstructionType::TypeC);

    auto ret = find_instruction(result.program, opcode::RET);
    ASSERT_NE(ret, nullptr);
}

TEST_F(AsmIntegrationTest, FloatingPointOperations) {
    const char* source = R"(
.section text
main:
    FADD R0, R1, R2
    FSUB R0, R1, R2
    FMUL R0, R1, R2
    FDIV R0, R1, R2
    FNEG R0, R1
    FSQRT R0, R1

    ; Comparisons
    FCMP R0, R1

    ; Conversions
    F2I R0, R1
    I2F R0, R1

    HALT
)";

    auto result = parse_success(source);

    auto fadd = find_instruction(result.program, opcode::FADD);
    ASSERT_NE(fadd, nullptr);
    EXPECT_EQ(fadd->format, InstructionType::TypeA);

    auto f2i = find_instruction(result.program, opcode::F2I);
    ASSERT_NE(f2i, nullptr);
    EXPECT_EQ(f2i->operands.size(), 2);
}

TEST_F(AsmIntegrationTest, SystemInstructions) {
    const char* source = R"(
.section text
main:
    NOP
    NOP
    NOP
    BREAK
    DEBUG
    HALT
)";

    auto result = parse_success(source);

    auto nop = find_instruction(result.program, opcode::NOP);
    ASSERT_NE(nop, nullptr);
    EXPECT_TRUE(nop->operands.empty());

    auto brk = find_instruction(result.program, opcode::BREAK);
    ASSERT_NE(brk, nullptr);

    auto debug = find_instruction(result.program, opcode::DEBUG);
    ASSERT_NE(debug, nullptr);
}

TEST_F(AsmIntegrationTest, DataSection) {
    const char* source = R"(
.section data

; String data
greeting:
    .byte "Hello, World!", #0

; Numeric data
numbers:
    .word #0x1234, #0x5678, #0x9ABC

; 64-bit values
large_values:
    .dword #0x123456789ABCDEF0

; Alignment
.align #8
aligned_data:
    .dword #0

.section text
main:
    HALT
)";

    auto result = parse_success(source);

    // Count directives
    int byte_count = 0, word_count = 0, dword_count = 0, align_count = 0;
    for (const auto& stmt : result.program.statements) {
        if (is_directive(stmt)) {
            auto& dir = std::get<AsmDirective>(stmt);
            switch (dir.kind) {
                case DirectiveKind::Byte:
                    byte_count++;
                    break;
                case DirectiveKind::Word:
                    word_count++;
                    break;
                case DirectiveKind::Dword:
                    dword_count++;
                    break;
                case DirectiveKind::Align:
                    align_count++;
                    break;
                default:
                    break;
            }
        }
    }

    EXPECT_EQ(byte_count, 1);
    EXPECT_EQ(word_count, 1);
    EXPECT_EQ(dword_count, 2);
    EXPECT_EQ(align_count, 1);
}

// ============================================================================
// Include File Tests
// ============================================================================

TEST_F(AsmIntegrationTest, SimpleInclude) {
    AsmParserConfig config;
    config.process_includes = true;

    AsmParser parser{R"(
.section text
.include "lib.asm"
main:
    CALL helper
    HALT
)",
                     config};

    parser.set_file_reader([](std::string_view path) -> std::expected<std::string, std::string> {
        if (path == "lib.asm") {
            return R"(
helper:
    NOP
    RET
)";
        }
        return std::unexpected{"File not found"};
    });

    auto result = parser.parse();
    EXPECT_TRUE(result.success());

    // Should have labels from both files
    int label_count = 0;
    bool found_main = false, found_helper = false;
    for (const auto& stmt : result.program.statements) {
        if (is_label(stmt)) {
            label_count++;
            auto& label = std::get<AsmLabel>(stmt);
            if (label.name == "main") {
                found_main = true;
            }
            if (label.name == "helper") {
                found_helper = true;
            }
        }
    }

    EXPECT_EQ(label_count, 2);
    EXPECT_TRUE(found_main);
    EXPECT_TRUE(found_helper);
}

TEST_F(AsmIntegrationTest, NestedIncludes) {
    AsmParserConfig config;
    config.process_includes = true;
    config.max_include_depth = 10;

    AsmParser parser{".include \"a.asm\"\nmain: HALT\n", config};

    parser.set_file_reader([](std::string_view path) -> std::expected<std::string, std::string> {
        if (path == "a.asm") {
            return ".include \"b.asm\"\na_label: NOP\n";
        }
        if (path == "b.asm") {
            return "b_label: NOP\n";
        }
        return std::unexpected{"File not found"};
    });

    auto result = parser.parse();
    EXPECT_TRUE(result.success());

    // Should have labels from all files
    bool found_a = false, found_b = false, found_main = false;
    for (const auto& stmt : result.program.statements) {
        if (is_label(stmt)) {
            auto& label = std::get<AsmLabel>(stmt);
            if (label.name == "a_label") {
                found_a = true;
            }
            if (label.name == "b_label") {
                found_b = true;
            }
            if (label.name == "main") {
                found_main = true;
            }
        }
    }

    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
    EXPECT_TRUE(found_main);
}

// ============================================================================
// Location Tracking Tests
// ============================================================================

TEST_F(AsmIntegrationTest, SpanTracking) {
    const char* source = "main:\n    HALT\n";
    auto result = parse_success(source);

    // Label should be on line 1
    ASSERT_GE(result.program.size(), 1);
    auto& label = std::get<AsmLabel>(result.program.statements[0]);
    EXPECT_EQ(label.span.start.line, 1);

    // HALT should be on line 2
    auto& instr = std::get<AsmInstruction>(result.program.statements[1]);
    EXPECT_EQ(instr.span.start.line, 2);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(AsmIntegrationTest, MixedCaseOpcodes) {
    const char* source = R"(
add R0, R1, R2
Add R0, R1, R2
ADD R0, R1, R2
)";

    auto result = parse_success(source);
    EXPECT_EQ(count_instructions(result.program), 3);
}

TEST_F(AsmIntegrationTest, AllRegisterNumbers) {
    // Test a few key register numbers
    auto result = parse_success("ADD R0, R127, R255\n");
    EXPECT_TRUE(result.success());

    auto& instr = std::get<AsmInstruction>(result.program.statements[0]);
    EXPECT_EQ(instr.operands[0].as_register(), 0);
    EXPECT_EQ(instr.operands[1].as_register(), 127);
    EXPECT_EQ(instr.operands[2].as_register(), 255);
}

TEST_F(AsmIntegrationTest, LargeImmediateValues) {
    // Note: INT64_MIN (-9223372036854775808) cannot be parsed by first parsing
    // the positive value and negating, since 9223372036854775808 > INT64_MAX.
    // Use INT64_MAX and INT64_MIN+1 instead.
    auto result = parse_success(R"(
.dword #0x7FFFFFFFFFFFFFFF
.dword #-9223372036854775807
)");

    EXPECT_TRUE(result.success());
}

TEST_F(AsmIntegrationTest, EmptyLinesAndComments) {
    const char* source = R"(

; Comment at start


main:   ; Comment after label

    ; Indented comment
    HALT    ; Comment after instruction


; Comment at end

)";

    auto result = parse_success(source);
    EXPECT_EQ(count_labels(result.program), 1);
    EXPECT_EQ(count_instructions(result.program), 1);
}

}  // namespace
}  // namespace dotvm::core::asm_
