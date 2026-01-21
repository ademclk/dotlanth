#pragma once

/// @file formatter.hpp
/// @brief DSL-003 Source code formatter
///
/// Auto-formats DSL source code with consistent indentation and style.

#include <string>
#include <string_view>

namespace dotvm::cli {

/// @brief Formatting options
struct FormatConfig {
    std::size_t indent_size = 4;       ///< Number of spaces per indent level
    bool use_tabs = false;             ///< Use tabs instead of spaces
    bool trim_trailing = true;         ///< Trim trailing whitespace
    bool ensure_final_newline = true;  ///< Ensure file ends with newline
};

/// @brief DSL source code formatter
///
/// Formats DSL source code with consistent style.
class Formatter {
public:
    /// @brief Construct formatter with default config
    Formatter() = default;

    /// @brief Construct formatter with custom config
    explicit Formatter(FormatConfig config);

    /// @brief Format DSL source code
    /// @param source Input source code
    /// @return Formatted source code
    [[nodiscard]] std::string format(std::string_view source) const;

    /// @brief Get the current configuration
    [[nodiscard]] const FormatConfig& config() const noexcept { return config_; }

    /// @brief Set the configuration
    void set_config(FormatConfig config) { config_ = std::move(config); }

private:
    FormatConfig config_;
};

}  // namespace dotvm::cli
