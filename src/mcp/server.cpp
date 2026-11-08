#include "mcp/server.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

#include "core/clock.hpp"
#include "core/paths.hpp"
#include "core/text.hpp"
#include "mcp/args.hpp"
#include "mcp/events.hpp"
#include "mcp/outputs.hpp"
#include "mcp/schema.hpp"
#include "mcp/tools.hpp"
#include "monitor/monitor.hpp"
#include "orch/ccbridge.hpp"

namespace hyperagent::mcp {

namespace {

constexpr char protocol_version[] = "2025-06-18";
constexpr char subscription_id_key[] = "io.modelcontextprotocol/subscriptionId";
constexpr char outputs_uri[] = "hyperagent://outputs";
constexpr char events_uri[] = "hyperagent://events";
constexpr char outputs_prefix[] = "hyperagent://outputs/";
constexpr char server_version[] = "0.1.0";
constexpr long long invalid_params = -32602;
constexpr long long method_not_found = -32601;
constexpr std::size_t log_queue_limit = 64;
constexpr std::size_t listed_event_limit = 100;
constexpr int default_rank = 4;
constexpr double default_progress_interval = 5.0;
constexpr long long drain_seconds = 2;

const std::vector<std::string> log_levels{"debug", "info", "notice", "warning", "error", "critical", "alert",
                                          "emergency"};

int level_rank(const std::string& level)
{
    for (std::size_t index = 0; index < log_levels.size(); ++index) {
        if (log_levels[index] == level) {
            return static_cast<int>(index);
        }
    }
    return default_rank;
}

bool known_level(const std::string& level)
{
    return std::find(log_levels.begin(), log_levels.end(), level) != log_levels.end();
}

std::string env_text(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

double env_double(const char* name, double fallback)
{
    const std::string text = env_text(name);
    if (text.empty()) {
        return fallback;
    }
    const Value value(text);
    return to_float(&value);
}

long long env_int(const char* name, long long fallback)
{
    const std::string text = env_text(name);
    if (text.empty()) {
        return fallback;
    }
    const Value value(text);
    return to_int(&value);
}

Value object_of(std::initializer_list<std::pair<std::string, Value>> entries)
{
    Value out = Value::object();
    for (const auto& [key, item] : entries) {
        out[key] = item;
    }
    return out;
}

Value response(const Value& id, Value result)
{
    return object_of({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
}

Value failure_response(const Value& id, long long code, const std::string& message)
{
    return object_of({{"jsonrpc", "2.0"}, {"id", id},
                      {"error", object_of({{"code", code}, {"message", message}})}});
}

Value notification(const std::string& method, Value params)
{
    return object_of({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
}

Value meta_for(const Value& listen_id)
{
    return object_of({{subscription_id_key, listen_id}});
}

Value capabilities()
{
    return object_of({{"tools", object_of({{"listChanged", false}})},
                      {"resources", object_of({{"subscribe", true}, {"listChanged", true}})},
                      {"logging", Value::object()}});
}

Value server_info()
{
    return object_of({{"name", "hyperagent"}, {"version", server_version}});
}

bool contains_text(const Value& list, const std::string& needle)
{
    for (const auto& item : text_list(&list)) {
        if (item == needle) {
            return true;
        }
    }
    return false;
}

bool listen_wants_resource(const Value& spec, const std::string& uri)
{
    const Value* wanted = core::json::member(&spec, "resourceSubscriptions");
    const Value list = core::json::truthy(wanted) ? *wanted : Value::array();
    return contains_text(list, uri) || contains_text(list, outputs_uri) || contains_text(list, events_uri);
}

std::optional<std::string> event_output_uri(const Value& event)
{
    const Value* session = core::json::member(&event, "session");
    const Value* cwd = core::json::member(&event, "cwd");
    if (!core::json::truthy(session) || !core::json::truthy(cwd)) {
        return std::nullopt;
    }
    return std::string(outputs_prefix) + core::paths::slug(core::json::py_str(cwd)) + "/"
        + core::json::py_str(session);
}

monitor::Options monitor_options()
{
    monitor::Options options;
    options.agents.clear();
    const std::string raw = env_text("ORCH_WATCH_AGENTS");
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t comma = std::min(raw.find(',', start), raw.size());
        const std::string item(core::text::strip(std::string_view(raw).substr(start, comma - start)));
        if (!item.empty()) {
            options.agents.insert(item);
        }
        start = comma + 1;
    }
    options.idle = env_double("ORCH_WATCH_IDLE", 20.0);
    options.poll = env_double("ORCH_WATCH_POLL", 1.0);
    options.last_chars = env_int("ORCH_WATCH_LAST_CHARS", 4000);
    return options;
}

class PeerSource : public monitor::SessionSource {
public:
    Value sessions() override
    {
        try {
            return orch::ccbridge::fetch_sessions();
        } catch (const std::exception&) {
            return Value::object();
        }
    }

    std::string capture(const std::string& name) override
    {
        return orch::ccbridge::capture(name);
    }
};

struct Ticker {
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
};

}

[[noreturn]] void Server::run()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const std::string_view trimmed = core::text::strip(line);
        if (trimmed.empty()) {
            continue;
        }
        const auto message = core::json::parse(trimmed);
        if (!message) {
            continue;
        }
        dispatch(*message);
    }
    drain();
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        closing_ = true;
    }
    cancel_listens();
    std::fflush(stdout);
    std::_Exit(0);
}

void Server::cancel_listens()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto& [listen_id, spec] : listens_) {
        send(notification("notifications/cancelled", object_of({{"requestId", listen_id}})), true);
    }
}

