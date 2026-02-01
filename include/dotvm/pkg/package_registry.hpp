#pragma once

/// @file package_registry.hpp
/// @brief PRD-007 Installed package registry
///
/// Tracks installed packages for a project. The registry stores information
/// about packages installed in the project's node_modules-style directory.
///
/// Registry structure (stored in dotpkg_registry.json):
/// ```json
/// {
///   "packages": {
///     "mylib": {
///       "version": "1.2.3",
///       "checksum": "abc123...",
///       "installedAt": 1234567890,
///       "dependencies": ["otherlib"]
///     }
///   }
/// }
/// ```

#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/pkg/package.hpp"
#include "dotvm/pkg/package_error.hpp"
#include "dotvm/pkg/version.hpp"

namespace dotvm::pkg {

/// @brief Registry entry for an installed package
struct RegistryEntry {
    /// @brief Package version
    Version version;

    /// @brief Package checksum
    Checksum checksum;

    /// @brief Installation timestamp (unix seconds)
    std::uint32_t installed_at{0};

    /// @brief Direct dependencies of this package
    std::vector<std::string> dependencies;

    /// @brief Installation path (relative to project root)
    std::filesystem::path install_path;
};

/// @brief Package registry for tracking installed packages
///
/// Maintains a JSON file tracking all installed packages in a project.
/// Used to determine what's installed, verify integrity, and handle
/// updates/uninstalls.
///
/// @example
/// ```cpp
/// PackageRegistry registry("./dotpkg_registry.json");
/// registry.load();
///
/// // Register a newly installed package
/// registry.add("mylib", entry);
///
/// // Check what's installed
/// if (auto entry = registry.get("mylib")) {
///     std::cout << "Installed: " << entry->version.to_string() << "\n";
/// }
///
/// registry.save();
/// ```
class PackageRegistry {
public:
    /// @brief Construct a registry at the given path
    /// @param path Path to the registry JSON file
    explicit PackageRegistry(std::filesystem::path path) noexcept;

    /// @brief Destructor
    ~PackageRegistry() = default;

    // Non-copyable, movable
    PackageRegistry(const PackageRegistry&) = delete;
    PackageRegistry& operator=(const PackageRegistry&) = delete;
    PackageRegistry(PackageRegistry&&) noexcept = default;
    PackageRegistry& operator=(PackageRegistry&&) noexcept = default;

    // =========================================================================
    // Persistence
    // =========================================================================

    /// @brief Load the registry from disk
    /// @return void on success, error if file exists but is invalid
    [[nodiscard]] core::Result<void, PackageError> load() noexcept;

    /// @brief Save the registry to disk
    [[nodiscard]] core::Result<void, PackageError> save() const noexcept;

    /// @brief Check if the registry file exists
    [[nodiscard]] bool exists() const noexcept;

    // =========================================================================
    // Package Management
    // =========================================================================

    /// @brief Add or update a package in the registry
    void add(std::string name, RegistryEntry entry) noexcept;

    /// @brief Remove a package from the registry
    /// @return true if removed, false if not found
    bool remove(std::string_view name) noexcept;

    /// @brief Get a registered package
    [[nodiscard]] const RegistryEntry* get(std::string_view name) const noexcept;

    /// @brief Check if a package is registered
    [[nodiscard]] bool has(std::string_view name) const noexcept;

    // =========================================================================
    // Query Operations
    // =========================================================================

    /// @brief Get all registered packages
    [[nodiscard]] const std::unordered_map<std::string, RegistryEntry>& packages() const noexcept {
        return packages_;
    }

    /// @brief Get the number of registered packages
    [[nodiscard]] std::size_t count() const noexcept { return packages_.size(); }

    /// @brief Get all package names
    [[nodiscard]] std::vector<std::string> package_names() const noexcept;

    /// @brief Get packages that depend on the given package
    [[nodiscard]] std::vector<std::string> dependents_of(std::string_view name) const noexcept;

    // =========================================================================
    // Modification
    // =========================================================================

    /// @brief Clear all registered packages
    void clear() noexcept;

    // =========================================================================
    // Path Access
    // =========================================================================

    /// @brief Get the registry file path
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    std::unordered_map<std::string, RegistryEntry> packages_;
};

}  // namespace dotvm::pkg
