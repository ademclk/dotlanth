#pragma once

/// @file doc_extractor.hpp
/// @brief CLI-005 Documentation extractor
///
/// Extracts documentation from DSL source by associating doc comments
/// with their corresponding AST entities.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/cli/doc/doc_comment.hpp"
#include "dotvm/core/dsl/ast.hpp"
#include "dotvm/core/dsl/source_location.hpp"

namespace dotvm::cli::doc {

/// @brief Type of documented entity
enum class EntityKind : std::uint8_t {
    Module,    ///< Module-level documentation
    Dot,       ///< Dot (agent) definition
    StateVar,  ///< State variable
    Trigger,   ///< Trigger definition
    Link,      ///< Link definition
};

/// @brief Convert EntityKind to string
[[nodiscard]] constexpr const char* to_string(EntityKind kind) noexcept {
    switch (kind) {
        case EntityKind::Module:
            return "module";
        case EntityKind::Dot:
            return "dot";
        case EntityKind::StateVar:
            return "state_var";
        case EntityKind::Trigger:
            return "trigger";
        case EntityKind::Link:
            return "link";
    }
    return "unknown";
}

/// @brief A documented entity extracted from DSL source
struct DocumentedEntity {
    EntityKind kind;                ///< Entity type
    std::string name;               ///< Entity name
    std::optional<DocComment> doc;  ///< Associated documentation
    core::dsl::SourceSpan span;     ///< Source location
    std::string parent_name;        ///< Parent entity name (for nested entities)
};

/// @brief Result of documentation extraction
struct DocumentationResult {
    std::string module_name;                 ///< Source file/module name
    std::optional<DocComment> module_doc;    ///< Module-level documentation
    std::vector<DocumentedEntity> entities;  ///< All documented entities
    std::vector<std::string> warnings;       ///< Extraction warnings (orphaned comments, etc.)
};

/// @brief Extracts documentation from DSL source and AST
///
/// Uses the lexer with comment collection enabled to capture doc comments,
/// then associates them with AST entities based on source position.
class DocExtractor {
public:
    /// @brief Extract documentation from DSL source
    ///
    /// @param source The original DSL source text
    /// @param module The parsed AST module
    /// @param module_name Name to use for the module (typically filename)
    /// @return Extracted documentation result
    [[nodiscard]] static DocumentationResult extract(std::string_view source,
                                                     const core::dsl::DslModule& module,
                                                     std::string_view module_name);

private:
    /// @brief Comment with its source location
    struct CommentInfo {
        std::string_view text;
        core::dsl::SourceSpan span;
    };

    /// @brief Collect all comments from source
    [[nodiscard]] static std::vector<CommentInfo> collect_comments(std::string_view source);

    /// @brief Find doc comments immediately preceding a given location
    [[nodiscard]] static std::optional<DocComment>
    find_preceding_doc(const std::vector<CommentInfo>& comments,
                       const core::dsl::SourceSpan& entity_span);

    /// @brief Extract documentation for a dot definition
    static void extract_dot(const core::dsl::DotDef& dot, const std::vector<CommentInfo>& comments,
                            std::vector<DocumentedEntity>& entities);

    /// @brief Extract documentation for a link definition
    static void extract_link(const core::dsl::LinkDef& link,
                             const std::vector<CommentInfo>& comments,
                             std::vector<DocumentedEntity>& entities);
};

}  // namespace dotvm::cli::doc