bool Server::runs_inline(const Value& message) const
{
    if (!message.is_object()) {
        return true;
    }
    const Value* method = core::json::member(&message, "method");
    if (method == nullptr || !method->is_string() || method->get<std::string>() != "tools/call") {
        return true;
    }
    const Value* params = core::json::member(&message, "params");
    const Value* name = params != nullptr && params->is_object() ? core::json::member(params, "name") : nullptr;
    if (name == nullptr || !name->is_string()) {
        return true;
    }
    return !tools::known(name->get<std::string>()) || tools::is_synchronous(name->get<std::string>());
}

void Server::run_guarded(const Value& message)
{
    try {
        handle(message);
    } catch (const Silent&) {
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "hyperagent mcp: %s\n", failure.what());
    }
}

void Server::dispatch(const Value& message)
{
    if (runs_inline(message)) {
        run_guarded(message);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(flight_mutex_);
        ++in_flight_;
    }
    std::thread([this, message]() {
        run_guarded(message);
        std::lock_guard<std::mutex> lock(flight_mutex_);
        --in_flight_;
        flight_done_.notify_all();
    }).detach();
}

void Server::drain()
{
    std::unique_lock<std::mutex> lock(flight_mutex_);
    flight_done_.wait_for(lock, std::chrono::seconds(drain_seconds), [this]() { return in_flight_ == 0; });
}

