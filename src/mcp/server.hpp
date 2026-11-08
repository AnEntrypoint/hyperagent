#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::mcp {

using Value = core::json::Value;

class Server {
public:
    [[noreturn]] void run();

private:
    bool runs_inline(const Value& message) const;
    void run_guarded(const Value& message);
    void dispatch(const Value& message);
    void drain();
    void handle(const Value& message);
    void handle_tools_call(const Value& id, const Value& params);
    void send(const Value& message, bool final = false);
    void log_message(const std::string& level, const Value& data);
    void emit_event(const Value& event);
    void ensure_monitor();
    void monitor_loop();
    void announce_progress(const Value& token, const std::string& name, long long step);
    void cancel_listens();

    std::mutex flight_mutex_;
    std::condition_variable flight_done_;
    int in_flight_ = 0;
    std::mutex write_mutex_;
    bool closing_ = false;
    std::mutex state_mutex_;
    std::optional<std::string> log_level_;
    std::vector<Value> log_queue_;
    std::vector<std::pair<Value, Value>> listens_;
    std::vector<std::string> subscriptions_;
    std::set<std::string> known_outputs_;
    bool monitor_running_ = false;
};

}
