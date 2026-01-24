/// @file inspector.cpp
/// @brief TOOL-009 BytecodeInspector implementation

#include "dotvm/core/inspector.hpp"

#include <fstream>

#include "dotvm/core/disassembler.hpp"
#include "dotvm/core/opcode.hpp"

namespace dotvm::core {

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

/// Check if an opcode is a branch instruction (conditional)
[[nodiscard]] bool is_branch_opcode(std::uint8_t op) noexcept {
    return op == opcode::JZ || op == opcode::JNZ || op == opcode::BEQ || op == opcode::BNE ||
           op == opcode::BLT || op == opcode::BLE || op == opcode::BGT || op == opcode::BGE;
}

/// Check if an opcode is a jump instruction (unconditional)
[[nodiscard]] bool is_jump_opcode(std::uint8_t op) noexcept {
    return op == opcode::JMP || op == opcode::CALL;
}

/// Check if an opcode is in reserved range
[[nodiscard]] bool is_unknown_opcode(std::uint8_t op) noexcept {
    auto cat = classify_opcode(op);
    return cat == OpcodeCategory::Reserved90 || cat == OpcodeCategory::ReservedD0;
}

}  // namespace

// ============================================================================
// BytecodeInspector Implementation
// ============================================================================

InspectionResult BytecodeInspector::inspect(std::span<const std::uint8_t> data) noexcept {
    InspectionResult result;

    // Set file size first
    result.stats.file_size = data.size();

    // Check minimum size
    if (data.size() < bytecode::HEADER_SIZE) {
        result.validation.all_passed = false;
        result.validation.checks.push_back(
            ValidationCheck{.name = "size",
                            .passed = false,
                            .error_message = to_string(BytecodeError::FileTooSmall),
                            .error_code = BytecodeError::FileTooSmall});
        return result;
    }

    // Read header
    auto header_result = read_header(data);
    if (!header_result.has_value()) {
        result.validation.all_passed = false;
        result.validation.checks.push_back(
            ValidationCheck{.name = "header",
                            .passed = false,
                            .error_message = to_string(header_result.error()),
                            .error_code = header_result.error()});
        return result;
    }

    const BytecodeHeader& header = header_result.value();

    // Populate HeaderInfo
    result.header = analyze_header(data);

    // Run validation
    result.validation = validate(data, header);

    // Analyze constant pool
    result.const_pool = analyze_const_pool(data, header);

    // Analyze code section
    result.code = analyze_code(data, header);

    // Calculate statistics
    result.stats = calculate_stats(data, header, result.code);

    return result;
}

std::expected<InspectionResult, std::string>
BytecodeInspector::inspect_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::unexpected("Failed to open file: " + path.string());
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

    return inspect(buffer);
}

HeaderInfo BytecodeInspector::analyze_header(std::span<const std::uint8_t> data) {
    HeaderInfo info{};

    if (data.size() < bytecode::HEADER_SIZE) {
        return info;
    }

    // Read raw header
    auto header_result = read_header(data);
    if (!header_result.has_value()) {
        return info;
    }

    const BytecodeHeader& header = header_result.value();

    info.magic = header.magic;
    info.version = header.version;
    info.arch = header.arch;
    info.flags = header.flags;
    info.entry_point = header.entry_point;
    info.const_pool_offset = header.const_pool_offset;
    info.const_pool_size = header.const_pool_size;
    info.code_offset = header.code_offset;
    info.code_size = header.code_size;

    return info;
}

