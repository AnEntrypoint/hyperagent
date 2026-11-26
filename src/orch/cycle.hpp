#pragma once

#include <string>
#include <vector>

#include <CLI/CLI.hpp>

namespace hyperagent::orch::cycle {

std::vector<std::string> board();
std::vector<std::string> queue(const std::string& peer);
std::vector<std::string> sent(const std::string& peer, const std::string& phrase);
std::vector<std::string> specs();
void add_command(CLI::App& app);

}
