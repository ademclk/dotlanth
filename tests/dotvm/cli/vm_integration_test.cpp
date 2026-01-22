/// @file vm_integration_test.cpp
/// @brief TOOL-005 VM CLI integration tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/cli/commands/vm_info_command.hpp"
#include "dotvm/cli/commands/vm_run_command.hpp"
#include "dotvm/cli/commands/vm_validate_command.hpp"
#include "dotvm/cli/vm_cli_app.hpp"
#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/instruction.hpp"
#include "dotvm/core/opcode.hpp"
#include "dotvm/exec/dispatch_macros.hpp"

using namespace dotvm::cli;
using namespace dotvm::core;
namespace exec_op = dotvm::exec::opcode;

// ============================================================================
// Test Fixture
// ============================================================================

class VmIntegrationTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;
    std::vector<std::filesystem::path> temp_files_;

    void SetUp() override {
        // Create a unique temporary directory for tests
        temp_dir_ = std::filesystem::temp_directory_path() /
                    ("vm_test_" + std::to_string(std::time(nullptr)));
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override {
        // Clean up temp files
        for (const auto& path : temp_files_) {
            std::filesystem::remove(path);
        }
        std::filesystem::remove_all(temp_dir_);
    }

    /// Create a minimal valid bytecode file
    /// MOVI R1, #42; HALT
    std::filesystem::path create_minimal_bytecode() {
        auto path = temp_dir_ / "minimal.dot";
        temp_files_.push_back(path);

        // Create bytecode with MOVI R1, #42 followed by HALT
        std::vector<std::uint8_t> bytecode;

        // Code: MOVI R1, #42; HALT
        std::vector<std::uint32_t> code = {
            encode_type_b(exec_op::MOVI, 1, 42),  // MOVI R1, #42
            encode_type_a(opcode::HALT, 0, 0, 0)  // HALT
        };

        // Create header
        BytecodeHeader header = make_header(Architecture::Arch64, bytecode::FLAG_NONE,
                                            0,                                  // entry_point
                                            bytecode::HEADER_SIZE,              // const_pool_offset
                                            0,                                  // const_pool_size
                                            bytecode::HEADER_SIZE,              // code_offset
                                            code.size() * sizeof(std::uint32_t) // code_size
        );

        auto header_bytes = write_header(header);

        // Write to file
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(header_bytes.data()),
                   static_cast<std::streamsize>(header_bytes.size()));
        file.write(reinterpret_cast<const char*>(code.data()),
                   static_cast<std::streamsize>(code.size() * sizeof(std::uint32_t)));
        file.close();

        return path;
    }

    /// Create bytecode with invalid magic
    std::filesystem::path create_invalid_magic_bytecode() {
        auto path = temp_dir_ / "invalid_magic.dot";
        temp_files_.push_back(path);

        std::vector<std::uint8_t> data(bytecode::HEADER_SIZE, 0);
        // Invalid magic: "BADM" instead of "DOTM"
        data[0] = 'B';
        data[1] = 'A';
        data[2] = 'D';
        data[3] = 'M';

        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        file.close();

        return path;
    }

    /// Create bytecode with an infinite loop for testing --limit
    /// JMP +0 (infinite loop)
    std::filesystem::path create_infinite_loop_bytecode() {
        auto path = temp_dir_ / "loop.dot";
        temp_files_.push_back(path);

        // Code: JMP +0 (jumps to itself)
        std::vector<std::uint32_t> code = {
            encode_type_c(opcode::JMP, 0) // JMP +0 (infinite loop)
        };

        // Create header
        BytecodeHeader header = make_header(Architecture::Arch64, bytecode::FLAG_NONE,
                                            0,                                  // entry_point
                                            bytecode::HEADER_SIZE,              // const_pool_offset
                                            0,                                  // const_pool_size
                                            bytecode::HEADER_SIZE,              // code_offset
                                            code.size() * sizeof(std::uint32_t) // code_size
        );

        auto header_bytes = write_header(header);

        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(header_bytes.data()),
                   static_cast<std::streamsize>(header_bytes.size()));
        file.write(reinterpret_cast<const char*>(code.data()),
                   static_cast<std::streamsize>(code.size() * sizeof(std::uint32_t)));
        file.close();

        return path;
    }

    /// Create bytecode with constant pool
    std::filesystem::path create_bytecode_with_constants() {
        auto path = temp_dir_ / "with_constants.dot";
        temp_files_.push_back(path);

        // Constant pool: one i64 value (100)
        std::vector<std::uint8_t> const_pool;
        // Header: entry_count = 1
        const_pool.push_back(1);
        const_pool.push_back(0);
        const_pool.push_back(0);
        const_pool.push_back(0);
        // Entry 1: type=i64, value=100
        const_pool.push_back(bytecode::CONST_TYPE_I64);
        std::int64_t value = 100;
        for (int i = 0; i < 8; ++i) {
            const_pool.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
        }

        // Code: LOADK R1, #0; HALT
        std::vector<std::uint32_t> code = {
            encode_type_b(exec_op::LOADK, 1, 0),  // LOADK R1, #0 (load constant 0)
            encode_type_a(opcode::HALT, 0, 0, 0)  // HALT
        };

        std::uint64_t const_pool_offset = bytecode::HEADER_SIZE;
        std::uint64_t const_pool_size = const_pool.size();
        std::uint64_t code_offset = const_pool_offset + const_pool_size;
        std::uint64_t code_size = code.size() * sizeof(std::uint32_t);

        BytecodeHeader header =
            make_header(Architecture::Arch64, bytecode::FLAG_NONE, 0, const_pool_offset,
                        const_pool_size, code_offset, code_size);

        auto header_bytes = write_header(header);

        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(header_bytes.data()),
                   static_cast<std::streamsize>(header_bytes.size()));
        file.write(reinterpret_cast<const char*>(const_pool.data()),
                   static_cast<std::streamsize>(const_pool.size()));
        file.write(reinterpret_cast<const char*>(code.data()),
                   static_cast<std::streamsize>(code_size));
        file.close();

        return path;
    }
};

