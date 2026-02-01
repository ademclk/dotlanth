/// @file pkg_main.cpp
/// @brief PRD-007 Package Manager CLI entry point
///
/// Main entry point for the dotpkg package manager tool.

#include "dotvm/cli/pkg_cli_app.hpp"

int main(int argc, char* argv[]) {
    dotvm::cli::PkgCliApp app;

    // Parse command-line arguments
    dotvm::cli::PkgExitCode parse_result = app.parse(argc, argv);

    if (parse_result != dotvm::cli::PkgExitCode::Success) {
        return static_cast<int>(parse_result);
    }

    // If help or version was requested, parse() handled output
    if (app.help_requested()) {
        return static_cast<int>(dotvm::cli::PkgExitCode::Success);
    }

    // Execute the selected command
    dotvm::cli::PkgExitCode run_result = app.run();
    return static_cast<int>(run_result);
}
