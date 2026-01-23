/// @file dis_integration_test.cpp
/// @brief TOOL-008 Disassembler integration tests

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/cli/dis_cli_app.hpp"
#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/disassembler.hpp"

using namespace dotvm;

// ============================================================================
// Test Fixture with Bytecode File Creation
// ============================================================================

class DisassemblerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temp directory for test files
        temp_dir_ = std::filesystem::temp_directory_path() / "dotdis_test";
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        // Clean up temp files
        std::filesystem::remove_all(temp_dir_);
    }

    /// @brief Create a minimal bytecode file with given code
    [[nodiscard]] std::filesystem::path create_bytecode_file(const std::string& name,
                                                             std::span<const std::uint8_t> code,
                                                             std::uint64_t entry_point = 0) {
        auto path = temp_dir_ / name;

        // Build bytecode file in memory
        std::vector<std::uint8_t> file_data;

        // Header: 48 bytes
        core::BytecodeHeader header =
            core::make_header(core::Architecture::Arch64, core::bytecode::FLAG_NONE, entry_point,
                              core::bytecode::HEADER_SIZE,  // const_pool_offset
                              4,  // const_pool_size (just entry count = 0)
                              core::bytecode::HEADER_SIZE + 4,  // code_offset (after const pool)
                              code.size());

        auto header_bytes = core::write_header(header);
        file_data.insert(file_data.end(), header_bytes.begin(), header_bytes.end());

        // Constant pool: 4 bytes (entry_count = 0)
        std::array<std::uint8_t, 4> const_pool = {0x00, 0x00, 0x00, 0x00};
        file_data.insert(file_data.end(), const_pool.begin(), const_pool.end());

        // Code section
        file_data.insert(file_data.end(), code.begin(), code.end());

        // Write to file
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(file_data.data()),
                   static_cast<std::streamsize>(file_data.size()));

        return path;
    }

    std::filesystem::path temp_dir_;
};

// ============================================================================
// Core Disassembly Integration Tests
// ============================================================================

TEST_F(DisassemblerIntegrationTest, DisassembleHaltOnly) {
    // Create bytecode with just HALT
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};  // HALT
    auto path = create_bytecode_file("halt.dot", code);

    // Run disassembler
    cli::DisCliApp app;
    const char* argv[] = {"dotdis", path.c_str()};
    auto parse_result = app.parse(2, argv);
    EXPECT_EQ(parse_result, cli::DisExitCode::Success);

    // Capture stdout by running the disassembly command directly
    auto instrs = core::disassemble(code);
    ASSERT_EQ(instrs.size(), 1u);
    EXPECT_EQ(instrs[0].mnemonic, "HALT");
}

