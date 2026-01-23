#pragma once

/// @file inspector.hpp
/// @brief TOOL-009 Bytecode Inspector - Analyzes DotVM bytecode files

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "arch_types.hpp"
#include "bytecode.hpp"
#include "instruction.hpp"

// Hash function for OpcodeCategory to use in unordered_map
// Must be declared before any unordered_map<OpcodeCategory, ...> usage
template <>
struct std::hash<dotvm::core::OpcodeCategory> {
    std::size_t operator()(dotvm::core::OpcodeCategory cat) const noexcept {
        return std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(cat));
    }
};

namespace dotvm::core {

// ============================================================================
// Inspection Result Structures
// ============================================================================

/// Parsed header information for inspection
struct HeaderInfo {
    std::array<std::uint8_t, 4> magic;
    std::uint8_t version;
    Architecture arch;
    std::uint16_t flags;
    std::uint64_t entry_point;
    std::uint64_t const_pool_offset;
    std::uint64_t const_pool_size;
    std::uint64_t code_offset;
    std::uint64_t code_size;

    /// Check if DEBUG flag is set
    [[nodiscard]] bool is_debug() const noexcept { return (flags & bytecode::FLAG_DEBUG) != 0; }

    /// Check if OPTIMIZED flag is set
    [[nodiscard]] bool is_optimized() const noexcept {
        return (flags & bytecode::FLAG_OPTIMIZED) != 0;
    }

    /// Get human-readable architecture name
    [[nodiscard]] std::string_view arch_name() const noexcept {
        return arch == Architecture::Arch64 ? "64-bit" : "32-bit";
    }

    /// Get magic bytes as string
    [[nodiscard]] std::string magic_string() const noexcept {
        return std::string(reinterpret_cast<const char*>(magic.data()), 4);
    }
};

/// Constant pool statistics
struct ConstPoolInfo {
    std::uint32_t entry_count = 0;
    std::uint32_t int64_count = 0;
    std::uint32_t float64_count = 0;
    std::uint32_t string_count = 0;
    std::uint64_t total_size = 0;
    bool loaded_successfully = true;
    std::string load_error;
};

/// Code section analysis
struct CodeInfo {
    std::uint32_t instruction_count = 0;
    std::unordered_map<std::uint8_t, std::uint32_t> opcode_histogram;
    std::unordered_map<OpcodeCategory, std::uint32_t> category_histogram;
    std::uint32_t branch_count = 0;
    std::uint32_t jump_count = 0;
    std::uint32_t memory_op_count = 0;
    std::uint32_t arithmetic_count = 0;
    std::uint32_t crypto_count = 0;
    std::uint32_t unknown_count = 0;
};

/// Single validation check result
struct ValidationCheck {
    std::string_view name;
    bool passed;
    std::string_view error_message;
    BytecodeError error_code;
};

/// Validation results
struct ValidationInfo {
    bool all_passed = true;
    std::vector<ValidationCheck> checks;
};

/// Overall statistics
struct Statistics {
    std::size_t file_size = 0;
    std::uint64_t estimated_cost = 0;
    double code_density = 0.0;
    double const_pool_density = 0.0;
};

/// Complete inspection result
struct InspectionResult {
    HeaderInfo header;
    ConstPoolInfo const_pool;
    CodeInfo code;
    ValidationInfo validation;
    Statistics stats;

    /// Check if bytecode is valid
    [[nodiscard]] bool is_valid() const noexcept { return validation.all_passed; }
};

// ============================================================================
// BytecodeInspector Class
// ============================================================================

/// Bytecode inspector - analyzes DotVM bytecode files
class BytecodeInspector {
public:
    /// Inspect bytecode from raw bytes
    /// @param data Raw bytecode data
    /// @return Inspection result with all analysis data
    [[nodiscard]] static InspectionResult inspect(std::span<const std::uint8_t> data) noexcept;

    /// Inspect bytecode from file path
    /// @param path Path to bytecode file
    /// @return Inspection result or error message
    [[nodiscard]] static std::expected<InspectionResult, std::string>
    inspect_file(const std::filesystem::path& path);

private:
    /// Analyze header and populate HeaderInfo
    [[nodiscard]] static HeaderInfo analyze_header(std::span<const std::uint8_t> data);

    /// Analyze constant pool and populate ConstPoolInfo
    [[nodiscard]] static ConstPoolInfo analyze_const_pool(std::span<const std::uint8_t> data,
                                                          const BytecodeHeader& header);

    /// Analyze code section and populate CodeInfo
    [[nodiscard]] static CodeInfo analyze_code(std::span<const std::uint8_t> data,
                                               const BytecodeHeader& header);

    /// Run all validation checks
    [[nodiscard]] static ValidationInfo validate(std::span<const std::uint8_t> data,
                                                 const BytecodeHeader& header);

    /// Calculate overall statistics
    [[nodiscard]] static Statistics calculate_stats(std::span<const std::uint8_t> data,
                                                    const BytecodeHeader& header,
                                                    const CodeInfo& code);

    /// Estimate execution cost based on code analysis
    [[nodiscard]] static std::uint64_t estimate_cost(const CodeInfo& code);
};

}  // namespace dotvm::core
