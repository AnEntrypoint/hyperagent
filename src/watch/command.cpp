#include "watch/command.hpp"

#include <cstdlib>
#include <memory>

#include "core/update.hpp"
#include "monitor/command.hpp"
#include "watch/watch.hpp"

namespace hyperagent::watch {

void add_command(CLI::App& app)
{
    auto options = std::make_shared<monitor::CommandOptions>();
    auto* command = app.add_subcommand("watch", "emit one line when a peer's output stops animating");
    monitor::add_monitor_options(command, *options);
    command->callback([options]() {
        core::update::start_background();
        std::exit(run(*options));
    });
}

}
