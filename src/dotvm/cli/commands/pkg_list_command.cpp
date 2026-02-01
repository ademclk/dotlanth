/// @file pkg_list_command.cpp
/// @brief PRD-007 Package list command implementation

#include "dotvm/cli/commands/pkg_list_command.hpp"

#include "dotvm/pkg/package_manager.hpp"

namespace dotvm::cli::commands {

namespace {

/// @brief Print dependency tree recursively
void print_tree(Terminal& term, const pkg::PackageManager::DependencyNode& node,
                const std::string& prefix, bool is_last) {
    // Print current node
    term.print(prefix);
    term.print(is_last ? "└── " : "├── ");
    term.print(node.name);
    term.print(" v");
    term.print(node.version.to_string());
    term.newline();

    // Print children
    for (std::size_t i = 0; i < node.dependencies.size(); ++i) {
        bool child_is_last = (i == node.dependencies.size() - 1);
        std::string child_prefix = prefix + (is_last ? "    " : "│   ");
        print_tree(term, node.dependencies[i], child_prefix, child_is_last);
    }
}

}  // namespace

PkgExitCode execute_list(const PkgListOptions& opts, const PkgGlobalOptions& global,
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

    if (opts.tree) {
        // Show dependency tree
        auto tree = pm.dependency_tree();

        if (tree.empty()) {
            if (!global.quiet) {
                term.info("No packages installed.");
                term.newline();
            }
            return PkgExitCode::Success;
        }

        if (!global.quiet) {
            term.info("Dependency tree:");
            term.newline();
        }

        for (std::size_t i = 0; i < tree.size(); ++i) {
            bool is_last = (i == tree.size() - 1);
            print_tree(term, tree[i], "", is_last);
        }

        return PkgExitCode::Success;
    }

    if (opts.outdated) {
        // Show outdated packages
        auto outdated = pm.list_outdated();

        if (outdated.empty()) {
            if (!global.quiet) {
                term.info("All packages are up to date.");
                term.newline();
            }
            return PkgExitCode::Success;
        }

        if (!global.quiet) {
            term.info("Outdated packages:");
            term.newline();
        }

        for (const auto& [name, version] : outdated) {
            term.print("  ");
            term.print(name);
            term.print(" v");
            term.print(version.to_string());
            term.newline();
        }

        return PkgExitCode::Success;
    }

    // Default: list all installed packages
    auto installed = pm.list_installed();

    if (installed.empty()) {
        if (!global.quiet) {
            term.info("No packages installed.");
            term.newline();
        }
        return PkgExitCode::Success;
    }

    if (!global.quiet) {
        term.info("Installed packages:");
        term.newline();
    }

    for (const auto& pkg : installed) {
        term.print("  ");
        term.print(pkg.name);
        term.print(" v");
        term.print(pkg.version.to_string());
        if (global.verbose) {
            term.print(" (");
            term.print(pkg.install_path.string());
            term.print(")");
        }
        term.newline();
    }

    return PkgExitCode::Success;
}

}  // namespace dotvm::cli::commands
