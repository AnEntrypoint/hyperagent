#include <CLI/CLI.hpp>

#include "cli/ccbridge.hpp"
#include "cli/open.hpp"
#include "gui/command.hpp"
#include "journal/command.hpp"
#include "monitor/command.hpp"
#include "orch/command.hpp"
#include "mcp/command.hpp"
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

    hyperagent::orch::add_command(app);
    hyperagent::mcp::add_command(app);
    hyperagent::gui::add_command(app);

    CLI11_PARSE(app, argc, argv);
    return 0;
}
