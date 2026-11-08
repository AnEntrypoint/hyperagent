#include "mcp/command.hpp"

#include "mcp/server.hpp"

namespace hyperagent::mcp {

void add_command(CLI::App& app)
{
    auto* command = app.add_subcommand("mcp", "serve the hyperagent MCP tools over stdio");
    command->callback([]() {
        Server server;
        server.run();
    });
}

}
