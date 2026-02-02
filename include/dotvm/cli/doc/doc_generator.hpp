#pragma once

/// @file doc_generator.hpp
/// @brief CLI-005 Documentation generator interface and implementations
///
/// Provides abstract interface for documentation generators and concrete
/// implementations for Markdown and HTML output formats.

#include <cctype>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

#include "dotvm/cli/doc/doc_extractor.hpp"

namespace dotvm::cli::doc {

/// @brief Convert name to anchor slug (lowercase, alphanumeric, hyphen-separated)
/// @param name The name to convert
/// @return Anchor slug suitable for HTML id or Markdown reference
[[nodiscard]] inline std::string make_anchor(std::string_view name) {
    std::string anchor;
    anchor.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            anchor.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (c == ' ' || c == '_' || c == '-') {
            anchor.push_back('-');
        }
        // Skip other characters
    }
    return anchor;
}

/// @brief Output format for documentation
enum class DocFormat : std::uint8_t {
    Markdown,  ///< Markdown format (.md)
    Html,      ///< HTML format (.html)
};

/// @brief Parse format string to DocFormat
[[nodiscard]] inline std::optional<DocFormat> parse_format(std::string_view format) noexcept {
    if (format == "md" || format == "markdown")
        return DocFormat::Markdown;
    if (format == "html")
        return DocFormat::Html;
    return std::nullopt;
}

/// @brief Get file extension for format
[[nodiscard]] constexpr const char* format_extension(DocFormat format) noexcept {
    switch (format) {
        case DocFormat::Markdown:
            return ".md";
        case DocFormat::Html:
            return ".html";
    }
    return "";
}

/// @brief Abstract documentation generator interface
class DocGenerator {
public:
    virtual ~DocGenerator() = default;

    /// @brief Generate documentation to output stream
    /// @param result Extracted documentation result
    /// @param out Output stream
    virtual void generate(const DocumentationResult& result, std::ostream& out) = 0;

    /// @brief Create a generator for the specified format
    [[nodiscard]] static std::unique_ptr<DocGenerator> create(DocFormat format);
};

/// @brief Markdown documentation generator
class MarkdownGenerator : public DocGenerator {
public:
    void generate(const DocumentationResult& result, std::ostream& out) override;

private:
    void write_header(const DocumentationResult& result, std::ostream& out);
    void write_toc(const DocumentationResult& result, std::ostream& out);
    void write_module_doc(const DocumentationResult& result, std::ostream& out);
    void write_entities(const DocumentationResult& result, std::ostream& out);
    void write_entity(const DocumentedEntity& entity, std::ostream& out);
    void write_doc_comment(const DocComment& doc, std::ostream& out);
};

/// @brief HTML documentation generator
class HtmlGenerator : public DocGenerator {
public:
    void generate(const DocumentationResult& result, std::ostream& out) override;

private:
    void write_html_header(const DocumentationResult& result, std::ostream& out);
    void write_navigation(const DocumentationResult& result, std::ostream& out);
    void write_content(const DocumentationResult& result, std::ostream& out);
    void write_entity_card(const DocumentedEntity& entity, std::ostream& out);
    void write_html_footer(std::ostream& out);

    /// @brief Escape HTML special characters
    [[nodiscard]] static std::string escape_html(std::string_view text);
};

}  // namespace dotvm::cli::doc
