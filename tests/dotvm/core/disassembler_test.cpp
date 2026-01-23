/// @file disassembler_test.cpp
/// @brief TOOL-008 Disassembler unit tests

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/disassembler.hpp"
#include "dotvm/core/instruction.hpp"
#include "dotvm/core/opcode.hpp"

using namespace dotvm::core;

// ============================================================================
// get_instruction_type Tests
// ============================================================================

TEST(DisassemblerTest, GetInstructionTypeArithmetic) {
    // Type A arithmetic
    EXPECT_EQ(get_instruction_type(opcode::ADD), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::SUB), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::MUL), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::DIV), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::NEG), InstructionType::TypeA);

    // Type B arithmetic (immediate)
    EXPECT_EQ(get_instruction_type(opcode::ADDI), InstructionType::TypeB);
    EXPECT_EQ(get_instruction_type(opcode::SUBI), InstructionType::TypeB);
    EXPECT_EQ(get_instruction_type(opcode::MULI), InstructionType::TypeB);
}

TEST(DisassemblerTest, GetInstructionTypeBitwise) {
    // Type A bitwise
    EXPECT_EQ(get_instruction_type(opcode::AND), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::OR), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::XOR), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::NOT), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::SHL), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::SHR), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::SAR), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::ROL), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::ROR), InstructionType::TypeA);

    // Type S shift immediate
    EXPECT_EQ(get_instruction_type(opcode::SHLI), InstructionType::TypeS);
    EXPECT_EQ(get_instruction_type(opcode::SHRI), InstructionType::TypeS);
    EXPECT_EQ(get_instruction_type(opcode::SARI), InstructionType::TypeS);

    // Type B bitwise immediate
    EXPECT_EQ(get_instruction_type(opcode::ANDI), InstructionType::TypeB);
    EXPECT_EQ(get_instruction_type(opcode::ORI), InstructionType::TypeB);
    EXPECT_EQ(get_instruction_type(opcode::XORI), InstructionType::TypeB);
}

TEST(DisassemblerTest, GetInstructionTypeComparison) {
    // Type A comparison
    EXPECT_EQ(get_instruction_type(opcode::EQ), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::NE), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::LT), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::LE), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::GT), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::GE), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::TEST), InstructionType::TypeA);

    // Type B comparison immediate
    EXPECT_EQ(get_instruction_type(opcode::CMPI_EQ), InstructionType::TypeB);
    EXPECT_EQ(get_instruction_type(opcode::CMPI_NE), InstructionType::TypeB);
    EXPECT_EQ(get_instruction_type(opcode::CMPI_LT), InstructionType::TypeB);
    EXPECT_EQ(get_instruction_type(opcode::CMPI_GE), InstructionType::TypeB);
}

TEST(DisassemblerTest, GetInstructionTypeControlFlow) {
    // Type C
    EXPECT_EQ(get_instruction_type(opcode::JMP), InstructionType::TypeC);
    EXPECT_EQ(get_instruction_type(opcode::CALL), InstructionType::TypeC);
    EXPECT_EQ(get_instruction_type(opcode::RET), InstructionType::TypeC);
    EXPECT_EQ(get_instruction_type(opcode::HALT), InstructionType::TypeC);

    // Type D (jump with register test)
    EXPECT_EQ(get_instruction_type(opcode::JZ), InstructionType::TypeD);
    EXPECT_EQ(get_instruction_type(opcode::JNZ), InstructionType::TypeD);

    // Type A branches
    EXPECT_EQ(get_instruction_type(opcode::BEQ), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::BNE), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::BLT), InstructionType::TypeA);
    EXPECT_EQ(get_instruction_type(opcode::BGE), InstructionType::TypeA);
}

