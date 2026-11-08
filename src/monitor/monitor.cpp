#include "monitor/monitor.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <openssl/evp.h>
#include <stdexcept>

#include "core/clock.hpp"
#include "core/json.hpp"
#include "core/text.hpp"
#include "journal/records.hpp"

namespace hyperagent::monitor {

namespace {

using core::json::is_string;
using core::json::member;
using core::json::py_str;
using core::json::str_or_empty;
using core::json::truthy;

constexpr std::string_view no_output_marker = "(no output recorded)";

Value value_or_null(const Value* value)
{
    return value != nullptr ? *value : Value(nullptr);
}

std::string hex(const unsigned char* bytes, unsigned int length)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (unsigned int index = 0; index < length; ++index) {
        out.push_back(digits[bytes[index] >> 4]);
        out.push_back(digits[bytes[index] & 0x0F]);
    }
    return out;
}

bool marker_busy(const Value* harness, const std::string& pane)
{
    if (harness == nullptr || !harness->is_string()) {
        return false;
    }
    const auto& name = harness->get_ref<const std::string&>();
    if (name == "claude" || name == "codex") {
        return core::text::replace_all(pane, "esc to interrupt", "") != pane;
    }
    if (name == "freebuff") {
        return core::text::replace_all(pane, "Starting your model session", "") != pane;
    }
    return false;
}

Value default_info()
{
    Value info = Value::object();
    info["cwd"] = nullptr;
    info["harness"] = nullptr;
    return info;
}

}

