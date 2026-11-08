#include "monitor/command.hpp"

#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

#include "core/json.hpp"

namespace hyperagent::monitor {

namespace {

class ReplaySource : public SessionSource {
public:
    struct Tick {
        double now;
        Value sessions;
        std::map<std::string, std::string> panes;
    };

    explicit ReplaySource(std::vector<Tick> ticks)
        : ticks_(std::move(ticks))
    {
    }

    void select(std::size_t index)
    {
        current_ = index;
    }

    double now() const
    {
        return ticks_.at(current_).now;
    }

    std::size_t size() const
    {
        return ticks_.size();
    }

    Value sessions() override
    {
        return ticks_.at(current_).sessions;
    }

    std::string capture(const std::string& name) override
    {
        const auto& panes = ticks_.at(current_).panes;
        const auto found = panes.find(name);
        return found == panes.end() ? std::string() : found->second;
    }

private:
    std::vector<Tick> ticks_;
    std::size_t current_ = 0;
};

std::vector<ReplaySource::Tick> load_ticks(const std::string& path)
{
    const auto document = core::json::read_file(path);
    if (!document || !document->contains("ticks") || !(*document)["ticks"].is_array()) {
        throw CLI::ValidationError("--replay", "expected a JSON object with a \"ticks\" array");
    }
    std::vector<ReplaySource::Tick> ticks;
    for (const auto& tick : (*document)["ticks"]) {
        ReplaySource::Tick loaded{tick.value("now", 0.0), tick.value("sessions", core::json::Value::object()), {}};
        if (tick.contains("panes")) {
            for (const auto& [name, pane] : tick["panes"].items()) {
                loaded.panes.emplace(name, pane.get<std::string>());
            }
        }
        ticks.push_back(std::move(loaded));
    }
    return ticks;
}

std::set<std::string> split_agents(const std::string& value)
{
    std::set<std::string> agents;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        const auto first = item.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        agents.insert(item.substr(first, item.find_last_not_of(" \t") - first + 1));
    }
    return agents;
}

void emit(const Value& event, bool text_mode)
{
    std::cout << render(event, text_mode) << std::endl;
}

}

void add_monitor_options(CLI::App* command, CommandOptions& options)
{
    command->add_option("--idle", options.idle, "seconds without a pane change before a peer counts as idle")
        ->capture_default_str();
    command->add_option("--poll", options.poll)->capture_default_str();
    command->add_option("--agents", options.agents, "comma separated peer names");
    command->add_flag("--once", options.once, "one pass over peers that airc reports not busy");
    command->add_flag("--text", options.text, "human line instead of JSON");
    command->add_option("--prompt", options.prompt, "instruct prompt template; {last}, {agent}, {session}")
        ->capture_default_str();
    command->add_option("--gone-prompt", options.gone_prompt)->capture_default_str();
    command->add_option("--last-chars", options.last_chars)->capture_default_str();
}

Options to_monitor_options(const CommandOptions& options)
{
    Options monitor_options;
    monitor_options.idle = options.idle;
    monitor_options.poll = options.poll;
    monitor_options.agents = split_agents(options.agents);
    monitor_options.last_chars = options.last_chars;
    monitor_options.prompt = options.prompt;
    monitor_options.gone_prompt = options.gone_prompt;
    return monitor_options;
}

void add_command(CLI::App& app)
{
    auto options = std::make_shared<CommandOptions>();
    auto replay = std::make_shared<std::string>();
    auto* command = app.add_subcommand("monitor", "replay recorded session snapshots through the peer monitor");
    command->add_option("--replay", *replay, "JSON file with a \"ticks\" array of {now, sessions, panes}")->required();
    add_monitor_options(command, *options);
    command->callback([options, replay]() {
        auto source = ReplaySource(load_ticks(*replay));
        Monitor monitor(source, to_monitor_options(*options), [&source]() { return source.now(); });
        if (options->once) {
            source.select(0);
            for (const auto& event : monitor.once()) {
                emit(event, options->text);
            }
            return;
        }
        for (std::size_t tick = 0; tick < source.size(); ++tick) {
            source.select(tick);
            for (const auto& event : monitor.poll()) {
                emit(event, options->text);
            }
        }
    });
}

}
