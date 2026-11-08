#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::monitor {

using Value = core::json::Value;

inline constexpr std::string_view idle_prompt_template =
    "You stopped producing output. Your last output was:\n{last}\n\n"
    "Continue the task from there and report what remains.";
inline constexpr std::string_view gone_prompt_template =
    "Your session stopped before the task finished. Your last output was:\n{last}\n\n"
    "Resume and continue from there.";

class SessionSource {
public:
    virtual ~SessionSource() = default;
    virtual Value sessions() = 0;
    virtual std::string capture(const std::string& name) = 0;
};

struct Options {
    double idle = 20.0;
    double poll = 1.0;
    std::set<std::string> agents;
    long last_chars = 4000;
    std::string prompt{idle_prompt_template};
    std::string gone_prompt{gone_prompt_template};
};

std::string pane_digest(const std::string& pane);
std::string pane_text(const std::string& pane, std::size_t count = 8);
std::string make_prompt(const std::string& template_text, const std::string& agent, const std::string& last,
                        const Value& session);
std::string render(const Value& event, bool text_mode);
void print_event(const Value& event, bool text_mode);

class Monitor {
public:
    using Clock = std::function<double()>;

    Monitor(SessionSource& source, Options options, Clock clock);

    std::vector<Value> once();
    std::vector<Value> poll();
    double poll_interval() const;

private:
    struct State {
        std::string digest;
        double changed = 0.0;
        bool reported = false;
        bool gone = false;
        Value info = Value::object();
        Value result_mark;
        bool was_busy = false;
        std::optional<double> quiet_since;
        bool armed = false;
    };

    Value fetch_sessions();
    std::string fetch_capture(const std::string& name);
    bool watched(const std::string& name) const;
    bool busy(const Value& info, const std::string& pane) const;
    Value result_mark(const Value& info, const std::string& name) const;
    Value last_output(const std::string& agent, const Value& info, const std::string& pane) const;
    Value idle_event(const std::string& agent, const Value& info, const Value& last, double idle_s,
                     const std::string& trigger) const;
    Value gone_event(const std::string& agent, const Value& info, const Value& last) const;
    State* find_state(const std::string& name);

    SessionSource& source_;
    Options options_;
    Clock clock_;
    std::vector<std::pair<std::string, State>> states_;
};

}
