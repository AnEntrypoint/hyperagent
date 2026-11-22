#include "cli/setup.hpp"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "core/paths.hpp"
#include "core/process.hpp"

namespace hyperagent::cli::setup {

namespace {

std::filesystem::path find_on_path(const std::string& name)
{
    const char* raw = std::getenv("PATH");
    std::stringstream directories(raw == nullptr ? "" : raw);
    std::string directory;
    while (std::getline(directories, directory, ':')) {
        const auto candidate = std::filesystem::path(directory) / name;
        if (!directory.empty() && std::filesystem::is_regular_file(candidate) && ::access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
    }
    return {};
}

void register_with_claude(const std::filesystem::path& claude)
{
    if (core::process::run_capture({claude.string(), "mcp", "get", "hyperagent"}).exit_code == 0) {
        std::cout << "mcp server hyperagent already registered with claude\n";
        return;
    }
    const auto binary = core::paths::executable_path().string();
    const auto added = core::process::run_capture({claude.string(), "mcp", "add", "--scope", "user", "hyperagent", "--", binary, "mcp"});
    if (added.exit_code != 0) {
        std::cout << "claude mcp add failed: " << added.standard_error << added.standard_output;
        std::exit(1);
    }
    std::cout << "registered mcp server hyperagent with claude: " << binary << " mcp\n";
}

void run()
{
    const auto state = core::paths::orch_home();
    std::filesystem::create_directories(state);
    std::cout << "state directory " << state.string() << "\n";
    const auto claude = find_on_path("claude");
    if (claude.empty()) {
        std::cout << "claude CLI not found on PATH: skipped mcp registration\n";
        return;
    }
    register_with_claude(claude);
}

}

void add_command(CLI::App& app)
{
    auto* command = app.add_subcommand("setup", "create the state directory and register the mcp server with claude");
    command->callback([]() { run(); });
}

}
