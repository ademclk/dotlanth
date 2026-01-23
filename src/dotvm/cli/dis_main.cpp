/// @file dis_main.cpp
/// @brief TOOL-008 dotdis entry point

#include "dotvm/cli/dis_cli_app.hpp"

int main(int argc, char* argv[]) {
    dotvm::cli::DisCliApp app;

    auto parse_result = app.parse(argc, argv);
    if (parse_result != dotvm::cli::DisExitCode::Success) {
        return static_cast<int>(parse_result);
    }

    if (app.help_requested()) {
        return 0;
    }

    return static_cast<int>(app.run());
}
