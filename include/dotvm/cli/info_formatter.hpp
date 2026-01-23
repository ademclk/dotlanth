#pragma once

/// @file info_formatter.hpp
/// @brief TOOL-009 Bytecode Inspector output formatters

#include <string>

#include "dotvm/core/inspector.hpp"

namespace dotvm::cli {

/// Output format for bytecode info
enum class InfoOutputFormat {
    Table,  ///< Human-readable table format
    Json    ///< Structured JSON format
};

/// @brief Format inspection result as human-readable table
///
/// Summary mode (default):
/// - File info, validity
/// - Header basics (version, arch, flags, entry)
/// - Section sizes
/// - Estimated cost
///
/// Detailed mode (--detailed):
/// - All summary info
/// - Opcode category breakdown with percentages
/// - Validation check results
///
/// @param result Inspection result to format
/// @param detailed Include detailed breakdown
/// @return Formatted text
[[nodiscard]] std::string format_info_table(const core::InspectionResult& result,
                                            bool detailed = false);

/// @brief Format inspection result as JSON
///
/// @param result Inspection result to format
/// @param detailed Include detailed fields
/// @return JSON string
[[nodiscard]] std::string format_info_json(const core::InspectionResult& result,
                                           bool detailed = false);

}  // namespace dotvm::cli
