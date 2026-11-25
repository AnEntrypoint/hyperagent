#include "cli/open.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/install.hpp"
#include "core/paths.hpp"
#include "core/platform.hpp"
#include "orch/config.hpp"

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
        if (const auto manifest = core::install::read()) {
            for (const auto& [name, value] : core::install::environment(*manifest)) {
                set_default_environment(name.c_str(), value.c_str());
            }
        }
        set_default_environment("MCP_TIMEOUT", orch::config::startup_timeout().c_str());
        set_default_environment("MCP_TOOL_TIMEOUT", orch::config::tool_timeout().c_str());
        std::filesystem::current_path(core::paths::repo_root());
        exec_claude(command->remaining());
    });
}

}