void Server::send(const Value& message, bool final)
{
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (closing_ && !final) {
        return;
    }
    const std::string line = dumps_rpc(message) + "\n";
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

void Server::log_message(const std::string& level, const Value& data)
{
    const Value entry = object_of({{"level", level}, {"logger", "hyperagent"}, {"data", data}});
    if (!log_level_) {
        log_queue_.push_back(entry);
        if (log_queue_.size() > log_queue_limit) {
            log_queue_.erase(log_queue_.begin(), log_queue_.end() - static_cast<std::ptrdiff_t>(log_queue_limit));
        }
        return;
    }
    if (level_rank(level) < level_rank(*log_level_)) {
        return;
    }
    send(notification("notifications/message", entry));
}

void Server::emit_event(const Value& incoming)
{
    Value event = incoming;
    const std::optional<std::string> uri = event_output_uri(event);
    if (uri) {
        event["output"] = *uri;
    }
    event_book().append(event);
    const bool new_output = uri && known_outputs_.insert(*uri).second;
    const Value* kind = core::json::member(&event, "event");
    const bool gone = kind != nullptr && kind->is_string() && kind->get<std::string>() == "gone";
    log_message(gone ? "warning" : "notice", event);
    for (const auto& [listen_id, spec] : listens_) {
        if (uri && listen_wants_resource(spec, *uri)) {
            send(notification("notifications/resources/updated",
                              object_of({{"uri", *uri}, {"_meta", meta_for(listen_id)}})));
        }
        if (new_output && core::json::truthy(core::json::member(&spec, "resourcesListChanged"))) {
            send(notification("notifications/resources/list_changed", object_of({{"_meta", meta_for(listen_id)}})));
        }
    }
    const std::vector<std::string> subscribed_now = subscriptions_;
    for (const auto& subscribed : subscribed_now) {
        if ((uri && subscribed == *uri) || subscribed == outputs_uri) {
            send(notification("notifications/resources/updated", object_of({{"uri", subscribed}})));
        } else if (subscribed == events_uri && uri) {
            send(notification("notifications/resources/updated", object_of({{"uri", *uri}})));
        }
    }
}

void Server::ensure_monitor()
{
    if (monitor_running_) {
        return;
    }
    monitor_running_ = true;
    std::thread([this]() { monitor_loop(); }).detach();
}

void Server::monitor_loop()
{
    PeerSource source;
    std::unique_ptr<monitor::Monitor> watcher;
    try {
        watcher = std::make_unique<monitor::Monitor>(source, monitor_options(), []() {
            return core::clock::now_epoch();
        });
    } catch (const std::exception&) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        monitor_running_ = false;
        return;
    }
    try {
        while (true) {
            const std::vector<Value> events = watcher->poll();
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                for (const auto& event : events) {
                    emit_event(event);
                }
            }
            std::this_thread::sleep_for(std::chrono::duration<double>(watcher->poll_interval()));
        }
    } catch (const std::exception& failure) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        log_message("error", object_of({{"error", failure_text(failure)}}));
        monitor_running_ = false;
    }
}

void Server::announce_progress(const Value& token, const std::string& name, long long step)
{
    send(notification("notifications/progress",
                      object_of({{"progressToken", token}, {"progress", step}, {"message", name + " 0s"}})));
}

