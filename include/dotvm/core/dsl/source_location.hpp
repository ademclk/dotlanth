#pragma once

/// @file source_location.hpp
/// @brief DSL-001 Source location tracking for error reporting
///
/// Provides precise source location information for tokens and AST nodes,
/// enabling clear error messages with line/column information.

#include <cstddef>
#include <cstdint>

namespace dotvm::core::dsl {

/// @brief Represents a position in source code
///
/// Used for pinpointing exact locations for error reporting.
/// All values are 1-based for user-friendly display.
struct SourceLocation {
    /// Line number (1-based)
    std::uint32_t line{1};

    /// Column number (1-based)
    std::uint32_t column{1};

    /// Byte offset from start of source (0-based)
    std::size_t offset{0};

    /// @brief Create a source location
    [[nodiscard]] static constexpr SourceLocation at(std::uint32_t line, std::uint32_t column,
                                                     std::size_t offset = 0) noexcept {
        return SourceLocation{line, column, offset};
    }

    /// @brief Check equality
    [[nodiscard]] constexpr bool operator==(const SourceLocation&) const noexcept = default;

    /// @brief Compare locations (by offset for ordering)
    [[nodiscard]] constexpr auto operator<=>(const SourceLocation& other) const noexcept {
        return offset <=> other.offset;
    }
};

/// @brief Represents a span of source code
///
/// Covers a range from start to end location, typically used for
/// tokens and AST nodes to track their full extent in the source.
struct SourceSpan {
    /// Start location of the span
    SourceLocation start;

    /// End location of the span (exclusive)
    SourceLocation end;

    /// @brief Create a span from two locations
    [[nodiscard]] static constexpr SourceSpan from(SourceLocation start,
                                                   SourceLocation end) noexcept {
        return SourceSpan{start, end};
    }

    /// @brief Create a span covering a single position
    [[nodiscard]] static constexpr SourceSpan at(SourceLocation loc) noexcept {
        return SourceSpan{loc, loc};
    }

    /// @brief Get the byte length of this span
    [[nodiscard]] constexpr std::size_t length() const noexcept {
        return end.offset >= start.offset ? end.offset - start.offset : 0;
    }

    /// @brief Check if this span contains a location
    [[nodiscard]] constexpr bool contains(SourceLocation loc) const noexcept {
        return loc.offset >= start.offset && loc.offset < end.offset;
    }

    /// @brief Merge two spans into one covering both
    [[nodiscard]] constexpr SourceSpan merge(const SourceSpan& other) const noexcept {
        return SourceSpan{start < other.start ? start : other.start,
                          end > other.end ? end : other.end};
    }

    /// @brief Check equality
    [[nodiscard]] constexpr bool operator==(const SourceSpan&) const noexcept = default;
};

}  // namespace dotvm::core::dsl
