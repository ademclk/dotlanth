/// @file dis_command.cpp
/// @brief TOOL-008 Disassembler command implementation

#include "dotvm/cli/commands/dis_command.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "dotvm/core/bytecode.hpp"
#include "dotvm/core/disassembler.hpp"

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

/// @brief Write string to file or stdout
[[nodiscard]] bool write_output(const std::string& content, const std::string& output_file) {
    if (output_file.empty()) {
        std::cout << content;
        return true;
    }

    std::ofstream file(output_file);
    if (!file) {
        return false;
    }
    file << content;
    return file.good();
}

}  // namespace

DisExitCode execute_disassemble(const DisOptions& opts, Terminal& term) {
    // Read file into buffer
    auto file_result = read_file_bytes(opts.input_file);
    if (!file_result.has_value()) {
        term.error("error: ");
        term.print(file_result.error());
        term.print(": ");
        term.print(opts.input_file);
        term.newline();
        return DisExitCode::IOError;
    }

    const auto& buffer = file_result.value();

    // Check minimum size
    if (buffer.size() < core::bytecode::HEADER_SIZE) {
        term.error("error: ");
        term.print(core::to_string(core::BytecodeError::FileTooSmall));
        term.newline();
        return DisExitCode::ValidationError;
    }

    // Read header
    auto header_result = core::read_header(buffer);
    if (!header_result.has_value()) {
        term.error("error: ");
        term.print(core::to_string(header_result.error()));
        term.newline();
        return DisExitCode::ValidationError;
    }

    const auto& header = header_result.value();

    // Validate header
    auto validation_error = core::validate_header(header, buffer.size());
    if (validation_error != core::BytecodeError::Success) {
        term.error("error: ");
        term.print(core::to_string(validation_error));
        term.newline();
        return DisExitCode::ValidationError;
    }

    // Verbose output: show file info
    if (opts.verbose && !opts.quiet) {
        term.print("File: ");
        term.print(opts.input_file);
        term.newline();
        term.print("Size: ");
        term.print(std::to_string(buffer.size()));
        term.print(" bytes");
        term.newline();
        term.print("Version: ");
        term.print(std::to_string(header.version));
        term.newline();
        term.print("Architecture: ");
        term.print(header.arch == core::Architecture::Arch64 ? "64-bit" : "32-bit");
        term.newline();
        term.newline();
    }

    // Load constant pool
    std::vector<core::Value> constants;
    if (header.const_pool_size > 0) {
        std::span<const std::uint8_t> pool_data(buffer.data() + header.const_pool_offset,
                                                static_cast<std::size_t>(header.const_pool_size));
        auto pool_result = core::load_constant_pool(pool_data);
        if (pool_result.has_value()) {
            constants = std::move(pool_result.value());
        }
        // Non-fatal if constant pool loading fails - we can still disassemble
    }

    // Extract code section
    if (header.code_size == 0) {
        if (!opts.quiet) {
            term.print("; Empty code section");
            term.newline();
        }
        return DisExitCode::Success;
    }

    std::span<const std::uint8_t> code(buffer.data() + header.code_offset,
                                       static_cast<std::size_t>(header.code_size));

    // Disassemble
    auto instructions = core::disassemble(code);

    // Build control flow graph
    auto cfg = core::build_cfg(instructions, static_cast<std::uint32_t>(header.entry_point));

    // Format output
    std::string output;

    core::DisasmOptions disasm_opts;
    disasm_opts.show_bytes = opts.show_bytes;
    disasm_opts.show_labels = opts.show_labels;
    disasm_opts.annotate = opts.annotate;

    if (opts.format == DisOutputFormat::Json) {
        output = core::format_json(instructions, cfg, header, constants);
    } else {
        output = core::format_disassembly(instructions, cfg, header, constants, disasm_opts);
    }

    // Write output
    if (!write_output(output, opts.output_file)) {
        term.error("error: Failed to write output file: ");
        term.print(opts.output_file);
        term.newline();
        return DisExitCode::IOError;
    }

    return DisExitCode::Success;
}

}  // namespace dotvm::cli::commands