std::string pane_digest(const std::string& pane)
{
    std::vector<std::string> lines;
    for (const auto& line : core::text::splitlines(pane)) {
        lines.emplace_back(core::text::rstrip(line));
    }
    std::size_t first = 0;
    while (first < lines.size() && core::text::strip(lines[first]).empty()) {
        ++first;
    }
    std::size_t last = lines.size();
    while (last > first && core::text::strip(lines[last - 1]).empty()) {
        --last;
    }
    std::string joined;
    for (std::size_t index = first; index < last; ++index) {
        if (index > first) {
            joined += "\n";
        }
        joined += lines[index];
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_Digest(joined.data(), joined.size(), digest, &length, EVP_sha256(), nullptr);
    return hex(digest, length);
}

std::string pane_text(const std::string& pane, std::size_t count)
{
    std::vector<std::string> lines;
    for (const auto& line : core::text::splitlines(pane)) {
        if (!core::text::strip(line).empty()) {
            lines.emplace_back(core::text::rstrip(line));
        }
    }
    const std::size_t first = lines.size() > count ? lines.size() - count : 0;
    std::string out;
    for (std::size_t index = first; index < lines.size(); ++index) {
        if (index > first) {
            out += "\n";
        }
        out += lines[index];
    }
    return out;
}

std::string make_prompt(const std::string& template_text, const std::string& agent, const std::string& last,
                        const Value& session)
{
    std::string prompt = core::text::replace_all(template_text, "{agent}", agent);
    prompt = core::text::replace_all(prompt, "{last}", last.empty() ? std::string(no_output_marker) : last);
    prompt = core::text::replace_all(prompt, "{session}", str_or_empty(&session));
    return prompt;
}

std::string render(const Value& event, bool text_mode)
{
    if (!text_mode) {
        return core::json::dumps(event);
    }
    std::string event_name = str_or_empty(member(&event, "event"));
    std::transform(event_name.begin(), event_name.end(), event_name.begin(),
                   [](unsigned char symbol) { return static_cast<char>(std::toupper(symbol)); });
    std::string head = event_name + " agent=" + py_str(member(&event, "agent"));
    if (truthy(member(&event, "session"))) {
        head += " session=" + py_str(member(&event, "session"));
    }
    const Value* idle = member(&event, "idle_s");
    if (idle != nullptr && !idle->is_null()) {
        head += " idle=" + py_str(idle) + "s";
    }
    if (truthy(member(&event, "cwd"))) {
        head += " cwd=" + py_str(member(&event, "cwd"));
    }
    const Value* last = member(&event, "last");
    const std::string last_text = str_or_empty(member(last, "text"));
    head += " last=" + core::json::dumps(Value(last_text));
    head += " instruct=" + core::json::dumps(event["instruct"]["arguments"], core::json::spaced);
    return head;
}

void print_event(const Value& event, bool text_mode)
{
    std::cout << render(event, text_mode) << std::endl;
}

Monitor::Monitor(SessionSource& source, Options options, Clock clock)
    : source_(source)
    , options_(std::move(options))
    , clock_(std::move(clock))
{
    options_.idle = std::max(0.0, options_.idle);
    options_.poll = std::max(0.05, options_.poll);
}

double Monitor::poll_interval() const
{
    return options_.poll;
}

Value Monitor::fetch_sessions()
{
    try {
        Value sessions = source_.sessions();
        return sessions.is_object() ? sessions : Value::object();
    } catch (const std::exception&) {
        return Value::object();
    }
}

std::string Monitor::fetch_capture(const std::string& name)
{
    try {
        return source_.capture(name);
    } catch (const std::exception&) {
        return "";
    }
}

bool Monitor::watched(const std::string& name) const
{
    return options_.agents.empty() || options_.agents.count(name) > 0;
}

bool Monitor::busy(const Value& info, const std::string& pane) const
{
    const Value* reported = member(&info, "busy");
    if (reported != nullptr && !reported->is_null()) {
        return truthy(reported);
    }
    return marker_busy(member(&info, "harness"), pane);
}

Value Monitor::result_mark(const Value& info, const std::string& name) const
{
    const Value* cwd = member(&info, "cwd");
    if (!truthy(cwd)) {
        return nullptr;
    }
    std::optional<journal::LocatedRecord> located;
    try {
        located = journal::latest_result(py_str(cwd), name);
    } catch (const std::exception&) {
        return nullptr;
    }
    if (!located) {
        return nullptr;
    }
    const Value* epoch = member(&located->record, "epoch");
    if (truthy(epoch)) {
        return *epoch;
    }
    return value_or_null(member(&located->record, "ts"));
}

Value Monitor::last_output(const std::string& agent, const Value& info, const std::string& pane) const
{
    const Value* cwd = member(&info, "cwd");
    std::optional<journal::LatestOutput> output;
    if (truthy(cwd)) {
        try {
            output = journal::latest_output(py_str(cwd), agent);
        } catch (const std::exception&) {
            output.reset();
        }
    }

    const Value* record = output && output->record ? &*output->record : nullptr;
    const Value* row = output && output->summary ? &*output->summary : nullptr;
    if (truthy(record)) {
        std::string text = str_or_empty(member(record, "text"));
        const Value* status = member(record, "status");
        if (text.empty() && truthy(status)) {
            text = "status=" + py_str(status) + " duration=" + py_str(member(record, "duration_s")) + "s";
        }
        Value last = Value::object();
        last["kind"] = core::json::or_else(member(record, "kind"), Value("text"));
        last["ts"] = core::json::or_else(member(record, "ts"), Value(core::clock::iso_stamp(core::clock::now_epoch())));
        last["text"] = core::text::truncate(text, options_.last_chars);
        last["source"] = "journal";
        last["path"] = value_or_null(member(row, "path"));
        last["session"] = value_or_null(member(row, "session"));
        return last;
    }

    Value last = Value::object();
    last["kind"] = "pane";
    last["ts"] = core::clock::iso_stamp(core::clock::now_epoch());
    last["text"] = core::text::truncate(pane_text(pane), options_.last_chars);
    last["source"] = "pane";
    last["path"] = nullptr;
    last["session"] = nullptr;
    return last;
}

Value Monitor::idle_event(const std::string& agent, const Value& info, const Value& last, double idle_s,
                          const std::string& trigger) const
{
    const Value session = value_or_null(member(&last, "session"));
    const std::string prompt = make_prompt(options_.prompt, agent, str_or_empty(member(&last, "text")), session);
    Value arguments = Value::object();
    arguments["name"] = agent;
    arguments["prompt"] = prompt;
    Value instruct = Value::object();
    instruct["tool"] = "instruct";
    instruct["arguments"] = arguments;

    Value event = Value::object();
    event["event"] = "idle";
    event["agent"] = agent;
    event["session"] = session;
    event["cwd"] = value_or_null(member(&info, "cwd"));
    event["harness"] = value_or_null(member(&info, "harness"));
    event["idle_s"] = core::clock::round_to(idle_s, 1);
    event["busy"] = truthy(member(&info, "busy"));
    event["trigger"] = trigger;
    event["last"] = last;
    event["instruct"] = instruct;
    return event;
}

Value Monitor::gone_event(const std::string& agent, const Value& info, const Value& last) const
{
    const Value session = value_or_null(member(&last, "session"));
    const std::string prompt = make_prompt(options_.gone_prompt, agent, str_or_empty(member(&last, "text")), session);
    Value arguments = Value::object();
    arguments["project"] = agent;
    arguments["prompt"] = prompt;
    Value instruct = Value::object();
    instruct["tool"] = "resume_project";
    instruct["arguments"] = arguments;

    Value event = Value::object();
    event["event"] = "gone";
    event["agent"] = agent;
    event["session"] = session;
    event["cwd"] = value_or_null(member(&info, "cwd"));
    event["harness"] = value_or_null(member(&info, "harness"));
    event["last"] = last;
    event["instruct"] = instruct;
    return event;
}

Monitor::State* Monitor::find_state(const std::string& name)
{
    for (auto& entry : states_) {
        if (entry.first == name) {
            return &entry.second;
        }
    }
    return nullptr;
}

std::vector<Value> Monitor::once()
{
    std::vector<Value> events;
    const Value sessions = fetch_sessions();
    std::map<std::string, Value> sorted;
    for (const auto& [name, info] : sessions.items()) {
        sorted.emplace(name, info);
    }
    for (const auto& [name, info] : sorted) {
        if (!truthy(member(&info, "alive")) || !watched(name)) {
            continue;
        }
        if (truthy(member(&info, "busy"))) {
            continue;
        }
        const std::string pane = fetch_capture(name);
        events.push_back(idle_event(name, info, last_output(name, info, pane), 0.0, "once"));
    }
    return events;
}

std::vector<Value> Monitor::poll()
{
    std::vector<Value> events;
    const Value sessions = fetch_sessions();
    const double now = clock_();

    std::map<std::string, Value> alive;
    for (const auto& [name, info] : sessions.items()) {
        if (truthy(member(&info, "alive")) && watched(name)) {
            alive.emplace(name, info);
        }
    }

    for (const auto& [name, info] : alive) {
        const std::string pane = fetch_capture(name);
        const std::string digest = pane_digest(pane);
        const bool is_busy = busy(info, pane);
        const Value mark = result_mark(info, name);
        State* state = find_state(name);
        if (state == nullptr) {
            State created;
            created.digest = digest;
            created.changed = now;
            created.info = info;
            created.result_mark = mark;
            created.was_busy = is_busy;
            states_.emplace_back(name, std::move(created));
            continue;
        }

        state->info = info;
        if (truthy(&mark) && mark != state->result_mark) {
            state->result_mark = mark;
            state->digest = digest;
            state->changed = now;
            state->reported = true;
            state->was_busy = is_busy;
            state->gone = false;
            state->armed = false;
            events.push_back(idle_event(name, info, last_output(name, info, pane), 0.0, "result"));
            continue;
        }

        if (is_busy) {
            state->quiet_since.reset();
            state->armed = true;
        } else if (!state->quiet_since) {
            state->quiet_since = now;
        }

        if (state->armed && !is_busy && !state->reported && now - *state->quiet_since >= options_.idle) {
            state->digest = digest;
            state->changed = now;
            state->reported = true;
            state->was_busy = is_busy;
            state->gone = false;
            state->armed = false;
            events.push_back(idle_event(name, info, last_output(name, info, pane), 0.0, "pane"));
            continue;
        }

        if (is_busy && !state->was_busy) {
            state->reported = false;
            state->changed = now;
        }
        state->was_busy = is_busy;

        if (digest != state->digest) {
            state->digest = digest;
            state->changed = now;
            state->reported = false;
            state->gone = false;
            continue;
        }

        const Value* reported_busy = member(&info, "busy");
        const bool busy_unreported = reported_busy == nullptr || reported_busy->is_null();
        if (busy_unreported && !state->reported && now - state->changed >= options_.idle) {
            events.push_back(idle_event(name, info, last_output(name, info, pane), now - state->changed, "idle"));
            state->reported = true;
        }
    }

    for (auto& [name, state] : states_) {
        if (alive.count(name) > 0 || state.gone) {
            continue;
        }
        state.gone = true;
        const Value* listed = member(&sessions, name);
        Value info = truthy(listed) ? *listed : (truthy(&state.info) ? state.info : default_info());
        events.push_back(gone_event(name, info, last_output(name, info, "")));
    }
    return events;
}

}