void Server::handle(const Value& message)
{
    if (!message.is_object()) {
        throw Silent("message is not an object");
    }
    const Value* method_value = core::json::member(&message, "method");
    const std::string method = method_value != nullptr && method_value->is_string()
        ? method_value->get<std::string>() : std::string();
    const Value* id_value = core::json::member(&message, "id");
    const Value request_id = id_value != nullptr ? *id_value : Value();
    const Value* params_value = core::json::member(&message, "params");
    const Value params = core::json::truthy(params_value) ? *params_value : Value::object();
    const auto need_object = [&params]() {
        if (!params.is_object()) {
            throw Silent("params is not an object");
        }
    };

    if (method == "initialize") {
        need_object();
        const Value* requested = core::json::member(&params, "protocolVersion");
        Value version = core::json::truthy(requested) ? *requested : Value(protocol_version);
        send(response(request_id, object_of({{"protocolVersion", version}, {"capabilities", capabilities()},
                                             {"serverInfo", server_info()},
                                             {"instructions", schema::instructions}})));
        return;
    }
    if (method == "server/discover") {
        send(response(request_id, object_of({{"resultType", "complete"},
                                             {"supportedVersions", Value::array({"2025-11-25", protocol_version})},
                                             {"capabilities", capabilities()},
                                             {"serverInfo", server_info()},
                                             {"instructions", schema::instructions}})));
        return;
    }
    if (method == "notifications/initialized" || method == "initialized" || method == "exit"
        || method == "notifications/progress") {
        return;
    }
    if (method == "notifications/cancelled") {
        need_object();
        const Value* cancelled_value = core::json::member(&params, "requestId");
        if (cancelled_value == nullptr || !(cancelled_value->is_string() || cancelled_value->is_number_integer()
                                            || cancelled_value->is_number_unsigned())) {
            return;
        }
        std::lock_guard<std::mutex> lock(state_mutex_);
        const auto found = std::find_if(listens_.begin(), listens_.end(), [cancelled_value](const auto& listen) {
            return listen.first == *cancelled_value;
        });
        if (found != listens_.end()) {
            listens_.erase(found);
            send(response(*cancelled_value, object_of({{"resultType", "complete"}})));
        }
        return;
    }
    if (method == "logging/setLevel") {
        need_object();
        const std::string level = text_arg(params, "level");
        if (!known_level(level)) {
            send(failure_response(request_id, invalid_params, "unknown log level: " + level));
            return;
        }
        std::lock_guard<std::mutex> lock(state_mutex_);
        log_level_ = level;
        const std::vector<Value> queued = log_queue_;
        log_queue_.clear();
        for (const auto& entry : queued) {
            const std::string entry_level = core::json::py_str(core::json::member(&entry, "level"));
            if (level_rank(entry_level) >= level_rank(level)) {
                send(notification("notifications/message", entry));
            }
        }
        ensure_monitor();
        send(response(request_id, Value::object()));
        return;
    }
    if (method == "resources/subscribe" || method == "resources/unsubscribe") {
        need_object();
        const std::string uri = text_arg(params, "uri");
        if (!resources::valid_uri(uri)) {
            send(failure_response(request_id, invalid_params, "unknown resource " + uri));
            return;
        }
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (method == "resources/subscribe") {
            if (std::find(subscriptions_.begin(), subscriptions_.end(), uri) == subscriptions_.end()) {
                subscriptions_.push_back(uri);
            }
        } else {
            subscriptions_.erase(std::remove(subscriptions_.begin(), subscriptions_.end(), uri), subscriptions_.end());
        }
        ensure_monitor();
        send(response(request_id, Value::object()));
        if (method == "resources/subscribe") {
            send(notification("notifications/resources/updated", object_of({{"uri", uri}})));
        }
        return;
    }
    if (method == "subscriptions/listen") {
        need_object();
        const Value* raw_spec = core::json::member(&params, "notifications");
        const Value spec = core::json::truthy(raw_spec) ? *raw_spec : Value::object();
        if (!spec.is_object()) {
            send(failure_response(request_id, invalid_params, "notifications must be an object"));
            return;
        }
        std::lock_guard<std::mutex> lock(state_mutex_);
        const auto existing = std::find_if(listens_.begin(), listens_.end(), [&request_id](const auto& listen) {
            return listen.first == request_id;
        });
        if (existing != listens_.end()) {
            existing->second = spec;
        } else {
            listens_.emplace_back(request_id, spec);
        }
        send(notification("notifications/subscriptions/acknowledged",
                          object_of({{"_meta", meta_for(request_id)}, {"notifications", spec}})));
        const Value* subscriptions = core::json::member(&spec, "resourceSubscriptions");
        const Value wanted = core::json::truthy(subscriptions) ? *subscriptions : Value::array();
        for (const auto& subscribed : text_list(&wanted)) {
            send(notification("notifications/resources/updated",
                              object_of({{"uri", subscribed}, {"_meta", meta_for(request_id)}})));
        }
        if (core::json::truthy(core::json::member(&spec, "resourcesListChanged"))) {
            send(notification("notifications/resources/list_changed", object_of({{"_meta", meta_for(request_id)}})));
        }
        ensure_monitor();
        return;
    }
    if (method == "ping") {
        send(response(request_id, Value::object()));
        return;
    }
    if (method == "tools/list") {
        send(response(request_id, object_of({{"tools", tools::descriptors()}})));
        return;
    }
    if (method == "resources/list") {
        send(response(request_id, object_of({{"resources", resources::list()}})));
        return;
    }
    if (method == "resources/templates/list") {
        send(response(request_id, object_of({{"resourceTemplates", resources::templates()}})));
        return;
    }
    if (method == "resources/read") {
        need_object();
        const std::string uri = text_arg(params, "uri");
        try {
            const resources::Content content = resources::read(uri);
            send(response(request_id, object_of({{"contents", Value::array({object_of({{"uri", uri},
                {"mimeType", content.mime}, {"text", content.text}})})}})));
        } catch (const ToolFailure& failure) {
            send(failure_response(request_id, invalid_params, failure.what()));
        }
        return;
    }
    if (method == "tools/call") {
        need_object();
        handle_tools_call(request_id, params);
        return;
    }
    if (!request_id.is_null()) {
        send(failure_response(request_id, method_not_found, "method not found: " + core::json::py_str(method_value)));
    }
}

