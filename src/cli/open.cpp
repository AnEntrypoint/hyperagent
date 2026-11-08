#include "cli/open.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "core/paths.hpp"

namespace hyperagent::cli::open {

namespace {

void set_default_environment(const char* name, const char* value)
{
#ifdef _WIN32
    if (std::getenv(name) == nullptr) {
        _putenv_s(name, value);
    }
#else
    ::setenv(name, value, 0);
#endif
}

}

void add_command(CLI::App& app)
{
    auto* command = app.add_subcommand("open", "set the MCP timeouts and start the hyperagent MCP server (bin/open)");
    command->callback([]() {
        set_default_environment("MCP_TIMEOUT", "60000");
        set_default_environment("MCP_TOOL_TIMEOUT", "1800000");
        std::filesystem::current_path(core::paths::repo_root());
        std::cerr << "hyperagent open: the mcp subcommand is not yet ported to C++; nothing was launched\n";
        std::exit(3);
    });
}

}
