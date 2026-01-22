/// @file vm_main.cpp
/// @brief TOOL-005 VM CLI Executor entry point
///
/// Main entry point for the dotvm bytecode executor tool.

#include "dotvm/cli/vm_cli_app.hpp"

int main(int argc, char* argv[]) {
    dotvm::cli::VmCliApp app;

    // Parse command-line arguments
    dotvm::cli::VmExitCode parse_result = app.parse(argc, argv);

    if (parse_result != dotvm::cli::VmExitCode::Success) {
        return static_cast<int>(parse_result);
    }

    // If help or version was requested, parse() handled output
    if (app.help_requested()) {
        return static_cast<int>(dotvm::cli::VmExitCode::Success);
    }

    // Execute the selected command
    dotvm::cli::VmExitCode run_result = app.run();
    return static_cast<int>(run_result);
}
