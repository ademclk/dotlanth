/// @file info_main.cpp
/// @brief TOOL-009 dotinfo entry point

#include "dotvm/cli/info_cli_app.hpp"

int main(int argc, char* argv[]) {
    dotvm::cli::InfoCliApp app;

    auto parse_result = app.parse(argc, argv);
    if (parse_result != dotvm::cli::InfoExitCode::Success) {
        return static_cast<int>(parse_result);
    }

    if (app.help_requested()) {
        return 0;
    }

    return static_cast<int>(app.run());
}
