/// @file pkg_uninstall_command.cpp
/// @brief PRD-007 Package uninstall command implementation

#include "dotvm/cli/commands/pkg_uninstall_command.hpp"

#include "dotvm/pkg/package_manager.hpp"

namespace dotvm::cli::commands {

PkgExitCode execute_uninstall(const PkgUninstallOptions& opts, const PkgGlobalOptions& global,
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

    // Check if package is installed
    if (!pm.is_installed(opts.package_name)) {
        term.error("error: ");
        term.print("Package not installed: ");
        term.print(opts.package_name);
        term.newline();
        return PkgExitCode::PackageNotFound;
    }

    // Get package info for display
    auto pkg_info = pm.get_installed(opts.package_name);
    if (!pkg_info.has_value()) {
        term.error("error: ");
        term.print("Package not found: ");
        term.print(opts.package_name);
        term.newline();
        return PkgExitCode::PackageNotFound;
    }

    if (global.verbose && !global.quiet) {
        term.info("Uninstalling ");
        term.print(pkg_info->name);
        term.print(" v");
        term.print(pkg_info->version.to_string());
        term.print("...");
        term.newline();
    }

    // Uninstall the package
    auto uninstall_result = pm.uninstall(opts.package_name, opts.force);
    if (uninstall_result.is_err()) {
        switch (uninstall_result.error()) {
            case pkg::PackageError::PackageNotFound:
                term.error("error: ");
                term.print("Package not installed: ");
                term.print(opts.package_name);
                term.newline();
                return PkgExitCode::PackageNotFound;

            case pkg::PackageError::UnresolvableDependency:
                term.error("error: ");
                term.print("Cannot uninstall: other packages depend on ");
                term.print(opts.package_name);
                term.newline();
                term.print("  Use --force to uninstall anyway.");
                term.newline();
                return PkgExitCode::DependencyError;

            default:
                term.error("error: ");
                term.print("Failed to uninstall package");
                term.newline();
                return PkgExitCode::IoError;
        }
    }

    if (!global.quiet) {
        term.success("Uninstalled ");
        term.print(opts.package_name);
        term.newline();
    }

    return PkgExitCode::Success;
}

}  // namespace dotvm::cli::commands
