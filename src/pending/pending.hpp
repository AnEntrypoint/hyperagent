#pragma once

#include <string>

#include <CLI/CLI.hpp>

namespace hyperagent::pending {

void add_command(CLI::App& app, const std::string& name, const std::string& description);

}