ConstPoolInfo BytecodeInspector::analyze_const_pool(std::span<const std::uint8_t> data,
                                                    const BytecodeHeader& header) {
    ConstPoolInfo info{};

    if (header.const_pool_size == 0) {
        info.total_size = 0;
        return info;
    }

    info.total_size = header.const_pool_size;

    // Check bounds
    if (header.const_pool_offset + header.const_pool_size > data.size()) {
        info.loaded_successfully = false;
        info.load_error = "Constant pool extends beyond file";
        return info;
    }

    std::span<const std::uint8_t> pool_data(data.data() + header.const_pool_offset,
                                            static_cast<std::size_t>(header.const_pool_size));

    // Read entry count
    if (pool_data.size() < sizeof(ConstantPoolHeader)) {
        info.loaded_successfully = false;
        info.load_error = "Constant pool header truncated";
        return info;
    }

    auto pool_header = read_const_pool_header(pool_data);
    if (!pool_header.has_value()) {
        info.loaded_successfully = false;
        info.load_error = to_string(pool_header.error());
        return info;
    }

    info.entry_count = pool_header->entry_count;

    // Count types by iterating through entries
    std::size_t offset = sizeof(ConstantPoolHeader);
    for (std::uint32_t i = 0; i < info.entry_count; ++i) {
        if (offset >= pool_data.size()) {
            info.loaded_successfully = false;
            info.load_error = "Constant pool truncated";
            return info;
        }

        std::uint8_t type_tag = pool_data[offset];
        switch (type_tag) {
            case bytecode::CONST_TYPE_I64:
                ++info.int64_count;
                offset += 9;  // 1 byte tag + 8 bytes value
                break;
            case bytecode::CONST_TYPE_F64:
                ++info.float64_count;
                offset += 9;  // 1 byte tag + 8 bytes value
                break;
            case bytecode::CONST_TYPE_STRING:
                ++info.string_count;
                // String format: tag(1) + length(4) + data(length)
                // For now, just mark as failed since strings not implemented
                info.loaded_successfully = false;
                info.load_error = "String constants not yet supported";
                return info;
            default:
                info.loaded_successfully = false;
                info.load_error = "Invalid constant type tag";
                return info;
        }
    }

    return info;
}

CodeInfo BytecodeInspector::analyze_code(std::span<const std::uint8_t> data,
                                         const BytecodeHeader& header) {
    CodeInfo info{};

    if (header.code_size == 0) {
        return info;
    }

    // Check bounds
    if (header.code_offset + header.code_size > data.size()) {
        return info;
    }

    std::span<const std::uint8_t> code(data.data() + header.code_offset,
                                       static_cast<std::size_t>(header.code_size));

    // Iterate through instructions (4 bytes each)
    for (std::size_t offset = 0; offset + 4 <= code.size(); offset += 4) {
        auto instr = decode_instruction(code.subspan(offset), static_cast<std::uint32_t>(offset));
        ++info.instruction_count;

        // Update opcode histogram
        ++info.opcode_histogram[instr.opcode];

        // Update category histogram
        OpcodeCategory cat = classify_opcode(instr.opcode);
        ++info.category_histogram[cat];

        // Count specific types
        if (is_branch_opcode(instr.opcode)) {
            ++info.branch_count;
        }

        if (is_jump_opcode(instr.opcode)) {
            ++info.jump_count;
        }

        if (is_memory_opcode(instr.opcode)) {
            ++info.memory_op_count;
        }

        if (is_arithmetic_opcode(instr.opcode)) {
            ++info.arithmetic_count;
        }

        if (is_crypto_opcode(instr.opcode)) {
            ++info.crypto_count;
        }

        if (is_unknown_opcode(instr.opcode)) {
            ++info.unknown_count;
        }
    }

    return info;
}

