/// @file doc_extractor.cpp
/// @brief CLI-005 Documentation extractor implementation

#include "dotvm/cli/doc/doc_extractor.hpp"

#include <algorithm>

#include "dotvm/core/dsl/lexer.hpp"
#include "dotvm/core/dsl/token.hpp"

namespace dotvm::cli::doc {

std::vector<DocExtractor::CommentInfo> DocExtractor::collect_comments(std::string_view source) {
    std::vector<CommentInfo> comments;

    core::dsl::Lexer lexer{source};
    lexer.set_collect_comments(true);

    while (true) {
        auto token = lexer.next_token();
        if (token.is_eof())
            break;

        if (token.type == core::dsl::TokenType::Comment) {
            comments.push_back({token.lexeme, token.span});
        }
    }

    return comments;
}

std::optional<DocComment>
DocExtractor::find_preceding_doc(const std::vector<CommentInfo>& comments,
                                 const core::dsl::SourceSpan& entity_span) {
    // Find comments that immediately precede the entity
    // (within 1 line, with only whitespace/newlines between)

    std::vector<std::string_view> doc_lines;
    core::dsl::SourceSpan doc_span = entity_span;  // Will be updated

    // Find the latest comment that ends before the entity starts
    // and is a doc comment (contains @tags)
    for (auto it = comments.rbegin(); it != comments.rend(); ++it) {
        // Comment must end before entity starts
        if (it->span.end.line > entity_span.start.line)
            continue;

        // Comment must be within 2 lines of entity or previous comment
        std::uint32_t target_line =
            doc_lines.empty() ? entity_span.start.line : doc_span.start.line;

        // Allow up to 1 blank line between comment and entity/next comment
        if (it->span.end.line + 2 < target_line)
            break;

        // Check if this is a doc comment
        if (DocCommentParser::is_doc_comment(it->text)) {
            doc_lines.insert(doc_lines.begin(), it->text);
            doc_span.start = it->span.start;
            if (doc_lines.size() == 1) {
                doc_span.end = it->span.end;
            }
        } else {
            // Non-doc comment breaks the chain
            break;
        }
    }

    if (doc_lines.empty()) {
        return std::nullopt;
    }

    return DocCommentParser::parse_block(doc_lines, doc_span);
}

void DocExtractor::extract_dot(const core::dsl::DotDef& dot,
                               const std::vector<CommentInfo>& comments,
                               std::vector<DocumentedEntity>& entities) {
    // Extract dot documentation
    DocumentedEntity dot_entity{
        .kind = EntityKind::Dot,
        .name = dot.name,
        .doc = find_preceding_doc(comments, dot.span),
        .span = dot.span,
        .parent_name = {},
    };
    entities.push_back(std::move(dot_entity));

    // Extract state variable documentation
    if (dot.state) {
        for (const auto& var : dot.state->variables) {
            DocumentedEntity var_entity{
                .kind = EntityKind::StateVar,
                .name = var.name,
                .doc = find_preceding_doc(comments, var.span),
                .span = var.span,
                .parent_name = dot.name,
            };
            entities.push_back(std::move(var_entity));
        }
    }

    // Extract trigger documentation
    for (std::size_t i = 0; i < dot.triggers.size(); ++i) {
        const auto& trigger = dot.triggers[i];
        DocumentedEntity trigger_entity{
            .kind = EntityKind::Trigger,
            .name = "trigger_" + std::to_string(i),  // Triggers don't have names
            .doc = find_preceding_doc(comments, trigger.span),
            .span = trigger.span,
            .parent_name = dot.name,
        };
        entities.push_back(std::move(trigger_entity));
    }
}

void DocExtractor::extract_link(const core::dsl::LinkDef& link,
                                const std::vector<CommentInfo>& comments,
                                std::vector<DocumentedEntity>& entities) {
    DocumentedEntity link_entity{
        .kind = EntityKind::Link,
        .name = link.source + " -> " + link.target,
        .doc = find_preceding_doc(comments, link.span),
        .span = link.span,
        .parent_name = {},
    };
    entities.push_back(std::move(link_entity));
}

DocumentationResult DocExtractor::extract(std::string_view source,
                                          const core::dsl::DslModule& module,
                                          std::string_view module_name) {
    DocumentationResult result;
    result.module_name = std::string(module_name);

    // Collect all comments from source
    auto comments = collect_comments(source);

    // Look for module-level documentation at the start of the file
    // (doc comments before any entity)
    if (!comments.empty() && module.span.start.line > 0) {
        std::vector<std::string_view> module_doc_lines;
        core::dsl::SourceSpan module_doc_span;
        bool found_first = false;

        // Find earliest entity
        std::uint32_t earliest_entity_line = UINT32_MAX;
        for (const auto& dot : module.dots) {
            earliest_entity_line = std::min(earliest_entity_line, dot.span.start.line);
        }
        for (const auto& link : module.links) {
            earliest_entity_line = std::min(earliest_entity_line, link.span.start.line);
        }
        for (const auto& import : module.imports) {
            earliest_entity_line = std::min(earliest_entity_line, import.span.start.line);
        }
        for (const auto& include : module.includes) {
            earliest_entity_line = std::min(earliest_entity_line, include.span.start.line);
        }

        // Collect doc comments that appear before all entities
        for (const auto& comment : comments) {
            if (comment.span.start.line >= earliest_entity_line)
                break;

            if (DocCommentParser::is_doc_comment(comment.text)) {
                if (!found_first) {
                    module_doc_span = comment.span;
                    found_first = true;
                }
                module_doc_span.end = comment.span.end;
                module_doc_lines.push_back(comment.text);
            }
        }

        if (!module_doc_lines.empty()) {
            result.module_doc = DocCommentParser::parse_block(module_doc_lines, module_doc_span);
        }
    }

    // Extract documentation for all entities
    for (const auto& dot : module.dots) {
        extract_dot(dot, comments, result.entities);
    }

    for (const auto& link : module.links) {
        extract_link(link, comments, result.entities);
    }

    return result;
}

}  // namespace dotvm::cli::doc
