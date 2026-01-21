/// @file main.cpp
/// @brief DSL-003 Main entry point for the dotdsl CLI compiler

#include <cstdlib>

#include "dotvm/cli/cli_app.hpp"

int main(int argc, char* argv[]) {
    dotvm::cli::CliApp app;

    dotvm::cli::ExitCode parse_result = app.parse(argc, argv);
    if (parse_result != dotvm::cli::ExitCode::Success) {
        return static_cast<int>(parse_result);
    }

    // If help/version was requested, don't run - parse already handled output
    if (app.help_requested()) {
        return static_cast<int>(dotvm::cli::ExitCode::Success);
    }

    dotvm::cli::ExitCode run_result = app.run();
    return static_cast<int>(run_result);
}
