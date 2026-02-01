#pragma once

/// @file package.hpp
/// @brief PRD-007 Package manifest types
///
/// Defines the package manifest structure and JSON serialization for
/// dotpkg.json files.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include "dotvm/core/result.hpp"
#include "dotvm/pkg/package_error.hpp"
#include "dotvm/pkg/version.hpp"
#include "dotvm/pkg/version_constraint.hpp"

namespace dotvm::pkg {

/// @brief SHA-256 checksum (32 bytes)
using Checksum = std::array<std::uint8_t, 32>;

/// @brief Package dependency with name and version constraint
struct Dependency {
    std::string name;
    VersionConstraint constraint;
};

/// @brief Package manifest representing a dotpkg.json file
///
/// Contains all metadata about a package including its name, version,
/// description, and dependencies.
///
/// @example JSON format:
/// ```json
/// {
///   "name": "mypackage",
///   "version": "1.2.3",
///   "description": "A sample package",
///   "dependencies": {
///     "libfoo": "^1.0.0",
///     "libbar": ">=2.0.0"
///   }
/// }
/// ```
struct PackageManifest {
    /// @brief Package name (required)
    std::string name;

    /// @brief Package version (required)
    Version version;

    /// @brief Package description (optional)
    std::string description;

    /// @brief Package dependencies
    std::unordered_map<std::string, VersionConstraint> dependencies;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /// @brief Parse a package manifest from JSON string
    [[nodiscard]] static core::Result<PackageManifest, PackageError> from_json(
        std::string_view json) noexcept;

    /// @brief Load a package manifest from file
    [[nodiscard]] static core::Result<PackageManifest, PackageError> from_file(
        const std::filesystem::path& path) noexcept;

    // =========================================================================
    // Serialization
    // =========================================================================

    /// @brief Convert manifest to JSON string
    [[nodiscard]] std::string to_json() const;

    /// @brief Save manifest to file
    [[nodiscard]] core::Result<void, PackageError> save(
        const std::filesystem::path& path) const noexcept;
};

/// @brief Installed package entry in the registry
struct InstalledPackage {
    /// @brief Package name
    std::string name;

    /// @brief Installed version
    Version version;

    /// @brief Package checksum for integrity verification
    Checksum checksum;

    /// @brief Installation path
    std::filesystem::path install_path;

    /// @brief Resolved dependencies (name -> installed version)
    std::unordered_map<std::string, Version> resolved_dependencies;

    /// @brief Unix timestamp of installation
    std::uint32_t installed_at{0};
};

/// @brief Package source - either a local path or archive
struct PackageSource {
    /// @brief Path to the package (directory or .tar.gz file)
    std::filesystem::path path;

    /// @brief Whether the source is an archive file
    bool is_archive{false};

    /// @brief Check if the source exists and is valid
    [[nodiscard]] bool is_valid() const noexcept;
};

/// @brief Locked package entry in the lock file
struct LockedPackage {
    /// @brief Package name
    std::string name;

    /// @brief Locked version
    Version version;

    /// @brief Package checksum
    Checksum checksum;

    /// @brief Direct dependencies of this package
    std::vector<std::string> dependencies;
};

// ============================================================================
// Utility Functions
// ============================================================================

/// @brief Validate a package name
///
/// Package names must:
/// - Be 1-128 characters
/// - Start with a letter
/// - Contain only lowercase letters, digits, hyphens
/// - Not start or end with a hyphen
///
/// @param name Package name to validate
/// @return true if valid
[[nodiscard]] bool is_valid_package_name(std::string_view name) noexcept;

/// @brief Convert checksum to hex string
[[nodiscard]] std::string checksum_to_hex(const Checksum& checksum) noexcept;

/// @brief Parse checksum from hex string
[[nodiscard]] core::Result<Checksum, PackageError> checksum_from_hex(
    std::string_view hex) noexcept;

}  // namespace dotvm::pkg
