/// @file vm_run_command.cpp
/// @brief TOOL-005 VM run command implementation

#include "dotvm/cli/commands/vm_run_command.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "dotvm/core/asm/asm_lexer.hpp"
#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/instruction.hpp"
#include "dotvm/core/opcode.hpp"
#include "dotvm/core/vm_context.hpp"
#include "dotvm/exec/debug_context.hpp"
#include "dotvm/exec/execution_engine.hpp"

namespace dotvm::cli::commands {

namespace {

/// @brief Read entire file into a buffer
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string>
read_file_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected("Failed to open file");
    }

    auto size = file.tellg();
    if (size < 0) {
        return std::unexpected("Failed to get file size");
    }

    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return std::unexpected("Failed to read file");
    }

    return buffer;
}

/// @brief Format a hex address with specified width
[[nodiscard]] std::string format_hex(std::uint64_t value, int width) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(width) << value;
    return oss.str();
}

/// @brief Format register name
[[nodiscard]] std::string format_reg(std::uint8_t reg) {
    return "R" + std::to_string(reg);
}

/// @brief Format an instruction for debug output
[[nodiscard]] std::string format_instruction(std::uint32_t instr) {
    std::uint8_t opcode = core::extract_opcode(instr);
    std::string_view name = core::asm_::opcode_name(opcode);
    if (name.empty()) {
        return "??? (0x" + format_hex(opcode, 2) + ")";
    }

    std::ostringstream oss;
    oss << name;

    // Decode Type B (most common for immediate operations)
    auto decoded_b = core::decode_type_b(instr);
    auto decoded_a = core::decode_type_a(instr);
    auto decoded_c = core::decode_type_c(instr);

    // Simple heuristic based on opcode ranges
    // Arithmetic Type A: 0x00-0x05, 0x10-0x18, 0x20-0x28, 0x30-0x3A
    // Control flow: 0x40-0x5F
    // Memory: 0x60-0x68
    // Data move: 0x80-0x8F

    if (opcode <= 0x05 || (opcode >= 0x10 && opcode <= 0x18) ||
        (opcode >= 0x20 && opcode <= 0x28) || (opcode >= 0x30 && opcode <= 0x3A)) {
        // Type A: register-register
        if (opcode != core::opcode::NEG && opcode != core::opcode::NOT &&
            opcode != core::opcode::FNEG) {
            oss << " " << format_reg(decoded_a.rd) << ", " << format_reg(decoded_a.rs1) << ", "
                << format_reg(decoded_a.rs2);
        } else {
            oss << " " << format_reg(decoded_a.rd) << ", " << format_reg(decoded_a.rs1);
        }
    } else if ((opcode >= 0x06 && opcode <= 0x08) || (opcode >= 0x2C && opcode <= 0x2E) ||
               (opcode >= 0x81 && opcode <= 0x82)) {
        // Type B: immediate
        oss << " " << format_reg(decoded_b.rd) << ", #" << decoded_b.imm16;
    } else if (opcode == 0x40 || opcode == 0x50) {
        // Type C: jump/call
        if (decoded_c.offset24 >= 0) {
            oss << " +" << decoded_c.offset24;
        } else {
            oss << " " << decoded_c.offset24;
        }
    } else if (opcode == 0x41 || opcode == 0x42) {
        // Type D: conditional jump
        auto decoded_d = core::decode_type_d(instr);
        oss << " " << format_reg(decoded_d.rs);
        if (decoded_d.offset16 >= 0) {
            oss << ", +" << decoded_d.offset16;
        } else {
            oss << ", " << decoded_d.offset16;
        }
    } else if (opcode >= 0x60 && opcode <= 0x68) {
        // Type M: memory
        auto decoded_m = core::decode_type_m(instr);
        oss << " " << format_reg(decoded_m.rd_rs2) << ", [" << format_reg(decoded_m.rs1);
        if (decoded_m.offset8 != 0) {
            if (decoded_m.offset8 > 0) {
                oss << "+" << static_cast<int>(decoded_m.offset8);
            } else {
                oss << static_cast<int>(decoded_m.offset8);
            }
        }
        oss << "]";
    } else if (opcode == 0x80) {
        // MOV
        oss << " " << format_reg(decoded_a.rd) << ", " << format_reg(decoded_a.rs1);
    }
    // For other opcodes, just show the mnemonic without operands

    return oss.str();
}

/// @brief Print verbose file information
void print_file_info(const core::BytecodeHeader& header, std::size_t file_size, Terminal& term) {
    term.info("File size:    ");
    term.print(std::to_string(file_size) + " bytes");
    term.newline();

    term.info("Architecture: ");
    term.print(header.arch == core::Architecture::Arch32 ? "32-bit" : "64-bit");
    term.newline();

    term.info("Entry point:  ");
    term.print("0x" + format_hex(header.entry_point, 8));
    term.newline();

    term.info("Code size:    ");
    std::size_t instr_count =
        static_cast<std::size_t>(header.code_size) / core::bytecode::INSTRUCTION_ALIGNMENT;
    term.print(std::to_string(instr_count) + " instructions");
    term.newline();

    term.newline();
}

}  // namespace

