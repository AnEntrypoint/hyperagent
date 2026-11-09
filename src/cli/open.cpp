#include "cli/open.hpp"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/paths.hpp"

namespace hyperagent::cli::open {

namespace {

void set_default_environment(const char* name, const char* value)
{
    ::setenv(name, value, 0);
}

[[noreturn]] void exec_claude(const std::vector<std::string>& args)
{
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("claude"));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    ::execvp("claude", argv.data());
    std::_Exit(127);
}

}

void add_command(CLI::App& app)
{
    auto* command = app.add_subcommand("open", "set the MCP timeouts and launch claude in the repo root");
    command->allow_extras();
    command->callback([command]() {
        set_default_environment("MCP_TIMEOUT", "60000");
        set_default_environment("MCP_TOOL_TIMEOUT", "14400000");
        std::filesystem::current_path(core::paths::repo_root());
        exec_claude(command->remaining());
    });
}

}
