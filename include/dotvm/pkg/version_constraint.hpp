#pragma once

/// @file version_constraint.hpp
/// @brief PRD-007 Version constraint types for dependency resolution
///
/// Supports SemVer-style version constraints:
/// - Exact: "1.2.3" - exactly this version
/// - Caret: "^1.2.3" - compatible updates (same major)
/// - Tilde: "~1.2.3" - patch-level updates (same minor)
/// - Greater: ">1.0.0"
/// - GreaterEq: ">=1.0.0"
/// - Less: "<2.0.0"
/// - LessEq: "<=2.0.0"

#include <string>
#include <string_view>

#include "dotvm/core/result.hpp"
#include "dotvm/pkg/package_error.hpp"
#include "dotvm/pkg/version.hpp"

namespace dotvm::pkg {

/// @brief Version constraint operator
enum class ConstraintOp : std::uint8_t {
    Exact,      ///< Exact version match: "1.2.3"
    Greater,    ///< Greater than: ">1.0.0"
    GreaterEq,  ///< Greater than or equal: ">=1.0.0"
    Less,       ///< Less than: "<2.0.0"
    LessEq,     ///< Less than or equal: "<=2.0.0"
    Caret,      ///< Compatible updates: "^1.2.3" (same major, or if 0.x.y same minor)
    Tilde,      ///< Patch-level updates: "~1.2.3" (same minor)
};

/// @brief Convert ConstraintOp to string
[[nodiscard]] constexpr std::string_view to_string(ConstraintOp op) noexcept {
    switch (op) {
        case ConstraintOp::Exact:
            return "";
        case ConstraintOp::Greater:
            return ">";
        case ConstraintOp::GreaterEq:
            return ">=";
        case ConstraintOp::Less:
            return "<";
        case ConstraintOp::LessEq:
            return "<=";
        case ConstraintOp::Caret:
            return "^";
        case ConstraintOp::Tilde:
            return "~";
    }
    return "";
}

/// @brief Version constraint for dependency resolution
///
/// Represents a constraint on a package version that can be used to
/// determine whether a given version satisfies the constraint.
///
/// @example
/// ```cpp
/// auto constraint = VersionConstraint::parse("^1.2.0").value();
/// auto v1 = Version::parse("1.3.0").value();
/// auto v2 = Version::parse("2.0.0").value();
/// assert(constraint.satisfies(v1));   // true
/// assert(!constraint.satisfies(v2));  // false
/// ```
struct VersionConstraint {
    /// @brief The constraint operator
    ConstraintOp op{ConstraintOp::Exact};

    /// @brief The reference version for the constraint
    Version version;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /// @brief Parse a version constraint string
    ///
    /// Supported formats:
    /// - "1.2.3" (exact)
    /// - "^1.2.3" (caret - compatible)
    /// - "~1.2.3" (tilde - patch updates)
    /// - ">1.0.0" (greater than)
    /// - ">=1.0.0" (greater than or equal)
    /// - "<2.0.0" (less than)
    /// - "<=2.0.0" (less than or equal)
    ///
    /// @param constraint_str The constraint string to parse
    /// @return VersionConstraint on success, PackageError on failure
    [[nodiscard]] static core::Result<VersionConstraint, PackageError> parse(
        std::string_view constraint_str) noexcept;

    // =========================================================================
    // Constraint Checking
    // =========================================================================

    /// @brief Check if a version satisfies this constraint
    ///
    /// @param v The version to check
    /// @return true if the version satisfies the constraint
    [[nodiscard]] bool satisfies(const Version& v) const noexcept;

    // =========================================================================
    // String Conversion
    // =========================================================================

    /// @brief Convert constraint to string representation
    [[nodiscard]] std::string to_string() const;
};

}  // namespace dotvm::pkg
