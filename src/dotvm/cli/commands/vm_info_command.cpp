/// @file vm_info_command.cpp
/// @brief TOOL-005 VM info command implementation

#include "dotvm/cli/commands/vm_info_command.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
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

/// @brief Format a size in bytes with human-readable suffix
[[nodiscard]] std::string format_size(std::uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1024 * 1024) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / 1024.0)
            << " KB";
        return oss.str();
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / (1024.0 * 1024.0))
        << " MB";
    return oss.str();
}

/// @brief Format a hex address
[[nodiscard]] std::string format_hex(std::uint64_t value, int width = 8) {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0') << std::setw(width) << value;
    return oss.str();
}

/// @brief Get architecture name
[[nodiscard]] std::string_view arch_name(core::Architecture arch) {
    switch (arch) {
        case core::Architecture::Arch32:
            return "32-bit";
        case core::Architecture::Arch64:
            return "64-bit";
        default:
            return "unknown";
    }
}

/// @brief Format flags string
[[nodiscard]] std::string format_flags(std::uint16_t flags) {
    if (flags == core::bytecode::FLAG_NONE) {
        return "none";
    }

    std::string result;
    if ((flags & core::bytecode::FLAG_DEBUG) != 0) {
        result += "DEBUG";
    }
    if ((flags & core::bytecode::FLAG_OPTIMIZED) != 0) {
        if (!result.empty()) {
            result += ", ";
        }
        result += "OPTIMIZED";
    }
    return result;
}

}  // namespace

VmExitCode execute_info(const VmInfoOptions& opts, const VmGlobalOptions& global, Terminal& term) {
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

    // Validate header
    auto validation_error = core::validate_header(header, buffer.size());
    if (validation_error != core::BytecodeError::Success) {
        term.error("error: ");
        term.print(core::to_string(validation_error));
        term.newline();
        return VmExitCode::ValidationError;
    }

    // Output stream - use cout or file based on options
    std::ostream& out = std::cout;

    // Display file information
    out << "File: " << opts.input_file << "\n";
    out << "Size: " << format_size(buffer.size()) << " (" << buffer.size() << " bytes)\n";
    out << "\n";

    // Header information
    out << "Header:\n";

    // Magic (as ASCII)
    out << "  Magic:        ";
    for (auto byte : header.magic) {
        if (byte >= 32 && byte < 127) {
            out << static_cast<char>(byte);
        } else {
            out << "?";
        }
    }
    out << " (" << format_hex(core::bytecode::MAGIC, 8) << ")\n";

    out << "  Version:      " << static_cast<int>(header.version) << "\n";
    out << "  Architecture: " << arch_name(header.arch) << "\n";
    out << "  Flags:        " << format_flags(header.flags) << "\n";
    out << "  Entry Point:  " << format_hex(header.entry_point);

    // Show instruction index
    if (header.entry_point % core::bytecode::INSTRUCTION_ALIGNMENT == 0) {
        out << " (instruction "
            << (header.entry_point / core::bytecode::INSTRUCTION_ALIGNMENT) << ")";
    }
    out << "\n";

    out << "\n";

    // Constant pool information
    out << "Constant Pool:\n";
    if (header.const_pool_size == 0) {
        out << "  (empty)\n";
    } else {
        out << "  Offset:       " << format_hex(header.const_pool_offset) << "\n";
        out << "  Size:         " << format_size(header.const_pool_size) << " ("
            << header.const_pool_size << " bytes)\n";

        // Try to load and count entries
        std::span<const std::uint8_t> pool_data(buffer.data() + header.const_pool_offset,
                                                static_cast<std::size_t>(header.const_pool_size));
        auto pool_result = core::load_constant_pool(pool_data);
        if (pool_result.has_value()) {
            out << "  Entries:      " << pool_result.value().size() << "\n";
        } else {
            out << "  Entries:      (error: " << core::to_string(pool_result.error()) << ")\n";
        }
    }

    out << "\n";

    // Code section information
    out << "Code Section:\n";
    if (header.code_size == 0) {
        out << "  (empty)\n";
    } else {
        out << "  Offset:       " << format_hex(header.code_offset) << "\n";
        out << "  Size:         " << format_size(header.code_size) << " (" << header.code_size
            << " bytes)\n";

        if (header.code_size % core::bytecode::INSTRUCTION_ALIGNMENT == 0) {
            std::size_t instruction_count =
                static_cast<std::size_t>(header.code_size) / core::bytecode::INSTRUCTION_ALIGNMENT;
            out << "  Instructions: " << instruction_count << "\n";
        } else {
            out << "  Instructions: (misaligned code section)\n";
        }
    }

    return VmExitCode::Success;
}

}  // namespace dotvm::cli::commands
