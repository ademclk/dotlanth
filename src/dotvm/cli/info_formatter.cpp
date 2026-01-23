/// @file info_formatter.cpp
/// @brief TOOL-009 Bytecode Inspector output formatters implementation

#include "dotvm/cli/info_formatter.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "dotvm/core/instruction.hpp"

namespace dotvm::cli {

namespace {

/// Get category name string
[[nodiscard]] std::string_view category_name(core::OpcodeCategory cat) noexcept {
    switch (cat) {
        case core::OpcodeCategory::Arithmetic:
            return "Arithmetic";
        case core::OpcodeCategory::Bitwise:
            return "Bitwise";
        case core::OpcodeCategory::Comparison:
            return "Comparison";
        case core::OpcodeCategory::ControlFlow:
            return "Control";
        case core::OpcodeCategory::Memory:
            return "Memory";
        case core::OpcodeCategory::DataMove:
            return "DataMove";
        case core::OpcodeCategory::Reserved90:
            return "Reserved";
        case core::OpcodeCategory::State:
            return "State";
        case core::OpcodeCategory::Crypto:
            return "Crypto";
        case core::OpcodeCategory::ParaDot:
            return "ParaDot";
        case core::OpcodeCategory::ReservedD0:
            return "Reserved";
        case core::OpcodeCategory::System:
            return "System";
    }
    return "Unknown";
}

/// Format a number with comma separators for readability
[[nodiscard]] std::string format_number(std::uint64_t num) {
    std::string str = std::to_string(num);
    std::string result;
    int count = 0;
    for (auto it = str.rbegin(); it != str.rend(); ++it) {
        if (count > 0 && count % 3 == 0) {
            result = ',' + result;
        }
        result = *it + result;
        ++count;
    }
    return result;
}

/// Escape string for JSON
[[nodiscard]] std::string json_escape(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
        }
    }
    return result;
}

}  // namespace

std::string format_info_table(const core::InspectionResult& result, bool detailed) {
    std::ostringstream oss;

    // File info
    oss << "Size:        " << format_number(result.stats.file_size) << " bytes\n";
    oss << "Valid:       " << (result.is_valid() ? "Yes" : "No") << "\n";
    oss << "\n";

    // Header section
    oss << "Header:\n";
    oss << "  Version:   " << static_cast<int>(result.header.version) << "\n";
    oss << "  Arch:      " << result.header.arch_name() << "\n";

    // Format flags
    std::string flags_str;
    if (result.header.flags == 0) {
        flags_str = "NONE";
    } else {
        if (result.header.is_debug()) {
            flags_str += "DEBUG";
        }
        if (result.header.is_optimized()) {
            if (!flags_str.empty())
                flags_str += " | ";
            flags_str += "OPTIMIZED";
        }
    }
    oss << "  Flags:     " << flags_str << "\n";
    oss << "  Entry:     0x" << std::hex << std::setfill('0') << std::setw(4)
        << result.header.entry_point << std::dec << "\n";
    oss << "\n";

    // Sections
    oss << "Sections:\n";

    // Constant pool
    oss << "  Const Pool: " << format_number(result.header.const_pool_size) << " bytes";
    if (result.const_pool.entry_count > 0) {
        oss << " (" << result.const_pool.entry_count << " entries:";
        if (result.const_pool.int64_count > 0) {
            oss << " " << result.const_pool.int64_count << " int";
        }
        if (result.const_pool.float64_count > 0) {
            oss << " " << result.const_pool.float64_count << " float";
        }
        if (result.const_pool.string_count > 0) {
            oss << " " << result.const_pool.string_count << " string";
        }
        oss << ")";
    }
    oss << "\n";

    // Code
    oss << "  Code:       " << format_number(result.header.code_size) << " bytes";
    if (result.code.instruction_count > 0) {
        oss << " (" << result.code.instruction_count << " instructions)";
    }
    oss << "\n";
    oss << "\n";

    // Stats
    oss << "Stats:\n";
    oss << "  Est. Cost: " << format_number(result.stats.estimated_cost) << " units\n";

    // Detailed output
    if (detailed) {
        oss << "\n";

        // Opcode category breakdown
        if (!result.code.category_histogram.empty()) {
            oss << "Opcode Categories:\n";

            // Sort categories for consistent output
            std::vector<std::pair<core::OpcodeCategory, std::uint32_t>> sorted_cats(
                result.code.category_histogram.begin(), result.code.category_histogram.end());
            std::sort(sorted_cats.begin(), sorted_cats.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });

            for (const auto& [cat, count] : sorted_cats) {
                double pct = 100.0 * static_cast<double>(count) /
                             static_cast<double>(result.code.instruction_count);
                // Pad category name manually
                std::string cat_name{category_name(cat)};
                while (cat_name.size() < 12) {
                    cat_name += ' ';
                }
                oss << "  " << cat_name << std::setw(5) << count << " (" << std::fixed
                    << std::setprecision(1) << pct << "%)\n";
            }
            oss << "\n";
        }

        // Validation results
        oss << "Validation:\n";
        for (const auto& check : result.validation.checks) {
            oss << "  [" << (check.passed ? "PASS" : "FAIL") << "] " << check.name;
            if (!check.passed && !check.error_message.empty()) {
                oss << " - " << check.error_message;
            }
            oss << "\n";
        }
    }

    return oss.str();
}

