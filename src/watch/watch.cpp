#include "watch/watch.hpp"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

#include "core/clock.hpp"
#include "core/http.hpp"
#include "core/json.hpp"

namespace hyperagent::watch {

namespace {

std::string environment_or(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::string(value) : std::string(fallback);
}

}

CcbridgeSource::CcbridgeSource(std::string host, std::string port)
    : host_(std::move(host))
    , port_(std::move(port))
{
}

monitor::Value CcbridgeSource::sessions()
{
    const auto response = core::http::get(host_, port_, "/sessions", std::chrono::seconds(10));
    if (response.status >= 400) {
        throw std::runtime_error("GET /sessions answered " + std::to_string(response.status));
    }
    const auto parsed = core::json::parse(response.body);
    if (!parsed) {
        throw std::runtime_error("GET /sessions returned invalid JSON");
    }
    return *parsed;
}

std::string CcbridgeSource::capture(const std::string& name)
{
    try {
        const auto listed = sessions();
        const auto* info = core::json::member(&listed, name);
        const auto* tail = core::json::member(info, "pane_tail");
        if (tail == nullptr || !tail->is_array()) {
            return "";
        }
        std::string pane;
        for (std::size_t index = 0; index < tail->size(); ++index) {
            if (index > 0) {
                pane += "\n";
            }
            pane += core::json::str_or_empty(&(*tail)[index]);
        }
        return pane;
    } catch (const std::exception&) {
        return "";
    }
}

int run(const monitor::CommandOptions& options)
{
    CcbridgeSource source(environment_or("ORCH_HOST", "127.0.0.1"), environment_or("ORCH_PORT", "8787"));
    monitor::Monitor watcher(source, monitor::to_monitor_options(options), []() {
        return core::clock::now_epoch();
    });

    if (options.once) {
        for (const auto& event : watcher.once()) {
            monitor::print_event(event, options.text);
        }
        return 0;
    }

    const auto interval = std::chrono::duration<double>(watcher.poll_interval());
    while (true) {
        for (const auto& event : watcher.poll()) {
            monitor::print_event(event, options.text);
        }
        std::this_thread::sleep_for(interval);
    }
}

}
