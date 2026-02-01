/// @file package_manager.cpp
/// @brief PRD-007 Package manager implementation

#include "dotvm/pkg/package_manager.hpp"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <unordered_set>

namespace dotvm::pkg {

PackageManagerConfig PackageManagerConfig::defaults() {
    PackageManagerConfig config;
    config.project_dir = std::filesystem::current_path();
    config.cache_config = CacheConfig::defaults();
    config.resolver_config = ResolverConfig::defaults();
    config.use_lock_file = true;
    return config;
}

PackageManager::PackageManager(PackageManagerConfig config)
    : config_(std::move(config)),
      cache_(config_.cache_config),
      registry_(config_.project_dir / "dotpkg_registry.json"),
      resolver_(config_.resolver_config) {}

core::Result<void, PackageError> PackageManager::initialize() noexcept {
    // Initialize the cache
    auto cache_result = cache_.initialize();
    if (cache_result.is_err()) {
        return cache_result.error();
    }

    // Load registry if it exists
    auto reg_result = registry_.load();
    if (reg_result.is_err()) {
        return reg_result.error();
    }

    // Load lock file if it exists
    auto lock_path = config_.project_dir / "dotpkg.lock";
    if (std::filesystem::exists(lock_path)) {
        auto lock_result = LockFile::load(lock_path);
        if (lock_result.is_ok()) {
            lock_file_ = std::move(lock_result).value();
        }
    }

    // Load manifest if it exists
    auto manifest_path = config_.project_dir / "dotpkg.json";
    if (std::filesystem::exists(manifest_path)) {
        auto manifest_result = PackageManifest::from_file(manifest_path);
        if (manifest_result.is_ok()) {
            manifest_ = std::move(manifest_result).value();
        }
    }

    initialized_ = true;
    return core::Ok;
}

core::Result<void, PackageError> PackageManager::load_manifest() noexcept {
    auto manifest_path = config_.project_dir / "dotpkg.json";
    auto result = PackageManifest::from_file(manifest_path);
    if (result.is_err()) {
        return result.error();
    }
    manifest_ = std::move(result).value();
    return core::Ok;
}

core::Result<void, PackageError> PackageManager::load_lock_file() noexcept {
    auto lock_path = config_.project_dir / "dotpkg.lock";
    auto result = LockFile::load(lock_path);
    if (result.is_err()) {
        return result.error();
    }
    lock_file_ = std::move(result).value();
    return core::Ok;
}

core::Result<void, PackageError> PackageManager::install_from_source(
    const PackageSource& source, ProgressCallback progress) noexcept {
    if (!initialized_) {
        auto init = initialize();
        if (init.is_err()) {
            return init.error();
        }
    }

    // Load manifest from source
    auto manifest_path = source.path / "dotpkg.json";
    auto manifest_result = PackageManifest::from_file(manifest_path);
    if (manifest_result.is_err()) {
        return manifest_result.error();
    }

    const auto& manifest = manifest_result.value();

    if (progress) {
        progress(manifest.name, 1, 1);
    }

    // Add to cache
    auto cache_result = cache_.add_from_source(source, manifest);
    if (cache_result.is_err()) {
        return cache_result.error();
    }

    const auto& cached = cache_result.value();

    // Register as installed
    RegistryEntry entry;
    entry.version = manifest.version;
    entry.checksum = cached.checksum;
    entry.installed_at = cached.cached_at;
    entry.install_path = cached.path;

    for (const auto& [dep_name, _] : manifest.dependencies) {
        entry.dependencies.push_back(dep_name);
    }

    registry_.add(manifest.name, std::move(entry));

    // Update lock file
    LockedPackage locked;
    locked.name = manifest.name;
    locked.version = manifest.version;
    locked.checksum = cached.checksum;
    for (const auto& [dep_name, _] : manifest.dependencies) {
        locked.dependencies.push_back(dep_name);
    }
    lock_file_.add_package(std::move(locked));

    // Save registry and lock file
    auto reg_save = registry_.save();
    if (reg_save.is_err()) {
        return reg_save.error();
    }

    if (config_.use_lock_file) {
        auto lock_save = save_lock_file();
        if (lock_save.is_err()) {
            return lock_save.error();
        }
    }

    return core::Ok;
}

core::Result<void, PackageError> PackageManager::install_all(ProgressCallback progress) noexcept {
    if (!manifest_) {
        auto load = load_manifest();
        if (load.is_err()) {
            return load.error();
        }
    }

    // For local package manager, each dependency must be a local path
    // The manifest specifies paths, not remote packages
    std::size_t total = manifest_->dependencies.size();
    std::size_t current = 0;

    for (const auto& [name, _] : manifest_->dependencies) {
        ++current;
        if (progress) {
            progress(name, current, total);
        }

        // Skip if already installed with compatible version
        if (is_installed(name)) {
            continue;
        }

        // In a local-only package manager, we'd need to know where to find packages
        // For now, this is a placeholder - real implementation would search local paths
    }

    return core::Ok;
}

core::Result<void, PackageError> PackageManager::uninstall(std::string_view name,
                                                           bool force) noexcept {
    if (!initialized_) {
        auto init = initialize();
        if (init.is_err()) {
            return init.error();
        }
    }

    // Check if package is installed
    if (!registry_.has(name)) {
        return PackageError::PackageNotFound;
    }

    // Check for dependents unless forcing
    if (!force) {
        auto dependents = registry_.dependents_of(name);
        if (!dependents.empty()) {
            return PackageError::UnresolvableDependency;
        }
    }

    // Get the installed entry
    const auto* entry = registry_.get(name);
    if (!entry) {
        return PackageError::PackageNotFound;
    }

    // Remove from cache
    auto cache_remove = cache_.remove(name, entry->version);
    if (cache_remove.is_err()) {
        return cache_remove.error();
    }

    // Remove from registry
    registry_.remove(name);

    // Remove from lock file
    lock_file_.remove_package(name);

    // Save changes
    auto reg_save = registry_.save();
    if (reg_save.is_err()) {
        return reg_save.error();
    }

    if (config_.use_lock_file) {
        auto lock_save = save_lock_file();
        if (lock_save.is_err()) {
            return lock_save.error();
        }
    }

    return core::Ok;
}

core::Result<void, PackageError> PackageManager::update(std::string_view name,
                                                         ProgressCallback progress) noexcept {
    // For local package manager, update is just reinstall from source
    // This is a simplified implementation
    (void)name;
    (void)progress;
    return core::Ok;
}

core::Result<void, PackageError> PackageManager::update_all(ProgressCallback progress) noexcept {
    (void)progress;
    return core::Ok;
}

std::vector<InstalledPackage> PackageManager::list_installed() const noexcept {
    std::vector<InstalledPackage> result;

    for (const auto& [name, entry] : registry_.packages()) {
        InstalledPackage pkg;
        pkg.name = name;
        pkg.version = entry.version;
        pkg.checksum = entry.checksum;
        pkg.install_path = entry.install_path;
        pkg.installed_at = entry.installed_at;

        for (const auto& dep : entry.dependencies) {
            const auto* dep_entry = registry_.get(dep);
            if (dep_entry) {
                pkg.resolved_dependencies[dep] = dep_entry->version;
            }
        }

        result.push_back(std::move(pkg));
    }

    return result;
}

bool PackageManager::is_installed(std::string_view name) const noexcept {
    return registry_.has(name);
}

std::optional<InstalledPackage> PackageManager::get_installed(std::string_view name) const noexcept {
    const auto* entry = registry_.get(name);
    if (!entry) {
        return std::nullopt;
    }

    InstalledPackage pkg;
    pkg.name = std::string(name);
    pkg.version = entry->version;
    pkg.checksum = entry->checksum;
    pkg.install_path = entry->install_path;
    pkg.installed_at = entry->installed_at;

    for (const auto& dep : entry->dependencies) {
        const auto* dep_entry = registry_.get(dep);
        if (dep_entry) {
            pkg.resolved_dependencies[dep] = dep_entry->version;
        }
    }

    return pkg;
}

std::vector<std::pair<std::string, Version>> PackageManager::list_outdated() const noexcept {
    // For local package manager, we can't easily determine outdated packages
    // without a remote registry. Return empty for now.
    return {};
}

std::vector<PackageManager::DependencyNode> PackageManager::dependency_tree() const noexcept {
    std::vector<DependencyNode> roots;

    // Find root packages (not depended upon by others)
    std::unordered_set<std::string> non_roots;
    for (const auto& [name, entry] : registry_.packages()) {
        for (const auto& dep : entry.dependencies) {
            non_roots.insert(dep);
        }
    }

    // Build tree for each root
    std::function<DependencyNode(const std::string&)> build_node;
    build_node = [&](const std::string& name) -> DependencyNode {
        DependencyNode node;
        node.name = name;

        const auto* entry = registry_.get(name);
        if (entry) {
            node.version = entry->version;
            for (const auto& dep : entry->dependencies) {
                node.dependencies.push_back(build_node(dep));
            }
        }

        return node;
    };

    for (const auto& [name, _] : registry_.packages()) {
        if (!non_roots.contains(name)) {
            roots.push_back(build_node(name));
        }
    }

    return roots;
}

core::Result<void, PackageError> PackageManager::save_lock_file() const noexcept {
    auto lock_path = config_.project_dir / "dotpkg.lock";
    return lock_file_.save(lock_path);
}

core::Result<void, PackageError> PackageManager::install_resolved_package(
    const ResolvedPackage& pkg) noexcept {
    // This would install a resolved package from cache
    // Implementation depends on how packages are stored
    (void)pkg;
    return core::Ok;
}

}  // namespace dotvm::pkg
