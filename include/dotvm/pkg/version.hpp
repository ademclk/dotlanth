#pragma once

/// @file version.hpp
/// @brief PRD-007 Semantic versioning (SemVer) type
///
/// Provides version parsing, comparison, and manipulation following
/// the Semantic Versioning 2.0.0 specification.
///
/// @see https://semver.org/

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

#include "dotvm/core/result.hpp"
#include "dotvm/pkg/package_error.hpp"

namespace dotvm::pkg {

/// @brief Semantic version representation
///
/// Represents a version following SemVer 2.0.0:
/// - MAJOR.MINOR.PATCH[-PRERELEASE]
///
/// Comparison follows SemVer precedence rules:
/// 1. Compare major, minor, patch numerically
/// 2. Prerelease versions have lower precedence than release versions
/// 3. Prereleases are compared lexicographically
///
/// @example
/// ```cpp
/// auto v1 = Version::parse("1.2.3").value();
/// auto v2 = Version::parse("1.2.4-alpha").value();
/// assert(v2 > v1);  // but v2.to_string() is "1.2.4-alpha"
/// ```
struct Version {
    /// @brief Major version component
    std::uint32_t major{0};

    /// @brief Minor version component
    std::uint32_t minor{0};

    /// @brief Patch version component
    std::uint32_t patch{0};

    /// @brief Optional prerelease identifier (e.g., "alpha", "beta.1", "rc.2")
    std::string prerelease;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /// @brief Parse a version string
    ///
    /// Accepts versions in the format: MAJOR.MINOR.PATCH[-PRERELEASE]
    /// Examples: "1.2.3", "0.1.0", "2.0.0-alpha", "1.0.0-beta.1"
    ///
    /// @param version_str The version string to parse
    /// @return Version on success, PackageError on failure
    [[nodiscard]] static core::Result<Version, PackageError> parse(
        std::string_view version_str) noexcept;

    // =========================================================================
    // Comparison Operators
    // =========================================================================

    /// @brief Three-way comparison operator
    ///
    /// Follows SemVer precedence rules:
    /// - Compare major, minor, patch numerically (most significant first)
    /// - A prerelease version has lower precedence than a normal version
    /// - Prereleases are compared lexicographically by component
    [[nodiscard]] constexpr std::strong_ordering operator<=>(
        const Version& other) const noexcept;

    /// @brief Equality operator
    [[nodiscard]] constexpr bool operator==(const Version& other) const noexcept = default;

    // =========================================================================
    // String Conversion
    // =========================================================================

    /// @brief Convert version to string representation
    ///
    /// Returns the canonical string representation: MAJOR.MINOR.PATCH[-PRERELEASE]
    [[nodiscard]] std::string to_string() const;

    // =========================================================================
    // Version Properties
    // =========================================================================

    /// @brief Check if this is a prerelease version
    [[nodiscard]] constexpr bool is_prerelease() const noexcept {
        return !prerelease.empty();
    }

    /// @brief Check if this is a stable version (major > 0, no prerelease)
    [[nodiscard]] constexpr bool is_stable() const noexcept {
        return major > 0 && prerelease.empty();
    }

    // =========================================================================
    // Version Arithmetic
    // =========================================================================

    /// @brief Create new version with incremented major (resets minor, patch, prerelease)
    [[nodiscard]] constexpr Version increment_major() const noexcept {
        return Version{major + 1, 0, 0, {}};
    }

    /// @brief Create new version with incremented minor (resets patch, prerelease)
    [[nodiscard]] constexpr Version increment_minor() const noexcept {
        return Version{major, minor + 1, 0, {}};
    }

    /// @brief Create new version with incremented patch (clears prerelease)
    [[nodiscard]] constexpr Version increment_patch() const noexcept {
        return Version{major, minor, patch + 1, {}};
    }
};

// =========================================================================
// Implementation of constexpr Methods
// =========================================================================

constexpr std::strong_ordering Version::operator<=>(const Version& other) const noexcept {
    // Compare major.minor.patch first
    if (auto cmp = major <=> other.major; cmp != 0) {
        return cmp;
    }
    if (auto cmp = minor <=> other.minor; cmp != 0) {
        return cmp;
    }
    if (auto cmp = patch <=> other.patch; cmp != 0) {
        return cmp;
    }

    // SemVer rule: prerelease version has lower precedence than release
    // Empty prerelease means release version (higher precedence)
    const bool this_has_pre = !prerelease.empty();
    const bool other_has_pre = !other.prerelease.empty();

    if (!this_has_pre && other_has_pre) {
        return std::strong_ordering::greater;  // release > prerelease
    }
    if (this_has_pre && !other_has_pre) {
        return std::strong_ordering::less;  // prerelease < release
    }
    if (!this_has_pre && !other_has_pre) {
        return std::strong_ordering::equal;  // both release
    }

    // Both have prereleases - compare lexicographically
    // Note: This is a simplification; full SemVer compares by dot-separated identifiers
    if (prerelease < other.prerelease) {
        return std::strong_ordering::less;
    }
    if (prerelease > other.prerelease) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

}  // namespace dotvm::pkg