void Server::handle_tools_call(const Value& id, const Value& params)
{
    const Value* name_value = core::json::member(&params, "name");
    const Value name = name_value != nullptr ? *name_value : Value();
    const Value* arguments_value = core::json::member(&params, "arguments");
    const Value arguments = core::json::truthy(arguments_value) ? *arguments_value : Value::object();
    const bool named = name.is_string() && tools::known(name.get<std::string>());
    if (!named) {
        send(response(id, object_of({{"content", Value::array({object_of({{"type", "text"},
            {"text", "unknown tool " + python_repr(&name)}})})}, {"isError", true}})));
        return;
    }
    const std::string tool_name = name.get<std::string>();

    const Value* meta = core::json::member(&params, "_meta");
    const Value meta_object = core::json::truthy(meta) ? *meta : Value::object();
    if (!meta_object.is_object()) {
        throw Silent("_meta is not an object");
    }
    const Value* token_value = core::json::member(&meta_object, "progressToken");
    const bool has_token = token_value != nullptr && !token_value->is_null();
    const Value token = has_token ? *token_value : Value();

    const auto ticker_state = std::make_shared<Ticker>();
    std::thread ticker;
    if (has_token) {
        announce_progress(token, tool_name, 0);
        ticker = std::thread([this, token, tool_name, ticker_state]() {
            double interval = default_progress_interval;
            try {
                interval = env_double("ORCH_PROGRESS_INTERVAL", default_progress_interval);
            } catch (const std::exception&) {
                return;
            }
            const auto started = std::chrono::steady_clock::now();
            long long step = 1;
            std::unique_lock<std::mutex> lock(ticker_state->mutex);
            while (!ticker_state->wake.wait_for(lock, std::chrono::duration<double>(interval),
                                                [&ticker_state]() { return ticker_state->stop; })) {
                lock.unlock();
                const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                send(notification("notifications/progress",
                                  object_of({{"progressToken", token}, {"progress", step},
                                             {"message", tool_name + " "
                                                 + std::to_string(static_cast<long long>(std::nearbyint(elapsed)))
                                                 + "s"}})));
                ++step;
                lock.lock();
            }
        });
    }

    const auto finish_ticker = [&ticker, &ticker_state]() {
        if (ticker.joinable()) {
            {
                std::lock_guard<std::mutex> lock(ticker_state->mutex);
                ticker_state->stop = true;
            }
            ticker_state->wake.notify_all();
            ticker.join();
        }
    };

    try {
        if (!arguments.is_object()) {
            throw ToolFailure("AttributeError", "'" + python_type_name(arguments)
                                                    + "' object has no attribute 'get'");
        }
        const std::string text = tools::call(tool_name, arguments);
        send(response(id, object_of({{"content", Value::array({object_of({{"type", "text"}, {"text", text}})})},
                                     {"isError", false}})));
    } catch (const std::exception& failure) {
        send(response(id, object_of({{"content", Value::array({object_of({{"type", "text"},
            {"text", failure_text(failure)}})})}, {"isError", true}})));
    }
    finish_ticker();
}

}
