#include "pending/pending.hpp"

#include <cstdlib>
#include <iostream>

namespace hyperagent::pending {

void add_command(CLI::App& app, const std::string& name, const std::string& description)
{
    auto* command = app.add_subcommand(name, description);
    command->allow_extras();
    command->callback([name]() {
        std::cerr << "hyperagent " << name << ": not yet ported to C++; the Python implementation still serves this role\n";
        std::exit(3);
    });
}

}
