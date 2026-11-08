#pragma once

#include <string>

#include <CLI/CLI.hpp>

#include "monitor/monitor.hpp"

namespace hyperagent::monitor {

struct CommandOptions {
    double idle = 20.0;
    double poll = 1.0;
    std::string agents;
    long last_chars = 4000;
    std::string prompt{idle_prompt_template};
    std::string gone_prompt{gone_prompt_template};
    bool text = false;
    bool once = false;
};

void add_monitor_options(CLI::App* command, CommandOptions& options);
Options to_monitor_options(const CommandOptions& options);
void add_command(CLI::App& app);

}
