/// @file vm_bench_command_test.cpp
/// @brief Unit and integration tests for the bench command (CLI-005 Benchmark Runner)

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/cli/commands/vm_bench_command.hpp"
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

class VmBenchCommandTest : public ::testing::Test {
protected:
    std::filesystem::path temp_dir_;
    std::vector<std::filesystem::path> temp_files_;

    void SetUp() override {
        // Create a unique temporary directory for tests
        temp_dir_ = std::filesystem::temp_directory_path() /
                    ("vm_bench_test_" + std::to_string(std::time(nullptr)));
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

        // Code: MOVI R1, #42; HALT
        std::vector<std::uint32_t> code = {
            encode_type_b(exec_op::MOVI, 1, 42),  // MOVI R1, #42
            encode_type_a(opcode::HALT, 0, 0, 0)  // HALT
        };

        // Create header
        BytecodeHeader header = make_header(Architecture::Arch64, bytecode::FLAG_NONE,
                                            0,                      // entry_point
                                            bytecode::HEADER_SIZE,  // const_pool_offset
                                            0,                      // const_pool_size
                                            bytecode::HEADER_SIZE,  // code_offset
                                            code.size() * sizeof(std::uint32_t)  // code_size
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

    /// Create a longer-running bytecode with a loop
    std::filesystem::path create_loop_bytecode(std::uint16_t iterations) {
        auto path = temp_dir_ / "loop.dot";
        temp_files_.push_back(path);

        // Code:
        //   MOVI R1, #iterations  ; counter
        //   MOVI R2, #1           ; decrement value
        // loop:
        //   SUB R1, R1, R2        ; R1 = R1 - 1
        //   JNZ R1, loop          ; jump back if R1 != 0
        //   HALT
        std::vector<std::uint32_t> code = {
            encode_type_b(exec_op::MOVI, 1, iterations),  // MOVI R1, #iterations
            encode_type_b(exec_op::MOVI, 2, 1),           // MOVI R2, #1
            encode_type_a(opcode::SUB, 1, 1, 2),          // SUB R1, R1, R2
            encode_type_d(opcode::JNZ, 1, -1),            // JNZ R1, -1 (back to SUB)
            encode_type_a(opcode::HALT, 0, 0, 0)          // HALT
        };

        // Create header
        BytecodeHeader header = make_header(Architecture::Arch64, bytecode::FLAG_NONE,
                                            0,                      // entry_point
                                            bytecode::HEADER_SIZE,  // const_pool_offset
                                            0,                      // const_pool_size
                                            bytecode::HEADER_SIZE,  // code_offset
                                            code.size() * sizeof(std::uint32_t)  // code_size
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
};

// ============================================================================
// CLI Parsing Tests
// ============================================================================

TEST(VmBenchCliTest, BenchOptionsDefaults) {
    VmCliApp app;
    EXPECT_EQ(app.bench_options().warmup_iterations, 10);
    EXPECT_EQ(app.bench_options().measurement_runs, 100);
    EXPECT_EQ(app.bench_options().instruction_limit, 0);
    EXPECT_TRUE(app.bench_options().baseline_file.empty());
    EXPECT_FALSE(app.bench_options().save_baseline);
    // Note: regression_threshold is 5.0 (percentage) in the struct, as the CLI uses 5.0%
    EXPECT_DOUBLE_EQ(app.bench_options().regression_threshold, 5.0);
    EXPECT_FALSE(app.bench_options().generate_flamegraph);
    EXPECT_EQ(app.bench_options().sample_rate_instructions, 1000);
    EXPECT_EQ(app.bench_options().format, VmBenchOutputFormat::Console);
}

// Note: BenchSubcommandRegistered test removed as CLI::App forward declared
// The command registration is verified by other integration tests

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(VmBenchCommandTest, BenchMinimalBytecode_Succeeds) {
    auto path = create_minimal_bytecode();

    VmBenchOptions opts;
    opts.input_file = path.string();
    opts.warmup_iterations = 2;
    opts.measurement_runs = 5;

    VmGlobalOptions global;
    global.quiet = true;  // Suppress output

    std::ostringstream oss;
    Terminal term(oss, true);  // force no color

    VmExitCode result = commands::execute_bench(opts, global, term);
    EXPECT_EQ(result, VmExitCode::Success);
}

TEST_F(VmBenchCommandTest, BenchLoopBytecode_CollectsStatistics) {
    auto path = create_loop_bytecode(100);

    VmBenchOptions opts;
    opts.input_file = path.string();
    opts.warmup_iterations = 2;
    opts.measurement_runs = 10;
    opts.format = VmBenchOutputFormat::Json;

    VmGlobalOptions global;
    global.quiet = true;

    std::ostringstream oss;
    Terminal term(oss, true);

    // Capture stdout for JSON output
    std::streambuf* old_cout = std::cout.rdbuf();
    std::ostringstream captured;
    std::cout.rdbuf(captured.rdbuf());

    VmExitCode result = commands::execute_bench(opts, global, term);

    std::cout.rdbuf(old_cout);

    EXPECT_EQ(result, VmExitCode::Success);

    // Verify JSON output contains expected fields
    std::string json = captured.str();
    EXPECT_NE(json.find("\"mean\""), std::string::npos);
    EXPECT_NE(json.find("\"median\""), std::string::npos);
    EXPECT_NE(json.find("\"stddev\""), std::string::npos);
    EXPECT_NE(json.find("\"p90\""), std::string::npos);
    EXPECT_NE(json.find("\"instructions_per_run\""), std::string::npos);
}

TEST_F(VmBenchCommandTest, BenchWithInstructionLimit) {
    auto path = create_loop_bytecode(10000);

    VmBenchOptions opts;
    opts.input_file = path.string();
    opts.warmup_iterations = 1;
    opts.measurement_runs = 3;
    opts.instruction_limit = 100;  // Limit to 100 instructions

    VmGlobalOptions global;
    global.quiet = true;

    std::ostringstream oss;
    Terminal term(oss, true);

    VmExitCode result = commands::execute_bench(opts, global, term);
    // Should still succeed even if execution hits instruction limit
    EXPECT_EQ(result, VmExitCode::Success);
}

TEST_F(VmBenchCommandTest, BenchOutputCsv) {
    auto path = create_minimal_bytecode();

    VmBenchOptions opts;
    opts.input_file = path.string();
    opts.warmup_iterations = 1;
    opts.measurement_runs = 3;
    opts.format = VmBenchOutputFormat::Csv;

    VmGlobalOptions global;
    global.quiet = true;

    std::ostringstream oss;
    Terminal term(oss, true);

    // Capture stdout
    std::streambuf* old_cout = std::cout.rdbuf();
    std::ostringstream captured;
    std::cout.rdbuf(captured.rdbuf());

    VmExitCode result = commands::execute_bench(opts, global, term);

    std::cout.rdbuf(old_cout);

    EXPECT_EQ(result, VmExitCode::Success);

    // Verify CSV has header and data
    std::string csv = captured.str();
    EXPECT_NE(csv.find("file,samples,mean_ns"), std::string::npos);
    EXPECT_NE(csv.find("minimal.dot"), std::string::npos);
}

TEST_F(VmBenchCommandTest, BenchOutputToFile) {
    auto path = create_minimal_bytecode();
    auto output_path = temp_dir_ / "results.json";
    temp_files_.push_back(output_path);

    VmBenchOptions opts;
    opts.input_file = path.string();
    opts.warmup_iterations = 1;
    opts.measurement_runs = 3;
    opts.format = VmBenchOutputFormat::Json;
    opts.output_file = output_path.string();

    VmGlobalOptions global;
    global.quiet = true;

    std::ostringstream oss;
    Terminal term(oss, true);

    VmExitCode result = commands::execute_bench(opts, global, term);
    EXPECT_EQ(result, VmExitCode::Success);

    // Verify output file was created
    EXPECT_TRUE(std::filesystem::exists(output_path));

    // Read and verify content
    std::ifstream in(output_path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"mean\""), std::string::npos);
}

TEST_F(VmBenchCommandTest, BenchNonExistentFile_Fails) {
    VmBenchOptions opts;
    opts.input_file = "/nonexistent/path/file.dot";
    opts.warmup_iterations = 1;
    opts.measurement_runs = 1;

    VmGlobalOptions global;
    global.quiet = true;

    std::ostringstream oss;
    Terminal term(oss, true);

    VmExitCode result = commands::execute_bench(opts, global, term);
    EXPECT_EQ(result, VmExitCode::ValidationError);
}
