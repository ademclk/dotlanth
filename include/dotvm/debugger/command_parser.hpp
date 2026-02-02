#pragma once

/// @file command_parser.hpp
/// @brief TOOL-011 Debug Client - Command parser with alias expansion
///
/// Provides LLDB-style command parsing for the DotVM interactive debugger.
/// Supports tokenization, quoted strings, and alias expansion.

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dotvm::debugger {

/// @brief Result of parsing a command line
struct ParsedCommand {
    /// All tokens from the command line (after alias expansion)
    std::vector<std::string> tokens;

    /// The primary command (first token after alias expansion)
    std::string command;

    /// The original command before alias expansion (empty if no alias used)
    std::string original_command;

    /// Check if the parsed command is empty
    [[nodiscard]] bool empty() const noexcept { return tokens.empty(); }

    /// Get arguments (all tokens after the command)
    [[nodiscard]] std::vector<std::string_view> args() const noexcept {
        std::vector<std::string_view> result;
        if (tokens.size() > 1) {
            result.reserve(tokens.size() - 1);
            for (std::size_t i = 1; i < tokens.size(); ++i) {
                result.emplace_back(tokens[i]);
            }
        }
        return result;
    }

    /// Get a specific argument by index (0-based, after command)
    [[nodiscard]] std::string_view arg(std::size_t index) const noexcept {
        if (index + 1 < tokens.size()) {
            return tokens[index + 1];
        }
        return {};
    }

    /// Get number of arguments (excluding command)
    [[nodiscard]] std::size_t arg_count() const noexcept {
        return tokens.empty() ? 0 : tokens.size() - 1;
    }
};

/// @brief LLDB-style command parser with alias expansion
///
/// Parses debugger command lines, handling:
/// - Tokenization by whitespace
/// - Quoted string handling (single and double quotes)
/// - Alias expansion (e.g., 'r' -> 'run', 'b' -> 'breakpoint set')
///
/// @example
/// ```cpp
/// CommandParser parser;
/// auto cmd = parser.parse("b 0x10");
/// // cmd.command == "breakpoint"
/// // cmd.tokens == {"breakpoint", "set", "0x10"}
/// ```
class CommandParser {
public:
    /// @brief Construct parser with default LLDB-style aliases
    CommandParser();

    /// @brief Parse a command line
    /// @param input The raw command line string
    /// @return Parsed command with expanded aliases
    [[nodiscard]] ParsedCommand parse(std::string_view input) const;

    /// @brief Register a custom alias
    /// @param alias The short form (e.g., "br")
    /// @param expansion The full command (e.g., "breakpoint")
    void register_alias(std::string alias, std::string expansion);

    /// @brief Get all registered aliases
    [[nodiscard]] const std::unordered_map<std::string, std::string>& get_aliases() const noexcept {
        return aliases_;
    }

private:
    /// @brief Tokenize input string
    [[nodiscard]] std::vector<std::string> tokenize(std::string_view input) const;

    /// @brief Expand aliases in the token list
    /// @param tokens The tokens to expand (modified in place)
    /// @return The original command if alias was used, empty otherwise
    [[nodiscard]] std::string expand_aliases(std::vector<std::string>& tokens) const;

    /// Simple aliases: alias -> full_command
    std::unordered_map<std::string, std::string> aliases_;

    /// Complex aliases that expand to multiple tokens
    /// Maps first token to a vector of replacement tokens
    struct ComplexAlias {
        std::vector<std::string> replacement;
        bool prepend_args;  // If true, remaining args come after replacement
    };
    std::unordered_map<std::string, ComplexAlias> complex_aliases_;
};

}  // namespace dotvm::debugger
