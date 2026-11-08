#include <CLI/CLI.hpp>

#include "pending/pending.hpp"

int main(int argc, char** argv)
{
    CLI::App app{"hyperagent: server-side tools for the projects beside this repo", "hyperagent"};
    app.set_version_flag("--version", HYPERAGENT_VERSION);
    app.require_subcommand(1);

    hyperagent::pending::add_command(app, "orch", "orchestrator CLI (bin/orch)");
    hyperagent::pending::add_command(app, "mcp", "MCP server (mcp_server.py)");
    hyperagent::pending::add_command(app, "gui", "GUI server (gui/server.py)");

    CLI11_PARSE(app, argc, argv);
    return 0;
}