// ============================================================================
// Validate Command Tests
// ============================================================================

TEST_F(VmIntegrationTest, ValidateMinimalBytecode) {
    auto path = create_minimal_bytecode();

    std::ostringstream out;
    Terminal term(out, true);  // No colors

    VmValidateOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;

    VmExitCode code = commands::execute_validate(opts, global, term);
    EXPECT_EQ(code, VmExitCode::Success);
    EXPECT_TRUE(out.str().find("valid:") != std::string::npos);
}

TEST_F(VmIntegrationTest, ValidateInvalidMagic) {
    auto path = create_invalid_magic_bytecode();

    std::ostringstream out;
    Terminal term(out, true);

    VmValidateOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;

    VmExitCode code = commands::execute_validate(opts, global, term);
    EXPECT_EQ(code, VmExitCode::ValidationError);
    EXPECT_TRUE(out.str().find("error:") != std::string::npos);
}

TEST_F(VmIntegrationTest, ValidateBytecodeWithConstants) {
    auto path = create_bytecode_with_constants();

    std::ostringstream out;
    Terminal term(out, true);

    VmValidateOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;

    VmExitCode code = commands::execute_validate(opts, global, term);
    EXPECT_EQ(code, VmExitCode::Success);
}

// ============================================================================
// Info Command Tests
// ============================================================================

TEST_F(VmIntegrationTest, InfoMinimalBytecode) {
    auto path = create_minimal_bytecode();

    std::ostringstream out;
    Terminal term(out, true);

    VmInfoOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;

    VmExitCode code = commands::execute_info(opts, global, term);
    EXPECT_EQ(code, VmExitCode::Success);

    // Check for expected output
    std::string output = out.str();
    // Info output goes to cout, not the terminal
    // Just verify no error occurred
}

TEST_F(VmIntegrationTest, InfoInvalidFile) {
    auto path = create_invalid_magic_bytecode();

    std::ostringstream out;
    Terminal term(out, true);

    VmInfoOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;

    VmExitCode code = commands::execute_info(opts, global, term);
    EXPECT_EQ(code, VmExitCode::ValidationError);
}

// ============================================================================
// Run Command Tests
// ============================================================================

