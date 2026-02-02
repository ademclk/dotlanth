/// @file doc_generator.cpp
/// @brief CLI-005 Documentation generator implementations

#include "dotvm/cli/doc/doc_generator.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace dotvm::cli::doc {

std::unique_ptr<DocGenerator> DocGenerator::create(DocFormat format) {
    switch (format) {
        case DocFormat::Markdown:
            return std::make_unique<MarkdownGenerator>();
        case DocFormat::Html:
            return std::make_unique<HtmlGenerator>();
    }
    return nullptr;
}

// ============================================================================
// MarkdownGenerator Implementation
// ============================================================================

void MarkdownGenerator::generate(const DocumentationResult& result, std::ostream& out) {
    write_header(result, out);
    write_toc(result, out);
    write_module_doc(result, out);
    write_entities(result, out);
}

void MarkdownGenerator::write_header(const DocumentationResult& result, std::ostream& out) {
    out << "# " << result.module_name << "\n\n";
}

void MarkdownGenerator::write_toc(const DocumentationResult& result, std::ostream& out) {
    out << "## Table of Contents\n\n";

    // Collect unique entity kinds
    bool has_dots = false;
    bool has_links = false;
    for (const auto& entity : result.entities) {
        if (entity.kind == EntityKind::Dot)
            has_dots = true;
        if (entity.kind == EntityKind::Link)
            has_links = true;
    }

    if (has_dots) {
        out << "- [Dots](#dots)\n";
        for (const auto& entity : result.entities) {
            if (entity.kind == EntityKind::Dot) {
                out << "  - [" << entity.name << "](#" << make_anchor(entity.name) << ")\n";
            }
        }
    }

    if (has_links) {
        out << "- [Links](#links)\n";
    }

    out << "\n";
}

void MarkdownGenerator::write_module_doc(const DocumentationResult& result, std::ostream& out) {
    if (result.module_doc) {
        write_doc_comment(*result.module_doc, out);
        out << "\n";
    }
}

void MarkdownGenerator::write_entities(const DocumentationResult& result, std::ostream& out) {
    // Write dots section
    bool first_dot = true;
    for (const auto& entity : result.entities) {
        if (entity.kind == EntityKind::Dot) {
            if (first_dot) {
                out << "## Dots\n\n";
                first_dot = false;
            }
            write_entity(entity, out);
        }
    }

    // Write links section
    bool first_link = true;
    for (const auto& entity : result.entities) {
        if (entity.kind == EntityKind::Link) {
            if (first_link) {
                out << "## Links\n\n";
                first_link = false;
            }
            write_entity(entity, out);
        }
    }
}

void MarkdownGenerator::write_entity(const DocumentedEntity& entity, std::ostream& out) {
    switch (entity.kind) {
        case EntityKind::Dot:
            out << "### " << entity.name << "\n\n";
            if (entity.doc) {
                write_doc_comment(*entity.doc, out);
            } else {
                out << "*No documentation*\n";
            }
            out << "\n";
            break;

        case EntityKind::StateVar:
            out << "- `" << entity.name << "`: ";
            if (entity.doc && !entity.doc->brief.empty()) {
                out << entity.doc->brief;
            } else {
                out << "(undocumented)";
            }
            out << "\n";
            break;

        case EntityKind::Link:
            out << "- **" << entity.name << "**";
            if (entity.doc && !entity.doc->brief.empty()) {
                out << ": " << entity.doc->brief;
            }
            out << "\n";
            break;

        case EntityKind::Trigger:
            out << "- Trigger: ";
            if (entity.doc && !entity.doc->brief.empty()) {
                out << entity.doc->brief;
            } else {
                out << "(undocumented)";
            }
            out << "\n";
            break;

        case EntityKind::Module:
            // Module is handled separately
            break;
    }
}

void MarkdownGenerator::write_doc_comment(const DocComment& doc, std::ostream& out) {
    if (!doc.brief.empty()) {
        out << doc.brief << "\n\n";
    }

    if (!doc.params.empty()) {
        out << "**Parameters:**\n\n";
        for (const auto& param : doc.params) {
            out << "- `" << param.name << "`: " << param.description << "\n";
        }
        out << "\n";
    }

    if (!doc.return_desc.empty()) {
        out << "**Returns:** " << doc.return_desc << "\n\n";
    }

    for (const auto& note : doc.notes) {
        out << "> **Note:** " << note << "\n\n";
    }

    if (!doc.see_also.empty()) {
        out << "**See also:** ";
        bool first = true;
        for (const auto& ref : doc.see_also) {
            if (!first)
                out << ", ";
            out << "[" << ref << "](#" << make_anchor(ref) << ")";
            first = false;
        }
        out << "\n\n";
    }
}

// ============================================================================
// HtmlGenerator Implementation
// ============================================================================

std::string HtmlGenerator::escape_html(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&#39;";
                break;
            default:
                result.push_back(c);
                break;
        }
    }
    return result;
}

void HtmlGenerator::generate(const DocumentationResult& result, std::ostream& out) {
    write_html_header(result, out);
    out << "<body>\n";
    out << "<div class=\"container\">\n";
    write_navigation(result, out);
    write_content(result, out);
    out << "</div>\n";
    write_html_footer(out);
}

