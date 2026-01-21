/// @file formatter.cpp
/// @brief DSL-003 Source code formatter implementation (skeleton)

#include "dotvm/cli/formatter.hpp"

namespace dotvm::cli {

Formatter::Formatter(FormatConfig config) : config_(std::move(config)) {}

std::string Formatter::format(std::string_view source) const {
    // Skeleton implementation - will be completed in Phase 3
    // For now, just return the source unchanged
    return std::string(source);
}

}  // namespace dotvm::cli
