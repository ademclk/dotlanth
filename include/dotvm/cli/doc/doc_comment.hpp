#pragma once

/// @file doc_comment.hpp
/// @brief CLI-005 Documentation comment model and parser
///
/// Defines structures for Doxygen-style documentation comments and
/// provides parsing functionality for extracting structured documentation
/// from DSL comments.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/core/dsl/source_location.hpp"

namespace dotvm::cli::doc {

/// @brief Doxygen-style documentation tags
enum class DocTag : std::uint8_t {
    Brief,    ///< @brief - Short description
    Param,    ///< @param name description - Parameter documentation
    Return,   ///< @return description - Return value documentation
    Note,     ///< @note - Additional notes
    Example,  ///< @example - Usage example
    See,      ///< @see reference - Cross-reference
};

/// @brief Convert DocTag to string
[[nodiscard]] constexpr const char* to_string(DocTag tag) noexcept {
    switch (tag) {
        case DocTag::Brief:
            return "@brief";
        case DocTag::Param:
            return "@param";
        case DocTag::Return:
            return "@return";
        case DocTag::Note:
            return "@note";
        case DocTag::Example:
            return "@example";
        case DocTag::See:
            return "@see";
    }
    return "@unknown";
}

/// @brief Parameter documentation entry
struct ParamDoc {
    std::string name;         ///< Parameter name
    std::string description;  ///< Parameter description
};

/// @brief Parsed documentation comment
///
/// Represents a structured documentation comment extracted from DSL source.
/// Supports Doxygen-style tags like @brief, @param, @return, etc.
struct DocComment {
    std::string brief;                  ///< Brief description (@brief)
    std::vector<ParamDoc> params;       ///< Parameter documentation (@param)
    std::string return_desc;            ///< Return value description (@return)
    std::vector<std::string> notes;     ///< Additional notes (@note)
    std::vector<std::string> examples;  ///< Usage examples (@example)
    std::vector<std::string> see_also;  ///< Cross-references (@see)
    core::dsl::SourceSpan span;         ///< Location in source

    /// @brief Check if the doc comment has any content
    [[nodiscard]] bool empty() const noexcept {
        return brief.empty() && params.empty() && return_desc.empty() && notes.empty() &&
               examples.empty() && see_also.empty();
    }

    /// @brief Check if the doc comment has a brief description
    [[nodiscard]] bool has_brief() const noexcept { return !brief.empty(); }

    /// @brief Check if the doc comment has parameter documentation
    [[nodiscard]] bool has_params() const noexcept { return !params.empty(); }
};

/// @brief Parser for Doxygen-style documentation comments
///
/// Parses comment text (with leading #) into structured DocComment.
/// Supports multiple comment lines forming a single doc block.
class DocCommentParser {
public:
    /// @brief Parse a single comment line into structured documentation
    ///
    /// @param comment_text The comment text including leading '#'
    /// @param span Source location of the comment
    /// @return Parsed DocComment (may be empty if no doc tags found)
    [[nodiscard]] static DocComment parse_line(std::string_view comment_text,
                                               core::dsl::SourceSpan span);

    /// @brief Parse multiple consecutive comment lines into a single DocComment
    ///
    /// Comments are merged based on tags - multiple @param tags become
    /// multiple entries in params vector, etc.
    ///
    /// @param comment_lines Vector of comment texts (each including leading '#')
    /// @param span Source location spanning all comments
    /// @return Merged DocComment
    [[nodiscard]] static DocComment parse_block(const std::vector<std::string_view>& comment_lines,
                                                core::dsl::SourceSpan span);

    /// @brief Check if a comment line contains documentation tags
    ///
    /// Returns true if the comment contains @brief, @param, etc.
    /// Regular comments without tags are not considered doc comments.
    ///
    /// @param comment_text The comment text including leading '#'
    /// @return True if this is a documentation comment
    [[nodiscard]] static bool is_doc_comment(std::string_view comment_text) noexcept;

private:
    /// @brief Strip leading # and whitespace from comment text
    [[nodiscard]] static std::string_view strip_comment_prefix(std::string_view text) noexcept;

    /// @brief Parse a single @tag from text
    [[nodiscard]] static std::optional<DocTag> parse_tag(std::string_view text) noexcept;

    /// @brief Extract tag value (text after the tag)
    [[nodiscard]] static std::string_view extract_tag_value(std::string_view text,
                                                            std::string_view tag) noexcept;

    /// @brief Parse @param tag: extracts name and description
    [[nodiscard]] static ParamDoc parse_param(std::string_view value);
};

}  // namespace dotvm::cli::doc
