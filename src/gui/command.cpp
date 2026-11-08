#include "gui/command.hpp"

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "core/paths.hpp"
#include "gui/constants.hpp"
#include "gui/feed.hpp"
#include "gui/reload.hpp"
#include "gui/server.hpp"

namespace hyperagent::gui {

namespace {

struct Options {
    std::string host = constants::default_host;
    std::uint16_t port = constants::default_port;
};

int run(const Options& options)
{
    std::signal(SIGPIPE, SIG_IGN);
    const std::filesystem::path public_dir = core::paths::repo_root() / "gui" / "public";
    JournalFeed feed(core::paths::journal_root(), core::paths::orch_home() / "server.log");
    ReloadWatcher reloads(public_dir);
    Server server(public_dir, feed, reloads);
    try {
        server.listen(options.host, options.port);
    } catch (const std::exception& error) {
        std::cerr << "gui: " << error.what() << "\n";
        return 1;
    }
    std::cout << "orch gui on http://" << options.host << ":" << options.port << "/" << std::endl;
    std::thread(&JournalFeed::run, &feed).detach();
    std::thread(&ReloadWatcher::run, &reloads).detach();
    server.accept_forever();
    return 0;
}

}

void add_command(CLI::App& app)
{
    auto options = std::make_shared<Options>();
    auto* command = app.add_subcommand("gui", "realtime view of the session journals (gui/server.py)");
    command->add_option("--host", options->host, "address to bind")->capture_default_str();
    command->add_option("--port", options->port, "port to bind")->capture_default_str();
    command->callback([options]() {
        if (const int status = run(*options); status != 0) {
            std::exit(status);
        }
    });
}

}