TEST_F(VmIntegrationTest, RunMinimalBytecode) {
    auto path = create_minimal_bytecode();

    std::ostringstream out;
    Terminal term(out, true);

    VmRunOptions opts;
    opts.input_file = path.string();
    opts.debug = false;
    opts.instruction_limit = 0;

    VmGlobalOptions global;
    global.quiet = true;  // Suppress output for test

    VmExitCode code = commands::execute_run(opts, global, term);
    EXPECT_EQ(code, VmExitCode::Success);
}

TEST_F(VmIntegrationTest, RunWithInstructionLimit) {
    auto path = create_infinite_loop_bytecode();

    std::ostringstream out;
    Terminal term(out, true);

    VmRunOptions opts;
    opts.input_file = path.string();
    opts.debug = false;
    opts.instruction_limit = 100;  // Should stop after 100 instructions

    VmGlobalOptions global;
    global.quiet = true;

    VmExitCode code = commands::execute_run(opts, global, term);
    // Should return RuntimeError due to execution limit
    EXPECT_EQ(code, VmExitCode::RuntimeError);
}

TEST_F(VmIntegrationTest, RunWithConstants) {
    auto path = create_bytecode_with_constants();

    std::ostringstream out;
    Terminal term(out, true);

    VmRunOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;
    global.quiet = true;

    VmExitCode code = commands::execute_run(opts, global, term);
    EXPECT_EQ(code, VmExitCode::Success);
}

TEST_F(VmIntegrationTest, RunInvalidFile) {
    auto path = create_invalid_magic_bytecode();

    std::ostringstream out;
    Terminal term(out, true);

    VmRunOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;
    global.quiet = true;

    VmExitCode code = commands::execute_run(opts, global, term);
    EXPECT_EQ(code, VmExitCode::ValidationError);
}

TEST_F(VmIntegrationTest, RunWithArchOverride) {
    auto path = create_minimal_bytecode();

    std::ostringstream out;
    Terminal term(out, true);

    VmRunOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;
    global.quiet = true;
    global.arch_override = 32;  // Override to 32-bit

    VmExitCode code = commands::execute_run(opts, global, term);
    EXPECT_EQ(code, VmExitCode::Success);
}

// ============================================================================
// Verbose Output Tests
// ============================================================================

TEST_F(VmIntegrationTest, RunVerboseOutput) {
    auto path = create_minimal_bytecode();

    std::ostringstream out;
    Terminal term(out, true);

    VmRunOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;
    global.verbose = true;
    global.quiet = false;

    VmExitCode code = commands::execute_run(opts, global, term);
    EXPECT_EQ(code, VmExitCode::Success);

    // Should contain file info
    std::string output = out.str();
    EXPECT_TRUE(output.find("File size:") != std::string::npos ||
                output.find("Result:") != std::string::npos);
}

// ============================================================================
// Debug Mode Tests (basic verification)
// ============================================================================

TEST_F(VmIntegrationTest, RunWithDebugMode) {
    auto path = create_minimal_bytecode();

    std::ostringstream out;
    Terminal term(out, true);

    VmRunOptions opts;
    opts.input_file = path.string();
    opts.debug = true;

    VmGlobalOptions global;
    global.quiet = false;

    // Debug output goes to cout, so just verify it doesn't crash
    VmExitCode code = commands::execute_run(opts, global, term);
    EXPECT_EQ(code, VmExitCode::Success);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(VmIntegrationTest, ValidateNonExistentFile) {
    std::ostringstream out;
    Terminal term(out, true);

    VmValidateOptions opts;
    opts.input_file = "/nonexistent/path/file.dot";

    VmGlobalOptions global;

    VmExitCode code = commands::execute_validate(opts, global, term);
    EXPECT_EQ(code, VmExitCode::ValidationError);
}

TEST_F(VmIntegrationTest, ValidateTooSmallFile) {
    auto path = temp_dir_ / "small.dot";
    temp_files_.push_back(path);

    // Create a file that's too small to contain a header
    std::ofstream file(path, std::ios::binary);
    file.write("DOTM", 4);  // Only magic bytes, incomplete header
    file.close();

    std::ostringstream out;
    Terminal term(out, true);

    VmValidateOptions opts;
    opts.input_file = path.string();

    VmGlobalOptions global;

    VmExitCode code = commands::execute_validate(opts, global, term);
    EXPECT_EQ(code, VmExitCode::ValidationError);
}
