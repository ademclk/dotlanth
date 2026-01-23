/// @file vm_validate_command.cpp
/// @brief TOOL-005 VM validate command implementation

#include "dotvm/cli/commands/vm_validate_command.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

#include "dotvm/core/bytecode.hpp"

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

}  // namespace

VmExitCode execute_validate(const VmValidateOptions& opts, const VmGlobalOptions& global,
                            Terminal& term) {
    // Read file into buffer
    auto file_result = read_file_bytes(opts.input_file);
    if (!file_result.has_value()) {
        term.error("error: ");
        term.print(file_result.error());
        term.print(": ");
        term.print(opts.input_file);
        term.newline();
        return VmExitCode::ValidationError;
    }

    const auto& buffer = file_result.value();

    // Check minimum size
    if (buffer.size() < core::bytecode::HEADER_SIZE) {
        term.error("error: ");
        term.print(core::to_string(core::BytecodeError::FileTooSmall));
        term.newline();
        return VmExitCode::ValidationError;
    }

    // Read header
    auto header_result = core::read_header(buffer);
    if (!header_result.has_value()) {
        term.error("error: ");
        term.print(core::to_string(header_result.error()));
        term.newline();
        return VmExitCode::ValidationError;
    }

    const auto& header = header_result.value();

    // Comprehensive header validation
    auto validation_error = core::validate_header(header, buffer.size());
    if (validation_error != core::BytecodeError::Success) {
        term.error("error: ");
        term.print(core::to_string(validation_error));
        term.newline();
        return VmExitCode::ValidationError;
    }

    // Validate constant pool if present
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

        if (!global.quiet) {
            term.info("info: ");
            term.print("validated ");
            term.print(std::to_string(pool_result.value().size()));
            term.print(" constant pool entries");
            term.newline();
        }
    }

    // Check code section alignment
    if (header.code_size > 0) {
        if (header.code_size % core::bytecode::INSTRUCTION_ALIGNMENT != 0) {
            term.error("error: ");
            term.print("code section size not aligned to instruction boundary");
            term.newline();
            return VmExitCode::ValidationError;
        }

        if (!global.quiet) {
            std::size_t instruction_count =
                static_cast<std::size_t>(header.code_size) / core::bytecode::INSTRUCTION_ALIGNMENT;
            term.info("info: ");
            term.print("validated ");
            term.print(std::to_string(instruction_count));
            term.print(" instructions");
            term.newline();
        }
    }

    // Success message
    term.success("valid: ");
    term.print(opts.input_file);
    term.newline();

    return VmExitCode::Success;
}

}  // namespace dotvm::cli::commands