TEST_F(DisassemblerIntegrationTest, DisassembleArithmeticSequence) {
    // ADDI R1, #10; ADDI R2, #20; ADD R3, R1, R2; HALT
    std::array<std::uint8_t, 16> code = {
        0x0A, 0x00, 0x01, 0x06,  // ADDI R1, #10
        0x14, 0x00, 0x02, 0x06,  // ADDI R2, #20
        0x02, 0x01, 0x03, 0x00,  // ADD R3, R1, R2
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto path = create_bytecode_file("arithmetic.dot", code);

    auto instrs = core::disassemble(code);
    ASSERT_EQ(instrs.size(), 4u);

    EXPECT_EQ(instrs[0].mnemonic, "ADDI");
    EXPECT_EQ(instrs[0].rd, 1);
    EXPECT_EQ(instrs[0].immediate, 10);

    EXPECT_EQ(instrs[1].mnemonic, "ADDI");
    EXPECT_EQ(instrs[1].rd, 2);
    EXPECT_EQ(instrs[1].immediate, 20);

    EXPECT_EQ(instrs[2].mnemonic, "ADD");
    EXPECT_EQ(instrs[2].rd, 3);
    EXPECT_EQ(instrs[2].rs1, 1);
    EXPECT_EQ(instrs[2].rs2, 2);

    EXPECT_EQ(instrs[3].mnemonic, "HALT");
}

TEST_F(DisassemblerIntegrationTest, DisassembleWithLoop) {
    // Simple countdown loop:
    // ADDI R1, #5        ; counter = 5
    // .loop:
    // SUBI R1, #1        ; counter--
    // JNZ R1, -4         ; if (counter != 0) goto .loop
    // HALT
    std::array<std::uint8_t, 16> code = {
        0x05, 0x00, 0x01, 0x06,  // ADDI R1, #5
        0x01, 0x00, 0x01, 0x07,  // SUBI R1, #1
        0xFC, 0xFF, 0x01, 0x42,  // JNZ R1, -4 (back to SUBI)
        0x00, 0x00, 0x00, 0x5F   // HALT
    };

    auto instrs = core::disassemble(code);
    auto cfg = core::build_cfg(instrs, 0);

    ASSERT_EQ(instrs.size(), 4u);

    // JNZ should have target at address 4 (the SUBI)
    EXPECT_TRUE(instrs[2].is_branch);
    ASSERT_TRUE(instrs[2].target.has_value());
    EXPECT_EQ(instrs[2].target.value(), 4u);  // PC(8) + offset(-4) = 4

    // CFG should have the loop target marked
    EXPECT_TRUE(cfg.branch_targets.contains(4u));
}

TEST_F(DisassemblerIntegrationTest, DisassembleWithCall) {
    // CALL +8; HALT; NOP; RET
    std::array<std::uint8_t, 16> code = {
        0x08, 0x00, 0x00, 0x50,  // CALL +8
        0x00, 0x00, 0x00, 0x5F,  // HALT
        0x00, 0x00, 0x00, 0xF0,  // NOP (function start)
        0x00, 0x00, 0x00, 0x51   // RET
    };

    auto instrs = core::disassemble(code);
    auto cfg = core::build_cfg(instrs, 0);

    EXPECT_TRUE(instrs[0].is_jump);
    ASSERT_TRUE(instrs[0].target.has_value());
    EXPECT_EQ(instrs[0].target.value(), 8u);

    // Function target should be in call_targets
    EXPECT_TRUE(cfg.call_targets.contains(8u));
}

TEST_F(DisassemblerIntegrationTest, DisassembleMemoryOps) {
    // LEA R1, [R0+100]; STORE64 R2, [R1+0]; LOAD64 R3, [R1+0]; HALT
    std::array<std::uint8_t, 16> code = {
        0x64, 0x00, 0x01, 0x68,  // LEA R1, [R0+100]
        0x00, 0x01, 0x02, 0x67,  // STORE64 R2, [R1+0]
        0x00, 0x01, 0x03, 0x63,  // LOAD64 R3, [R1+0]
        0x00, 0x00, 0x00, 0x5F   // HALT
    };

    auto instrs = core::disassemble(code);
    ASSERT_EQ(instrs.size(), 4u);

    EXPECT_EQ(instrs[0].mnemonic, "LEA");
    EXPECT_EQ(instrs[0].rd, 1);
    EXPECT_EQ(instrs[0].rs1, 0);
    EXPECT_EQ(instrs[0].immediate, 100);

    EXPECT_EQ(instrs[1].mnemonic, "STORE64");
    EXPECT_EQ(instrs[2].mnemonic, "LOAD64");
}

// ============================================================================
// Output Format Integration Tests
// ============================================================================

TEST_F(DisassemblerIntegrationTest, FormatTextOutput) {
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};  // HALT

    auto instrs = core::disassemble(code);
    auto cfg = core::build_cfg(instrs, 0);

    core::BytecodeHeader header{};
    header.magic = core::bytecode::MAGIC_BYTES;
    header.version = 26;
    header.arch = core::Architecture::Arch64;
    header.entry_point = 0;
    header.code_size = 4;

    std::vector<core::Value> constants;
    core::DisasmOptions opts;

    auto text = core::format_disassembly(instrs, cfg, header, constants, opts);

    // Should contain version and architecture info
    EXPECT_NE(text.find("v26"), std::string::npos);
    EXPECT_NE(text.find("64-bit"), std::string::npos);

    // Should contain HALT instruction
    EXPECT_NE(text.find("HALT"), std::string::npos);
}

