/// @file pkg_install_command.cpp
/// @brief PRD-007 Package install command implementation

#include "dotvm/cli/commands/pkg_install_command.hpp"

#include <sstream>

#include "dotvm/pkg/package.hpp"
#include "dotvm/pkg/package_manager.hpp"

namespace dotvm::cli::commands {

PkgExitCode execute_install(const PkgInstallOptions& opts, const PkgGlobalOptions& global,
                            Terminal& term) {
    // Validate source path exists
    std::filesystem::path source_path(opts.package_path);
    std::error_code ec;

    if (!std::filesystem::exists(source_path, ec)) {
        term.error("error: ");
        term.print("Package source not found: ");
        term.print(opts.package_path);
        term.newline();
        return PkgExitCode::PackageNotFound;
    }

    // Create package source
    pkg::PackageSource source;
    source.path = source_path;
    source.is_archive = std::filesystem::is_regular_file(source_path, ec);

    // Validate the source
    if (!source.is_valid()) {
        term.error("error: ");
        term.print("Invalid package source: ");
        term.print(opts.package_path);
        term.newline();
        term.print("  Package directory must contain dotpkg.json manifest.");
        term.newline();
        return PkgExitCode::PackageNotFound;
    }

    // Read manifest for display
    auto manifest_path = source.is_archive ? source_path : source_path / "dotpkg.json";
    auto manifest_result = pkg::PackageManifest::from_file(manifest_path);
    if (manifest_result.is_err()) {
        term.error("error: ");
        term.print("Failed to read package manifest: ");
        term.print(opts.package_path);
        term.newline();
        return PkgExitCode::PackageNotFound;
    }

    const auto& manifest = manifest_result.value();

    if (opts.dry_run) {
        term.info("[DRY RUN] ");
        term.print("Would install: ");
        term.print(manifest.name);
        term.print(" v");
        term.print(manifest.version.to_string());
        term.newline();
        if (!manifest.dependencies.empty()) {
            term.print("  Dependencies:");
            term.newline();
            for (const auto& [dep_name, constraint] : manifest.dependencies) {
                term.print("    - ");
                term.print(dep_name);
                term.print(" ");
                term.print(constraint.to_string());
                term.newline();
            }
        }
        return PkgExitCode::Success;
    }

    if (global.verbose && !global.quiet) {
        term.info("Installing ");
        term.print(manifest.name);
        term.print(" v");
        term.print(manifest.version.to_string());
        term.print("...");
        term.newline();
    }

    // Configure package manager
    pkg::PackageManagerConfig config;
    config.project_dir = global.project_dir;
    config.cache_config.root_dir = global.config_dir;
    config.use_lock_file = true;

    pkg::PackageManager pm(config);

    // Initialize the package manager
    auto init_result = pm.initialize();
    if (init_result.is_err()) {
        term.error("error: ");
        term.print("Failed to initialize package manager");
        term.newline();
        return PkgExitCode::CacheError;
    }

    // Progress callback
    auto progress = [&](std::string_view name, std::size_t current, std::size_t total) {
        if (!global.quiet) {
            std::ostringstream oss;
            oss << "  [" << current << "/" << total << "] " << name;
            term.print(oss.str());
            term.newline();
        }
    };

    // Install the package
    auto install_result = pm.install_from_source(source, progress);
    if (install_result.is_err()) {
        switch (install_result.error()) {
            case pkg::PackageError::PackageNotFound:
            case pkg::PackageError::ManifestNotFound:
                term.error("error: ");
                term.print("Package manifest not found");
                term.newline();
                return PkgExitCode::PackageNotFound;

            case pkg::PackageError::UnresolvableDependency:
            case pkg::PackageError::DependencyCycle:
                term.error("error: ");
                term.print("Failed to resolve dependencies");
                term.newline();
                return PkgExitCode::DependencyError;

            case pkg::PackageError::CacheCorrupted:
            case pkg::PackageError::ChecksumMismatch:
                term.error("error: ");
                term.print("Package cache error");
                term.newline();
                return PkgExitCode::CacheError;

            default:
                term.error("error: ");
                term.print("Installation failed");
                term.newline();
                return PkgExitCode::IoError;
        }
    }

    if (!global.quiet) {
        term.success("Installed ");
        term.print(manifest.name);
        term.print(" v");
        term.print(manifest.version.to_string());
        term.newline();
    }

    return PkgExitCode::Success;
}

}  // namespace dotvm::cli::commands
