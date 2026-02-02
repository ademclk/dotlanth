/// @file doc_comment.cpp
/// @brief CLI-005 Documentation comment parser implementation

#include "dotvm/cli/doc/doc_comment.hpp"

#include <algorithm>
#include <cctype>

namespace dotvm::cli::doc {

namespace {

/// @brief Trim leading whitespace from a string_view
[[nodiscard]] std::string_view ltrim(std::string_view s) noexcept {
    auto it = std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); });
    return s.substr(static_cast<std::size_t>(it - s.begin()));
}

/// @brief Trim trailing whitespace from a string_view
[[nodiscard]] std::string_view rtrim(std::string_view s) noexcept {
    auto it = std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); });
    return s.substr(0, s.size() - static_cast<std::size_t>(it - s.rbegin()));
}

/// @brief Trim both leading and trailing whitespace
[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    return rtrim(ltrim(s));
}

/// @brief Check if string starts with prefix (case-insensitive for tags)
[[nodiscard]] bool starts_with_tag(std::string_view text, std::string_view tag) noexcept {
    if (text.size() < tag.size())
        return false;
    for (std::size_t i = 0; i < tag.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(tag[i]))) {
            return false;
        }
    }
    // Must be followed by whitespace or end of string
    return text.size() == tag.size() || std::isspace(static_cast<unsigned char>(text[tag.size()]));
}

}  // namespace

std::string_view DocCommentParser::strip_comment_prefix(std::string_view text) noexcept {
    // Skip leading whitespace
    text = ltrim(text);

    // Skip '#' character(s)
    while (!text.empty() && text[0] == '#') {
        text.remove_prefix(1);
    }

    // Skip whitespace after #
    return ltrim(text);
}

std::optional<DocTag> DocCommentParser::parse_tag(std::string_view text) noexcept {
    text = ltrim(text);

    if (!text.empty() && text[0] == '@') {
        if (starts_with_tag(text, "@brief"))
            return DocTag::Brief;
        if (starts_with_tag(text, "@param"))
            return DocTag::Param;
        if (starts_with_tag(text, "@return"))
            return DocTag::Return;
        if (starts_with_tag(text, "@returns"))
            return DocTag::Return;  // Accept both
        if (starts_with_tag(text, "@note"))
            return DocTag::Note;
        if (starts_with_tag(text, "@example"))
            return DocTag::Example;
        if (starts_with_tag(text, "@see"))
            return DocTag::See;
    }

    return std::nullopt;
}

std::string_view DocCommentParser::extract_tag_value(std::string_view text,
                                                     std::string_view tag) noexcept {
    // Find the tag in the text
    auto pos = text.find(tag);
    if (pos == std::string_view::npos)
        return {};

    // Skip past the tag
    text.remove_prefix(pos + tag.size());

    // Trim and return the rest
    return trim(text);
}

ParamDoc DocCommentParser::parse_param(std::string_view value) {
    value = trim(value);

    ParamDoc param;

    // First word is the parameter name
    auto space_pos = value.find_first_of(" \t");
    if (space_pos == std::string_view::npos) {
        // Only name, no description
        param.name = std::string(value);
        return param;
    }

    param.name = std::string(value.substr(0, space_pos));
    param.description = std::string(trim(value.substr(space_pos)));

    return param;
}

bool DocCommentParser::is_doc_comment(std::string_view comment_text) noexcept {
    auto stripped = strip_comment_prefix(comment_text);
    return parse_tag(stripped).has_value();
}

DocComment DocCommentParser::parse_line(std::string_view comment_text, core::dsl::SourceSpan span) {
    DocComment doc;
    doc.span = span;

    auto content = strip_comment_prefix(comment_text);
    auto tag = parse_tag(content);

    if (!tag) {
        // No tag found - treat as plain description (could be brief continuation)
        return doc;
    }

    switch (*tag) {
        case DocTag::Brief: {
            auto value = extract_tag_value(content, "@brief");
            doc.brief = std::string(value);
            break;
        }
        case DocTag::Param: {
            auto value = extract_tag_value(content, "@param");
            doc.params.push_back(parse_param(value));
            break;
        }
        case DocTag::Return: {
            // Handle both @return and @returns
            auto value = content.find("@returns") != std::string_view::npos
                             ? extract_tag_value(content, "@returns")
                             : extract_tag_value(content, "@return");
            doc.return_desc = std::string(value);
            break;
        }
        case DocTag::Note: {
            auto value = extract_tag_value(content, "@note");
            doc.notes.emplace_back(value);
            break;
        }
        case DocTag::Example: {
            auto value = extract_tag_value(content, "@example");
            doc.examples.emplace_back(value);
            break;
        }
        case DocTag::See: {
            auto value = extract_tag_value(content, "@see");
            doc.see_also.emplace_back(value);
            break;
        }
    }

    return doc;
}

DocComment DocCommentParser::parse_block(const std::vector<std::string_view>& comment_lines,
                                         core::dsl::SourceSpan span) {
    DocComment merged;
    merged.span = span;

    for (const auto& line : comment_lines) {
        auto parsed = parse_line(line, span);

        // Merge parsed content
        if (!parsed.brief.empty()) {
            if (merged.brief.empty()) {
                merged.brief = std::move(parsed.brief);
            } else {
                merged.brief += " " + parsed.brief;
            }
        }

        for (auto& param : parsed.params) {
            merged.params.push_back(std::move(param));
        }

        if (!parsed.return_desc.empty()) {
            if (merged.return_desc.empty()) {
                merged.return_desc = std::move(parsed.return_desc);
            } else {
                merged.return_desc += " " + parsed.return_desc;
            }
        }

        for (auto& note : parsed.notes) {
            merged.notes.push_back(std::move(note));
        }

        for (auto& example : parsed.examples) {
            merged.examples.push_back(std::move(example));
        }

        for (auto& see : parsed.see_also) {
            merged.see_also.push_back(std::move(see));
        }
    }

    return merged;
}

}  // namespace dotvm::cli::doc