VmExitCode execute_run(const VmRunOptions& opts, const VmGlobalOptions& global, Terminal& term) {
    // Read file into buffer
    auto file_result = read_file_bytes(opts.input_file);
    if (!file_result.has_value()) {
        term.error("error: ");
        term.print(file_result.error());
        term.print(": ");
        term.print(opts.input_file);
        term.newline();
        return VmExitCode::RuntimeError;
    }

    const auto& buffer = file_result.value();

    // Read and validate header
    if (buffer.size() < core::bytecode::HEADER_SIZE) {
        term.error("error: ");
        term.print(core::to_string(core::BytecodeError::FileTooSmall));
        term.newline();
        return VmExitCode::ValidationError;
    }

    auto header_result = core::read_header(buffer);
    if (!header_result.has_value()) {
        term.error("error: ");
        term.print(core::to_string(header_result.error()));
        term.newline();
        return VmExitCode::ValidationError;
    }

    auto header = header_result.value();

    auto validation_error = core::validate_header(header, buffer.size());
    if (validation_error != core::BytecodeError::Success) {
        term.error("error: ");
        term.print(core::to_string(validation_error));
        term.newline();
        return VmExitCode::ValidationError;
    }

    // Override architecture if specified
    if (global.arch_override != 0) {
        header.arch =
            (global.arch_override == 32) ? core::Architecture::Arch32 : core::Architecture::Arch64;
    }

    // Load constant pool
    std::vector<core::Value> const_pool;
    if (header.const_pool_size > 0) {
        std::span<const std::uint8_t> pool_data(buffer.data() + header.const_pool_offset,
                                                static_cast<std::size_t>(header.const_pool_size));
        auto pool_result = core::load_constant_pool(pool_data);
        if (!pool_result.has_value()) {
            term.error("error: ");
            term.print("constant pool: ");
            term.print(core::to_string(pool_result.error()));
            term.newline();
            return VmExitCode::ValidationError;
        }
        const_pool = std::move(pool_result.value());
    }

    // Print verbose info
    if (global.verbose && !global.quiet) {
        print_file_info(header, buffer.size(), term);
    }

    // Create VM configuration
    core::VmConfig config = core::VmConfig::for_arch(header.arch);
    config.strict_overflow = header.is_debug();

    // Apply instruction limit if specified
    if (opts.instruction_limit > 0) {
        config.resource_limits.max_instructions = opts.instruction_limit;
    }

    // Create VM context and execution engine
    core::VmContext vm_ctx(config);
    exec::ExecutionEngine engine(vm_ctx);

    // Set up code pointer
    const auto* code_ptr =
        reinterpret_cast<const std::uint32_t*>(buffer.data() + header.code_offset);
    std::size_t code_size =
        static_cast<std::size_t>(header.code_size) / core::bytecode::INSTRUCTION_ALIGNMENT;
    std::size_t entry_point = header.entry_point / core::bytecode::INSTRUCTION_ALIGNMENT;

    // Set up debug mode if requested
    if (opts.debug) {
        engine.enable_debug(true);

        // Set up debug callback for tracing
        engine.set_debug_callback(
            [code_ptr, code_size](exec::DebugEvent event, exec::ExecutionContext& ctx) {
                if (event != exec::DebugEvent::Step && event != exec::DebugEvent::Break) {
                    return;
                }

                if (ctx.pc >= code_size) {
                    return;
                }

                std::uint32_t instr = code_ptr[ctx.pc];
                std::string instr_str = format_instruction(instr);

                // Format: [NNNN] INSTR
                std::cout << "[" << std::setw(4) << std::setfill('0') << ctx.pc << "] " << std::left
                          << std::setw(25) << std::setfill(' ') << instr_str << std::endl;
            });
    }

    // Execute with timing
    auto start_time = std::chrono::high_resolution_clock::now();

    exec::ExecResult result =
        engine.execute(code_ptr, code_size, entry_point, std::span{const_pool});

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    // Print result summary
    if (!global.quiet) {
        term.newline();
        if (result == exec::ExecResult::Success) {
            term.success("Result: ");
            term.print("Success");
        } else {
            term.error("Result: ");
            term.print(exec::to_string(result));
        }
        term.newline();

        // Show R1 value (common return register)
        core::Value r1 = vm_ctx.registers().read(1);
        term.info("R1: ");
        term.print(std::to_string(r1.as_integer()));
        term.newline();

        // Show instruction count
        term.info("Instructions: ");
        term.print(std::to_string(engine.instructions_executed()));
        term.newline();

        // Show timing
        term.info("Time: ");
        if (duration < 1000) {
            term.print(std::to_string(duration) + "us");
        } else {
            double ms = static_cast<double>(duration) / 1000.0;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << ms << "ms";
            term.print(oss.str());
        }
        term.newline();
    }

    // Return appropriate exit code
    if (result == exec::ExecResult::Success) {
        return VmExitCode::Success;
    }

    return VmExitCode::RuntimeError;
}

}  // namespace dotvm::cli::commands
