/// @file command_parser.cpp
/// @brief TOOL-011 Debug Client - Command parser implementation

#include "dotvm/debugger/command_parser.hpp"

#include <algorithm>
#include <cctype>

namespace dotvm::debugger {

CommandParser::CommandParser() {
    // Simple aliases (command -> command)
    aliases_ = {
        // Execution
        {"r", "run"},
        {"c", "continue"},
        {"s", "step"},
        {"n", "next"},
        {"fin", "finish"},
        {"q", "quit"},

        // Inspection
        {"reg", "register"},
        {"dis", "disassemble"},
        {"bt", "backtrace"},

        // Source
        {"l", "source"},
        {"h", "help"},
    };

    // Complex aliases (expand to multiple tokens)
    complex_aliases_ = {
        // b <addr> -> breakpoint set <addr>
        {"b", {{"breakpoint", "set"}, true}},

        // bl -> breakpoint list
        {"bl", {{"breakpoint", "list"}, true}},

        // bd <id> -> breakpoint delete <id>
        {"bd", {{"breakpoint", "delete"}, true}},

        // be <id> -> breakpoint enable <id>
        {"be", {{"breakpoint", "enable"}, true}},

        // bdi <id> -> breakpoint disable <id>
        {"bdi", {{"breakpoint", "disable"}, true}},

        // bc <id> <cond> -> breakpoint condition <id> <cond>
        {"bc", {{"breakpoint", "condition"}, true}},

        // x <h> <off> <sz> -> memory read <h> <off> <sz>
        {"x", {{"memory", "read"}, true}},

        // f <n> -> frame select <n>
        {"f", {{"frame", "select"}, true}},

        // w <h> <off> <sz> -> watchpoint set <h> <off> <sz>
        {"w", {{"watchpoint", "set"}, true}},

        // wl -> watchpoint list
        {"wl", {{"watchpoint", "list"}, true}},

        // wd <id> -> watchpoint delete <id>
        {"wd", {{"watchpoint", "delete"}, true}},
    };
}

std::vector<std::string> CommandParser::tokenize(std::string_view input) const {
    std::vector<std::string> tokens;
    std::string current_token;
    bool in_double_quote = false;
    bool in_single_quote = false;

    for (std::size_t i = 0; i < input.size(); ++i) {
        char c = input[i];

        if (in_double_quote) {
            if (c == '"') {
                in_double_quote = false;
                // End of quoted string - add token if non-empty or if it was an empty string ""
                tokens.push_back(std::move(current_token));
                current_token.clear();
            } else {
                current_token += c;
            }
        } else if (in_single_quote) {
            if (c == '\'') {
                in_single_quote = false;
                tokens.push_back(std::move(current_token));
                current_token.clear();
            } else {
                current_token += c;
            }
        } else if (c == '"') {
            // Start double quote
            if (!current_token.empty()) {
                tokens.push_back(std::move(current_token));
                current_token.clear();
            }
            in_double_quote = true;
        } else if (c == '\'') {
            // Start single quote
            if (!current_token.empty()) {
                tokens.push_back(std::move(current_token));
                current_token.clear();
            }
            in_single_quote = true;
        } else if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            // Whitespace - end current token
            if (!current_token.empty()) {
                tokens.push_back(std::move(current_token));
                current_token.clear();
            }
        } else {
            current_token += c;
        }
    }

    // Handle unterminated quotes - just include whatever we have
    if (!current_token.empty() || in_double_quote || in_single_quote) {
        tokens.push_back(std::move(current_token));
    }

    return tokens;
}

std::string CommandParser::expand_aliases(std::vector<std::string>& tokens) const {
    if (tokens.empty()) {
        return {};
    }

    const std::string& first = tokens[0];
    std::string original_command;

    // Check complex aliases first
    auto complex_it = complex_aliases_.find(first);
    if (complex_it != complex_aliases_.end()) {
        original_command = first;
        const auto& alias = complex_it->second;

        // Build new token list
        std::vector<std::string> new_tokens = alias.replacement;

        // Append remaining arguments
        for (std::size_t i = 1; i < tokens.size(); ++i) {
            new_tokens.push_back(std::move(tokens[i]));
        }

        tokens = std::move(new_tokens);
        return original_command;
    }

    // Check simple aliases
    auto simple_it = aliases_.find(first);
    if (simple_it != aliases_.end()) {
        original_command = first;
        tokens[0] = simple_it->second;
        return original_command;
    }

    return {};
}

ParsedCommand CommandParser::parse(std::string_view input) const {
    ParsedCommand result;

    // Tokenize
    result.tokens = tokenize(input);

    if (result.tokens.empty()) {
        return result;
    }

    // Expand aliases
    result.original_command = expand_aliases(result.tokens);

    // Set command (first token after expansion)
    result.command = result.tokens[0];

    // If no alias was used, original_command should match command
    if (result.original_command.empty()) {
        result.original_command = result.command;
    }

    return result;
}

void CommandParser::register_alias(std::string alias, std::string expansion) {
    aliases_[std::move(alias)] = std::move(expansion);
}

}  // namespace dotvm::debugger