TEST(DisassemblerTest, GetInstructionTypeMemory) {
    // Type M memory operations
    EXPECT_EQ(get_instruction_type(opcode::LOAD8), InstructionType::TypeM);
    EXPECT_EQ(get_instruction_type(opcode::LOAD16), InstructionType::TypeM);
    EXPECT_EQ(get_instruction_type(opcode::LOAD32), InstructionType::TypeM);
    EXPECT_EQ(get_instruction_type(opcode::LOAD64), InstructionType::TypeM);
    EXPECT_EQ(get_instruction_type(opcode::STORE8), InstructionType::TypeM);
    EXPECT_EQ(get_instruction_type(opcode::STORE16), InstructionType::TypeM);
    EXPECT_EQ(get_instruction_type(opcode::STORE32), InstructionType::TypeM);
    EXPECT_EQ(get_instruction_type(opcode::STORE64), InstructionType::TypeM);
    EXPECT_EQ(get_instruction_type(opcode::LEA), InstructionType::TypeM);
}

TEST(DisassemblerTest, GetInstructionTypeSystem) {
    EXPECT_EQ(get_instruction_type(opcode::NOP), InstructionType::TypeC);
    EXPECT_EQ(get_instruction_type(opcode::BREAK), InstructionType::TypeC);
    EXPECT_EQ(get_instruction_type(opcode::SYSCALL), InstructionType::TypeC);
    EXPECT_EQ(get_instruction_type(opcode::DEBUG), InstructionType::TypeC);
}

// ============================================================================
// decode_instruction Tests
// ============================================================================

TEST(DisassemblerTest, DecodeTypeAInstruction) {
    // ADD R1, R2, R3 -> 0x00010203
    std::array<std::uint8_t, 4> code = {0x03, 0x02, 0x01, 0x00};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.address, 0u);
    EXPECT_EQ(instr.opcode, opcode::ADD);
    EXPECT_EQ(instr.mnemonic, "ADD");
    EXPECT_EQ(instr.type, InstructionType::TypeA);
    EXPECT_EQ(instr.rd, 1);
    EXPECT_EQ(instr.rs1, 2);
    EXPECT_EQ(instr.rs2, 3);
    EXPECT_FALSE(instr.is_branch);
    EXPECT_FALSE(instr.is_jump);
    EXPECT_FALSE(instr.is_terminator);
}

TEST(DisassemblerTest, DecodeTypeBInstruction) {
    // ADDI R1, #100 (0x0064) -> 0x06016400
    std::array<std::uint8_t, 4> code = {0x64, 0x00, 0x01, 0x06};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::ADDI);
    EXPECT_EQ(instr.mnemonic, "ADDI");
    EXPECT_EQ(instr.type, InstructionType::TypeB);
    EXPECT_EQ(instr.rd, 1);
    EXPECT_EQ(instr.immediate, 100);
}

TEST(DisassemblerTest, DecodeTypeBInstructionNegative) {
    // ADDI R1, #-1 (0xFFFF) -> sign-extended to -1
    std::array<std::uint8_t, 4> code = {0xFF, 0xFF, 0x01, 0x06};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::ADDI);
    EXPECT_EQ(instr.immediate, -1);
}

TEST(DisassemblerTest, DecodeTypeCJump) {
    // JMP +8 -> 0x40000008
    std::array<std::uint8_t, 4> code = {0x08, 0x00, 0x00, 0x40};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::JMP);
    EXPECT_EQ(instr.mnemonic, "JMP");
    EXPECT_EQ(instr.type, InstructionType::TypeC);
    EXPECT_EQ(instr.immediate, 8);
    EXPECT_TRUE(instr.is_jump);
    EXPECT_FALSE(instr.is_branch);
    EXPECT_FALSE(instr.is_terminator);
    ASSERT_TRUE(instr.target.has_value());
    EXPECT_EQ(instr.target.value(), 8u);  // PC(0) + offset(8) = 8
}

TEST(DisassemblerTest, DecodeTypeCJumpNegative) {
    // JMP -4 (backward jump) -> 0x40FFFFFC
    std::array<std::uint8_t, 4> code = {0xFC, 0xFF, 0xFF, 0x40};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 8);

    EXPECT_EQ(instr.opcode, opcode::JMP);
    EXPECT_EQ(instr.immediate, -4);
    ASSERT_TRUE(instr.target.has_value());
    EXPECT_EQ(instr.target.value(), 4u);  // PC(8) + offset(-4) = 4
}

