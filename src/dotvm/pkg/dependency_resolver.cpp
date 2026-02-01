/// @file dependency_resolver.cpp
/// @brief PRD-007 Dependency resolver implementation

#include "dotvm/pkg/dependency_resolver.hpp"

#include <algorithm>
#include <unordered_set>

namespace dotvm::pkg {

DependencyResolver::DependencyResolver(ResolverConfig config) noexcept : config_(config) {}

void DependencyResolver::set_provider(PackageProvider provider) noexcept {
    provider_ = std::move(provider);
}

core::Result<ResolutionResult, PackageError>
DependencyResolver::resolve(std::string_view name, const VersionConstraint& constraint) noexcept {
    std::unordered_map<std::string, VersionConstraint> packages;
    packages.emplace(std::string(name), constraint);
    return resolve_all(packages);
}

core::Result<ResolutionResult, PackageError> DependencyResolver::resolve_all(
    const std::unordered_map<std::string, VersionConstraint>& packages) noexcept {
    if (!provider_) {
        return PackageError::PackageNotFound;
    }

    ResolveState state;
    std::vector<std::string> roots;

    for (const auto& [name, constraint] : packages) {
        roots.push_back(name);
        auto result = resolve_recursive(name, constraint, state);
        if (result.is_err()) {
            return result.error();
        }
    }

    // Build result in topological order
    ResolutionResult result;
    result.roots = std::move(roots);

    for (const auto& name : state.order) {
        auto it = state.resolved.find(name);
        if (it != state.resolved.end()) {
            result.packages.push_back(std::move(it->second));
        }
    }

    return result;
}

core::Result<void, PackageError>
DependencyResolver::resolve_recursive(std::string_view name, const VersionConstraint& constraint,
                                      ResolveState& state) noexcept {
    // Check depth limit
    if (state.depth >= config_.max_depth) {
        return PackageError::MaxDepthExceeded;
    }

    // Check package count limit
    if (state.resolved.size() >= config_.max_packages) {
        return PackageError::MaxDepthExceeded;
    }

    std::string name_str(name);

    // Check if already resolved
    auto existing = state.resolved.find(name_str);
    if (existing != state.resolved.end()) {
        // Verify constraint compatibility
        if (!constraint.satisfies(existing->second.version)) {
            return PackageError::ConflictingVersions;
        }
        return core::Ok;
    }

    // Fetch the package
    auto manifest_result = provider_(name, constraint);
    if (manifest_result.is_err()) {
        return manifest_result.error();
    }

    const auto& manifest = manifest_result.value();

    // Add to resolved
    ResolvedPackage resolved;
    resolved.name = manifest.name;
    resolved.version = manifest.version;

    // Resolve dependencies first (depth-first)
    state.depth++;
    for (const auto& [dep_name, dep_constraint] : manifest.dependencies) {
        resolved.dependencies.push_back(dep_name);
        auto dep_result = resolve_recursive(dep_name, dep_constraint, state);
        if (dep_result.is_err()) {
            return dep_result.error();
        }
    }
    state.depth--;

    // Add this package after its dependencies
    state.resolved.emplace(name_str, std::move(resolved));
    state.order.push_back(name_str);

    return core::Ok;
}

void DependencyResolver::clear() noexcept {
    // No internal state to clear in current implementation
}

}  // namespace dotvm::pkg
