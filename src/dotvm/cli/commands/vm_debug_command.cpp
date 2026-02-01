/// @file vm_debug_command.cpp
/// @brief TOOL-011 VM debug command implementation

#include "dotvm/cli/commands/vm_debug_command.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/vm_context.hpp"
#include "dotvm/debugger/debug_client.hpp"
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

/// @brief Read a line from stdin
[[nodiscard]] std::string read_line() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}

}  // namespace

VmExitCode execute_debug(const VmDebugOptions& opts, const VmGlobalOptions& global,
                         Terminal& term) {
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

    // Create VM configuration
    core::VmConfig config = core::VmConfig::for_arch(header.arch);
    config.strict_overflow = header.is_debug();

    // Create VM context and execution engine
    core::VmContext vm_ctx(config);
    exec::ExecutionEngine engine(vm_ctx);

    // Set up code pointer
    const auto* code_ptr =
        reinterpret_cast<const std::uint32_t*>(buffer.data() + header.code_offset);
    std::size_t code_size =
        static_cast<std::size_t>(header.code_size) / core::bytecode::INSTRUCTION_ALIGNMENT;
    std::size_t entry_point = header.entry_point / core::bytecode::INSTRUCTION_ALIGNMENT;

    // Create debug client
    debugger::DebugClientOptions debug_opts{.colors = !opts.no_color && !global.no_color,
                                            .show_hex = true,
                                            .show_decimal = true,
                                            .disasm_count = 5};

    debugger::DebugClient client(engine, vm_ctx, debug_opts);
    client.load_bytecode(code_ptr, code_size, entry_point, std::span{const_pool});

    // Print banner
    if (!global.quiet) {
        std::cout << "DotVM Debugger v0.1.0\n";
        std::cout << "Loaded: " << opts.input_file << " (" << code_size << " instructions)\n";
        std::cout << "Type 'help' for commands, 'quit' to exit.\n\n";
    }

    // REPL loop
    while (client.active()) {
        std::cout << client.prompt() << std::flush;

        if (!std::cin) {
            // EOF or error
            break;
        }

        std::string line = read_line();

        // Handle empty line (could repeat last command in future)
        if (line.empty()) {
            continue;
        }

        auto result = client.execute_command(line);
        std::cout << result.output;

        if (result.should_quit) {
            break;
        }
    }

    return VmExitCode::Success;
}

}  // namespace dotvm::cli::commands