TEST(DisassemblerTest, DecodeTypeCHalt) {
    // HALT -> 0x5F000000
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::HALT);
    EXPECT_EQ(instr.mnemonic, "HALT");
    EXPECT_TRUE(instr.is_terminator);
    EXPECT_FALSE(instr.target.has_value());
}

TEST(DisassemblerTest, DecodeTypeDInstruction) {
    // JZ R1, +16 -> 0x41010010
    std::array<std::uint8_t, 4> code = {0x10, 0x00, 0x01, 0x41};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::JZ);
    EXPECT_EQ(instr.mnemonic, "JZ");
    EXPECT_EQ(instr.type, InstructionType::TypeD);
    EXPECT_EQ(instr.rs1, 1);  // Register to test
    EXPECT_EQ(instr.immediate, 16);
    EXPECT_TRUE(instr.is_branch);
    EXPECT_FALSE(instr.is_jump);
    ASSERT_TRUE(instr.target.has_value());
    EXPECT_EQ(instr.target.value(), 16u);
}

TEST(DisassemblerTest, DecodeTypeSInstruction) {
    // SHLI R1, R2, #5 -> 0x29010205
    std::array<std::uint8_t, 4> code = {0x05, 0x02, 0x01, 0x29};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::SHLI);
    EXPECT_EQ(instr.mnemonic, "SHLI");
    EXPECT_EQ(instr.type, InstructionType::TypeS);
    EXPECT_EQ(instr.rd, 1);
    EXPECT_EQ(instr.rs1, 2);
    EXPECT_EQ(instr.immediate, 5);  // shamt6
}

TEST(DisassemblerTest, DecodeTypeMInstruction) {
    // LOAD64 R1, [R2+4] -> 0x63010204
    std::array<std::uint8_t, 4> code = {0x04, 0x02, 0x01, 0x63};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::LOAD64);
    EXPECT_EQ(instr.mnemonic, "LOAD64");
    EXPECT_EQ(instr.type, InstructionType::TypeM);
    EXPECT_EQ(instr.rd, 1);
    EXPECT_EQ(instr.rs1, 2);
    EXPECT_EQ(instr.immediate, 4);  // offset8
}

TEST(DisassemblerTest, DecodeTypeMInstructionNegativeOffset) {
    // LOAD64 R1, [R2-1] -> 0x630102FF
    std::array<std::uint8_t, 4> code = {0xFF, 0x02, 0x01, 0x63};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::LOAD64);
    EXPECT_EQ(instr.immediate, -1);  // signed offset8
}

TEST(DisassemblerTest, DecodeCall) {
    // CALL +100 -> 0x50000064
    std::array<std::uint8_t, 4> code = {0x64, 0x00, 0x00, 0x50};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::CALL);
    EXPECT_EQ(instr.mnemonic, "CALL");
    EXPECT_TRUE(instr.is_jump);
    ASSERT_TRUE(instr.target.has_value());
    EXPECT_EQ(instr.target.value(), 100u);
}

TEST(DisassemblerTest, DecodeRet) {
    // RET -> 0x51000000
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x51};  // Little-endian
    auto instr = decode_instruction(std::span{code}, 0);

    EXPECT_EQ(instr.opcode, opcode::RET);
    EXPECT_EQ(instr.mnemonic, "RET");
    EXPECT_TRUE(instr.is_terminator);
    EXPECT_FALSE(instr.target.has_value());
}

// ============================================================================
// disassemble Tests
// ============================================================================

TEST(DisassemblerTest, DisassembleEmptyCode) {
    std::span<const std::uint8_t> code;
    auto instrs = disassemble(code);
    EXPECT_TRUE(instrs.empty());
}

TEST(DisassemblerTest, DisassembleSingleInstruction) {
    // HALT instruction
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};
    auto instrs = disassemble(std::span{code});

    ASSERT_EQ(instrs.size(), 1u);
    EXPECT_EQ(instrs[0].opcode, opcode::HALT);
    EXPECT_EQ(instrs[0].address, 0u);
}

