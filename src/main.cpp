#include <CLI/CLI.hpp>

#include "cli/ccbridge.hpp"
#include "cli/open.hpp"
#include "journal/command.hpp"
#include "monitor/command.hpp"
#include "pending/pending.hpp"
#include "watch/command.hpp"

int main(int argc, char** argv)
{
    CLI::App app{"hyperagent: server-side tools for the projects beside this repo", "hyperagent"};
    app.set_version_flag("--version", HYPERAGENT_VERSION);
    app.require_subcommand(1);

    hyperagent::journal::add_command(app);
    hyperagent::monitor::add_command(app);
    hyperagent::watch::add_command(app);
    hyperagent::cli::ccbridge::add_command(app);
    hyperagent::cli::open::add_command(app);

    hyperagent::pending::add_command(app, "orch", "orchestrator CLI (bin/orch)");
    hyperagent::pending::add_command(app, "mcp", "MCP server (mcp_server.py)");
    hyperagent::pending::add_command(app, "gui", "GUI server (gui/server.py)");

    CLI11_PARSE(app, argc, argv);
    return 0;
}
