#include "cli/ccbridge.hpp"

#include <iostream>

#include "orch/ccbridge.hpp"

namespace hyperagent::cli::ccbridge {

void add_command(CLI::App& app)
{
    auto* command = app.add_subcommand("ccbridge", "print the airc (ccbridge) binary that hyperagent runs");
    command->callback([]() { std::cout << hyperagent::orch::ccbridge::ccbridge_binary() << "\n"; });
}

}