void HtmlGenerator::write_html_header(const DocumentationResult& result, std::ostream& out) {
    out << R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>)"
        << escape_html(result.module_name) << R"( - Documentation</title>
    <style>
        :root {
            --bg-color: #1a1a2e;
            --card-bg: #16213e;
            --text-color: #eaeaea;
            --accent-color: #0f3460;
            --highlight: #e94560;
            --code-bg: #0d1117;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: var(--bg-color);
            color: var(--text-color);
            line-height: 1.6;
        }
        .container {
            display: grid;
            grid-template-columns: 250px 1fr;
            min-height: 100vh;
        }
        nav {
            background: var(--card-bg);
            padding: 2rem 1rem;
            position: sticky;
            top: 0;
            height: 100vh;
            overflow-y: auto;
        }
        nav h2 { color: var(--highlight); margin-bottom: 1rem; font-size: 1.2rem; }
        nav ul { list-style: none; }
        nav li { margin: 0.5rem 0; }
        nav a {
            color: var(--text-color);
            text-decoration: none;
            opacity: 0.8;
            transition: opacity 0.2s;
        }
        nav a:hover { opacity: 1; color: var(--highlight); }
        main {
            padding: 2rem;
            max-width: 900px;
        }
        h1 { color: var(--highlight); margin-bottom: 1rem; }
        h2 { color: var(--text-color); margin: 2rem 0 1rem; border-bottom: 1px solid var(--accent-color); padding-bottom: 0.5rem; }
        h3 { color: var(--text-color); margin: 1.5rem 0 0.5rem; }
        .card {
            background: var(--card-bg);
            border-radius: 8px;
            padding: 1.5rem;
            margin: 1rem 0;
        }
        .entity-kind {
            display: inline-block;
            background: var(--accent-color);
            color: var(--highlight);
            padding: 0.2rem 0.5rem;
            border-radius: 4px;
            font-size: 0.8rem;
            margin-bottom: 0.5rem;
        }
        code {
            background: var(--code-bg);
            padding: 0.2rem 0.4rem;
            border-radius: 3px;
            font-family: 'Consolas', 'Monaco', monospace;
        }
        .params { margin: 1rem 0; }
        .params dt { color: var(--highlight); font-weight: bold; }
        .params dd { margin-left: 1rem; margin-bottom: 0.5rem; }
        .note {
            background: var(--accent-color);
            border-left: 3px solid var(--highlight);
            padding: 0.5rem 1rem;
            margin: 1rem 0;
        }
        .see-also a { color: var(--highlight); }
    </style>
</head>
)";
}

void HtmlGenerator::write_navigation(const DocumentationResult& result, std::ostream& out) {
    out << "<nav>\n";
    out << "<h2>" << escape_html(result.module_name) << "</h2>\n";
    out << "<ul>\n";

    // Collect dots
    for (const auto& entity : result.entities) {
        if (entity.kind == EntityKind::Dot) {
            out << "<li><a href=\"#" << make_anchor(entity.name) << "\">"
                << escape_html(entity.name) << "</a></li>\n";
        }
    }

    // Links section
    bool has_links = std::any_of(result.entities.begin(), result.entities.end(),
                                 [](const auto& e) { return e.kind == EntityKind::Link; });
    if (has_links) {
        out << "<li><a href=\"#links\">Links</a></li>\n";
    }

    out << "</ul>\n";
    out << "</nav>\n";
}

void HtmlGenerator::write_content(const DocumentationResult& result, std::ostream& out) {
    out << "<main>\n";
    out << "<h1>" << escape_html(result.module_name) << "</h1>\n";

    if (result.module_doc && !result.module_doc->brief.empty()) {
        out << "<p>" << escape_html(result.module_doc->brief) << "</p>\n";
    }

    // Dots section
    bool first_dot = true;
    for (const auto& entity : result.entities) {
        if (entity.kind == EntityKind::Dot) {
            if (first_dot) {
                out << "<h2>Dots</h2>\n";
                first_dot = false;
            }
            write_entity_card(entity, out);

            // Write associated state vars
            out << "<div style=\"margin-left: 1rem;\">\n";
            for (const auto& state : result.entities) {
                if (state.kind == EntityKind::StateVar && state.parent_name == entity.name) {
                    write_entity_card(state, out);
                }
            }
            out << "</div>\n";
        }
    }

    // Links section
    bool first_link = true;
    for (const auto& entity : result.entities) {
        if (entity.kind == EntityKind::Link) {
            if (first_link) {
                out << "<h2 id=\"links\">Links</h2>\n";
                first_link = false;
            }
            write_entity_card(entity, out);
        }
    }

    out << "</main>\n";
}

void HtmlGenerator::write_entity_card(const DocumentedEntity& entity, std::ostream& out) {
    out << "<div class=\"card\" id=\"" << make_anchor(entity.name) << "\">\n";
    out << "<span class=\"entity-kind\">" << to_string(entity.kind) << "</span>\n";
    out << "<h3>" << escape_html(entity.name) << "</h3>\n";

    if (entity.doc) {
        if (!entity.doc->brief.empty()) {
            out << "<p>" << escape_html(entity.doc->brief) << "</p>\n";
        }

        if (!entity.doc->params.empty()) {
            out << "<dl class=\"params\">\n";
            for (const auto& param : entity.doc->params) {
                out << "<dt><code>" << escape_html(param.name) << "</code></dt>\n";
                out << "<dd>" << escape_html(param.description) << "</dd>\n";
            }
            out << "</dl>\n";
        }

        for (const auto& note : entity.doc->notes) {
            out << "<div class=\"note\">" << escape_html(note) << "</div>\n";
        }

        if (!entity.doc->see_also.empty()) {
            out << "<p class=\"see-also\">See also: ";
            bool first = true;
            for (const auto& ref : entity.doc->see_also) {
                if (!first)
                    out << ", ";
                out << "<a href=\"#" << make_anchor(ref) << "\">" << escape_html(ref) << "</a>";
                first = false;
            }
            out << "</p>\n";
        }
    } else {
        out << "<p><em>No documentation</em></p>\n";
    }

    out << "</div>\n";
}

void HtmlGenerator::write_html_footer(std::ostream& out) {
    out << R"(</body>
</html>
)";
}

}  // namespace dotvm::cli::doc