std::string format_info_json(const core::InspectionResult& result, bool detailed) {
    std::ostringstream oss;

    oss << "{\n";

    // Header
    oss << "  \"header\": {\n";
    oss << "    \"magic\": \"" << result.header.magic_string() << "\",\n";
    oss << "    \"version\": " << static_cast<int>(result.header.version) << ",\n";
    oss << "    \"arch\": \"" << result.header.arch_name() << "\",\n";
    oss << "    \"flags\": " << result.header.flags << ",\n";
    oss << "    \"is_debug\": " << (result.header.is_debug() ? "true" : "false") << ",\n";
    oss << "    \"is_optimized\": " << (result.header.is_optimized() ? "true" : "false") << ",\n";
    oss << "    \"entry_point\": " << result.header.entry_point << ",\n";
    oss << "    \"const_pool_offset\": " << result.header.const_pool_offset << ",\n";
    oss << "    \"const_pool_size\": " << result.header.const_pool_size << ",\n";
    oss << "    \"code_offset\": " << result.header.code_offset << ",\n";
    oss << "    \"code_size\": " << result.header.code_size << "\n";
    oss << "  },\n";

    // Constant pool
    oss << "  \"const_pool\": {\n";
    oss << "    \"entry_count\": " << result.const_pool.entry_count << ",\n";
    oss << "    \"int64_count\": " << result.const_pool.int64_count << ",\n";
    oss << "    \"float64_count\": " << result.const_pool.float64_count << ",\n";
    oss << "    \"string_count\": " << result.const_pool.string_count << ",\n";
    oss << "    \"total_size\": " << result.const_pool.total_size << ",\n";
    oss << "    \"loaded_successfully\": "
        << (result.const_pool.loaded_successfully ? "true" : "false");
    if (!result.const_pool.load_error.empty()) {
        oss << ",\n    \"load_error\": \"" << json_escape(result.const_pool.load_error) << "\"";
    }
    oss << "\n  },\n";

    // Code
    oss << "  \"code\": {\n";
    oss << "    \"instruction_count\": " << result.code.instruction_count << ",\n";
    oss << "    \"branch_count\": " << result.code.branch_count << ",\n";
    oss << "    \"jump_count\": " << result.code.jump_count << ",\n";
    oss << "    \"memory_op_count\": " << result.code.memory_op_count << ",\n";
    oss << "    \"arithmetic_count\": " << result.code.arithmetic_count << ",\n";
    oss << "    \"crypto_count\": " << result.code.crypto_count << ",\n";
    oss << "    \"unknown_count\": " << result.code.unknown_count;

    if (detailed && !result.code.category_histogram.empty()) {
        oss << ",\n    \"category_histogram\": {";
        bool first = true;
        for (const auto& [cat, count] : result.code.category_histogram) {
            if (!first)
                oss << ",";
            first = false;
            oss << "\n      \"" << category_name(cat) << "\": " << count;
        }
        oss << "\n    }";
    }
    oss << "\n  },\n";

    // Validation
    oss << "  \"validation\": {\n";
    oss << "    \"all_passed\": " << (result.validation.all_passed ? "true" : "false") << ",\n";
    oss << "    \"checks\": [";
    for (std::size_t i = 0; i < result.validation.checks.size(); ++i) {
        const auto& check = result.validation.checks[i];
        if (i > 0)
            oss << ",";
        oss << "\n      {";
        oss << "\"name\": \"" << check.name << "\", ";
        oss << "\"passed\": " << (check.passed ? "true" : "false");
        if (!check.passed) {
            oss << ", \"error\": \"" << json_escape(check.error_message) << "\"";
            oss << ", \"error_code\": " << static_cast<int>(check.error_code);
        }
        oss << "}";
    }
    oss << "\n    ]\n";
    oss << "  },\n";

    // Stats
    oss << "  \"stats\": {\n";
    oss << "    \"file_size\": " << result.stats.file_size << ",\n";
    oss << "    \"estimated_cost\": " << result.stats.estimated_cost << ",\n";
    oss << "    \"code_density\": " << std::fixed << std::setprecision(4)
        << result.stats.code_density << ",\n";
    oss << "    \"const_pool_density\": " << result.stats.const_pool_density << "\n";
    oss << "  },\n";

    // Top-level validity
    oss << "  \"valid\": " << (result.is_valid() ? "true" : "false") << "\n";

    oss << "}\n";

    return oss.str();
}

}  // namespace dotvm::cli