TEST_F(DisassemblerIntegrationTest, FormatJsonOutput) {
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};  // HALT

    auto instrs = core::disassemble(code);
    auto cfg = core::build_cfg(instrs, 0);

    core::BytecodeHeader header{};
    header.magic = core::bytecode::MAGIC_BYTES;
    header.version = 26;
    header.arch = core::Architecture::Arch64;
    header.entry_point = 0;
    header.code_size = 4;

    std::vector<core::Value> constants;

    auto json = core::format_json(instrs, cfg, header, constants);

    // Verify JSON structure
    EXPECT_NE(json.find("\"header\""), std::string::npos);
    EXPECT_NE(json.find("\"instructions\""), std::string::npos);
    EXPECT_NE(json.find("\"version\": 26"), std::string::npos);
    EXPECT_NE(json.find("HALT"), std::string::npos);
}

TEST_F(DisassemblerIntegrationTest, FormatWithShowBytes) {
    std::array<std::uint8_t, 4> code = {0x03, 0x02, 0x01, 0x00};  // ADD R1, R2, R3
    auto instr = core::decode_instruction(code, 0);

    core::DisasmOptions opts;
    opts.show_bytes = true;

    auto text = core::format_instruction(instr, nullptr, opts);

    // Should show raw bytes in the output
    EXPECT_NE(text.find("00 01 02 03"), std::string::npos);
}

TEST_F(DisassemblerIntegrationTest, FormatWithLabels) {
    // JMP +8; HALT; HALT
    std::array<std::uint8_t, 12> code = {
        0x08, 0x00, 0x00, 0x40,  // JMP +8
        0x00, 0x00, 0x00, 0x5F,  // HALT
        0x00, 0x00, 0x00, 0x5F   // HALT (jump target)
    };

    auto instrs = core::disassemble(code);
    auto cfg = core::build_cfg(instrs, 0);

    core::BytecodeHeader header{};
    header.entry_point = 0;
    header.code_size = 12;

    std::vector<core::Value> constants;
    core::DisasmOptions opts;
    opts.show_labels = true;

    auto text = core::format_disassembly(instrs, cfg, header, constants, opts);

    // Should have entry point label
    EXPECT_NE(text.find("_entry"), std::string::npos);
}

// ============================================================================
// CLI Integration Tests
// ============================================================================

TEST_F(DisassemblerIntegrationTest, CliWithRealFile) {
    // Create a bytecode file
    std::array<std::uint8_t, 8> code = {
        0x0A, 0x00, 0x01, 0x06,  // ADDI R1, #10
        0x00, 0x00, 0x00, 0x5F   // HALT
    };
    auto path = create_bytecode_file("test.dot", code);

    // Parse CLI args
    cli::DisCliApp app;
    std::string path_str = path.string();
    const char* argv[] = {"dotdis", path_str.c_str()};

    auto parse_result = app.parse(2, argv);
    EXPECT_EQ(parse_result, cli::DisExitCode::Success);
    EXPECT_FALSE(app.help_requested());
    EXPECT_EQ(app.options().input_file, path_str);
}

TEST_F(DisassemblerIntegrationTest, CliJsonFormat) {
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};
    auto path = create_bytecode_file("json_test.dot", code);

    cli::DisCliApp app;
    std::string path_str = path.string();
    const char* argv[] = {"dotdis", "--format", "json", path_str.c_str()};

    auto parse_result = app.parse(4, argv);
    EXPECT_EQ(parse_result, cli::DisExitCode::Success);
    EXPECT_EQ(app.options().format, cli::DisOutputFormat::Json);
}

TEST_F(DisassemblerIntegrationTest, CliShowOptions) {
    std::array<std::uint8_t, 4> code = {0x00, 0x00, 0x00, 0x5F};
    auto path = create_bytecode_file("options_test.dot", code);

    cli::DisCliApp app;
    std::string path_str = path.string();
    const char* argv[] = {"dotdis", "--show-bytes", "--show-labels", "--annotate",
                          path_str.c_str()};

    auto parse_result = app.parse(5, argv);
    EXPECT_EQ(parse_result, cli::DisExitCode::Success);
    EXPECT_TRUE(app.options().show_bytes);
    EXPECT_TRUE(app.options().show_labels);
    EXPECT_TRUE(app.options().annotate);
}