ValidationInfo BytecodeInspector::validate(std::span<const std::uint8_t> data,
                                           const BytecodeHeader& header) {
    ValidationInfo info;
    info.all_passed = true;

    // Reserve capacity for all checks to avoid reallocations
    // (fixes GCC -Wstringop-overflow false positive)
    constexpr std::size_t NUM_CHECKS = 7;
    info.checks.reserve(NUM_CHECKS);

    // Magic check
    bool magic_ok = validate_magic(std::span<const std::uint8_t, 4>{header.magic});
    info.checks.push_back(ValidationCheck{
        .name = "magic",
        .passed = magic_ok,
        .error_message = magic_ok ? "" : to_string(BytecodeError::InvalidMagic),
        .error_code = magic_ok ? BytecodeError::Success : BytecodeError::InvalidMagic});
    if (!magic_ok) {
        info.all_passed = false;
    }

    // Version check
    bool version_ok = validate_version(header.version);
    info.checks.push_back(ValidationCheck{
        .name = "version",
        .passed = version_ok,
        .error_message = version_ok ? "" : to_string(BytecodeError::UnsupportedVersion),
        .error_code = version_ok ? BytecodeError::Success : BytecodeError::UnsupportedVersion});
    if (!version_ok) {
        info.all_passed = false;
    }

    // Architecture check
    bool arch_ok = validate_architecture(header.arch);
    info.checks.push_back(ValidationCheck{
        .name = "architecture",
        .passed = arch_ok,
        .error_message = arch_ok ? "" : to_string(BytecodeError::InvalidArchitecture),
        .error_code = arch_ok ? BytecodeError::Success : BytecodeError::InvalidArchitecture});
    if (!arch_ok) {
        info.all_passed = false;
    }

    // Flags check
    bool flags_ok = (header.flags & ~VALID_FLAGS_MASK) == 0;
    info.checks.push_back(ValidationCheck{
        .name = "flags",
        .passed = flags_ok,
        .error_message = flags_ok ? "" : to_string(BytecodeError::InvalidFlags),
        .error_code = flags_ok ? BytecodeError::Success : BytecodeError::InvalidFlags});
    if (!flags_ok) {
        info.all_passed = false;
    }

    // Section bounds and overlap check
    bool const_pool_ok =
        validate_section_bounds(header.const_pool_offset, header.const_pool_size, data.size());
    bool code_ok = validate_section_bounds(header.code_offset, header.code_size, data.size());
    bool no_overlap = !sections_overlap(header.const_pool_offset, header.const_pool_size,
                                        header.code_offset, header.code_size);

    BytecodeError section_error = BytecodeError::Success;
    std::string_view section_msg = "";
    if (!const_pool_ok) {
        section_error = BytecodeError::ConstPoolOutOfBounds;
        section_msg = to_string(BytecodeError::ConstPoolOutOfBounds);
    } else if (!code_ok) {
        section_error = BytecodeError::CodeSectionOutOfBounds;
        section_msg = to_string(BytecodeError::CodeSectionOutOfBounds);
    } else if (!no_overlap) {
        section_error = BytecodeError::SectionsOverlap;
        section_msg = to_string(BytecodeError::SectionsOverlap);
    }

    bool sections_ok = const_pool_ok && code_ok && no_overlap;
    info.checks.push_back(ValidationCheck{.name = "sections",
                                          .passed = sections_ok,
                                          .error_message = section_msg,
                                          .error_code = section_error});
    if (!sections_ok) {
        info.all_passed = false;
    }

    // Entry point check
    bool entry_ok = true;
    BytecodeError entry_error = BytecodeError::Success;
    if (header.code_size == 0) {
        entry_ok = (header.entry_point == 0);
        if (!entry_ok) {
            entry_error = BytecodeError::EntryPointOutOfBounds;
        }
    } else {
        entry_ok = header.entry_point < header.code_size;
        if (entry_ok) {
            entry_ok = (header.entry_point % bytecode::INSTRUCTION_ALIGNMENT) == 0;
            if (!entry_ok) {
                entry_error = BytecodeError::EntryPointNotAligned;
            }
        } else {
            entry_error = BytecodeError::EntryPointOutOfBounds;
        }
    }

    info.checks.push_back(ValidationCheck{.name = "entry_point",
                                          .passed = entry_ok,
                                          .error_message = entry_ok ? "" : to_string(entry_error),
                                          .error_code = entry_error});
    if (!entry_ok) {
        info.all_passed = false;
    }

    // Constant pool validation (if present)
    if (header.const_pool_size > 0 && const_pool_ok) {
        std::span<const std::uint8_t> pool_data(data.data() + header.const_pool_offset,
                                                static_cast<std::size_t>(header.const_pool_size));
        auto pool_result = load_constant_pool(pool_data);
        bool pool_ok = pool_result.has_value();

        info.checks.push_back(
            ValidationCheck{.name = "const_pool",
                            .passed = pool_ok,
                            .error_message = pool_ok ? "" : to_string(pool_result.error()),
                            .error_code = pool_ok ? BytecodeError::Success : pool_result.error()});
        if (!pool_ok) {
            info.all_passed = false;
        }
    }

    return info;
}

Statistics BytecodeInspector::calculate_stats(std::span<const std::uint8_t> data,
                                              const BytecodeHeader& header, const CodeInfo& code) {
    Statistics stats;

    stats.file_size = data.size();
    stats.estimated_cost = estimate_cost(code);

    // Code density = code_size / file_size
    if (stats.file_size > 0) {
        stats.code_density =
            static_cast<double>(header.code_size) / static_cast<double>(stats.file_size);
    }

    // Const pool density = const_pool_size / file_size
    if (stats.file_size > 0) {
        stats.const_pool_density =
            static_cast<double>(header.const_pool_size) / static_cast<double>(stats.file_size);
    }

    return stats;
}

std::uint64_t BytecodeInspector::estimate_cost(const CodeInfo& code) {
    // Base cost: 1 unit per instruction
    std::uint64_t cost = code.instruction_count;

    // Memory ops: +2 penalty (total 3x base)
    cost += code.memory_op_count * 2;

    // Branches: +1 penalty (total 2x base)
    cost += code.branch_count;

    // Crypto ops: +10 penalty (total 11x base)
    cost += code.crypto_count * 10;

    return cost;
}

}  // namespace dotvm::core
