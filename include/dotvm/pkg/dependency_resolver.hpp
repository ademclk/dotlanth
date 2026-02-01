#pragma once

/// @file dependency_resolver.hpp
/// @brief PRD-007 Dependency resolution for package installation
///
/// Resolves package dependencies using the DependencyGraph for cycle detection
/// and topological ordering.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/pkg/package.hpp"
#include "dotvm/pkg/package_error.hpp"
#include "dotvm/pkg/version.hpp"

namespace dotvm::pkg {

/// @brief Resolved package with selected version
struct ResolvedPackage {
    std::string name;
    Version version;
    std::vector<std::string> dependencies;
};

/// @brief Resolution result containing installation order
struct ResolutionResult {
    /// @brief Packages in topological order (dependencies first)
    std::vector<ResolvedPackage> packages;

    /// @brief Root packages that were explicitly requested
    std::vector<std::string> roots;
};

/// @brief Configuration for the dependency resolver
struct ResolverConfig {
    /// @brief Maximum depth of dependency tree
    std::size_t max_depth{100};

    /// @brief Maximum number of packages to resolve
    std::size_t max_packages{10000};

    /// @brief Default configuration
    [[nodiscard]] static constexpr ResolverConfig defaults() noexcept { return ResolverConfig{}; }
};

/// @brief Callback to fetch package manifests by name and version constraint
using PackageProvider = std::function<core::Result<PackageManifest, PackageError>(
    std::string_view name, const VersionConstraint& constraint)>;

/// @brief Dependency resolver using DependencyGraph
///
/// Resolves package dependencies to produce an installation order that
/// satisfies all constraints. Uses cycle detection to prevent circular
/// dependencies.
///
/// @example
/// ```cpp
/// DependencyResolver resolver(config);
///
/// // Set up a provider that can fetch package manifests
/// resolver.set_provider([&](std::string_view name, const VersionConstraint& constraint) {
///     return lookup_package(name, constraint);
/// });
///
/// // Resolve a package and its dependencies
/// auto result = resolver.resolve("mypackage", VersionConstraint::parse("^1.0.0").value());
/// if (result.is_ok()) {
///     for (const auto& pkg : result.value().packages) {
///         install(pkg);
///     }
/// }
/// ```
class DependencyResolver {
public:
    /// @brief Construct a resolver with configuration
    explicit DependencyResolver(ResolverConfig config = ResolverConfig::defaults()) noexcept;

    /// @brief Set the package provider callback
    void set_provider(PackageProvider provider) noexcept;

    // =========================================================================
    // Resolution Operations
    // =========================================================================

    /// @brief Resolve a single package and its dependencies
    /// @param name Package name
    /// @param constraint Version constraint
    /// @return Resolution result with packages in install order
    [[nodiscard]] core::Result<ResolutionResult, PackageError>
    resolve(std::string_view name, const VersionConstraint& constraint) noexcept;

    /// @brief Resolve multiple packages and their dependencies
    /// @param packages Map of package names to version constraints
    /// @return Resolution result with packages in install order
    [[nodiscard]] core::Result<ResolutionResult, PackageError>
    resolve_all(const std::unordered_map<std::string, VersionConstraint>& packages) noexcept;

    // =========================================================================
    // Query Operations
    // =========================================================================

    /// @brief Clear the resolver state
    void clear() noexcept;

private:
    /// @brief Internal resolution state
    struct ResolveState {
        std::unordered_map<std::string, ResolvedPackage> resolved;
        std::vector<std::string> order;
        std::size_t depth{0};
    };

    /// @brief Recursively resolve a package
    [[nodiscard]] core::Result<void, PackageError>
    resolve_recursive(std::string_view name, const VersionConstraint& constraint,
                      ResolveState& state) noexcept;

    ResolverConfig config_;
    PackageProvider provider_;
};

}  // namespace dotvm::pkg
