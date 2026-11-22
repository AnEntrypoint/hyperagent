#include "cli/open.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/paths.hpp"
#include "core/platform.hpp"

namespace hyperagent::cli::open {

namespace {

void set_default_environment(const char* name, const char* value)
{
    core::platform::set_default_environment(name, value);
}

[[noreturn]] void exec_claude(const std::vector<std::string>& args)
{
    std::vector<std::string> argv{"claude"};
    argv.insert(argv.end(), args.begin(), args.end());
    core::platform::exec_program(argv);
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
