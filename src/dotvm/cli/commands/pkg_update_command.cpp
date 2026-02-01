/// @file pkg_update_command.cpp
/// @brief PRD-007 Package update command implementation

#include "dotvm/cli/commands/pkg_update_command.hpp"

#include <sstream>

#include "dotvm/pkg/package_manager.hpp"

namespace dotvm::cli::commands {

PkgExitCode execute_update(const PkgUpdateOptions& opts, const PkgGlobalOptions& global,
                           Terminal& term) {
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

    if (opts.package_name.has_value()) {
        // Update specific package
        const std::string& name = opts.package_name.value();

        // Check if package is installed
        if (!pm.is_installed(name)) {
            term.error("error: ");
            term.print("Package not installed: ");
            term.print(name);
            term.newline();
            return PkgExitCode::PackageNotFound;
        }

        auto pkg_info = pm.get_installed(name);
        if (!pkg_info.has_value()) {
            term.error("error: ");
            term.print("Package not found: ");
            term.print(name);
            term.newline();
            return PkgExitCode::PackageNotFound;
        }

        if (opts.dry_run) {
            term.info("[DRY RUN] ");
            term.print("Would update: ");
            term.print(name);
            term.print(" v");
            term.print(pkg_info->version.to_string());
            term.newline();
            return PkgExitCode::Success;
        }

        if (global.verbose && !global.quiet) {
            term.info("Updating ");
            term.print(name);
            term.print("...");
            term.newline();
        }

        // Progress callback
        auto progress = [&](std::string_view pkg_name, std::size_t current, std::size_t total) {
            if (!global.quiet) {
                std::ostringstream oss;
                oss << "  [" << current << "/" << total << "] " << pkg_name;
                term.print(oss.str());
                term.newline();
            }
        };

        auto update_result = pm.update(name, progress);
        if (update_result.is_err()) {
            term.error("error: ");
            term.print("Failed to update ");
            term.print(name);
            term.newline();
            return PkgExitCode::IoError;
        }

        if (!global.quiet) {
            term.success("Updated ");
            term.print(name);
            term.newline();
        }
    } else {
        // Update all packages
        auto installed = pm.list_installed();

        if (installed.empty()) {
            if (!global.quiet) {
                term.info("No packages to update.");
                term.newline();
            }
            return PkgExitCode::Success;
        }

        if (opts.dry_run) {
            std::ostringstream oss;
            oss << "[DRY RUN] Would update " << installed.size() << " package(s):";
            term.info(oss.str());
            term.newline();
            for (const auto& pkg : installed) {
                term.print("  - ");
                term.print(pkg.name);
                term.print(" v");
                term.print(pkg.version.to_string());
                term.newline();
            }
            return PkgExitCode::Success;
        }

        if (global.verbose && !global.quiet) {
            std::ostringstream oss;
            oss << "Updating " << installed.size() << " packages...";
            term.info(oss.str());
            term.newline();
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

        auto update_result = pm.update_all(progress);
        if (update_result.is_err()) {
            term.error("error: ");
            term.print("Failed to update packages");
            term.newline();
            return PkgExitCode::IoError;
        }

        if (!global.quiet) {
            std::ostringstream oss;
            oss << "Updated " << installed.size() << " package(s)";
            term.success(oss.str());
            term.newline();
        }
    }

    return PkgExitCode::Success;
}

}  // namespace dotvm::cli::commands
