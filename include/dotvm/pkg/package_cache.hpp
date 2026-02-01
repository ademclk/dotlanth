#pragma once

/// @file package_cache.hpp
/// @brief PRD-007 Local package cache management
///
/// Manages the local package cache at ~/.dotpkg/cache. The cache stores
/// downloaded and extracted packages for fast access.
///
/// Cache Directory Structure:
/// ```
/// ~/.dotpkg/
/// ├── config.json
/// └── cache/
///     └── packages/
///         └── <name>/
///             └── <version>/
///                 ├── dotpkg.json      # Package manifest
///                 ├── contents/        # Extracted package files
///                 └── .checksum        # SHA-256 checksum
/// ```

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/pkg/package.hpp"
#include "dotvm/pkg/package_error.hpp"
#include "dotvm/pkg/version.hpp"

namespace dotvm::pkg {

/// @brief Cache configuration
struct CacheConfig {
    /// @brief Root directory for the cache (default: ~/.dotpkg)
    std::filesystem::path root_dir;

    /// @brief Maximum cache size in bytes (0 = unlimited)
    std::uint64_t max_size{0};

    /// @brief Create default configuration
    [[nodiscard]] static CacheConfig defaults();
};

/// @brief Cached package information
struct CachedPackage {
    /// @brief Package name
    std::string name;

    /// @brief Package version
    Version version;

    /// @brief Path to the cached package directory
    std::filesystem::path path;

    /// @brief Package checksum
    Checksum checksum;

    /// @brief Size of the cached package in bytes
    std::uint64_t size{0};

    /// @brief Unix timestamp of when the package was cached
    std::uint32_t cached_at{0};
};

/// @brief Local package cache manager
///
/// The PackageCache manages a local filesystem cache of packages.
/// Packages are stored by name and version, with integrity verification
/// via SHA-256 checksums.
///
/// @example
/// ```cpp
/// PackageCache cache(CacheConfig::defaults());
///
/// // Add a package to the cache
/// cache.add_from_source(source, manifest);
///
/// // Check if a package is cached
/// if (cache.has(name, version)) {
///     auto pkg = cache.get(name, version);
/// }
///
/// // List all versions of a package
/// auto versions = cache.list_versions(name);
/// ```
class PackageCache {
public:
    /// @brief Construct a package cache
    /// @param config Cache configuration
    explicit PackageCache(CacheConfig config);

    /// @brief Destructor
    ~PackageCache() = default;

    // Non-copyable, movable
    PackageCache(const PackageCache&) = delete;
    PackageCache& operator=(const PackageCache&) = delete;
    PackageCache(PackageCache&&) noexcept = default;
    PackageCache& operator=(PackageCache&&) noexcept = default;

    // =========================================================================
    // Cache Operations
    // =========================================================================

    /// @brief Initialize the cache directory structure
    /// @return void on success, error on failure
    [[nodiscard]] core::Result<void, PackageError> initialize() noexcept;

    /// @brief Check if the cache is initialized and valid
    [[nodiscard]] bool is_valid() const noexcept;

    // =========================================================================
    // Package Operations
    // =========================================================================

    /// @brief Add a package to the cache from a source
    /// @param source Package source (directory or archive)
    /// @param manifest Package manifest
    /// @return Cached package info on success
    [[nodiscard]] core::Result<CachedPackage, PackageError>
    add_from_source(const PackageSource& source, const PackageManifest& manifest) noexcept;

    /// @brief Add a package to the cache from a directory
    /// @param source_dir Directory containing the package
    /// @param manifest Package manifest
    /// @return Cached package info on success
    [[nodiscard]] core::Result<CachedPackage, PackageError>
    add_from_directory(const std::filesystem::path& source_dir,
                       const PackageManifest& manifest) noexcept;

    /// @brief Get a cached package
    /// @param name Package name
    /// @param version Package version
    /// @return Cached package info, or nullopt if not cached
    [[nodiscard]] std::optional<CachedPackage> get(std::string_view name,
                                                   const Version& version) const noexcept;

    /// @brief Check if a package is in the cache
    [[nodiscard]] bool has(std::string_view name, const Version& version) const noexcept;

    /// @brief Remove a package from the cache
    /// @param name Package name
    /// @param version Package version
    /// @return true if removed, false if not found
    [[nodiscard]] core::Result<bool, PackageError> remove(std::string_view name,
                                                          const Version& version) noexcept;

    /// @brief Remove all versions of a package from the cache
    /// @param name Package name
    /// @return Number of versions removed
    [[nodiscard]] core::Result<std::size_t, PackageError>
    remove_all(std::string_view name) noexcept;

    // =========================================================================
    // Query Operations
    // =========================================================================

    /// @brief List all cached versions of a package
    [[nodiscard]] std::vector<Version> list_versions(std::string_view name) const noexcept;

    /// @brief List all cached packages
    [[nodiscard]] std::vector<CachedPackage> list_all() const noexcept;

    /// @brief Get the total cache size in bytes
    [[nodiscard]] std::uint64_t total_size() const noexcept;

    /// @brief Get the package count in the cache
    [[nodiscard]] std::size_t package_count() const noexcept;

    // =========================================================================
    // Verification
    // =========================================================================

    /// @brief Verify the integrity of a cached package
    /// @param name Package name
    /// @param version Package version
    /// @return true if checksum matches
    [[nodiscard]] core::Result<bool, PackageError> verify(std::string_view name,
                                                          const Version& version) const noexcept;

    /// @brief Verify all packages in the cache
    /// @return List of packages that failed verification
    [[nodiscard]] std::vector<std::pair<std::string, Version>> verify_all() const noexcept;

    // =========================================================================
    // Maintenance
    // =========================================================================

    /// @brief Clean up the cache (remove invalid entries)
    /// @return Number of entries removed
    [[nodiscard]] core::Result<std::size_t, PackageError> cleanup() noexcept;

    /// @brief Clear the entire cache
    [[nodiscard]] core::Result<void, PackageError> clear() noexcept;

    // =========================================================================
    // Path Utilities
    // =========================================================================

    /// @brief Get the path to a cached package
    [[nodiscard]] std::filesystem::path package_path(std::string_view name,
                                                     const Version& version) const noexcept;

    /// @brief Get the cache root directory
    [[nodiscard]] const std::filesystem::path& root_dir() const noexcept {
        return config_.root_dir;
    }

private:
    /// @brief Compute checksum of a directory's contents
    [[nodiscard]] core::Result<Checksum, PackageError>
    compute_directory_checksum(const std::filesystem::path& dir) const noexcept;

    /// @brief Read stored checksum from .checksum file
    [[nodiscard]] std::optional<Checksum>
    read_stored_checksum(const std::filesystem::path& pkg_dir) const noexcept;

    /// @brief Write checksum to .checksum file
    [[nodiscard]] core::Result<void, PackageError>
    write_checksum(const std::filesystem::path& pkg_dir, const Checksum& checksum) const noexcept;

    CacheConfig config_;
    bool initialized_{false};
};

}  // namespace dotvm::pkg