TEST(DisassemblerTest, DisassembleMultipleInstructions) {
    // ADDI R1, #10; ADDI R2, #20; HALT
    std::array<std::uint8_t, 12> code = {
        0x0A, 0x00, 0x01, 0x06,  // ADDI R1, #10
        0x14, 0x00, 0x02, 0x06,  // ADDI R2, #20
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto instrs = disassemble(std::span{code});

    ASSERT_EQ(instrs.size(), 3u);

    EXPECT_EQ(instrs[0].opcode, opcode::ADDI);
    EXPECT_EQ(instrs[0].address, 0u);
    EXPECT_EQ(instrs[0].rd, 1);
    EXPECT_EQ(instrs[0].immediate, 10);

    EXPECT_EQ(instrs[1].opcode, opcode::ADDI);
    EXPECT_EQ(instrs[1].address, 4u);
    EXPECT_EQ(instrs[1].rd, 2);
    EXPECT_EQ(instrs[1].immediate, 20);

    EXPECT_EQ(instrs[2].opcode, opcode::HALT);
    EXPECT_EQ(instrs[2].address, 8u);
}

TEST(DisassemblerTest, DisassembleWithRawBytes) {
    // ADD R1, R2, R3 -> 0x00010203
    std::array<std::uint8_t, 4> code = {0x03, 0x02, 0x01, 0x00};
    auto instrs = disassemble(std::span{code});

    ASSERT_EQ(instrs.size(), 1u);
    EXPECT_EQ(instrs[0].raw_bytes, 0x00010203u);
}

// ============================================================================
// build_cfg Tests
// ============================================================================

TEST(DisassemblerTest, BuildCfgNoBranches) {
    // Just ADDI and HALT - no branches (but entry point is always marked)
    std::array<std::uint8_t, 8> code = {
        0x0A, 0x00, 0x01, 0x06,  // ADDI R1, #10
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto instrs = disassemble(std::span{code});
    auto cfg = build_cfg(instrs, 0);

    // Entry point is always added to branch_targets
    EXPECT_EQ(cfg.branch_targets.size(), 1u);
    EXPECT_TRUE(cfg.branch_targets.contains(0u));
    EXPECT_TRUE(cfg.call_targets.empty());
}

TEST(DisassemblerTest, BuildCfgWithJump) {
    // JMP +8; NOP; NOP; HALT (JMP targets address 8)
    std::array<std::uint8_t, 16> code = {
        0x08, 0x00, 0x00, 0x40,  // JMP +8
        0x00, 0x00, 0x00, 0xF0,  // NOP
        0x00, 0x00, 0x00, 0x5F,  // HALT (target of JMP)
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto instrs = disassemble(std::span{code});
    auto cfg = build_cfg(instrs, 0);

    EXPECT_TRUE(cfg.branch_targets.contains(8u));
}

TEST(DisassemblerTest, BuildCfgWithCall) {
    // CALL +8; HALT; NOP; RET (CALL targets address 8)
    std::array<std::uint8_t, 16> code = {
        0x08, 0x00, 0x00, 0x50,  // CALL +8
        0x00, 0x00, 0x00, 0x5F,  // HALT
        0x00, 0x00, 0x00, 0xF0,  // NOP (function start)
        0x00, 0x00, 0x00, 0x51   // RET
    };
    auto instrs = disassemble(std::span{code});
    auto cfg = build_cfg(instrs, 0);

    EXPECT_TRUE(cfg.call_targets.contains(8u));
}

TEST(DisassemblerTest, BuildCfgWithConditionalBranch) {
    // JZ R1, +8; NOP; HALT
    std::array<std::uint8_t, 12> code = {
        0x08, 0x00, 0x01, 0x41,  // JZ R1, +8
        0x00, 0x00, 0x00, 0xF0,  // NOP
        0x00, 0x00, 0x00, 0x5F   // HALT (branch target)
    };
    auto instrs = disassemble(std::span{code});
    auto cfg = build_cfg(instrs, 0);

    EXPECT_TRUE(cfg.branch_targets.contains(8u));
}

TEST(DisassemblerTest, BuildCfgEntryPoint) {
    // Entry point at address 0 should be in branch_targets
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};  // HALT
    auto instrs = disassemble(std::span{code});
    auto cfg = build_cfg(instrs, 0);

    // Entry point should be marked
    EXPECT_TRUE(cfg.branch_targets.contains(0u));
}

// ============================================================================
// format_instruction Tests
// ============================================================================

TEST(DisassemblerTest, FormatInstructionBasic) {
    std::array<std::uint8_t, 4> code = {0x03, 0x02, 0x01, 0x00};  // ADD R1, R2, R3
    auto instr = decode_instruction(std::span{code}, 0);

    DisasmOptions opts;
    auto text = format_instruction(instr, nullptr, opts);

    // Should contain address and mnemonic
    EXPECT_NE(text.find("0x0000"), std::string::npos);
    EXPECT_NE(text.find("ADD"), std::string::npos);
    EXPECT_NE(text.find("R1"), std::string::npos);
    EXPECT_NE(text.find("R2"), std::string::npos);
    EXPECT_NE(text.find("R3"), std::string::npos);
}

TEST(DisassemblerTest, FormatInstructionShowBytes) {
    std::array<std::uint8_t, 4> code = {0x03, 0x02, 0x01, 0x00};  // ADD R1, R2, R3
    auto instr = decode_instruction(std::span{code}, 0);

    DisasmOptions opts;
    opts.show_bytes = true;
    auto text = format_instruction(instr, nullptr, opts);

    // Should contain raw bytes
    EXPECT_NE(text.find("00 01 02 03"), std::string::npos);
}

TEST(DisassemblerTest, FormatInstructionImmediate) {
    std::array<std::uint8_t, 4> code = {0x64, 0x00, 0x01, 0x06};  // ADDI R1, #100
    auto instr = decode_instruction(std::span{code}, 0);

    DisasmOptions opts;
    auto text = format_instruction(instr, nullptr, opts);

    EXPECT_NE(text.find("ADDI"), std::string::npos);
    EXPECT_NE(text.find("R1"), std::string::npos);
    EXPECT_NE(text.find("#100"), std::string::npos);
}

TEST(DisassemblerTest, FormatInstructionNegativeImmediate) {
    std::array<std::uint8_t, 4> code = {0xFF, 0xFF, 0x01, 0x06};  // ADDI R1, #-1
    auto instr = decode_instruction(std::span{code}, 0);

    DisasmOptions opts;
    auto text = format_instruction(instr, nullptr, opts);

    EXPECT_NE(text.find("#-1"), std::string::npos);
}

TEST(DisassemblerTest, FormatInstructionMemory) {
    std::array<std::uint8_t, 4> code = {0x04, 0x02, 0x01, 0x63};  // LOAD64 R1, [R2+4]
    auto instr = decode_instruction(std::span{code}, 0);

    DisasmOptions opts;
    auto text = format_instruction(instr, nullptr, opts);

    EXPECT_NE(text.find("LOAD64"), std::string::npos);
    EXPECT_NE(text.find("R1"), std::string::npos);
    EXPECT_NE(text.find("R2"), std::string::npos);
}

TEST(DisassemblerTest, FormatInstructionWithLabels) {
    // JMP +8 at PC 0 targets address 8
    std::array<std::uint8_t, 4> code = {0x08, 0x00, 0x00, 0x40};
    auto instr = decode_instruction(std::span{code}, 0);

    // Create a CFG with the target labeled
    ControlFlowGraph cfg;
    cfg.branch_targets.insert(8);

    DisasmOptions opts;
    opts.show_labels = true;
    auto text = format_instruction(instr, &cfg, opts);

    // Should use label instead of raw offset
    EXPECT_NE(text.find(".L"), std::string::npos);
}

// ============================================================================
// format_disassembly Tests
// ============================================================================

TEST(DisassemblerTest, FormatDisassemblyBasic) {
    std::array<std::uint8_t, 8> code = {
        0x0A, 0x00, 0x01, 0x06,  // ADDI R1, #10
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto instrs = disassemble(std::span{code});
    auto cfg = build_cfg(instrs, 0);

    BytecodeHeader header{};
    header.version = 26;
    header.arch = Architecture::Arch64;
    header.entry_point = 0;
    header.code_size = 8;

    std::vector<Value> constants;
    DisasmOptions opts;

    auto text = format_disassembly(instrs, cfg, header, constants, opts);

    EXPECT_NE(text.find("ADDI"), std::string::npos);
    EXPECT_NE(text.find("HALT"), std::string::npos);
}

TEST(DisassemblerTest, FormatDisassemblyWithLabels) {
    // JMP to address 8, then HALT at 4, target at 8
    std::array<std::uint8_t, 12> code = {
        0x08, 0x00, 0x00, 0x40,  // JMP +8
        0x00, 0x00, 0x00, 0x5F,  // HALT
        0x00, 0x00, 0x00, 0x5F   // HALT (jump target)
    };
    auto instrs = disassemble(std::span{code});
    auto cfg = build_cfg(instrs, 0);

    BytecodeHeader header{};
    header.entry_point = 0;
    header.code_size = 12;

    std::vector<Value> constants;
    DisasmOptions opts;
    opts.show_labels = true;

    auto text = format_disassembly(instrs, cfg, header, constants, opts);

    // Should have label for entry point and jump target
    EXPECT_NE(text.find("_entry"), std::string::npos);
}

// ============================================================================
// format_json Tests
// ============================================================================

TEST(DisassemblerTest, FormatJsonBasic) {
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};  // HALT
    auto instrs = disassemble(std::span{code});
    auto cfg = build_cfg(instrs, 0);

    BytecodeHeader header{};
    header.magic = bytecode::MAGIC_BYTES;
    header.version = 26;
    header.arch = Architecture::Arch64;
    header.entry_point = 0;
    header.code_size = 4;

    std::vector<Value> constants;

    auto json = format_json(instrs, cfg, header, constants);

    // Basic JSON structure checks
    EXPECT_NE(json.find("\"header\""), std::string::npos);
    EXPECT_NE(json.find("\"instructions\""), std::string::npos);
    EXPECT_NE(json.find("\"version\""), std::string::npos);
    EXPECT_NE(json.find("HALT"), std::string::npos);
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST(DisassemblerTest, UnknownOpcodeHandling) {
    // Use a reserved/unknown opcode (e.g., 0x99)
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x99};
    auto instr = decode_instruction(std::span{code}, 0);

    // Should still decode, mnemonic should indicate unknown
    EXPECT_EQ(instr.opcode, 0x99);
    // Unknown opcodes should return empty or "???" mnemonic
    EXPECT_TRUE(instr.mnemonic.empty() || instr.mnemonic == "???");
}

TEST(DisassemblerTest, DecodeAtNonZeroAddress) {
    std::array<std::uint8_t, 8> code = {
        0x00, 0x00, 0x00, 0xF0,  // NOP at address 0
        0x00, 0x00, 0x00, 0x5F   // HALT at address 4
    };

    // Decode second instruction
    auto instr = decode_instruction(std::span{code.data() + 4, 4}, 4);

    EXPECT_EQ(instr.address, 4u);
    EXPECT_EQ(instr.opcode, opcode::HALT);
}

TEST(DisassemblerTest, TruncatedCodeHandling) {
    // Code section not aligned to 4 bytes - should handle gracefully
    std::array<std::uint8_t, 6> code = {
        0x00, 0x00, 0x00, 0x5F,  // HALT
        0x00, 0x00               // Incomplete instruction
    };
    auto instrs = disassemble(std::span{code});

    // Should only disassemble complete instructions
    ASSERT_EQ(instrs.size(), 1u);
    EXPECT_EQ(instrs[0].opcode, opcode::HALT);
}
