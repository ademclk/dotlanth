#pragma once

/// @file package_manager.hpp
/// @brief PRD-007 Main package manager orchestrator
///
/// Coordinates package installation, uninstallation, updates, and listing.
/// Uses the cache, registry, lock file, and resolver to manage packages.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/pkg/dependency_resolver.hpp"
#include "dotvm/pkg/lock_file.hpp"
#include "dotvm/pkg/package.hpp"
#include "dotvm/pkg/package_cache.hpp"
#include "dotvm/pkg/package_error.hpp"
#include "dotvm/pkg/package_registry.hpp"

namespace dotvm::pkg {

/// @brief Package manager configuration
struct PackageManagerConfig {
    /// @brief Project root directory (where dotpkg.json lives)
    std::filesystem::path project_dir;

    /// @brief Cache configuration
    CacheConfig cache_config{CacheConfig::defaults()};

    /// @brief Resolver configuration
    ResolverConfig resolver_config{ResolverConfig::defaults()};

    /// @brief Whether to write lock file
    bool use_lock_file{true};

    /// @brief Default configuration for current directory
    [[nodiscard]] static PackageManagerConfig defaults();
};

/// @brief Progress callback for install/update operations
using ProgressCallback =
    std::function<void(std::string_view package, std::size_t current, std::size_t total)>;

/// @brief Package manager - main entry point for dotpkg operations
///
/// Orchestrates package installation, uninstallation, updates, and listing
/// by coordinating the cache, registry, lock file, and dependency resolver.
///
/// @example
/// ```cpp
/// PackageManager pm(PackageManagerConfig::defaults());
/// auto init_result = pm.initialize();
///
/// // Install a package
/// pm.install("mylib", VersionConstraint::parse("^1.0.0").value());
///
/// // List installed packages
/// for (const auto& pkg : pm.list_installed()) {
///     std::cout << pkg.name << " " << pkg.version.to_string() << "\n";
/// }
///
/// // Uninstall a package
/// pm.uninstall("mylib");
/// ```
class PackageManager {
public:
    /// @brief Construct a package manager
    explicit PackageManager(PackageManagerConfig config);

    /// @brief Destructor
    ~PackageManager() = default;

    // Non-copyable, movable
    PackageManager(const PackageManager&) = delete;
    PackageManager& operator=(const PackageManager&) = delete;
    PackageManager(PackageManager&&) noexcept = default;
    PackageManager& operator=(PackageManager&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Initialize the package manager
    /// @return void on success, error on failure
    [[nodiscard]] core::Result<void, PackageError> initialize() noexcept;

    /// @brief Check if the package manager is initialized
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

    /// @brief Load the project manifest (dotpkg.json)
    [[nodiscard]] core::Result<void, PackageError> load_manifest() noexcept;

    /// @brief Load the lock file
    [[nodiscard]] core::Result<void, PackageError> load_lock_file() noexcept;

    // =========================================================================
    // Package Installation
    // =========================================================================

    /// @brief Install a package from a local source
    /// @param source Package source (directory or archive path)
    /// @param progress Optional progress callback
    /// @return void on success, error on failure
    [[nodiscard]] core::Result<void, PackageError>
    install_from_source(const PackageSource& source, ProgressCallback progress = nullptr) noexcept;

    /// @brief Install all dependencies from the manifest
    /// @param progress Optional progress callback
    /// @return void on success, error on failure
    [[nodiscard]] core::Result<void, PackageError>
    install_all(ProgressCallback progress = nullptr) noexcept;

    // =========================================================================
    // Package Removal
    // =========================================================================

    /// @brief Uninstall a package
    /// @param name Package name
    /// @param force Force removal even if other packages depend on it
    /// @return void on success, error on failure
    [[nodiscard]] core::Result<void, PackageError> uninstall(std::string_view name,
                                                             bool force = false) noexcept;

    // =========================================================================
    // Package Updates
    // =========================================================================

    /// @brief Update a specific package
    /// @param name Package name
    /// @param progress Optional progress callback
    /// @return void on success, error on failure
    [[nodiscard]] core::Result<void, PackageError>
    update(std::string_view name, ProgressCallback progress = nullptr) noexcept;

    /// @brief Update all packages
    /// @param progress Optional progress callback
    /// @return void on success, error on failure
    [[nodiscard]] core::Result<void, PackageError>
    update_all(ProgressCallback progress = nullptr) noexcept;

    // =========================================================================
    // Query Operations
    // =========================================================================

    /// @brief Get the project manifest
    [[nodiscard]] const PackageManifest* manifest() const noexcept {
        return manifest_ ? &*manifest_ : nullptr;
    }

    /// @brief Get the lock file
    [[nodiscard]] const LockFile& lock_file() const noexcept { return lock_file_; }

    /// @brief List all installed packages
    [[nodiscard]] std::vector<InstalledPackage> list_installed() const noexcept;

    /// @brief Check if a package is installed
    [[nodiscard]] bool is_installed(std::string_view name) const noexcept;

    /// @brief Get an installed package
    [[nodiscard]] std::optional<InstalledPackage>
    get_installed(std::string_view name) const noexcept;

    /// @brief List outdated packages (installed version < available)
    [[nodiscard]] std::vector<std::pair<std::string, Version>> list_outdated() const noexcept;

    // =========================================================================
    // Dependency Tree
    // =========================================================================

    /// @brief Node in the dependency tree
    struct DependencyNode {
        std::string name;
        Version version;
        std::vector<DependencyNode> dependencies;
    };

    /// @brief Get the dependency tree for installed packages
    [[nodiscard]] std::vector<DependencyNode> dependency_tree() const noexcept;

    // =========================================================================
    // Persistence
    // =========================================================================

    /// @brief Save the lock file
    [[nodiscard]] core::Result<void, PackageError> save_lock_file() const noexcept;

private:
    /// @brief Install a resolved package from cache
    [[nodiscard]] core::Result<void, PackageError>
    install_resolved_package(const ResolvedPackage& pkg) noexcept;

    PackageManagerConfig config_;
    PackageCache cache_;
    PackageRegistry registry_;
    LockFile lock_file_;
    DependencyResolver resolver_;
    std::optional<PackageManifest> manifest_;
    bool initialized_{false};
};

}  // namespace dotvm::pkg
