/// @file dependency_resolver_test.cpp
/// @brief Unit tests for PRD-007 dependency resolver

#include <gtest/gtest.h>

#include "dotvm/pkg/dependency_resolver.hpp"

namespace dotvm::pkg {
namespace {

class DependencyResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up a simple package provider
        resolver_.set_provider([this](std::string_view name, const VersionConstraint& constraint) {
            return provide_package(name, constraint);
        });
    }

    // Simple in-memory package provider
    core::Result<PackageManifest, PackageError> provide_package(
        std::string_view name, const VersionConstraint& constraint) {
        auto it = available_packages_.find(std::string(name));
        if (it == available_packages_.end()) {
            return PackageError::PackageNotFound;
        }

        // Find best matching version
        for (const auto& manifest : it->second) {
            if (constraint.satisfies(manifest.version)) {
                return manifest;
            }
        }

        return PackageError::UnresolvableDependency;
    }

    void add_package(PackageManifest manifest) {
        available_packages_[manifest.name].push_back(std::move(manifest));
    }

    DependencyResolver resolver_;
    std::unordered_map<std::string, std::vector<PackageManifest>> available_packages_;
};

// ============================================================================
// Basic Resolution Tests
// ============================================================================

TEST_F(DependencyResolverTest, ResolveSimplePackage) {
    PackageManifest pkg;
    pkg.name = "simple";
    pkg.version = Version{1, 0, 0, {}};
    add_package(pkg);

    auto result = resolver_.resolve("simple", VersionConstraint::parse("^1.0.0").value());

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().packages.size(), 1);
    EXPECT_EQ(result.value().packages[0].name, "simple");
}

TEST_F(DependencyResolverTest, ResolveWithDependency) {
    PackageManifest dep;
    dep.name = "dependency";
    dep.version = Version{1, 0, 0, {}};
    add_package(dep);

    PackageManifest pkg;
    pkg.name = "main";
    pkg.version = Version{1, 0, 0, {}};
    pkg.dependencies["dependency"] = VersionConstraint::parse("^1.0.0").value();
    add_package(pkg);

    auto result = resolver_.resolve("main", VersionConstraint::parse("^1.0.0").value());

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().packages.size(), 2);

    // Dependencies should come first
    EXPECT_EQ(result.value().packages[0].name, "dependency");
    EXPECT_EQ(result.value().packages[1].name, "main");
}

TEST_F(DependencyResolverTest, ResolveTransitiveDependencies) {
    PackageManifest a;
    a.name = "a";
    a.version = Version{1, 0, 0, {}};
    add_package(a);

    PackageManifest b;
    b.name = "b";
    b.version = Version{1, 0, 0, {}};
    b.dependencies["a"] = VersionConstraint::parse("^1.0.0").value();
    add_package(b);

    PackageManifest c;
    c.name = "c";
    c.version = Version{1, 0, 0, {}};
    c.dependencies["b"] = VersionConstraint::parse("^1.0.0").value();
    add_package(c);

    auto result = resolver_.resolve("c", VersionConstraint::parse("^1.0.0").value());

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().packages.size(), 3);

    // Order should be: a, b, c (dependencies first)
    EXPECT_EQ(result.value().packages[0].name, "a");
    EXPECT_EQ(result.value().packages[1].name, "b");
    EXPECT_EQ(result.value().packages[2].name, "c");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(DependencyResolverTest, ResolveNotFoundFails) {
    auto result = resolver_.resolve("nonexistent", VersionConstraint::parse("^1.0.0").value());

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::PackageNotFound);
}

TEST_F(DependencyResolverTest, ResolveVersionMismatchFails) {
    PackageManifest pkg;
    pkg.name = "versioned";
    pkg.version = Version{2, 0, 0, {}};
    add_package(pkg);

    // Request version 1.x but only 2.x available
    auto result = resolver_.resolve("versioned", VersionConstraint::parse("^1.0.0").value());

    ASSERT_TRUE(result.is_err());
}

TEST_F(DependencyResolverTest, ResolveMaxDepthExceeded) {
    ResolverConfig config;
    config.max_depth = 2;
    DependencyResolver limited_resolver(config);

    // Create a chain: a -> b -> c -> d (depth 3)
    PackageManifest a, b, c, d;
    d.name = "d";
    d.version = Version{1, 0, 0, {}};

    c.name = "c";
    c.version = Version{1, 0, 0, {}};
    c.dependencies["d"] = VersionConstraint::parse("^1.0.0").value();

    b.name = "b";
    b.version = Version{1, 0, 0, {}};
    b.dependencies["c"] = VersionConstraint::parse("^1.0.0").value();

    a.name = "a";
    a.version = Version{1, 0, 0, {}};
    a.dependencies["b"] = VersionConstraint::parse("^1.0.0").value();

    std::unordered_map<std::string, std::vector<PackageManifest>> packages;
    packages["a"].push_back(a);
    packages["b"].push_back(b);
    packages["c"].push_back(c);
    packages["d"].push_back(d);

    limited_resolver.set_provider([&](std::string_view name, const VersionConstraint& constraint) {
        auto it = packages.find(std::string(name));
        if (it == packages.end()) {
            return core::Result<PackageManifest, PackageError>(PackageError::PackageNotFound);
        }
        for (const auto& m : it->second) {
            if (constraint.satisfies(m.version)) {
                return core::Result<PackageManifest, PackageError>(m);
            }
        }
        return core::Result<PackageManifest, PackageError>(PackageError::UnresolvableDependency);
    });

    auto result = limited_resolver.resolve("a", VersionConstraint::parse("^1.0.0").value());

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), PackageError::MaxDepthExceeded);
}

// ============================================================================
// Multiple Package Resolution Tests
// ============================================================================

TEST_F(DependencyResolverTest, ResolveMultiplePackages) {
    PackageManifest a;
    a.name = "a";
    a.version = Version{1, 0, 0, {}};
    add_package(a);

    PackageManifest b;
    b.name = "b";
    b.version = Version{1, 0, 0, {}};
    add_package(b);

    std::unordered_map<std::string, VersionConstraint> packages;
    packages["a"] = VersionConstraint::parse("^1.0.0").value();
    packages["b"] = VersionConstraint::parse("^1.0.0").value();

    auto result = resolver_.resolve_all(packages);

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value().packages.size(), 2);
}

TEST_F(DependencyResolverTest, SharedDependencyResolvedOnce) {
    PackageManifest shared;
    shared.name = "shared";
    shared.version = Version{1, 0, 0, {}};
    add_package(shared);

    PackageManifest a;
    a.name = "a";
    a.version = Version{1, 0, 0, {}};
    a.dependencies["shared"] = VersionConstraint::parse("^1.0.0").value();
    add_package(a);

    PackageManifest b;
    b.name = "b";
    b.version = Version{1, 0, 0, {}};
    b.dependencies["shared"] = VersionConstraint::parse("^1.0.0").value();
    add_package(b);

    std::unordered_map<std::string, VersionConstraint> packages;
    packages["a"] = VersionConstraint::parse("^1.0.0").value();
    packages["b"] = VersionConstraint::parse("^1.0.0").value();

    auto result = resolver_.resolve_all(packages);

    ASSERT_TRUE(result.is_ok());

    // Should have shared, a, b (shared only once)
    EXPECT_EQ(result.value().packages.size(), 3);

    // Count occurrences of "shared"
    int shared_count = 0;
    for (const auto& pkg : result.value().packages) {
        if (pkg.name == "shared") {
            ++shared_count;
        }
    }
    EXPECT_EQ(shared_count, 1);
}

}  // namespace
}  // namespace dotvm::pkg
