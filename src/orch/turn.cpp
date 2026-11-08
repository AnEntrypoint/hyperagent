#include "orch/turn.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/clock.hpp"
#include "core/files.hpp"
#include "core/json.hpp"
#include "core/text.hpp"
#include "journal/journal.hpp"
#include "orch/ccbridge.hpp"
#include "orch/config.hpp"
#include "orch/harness.hpp"
#include "orch/lock.hpp"
#include "orch/projects.hpp"
#include "orch/state.hpp"
#include "orch/websocket.hpp"

namespace hyperagent::orch::turn {

namespace {

using Clock = std::chrono::steady_clock;

double epoch_now()
{
    return core::clock::now_epoch();
}

std::string text_of(const Value* value)
{
    return core::json::str_or_empty(value);
}

bool has_marker(const std::string& haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::optional<std::string> optional_text(const std::string& value)
{
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::string url_encode(std::string_view text)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char symbol : text) {
        if (std::isalnum(symbol) || symbol == '_' || symbol == '.' || symbol == '-' || symbol == '~') {
            out.push_back(static_cast<char>(symbol));
        } else if (symbol == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[symbol >> 4U]);
            out.push_back(hex[symbol & 0x0FU]);
        }
    }
    return out;
}

std::string session_from_launch_args(const Value& args, bool& found)
{
    found = false;
    if (!args.is_array()) {
        return "";
    }
    static const std::vector<std::string> selectors{"--resume", "-r", "--session", "resume", "--continue"};
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string value = core::json::py_str(&args[index]);
        bool selector = false;
        for (const auto& known : selectors) {
            selector = selector || value == known;
        }
        if (!selector) {
            continue;
        }
        const std::string following = index + 1 < args.size() ? core::json::py_str(&args[index + 1]) : std::string();
        if (!following.empty() && !following.starts_with('-')) {
            found = true;
            return following;
        }
    }
    return "";
}

bool resumes_saved_session(const Value& args)
{
    if (!args.is_array()) {
        return false;
    }
    for (const auto& item : args) {
        const std::string value = core::json::py_str(&item);
        if (value == "--resume" || value == "-r" || value == "--continue" || value == "--fork-session") {
            return true;
        }
    }
    return false;
}

bool days_valid(int year, int month, int day)
{
    static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12 || day < 1) {
        return false;
    }
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const int limit = month == 2 && leap ? 29 : days[month - 1];
    return day <= limit;
}

std::optional<double> parse_stamp(const std::string& raw)
{
    std::string text = raw;
    if (!text.empty() && text.back() == 'Z') {
        text.pop_back();
        text += "+00:00";
    }
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int consumed = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2d%n", &year, &month, &day, &consumed) != 3 || consumed != 10) {
        return std::nullopt;
    }
    std::size_t cursor = 10;
    bool has_time = false;
    if (cursor < text.size() && (text[cursor] == 'T' || text[cursor] == ' ')) {
        ++cursor;
        if (std::sscanf(text.c_str() + cursor, "%2d:%2d%n", &hour, &minute, &consumed) != 2 || consumed != 5) {
            return std::nullopt;
        }
        cursor += 5;
        has_time = true;
        if (cursor < text.size() && text[cursor] == ':') {
            if (std::sscanf(text.c_str() + cursor + 1, "%2d%n", &second, &consumed) != 1 || consumed != 2) {
                return std::nullopt;
            }
            cursor += 3;
        }
    }
    double fraction = 0.0;
    if (has_time && cursor < text.size() && text[cursor] == '.') {
        std::size_t end = cursor + 1;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        if (end == cursor + 1) {
            return std::nullopt;
        }
        fraction = std::stod("0." + text.substr(cursor + 1, end - cursor - 1));
        cursor = end;
    }
    std::optional<long> offset_seconds;
    if (has_time && cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-')) {
        const int sign = text[cursor] == '-' ? -1 : 1;
        int offset_hours = 0, offset_minutes = 0;
        if (std::sscanf(text.c_str() + cursor + 1, "%2d:%2d%n", &offset_hours, &offset_minutes, &consumed) == 2
            && consumed == 5) {
            cursor += 6;
        } else if (std::sscanf(text.c_str() + cursor + 1, "%2d%2d%n", &offset_hours, &offset_minutes, &consumed) == 2
                   && consumed == 4) {
            cursor += 5;
        } else {
            return std::nullopt;
        }
        offset_seconds = sign * (offset_hours * 3600L + offset_minutes * 60L);
    }
    if (cursor != text.size()) {
        return std::nullopt;
    }
    if (!days_valid(year, month, day) || hour > 23 || minute > 59 || second > 59) {
        return std::nullopt;
    }

    std::tm moment{};
    moment.tm_year = year - 1900;
    moment.tm_mon = month - 1;
    moment.tm_mday = day;
    moment.tm_hour = hour;
    moment.tm_min = minute;
    moment.tm_sec = second;
    moment.tm_isdst = -1;
    if (offset_seconds) {
        return static_cast<double>(timegm(&moment)) - static_cast<double>(*offset_seconds) + fraction;
    }
    return static_cast<double>(mktime(&moment)) + fraction;
}

std::optional<double> session_first_stamp(const std::string& path)
{
    const auto bytes = core::files::read_bytes(path);
    if (!bytes) {
        return std::nullopt;
    }
    for (const auto& raw_line : core::text::splitlines(*bytes)) {
        const auto line = core::text::strip(raw_line);
        if (line.empty()) {
            continue;
        }
        const auto record = core::json::parse(line);
        if (!record || !record->is_object()) {
            continue;
        }
        const Value* stamp = core::json::member(&*record, "timestamp");
        if (!core::json::truthy(stamp)) {
            continue;
        }
        return parse_stamp(core::json::py_str(stamp));
    }
    return std::nullopt;
}

std::optional<std::string> resolve_output_session(const Value& job, double since)
{
    const std::string resume = text_of(core::json::member(&job, "resume"));
    if (!resume.empty()) {
        return resume;
    }
    try {
        const Value live = ccbridge::fetch_sessions();
        const Value* info = core::json::member(&live, text_of(core::json::member(&job, "name")));
        bool found = false;
        const Value* launch_args = core::json::member(info, "launch_args");
        const std::string session = session_from_launch_args(
            launch_args != nullptr ? *launch_args : Value::array(), found);
        if (found) {
            return session;
        }
    } catch (const std::exception&) {
    }
    const Value session = session_created_after(text_of(core::json::member(&job, "harness")),
                                                text_of(core::json::member(&job, "cwd")), since);
    if (session.is_null()) {
        return std::nullopt;
    }
    return text_of(core::json::member(&session, "session_id"));
}

bool hub_stopped(const std::string& name)
{
    try {
        const Value sessions = ccbridge::fetch_sessions();
        const Value* info = ccbridge::hub_info(sessions, name);
        return info == nullptr || !core::json::truthy(core::json::member(info, "alive"));
    } catch (const std::exception&) {
        return false;
    }
}

bool trust_dialog_open(const std::string& name, double timeout_seconds)
{
    const auto started = Clock::now();
    while (true) {
        const std::string pane = ccbridge::capture(name);
        if (has_marker(pane, config::trust_text) && has_marker(pane, config::trust_hint)) {
            return true;
        }
        if (std::chrono::duration<double>(Clock::now() - started).count() >= timeout_seconds) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

bool pane_idle_ready(const std::string& harness_name, const std::string& name)
{
    const std::string pane = ccbridge::capture(name);
    try {
        return harness::pane_ready(harness_name, pane) && !harness::pane_busy(harness_name, pane);
    } catch (const std::exception&) {
        return false;
    }
}

bool is_busy(const std::string& harness_name, const std::string& pane)
{
    try {
        return harness::pane_busy(harness_name, pane);
    } catch (const std::exception&) {
        return false;
    }
}

bool local_command_done(const std::string& harness_name, const std::string& prompt, const std::string& pane)
{
    if (!prompt.starts_with('/') || is_busy(harness_name, pane)) {
        return false;
    }
    const auto found = pane.rfind(prompt);
    if (found == std::string::npos) {
        return false;
    }
    return has_marker(pane.substr(found + prompt.size()), "⎿");
}

class OutputWriter {
public:
    OutputWriter(const Value& job, double started)
        : job_(job)
        , started_(started)
        , resume_(text_of(core::json::member(&job, "resume")))
    {
    }

    void append(const std::string& kind, const std::string& text, Value fields = Value::object())
    {
        ensure();
        if (!bound_ && epoch_now() - bound_at_ >= 1.0) {
            bound_at_ = epoch_now();
            bind_to_session();
        }
        writer_->append(kind, text, std::nullopt, std::move(fields));
    }

    void finish(const std::string& status, bool acked, const Value& tail)
    {
        ensure();
        if (!bound_) {
            bind_to_session();
        }
        Value fields = Value::object();
        fields["status"] = status;
        fields["duration_s"] = core::clock::round_to(epoch_now() - started_, 1);
        fields["acked"] = acked;
        fields["pane_tail"] = tail;
        writer_->append("result", "", std::nullopt, fields);
        writer_->flush();
    }

    std::string path() const
    {
        return writer_->path().string();
    }

    std::string session() const
    {
        return writer_->session();
    }

private:
    void ensure()
    {
        if (!writer_) {
            const std::string cwd = text_of(core::json::member(&job_, "cwd"));
            writer_ = std::make_unique<journal::Journal>(
                text_of(core::json::member(&job_, "name")), cwd.empty() ? config::root_dir().string() : cwd,
                text_of(core::json::member(&job_, "harness")), optional_text(resume_));
            bound_ = !resume_.empty();
        }
    }

    void bind_to_session()
    {
        const auto found = resolve_output_session(job_, started_);
        if (found && !found->empty()) {
            writer_->bind(found);
            bound_ = true;
        }
    }

    const Value& job_;
    double started_;
    std::string resume_;
    std::unique_ptr<journal::Journal> writer_;
    bool bound_ = false;
    double bound_at_ = 0.0;
};

}

Value session_created_after(const std::string& harness_name, const std::string& cwd, double since)
{
    std::optional<double> best_stamp;
    Value best(nullptr);
    for (const auto& session : harness::sessions(harness_name, cwd)) {
        const auto first = session_first_stamp(text_of(core::json::member(&session, "path")));
        if (first && *first >= since - 2.0 && (!best_stamp || *first < *best_stamp)) {
            best_stamp = first;
            best = session;
        }
    }
    return best;
}

std::string websocket_target(const std::string& name, const std::string& cwd, const std::string& harness_name,
                             const std::string& resume, bool continue_last, bool fork)
{
    std::string query = "session=" + url_encode(name) + "&harness=" + url_encode(harness_name)
        + "&mode=tagged&kinds=" + url_encode("text,tool,you");
    if (!cwd.empty()) {
        query += "&cwd=" + url_encode(cwd);
    }
    if (!resume.empty()) {
        query += "&resume=" + url_encode(resume);
    }
    if (continue_last) {
        query += "&continue=1";
    }
    if (fork && (!resume.empty() || continue_last)) {
        query += "&fork=1";
    }
    return "/ws?" + query;
}

namespace {

std::string target_of(const Value& job)
{
    return websocket_target(text_of(core::json::member(&job, "name")), text_of(core::json::member(&job, "cwd")),
                            text_of(core::json::member(&job, "harness")), text_of(core::json::member(&job, "resume")),
                            core::json::truthy(core::json::member(&job, "continue")),
                            core::json::truthy(core::json::member(&job, "fork")));
}

void record_latest(Value& result, const Value& job)
{
    const bool fork = core::json::truthy(core::json::member(&job, "fork"));
    if (!fork && !core::json::truthy(core::json::member(&job, "record_latest"))) {
        return;
    }
    const std::string harness_name = text_of(core::json::member(&job, "harness"));
    const std::string cwd = text_of(core::json::member(&job, "cwd"));
    const Value session = projects::latest_session(harness_name, cwd);
    if (session.is_null()) {
        return;
    }
    const std::string session_id = text_of(core::json::member(&session, "session_id"));
    state::record_project(text_of(core::json::member(&job, "name")), cwd, session_id, harness_name);
    result[fork ? "forked_session" : "resolved_session"] = session_id;
}

}

Value run_job(const Value& job, const TurnOptions& options)
{
    const std::string name = text_of(core::json::member(&job, "name"));
    const std::string harness_name = text_of(core::json::member(&job, "harness"));
    const std::string prompt = text_of(core::json::member(&job, "prompt"));
    const double started = epoch_now();
    OutputWriter output(job, started);

    output.append("instruct", prompt);

    std::vector<std::string> text_parts;
    std::vector<std::string> final_parts;
    Value tool_names = Value::array();
    std::string status = "died";
    bool acked = false;
    std::vector<std::string> tail;
    const auto deadline_of = [&](double seconds) {
        return started + seconds;
    };

    {
        SessionLock lock(name, options.timing.lock_wait);
        const bool resuming = core::json::truthy(core::json::member(&job, "resume"))
            || core::json::truthy(core::json::member(&job, "continue"));
        if (resuming) {
            ccbridge::retire_hub(name);
        }
        const bool existing = ccbridge::hub_exists(name);
        websocket::Client peer(config::host(), config::port(), target_of(job));
        if (options.trust && !existing && trust_dialog_open(name, 4.0)) {
            std::cerr << "[orch] " << name
                      << ": folder-trust dialog is open; a person must confirm it in the pane\n";
        }
        peer.send_text(prompt);
        double sent = epoch_now();
        int retries = 0;
        double last = epoch_now();

        while (true) {
            const double remaining = deadline_of(options.timing.timeout) - epoch_now();
            if (remaining <= 0) {
                status = "timeout";
                break;
            }
            const auto wait = std::min(remaining, config::poll_interval);
            const auto event = peer.next(std::chrono::milliseconds(static_cast<long>(wait * 1000.0)));
            if (event.kind == websocket::EventKind::timeout) {
                const double now = epoch_now();
                if (hub_stopped(name)) {
                    status = "stopped";
                    break;
                }
                if (!acked && text_parts.empty() && retries < config::prompt_retries
                    && now - sent >= config::prompt_retry_after && pane_idle_ready(harness_name, name)) {
                    peer.send_text(prompt);
                    sent = now;
                    ++retries;
                    last = now;
                    continue;
                }
                if (now - sent >= config::local_command_settle) {
                    const std::string pane = ccbridge::capture(name);
                    if (local_command_done(harness_name, prompt, pane)) {
                        tail = ccbridge::pane_tail(pane);
                        status = "turn_end";
                        break;
                    }
                }
                if (options.timing.idle_exit > 0 && now - last >= options.timing.idle_exit) {
                    const std::string pane = ccbridge::capture(name);
                    if (!is_busy(harness_name, pane)) {
                        tail = ccbridge::pane_tail(pane);
                        status = "blocked";
                        break;
                    }
                }
                continue;
            }
            if (event.kind == websocket::EventKind::closed) {
                status = "died";
                break;
            }
            const std::string& frame = event.text;
            last = epoch_now();
            if (frame.empty() || frame == "e:") {
                status = frame == "e:" ? "turn_end" : "died";
                break;
            }
            if (frame.size() > 1 && frame[1] == ':' && (frame[0] == 't' || frame[0] == 'u' || frame[0] == 'y')) {
                const std::string payload = frame.substr(2);
                if (frame[0] == 't') {
                    text_parts.push_back(payload);
                    final_parts.push_back(payload);
                    output.append("text", payload);
                } else if (frame[0] == 'u') {
                    tool_names.push_back(payload);
                    final_parts.clear();
                    output.append("tool", payload);
                } else {
                    acked = true;
                    final_parts.clear();
                    output.append("you", payload);
                }
            } else {
                text_parts.push_back(frame);
                output.append("raw", frame);
            }
        }
        peer.close();
    }

    if (status == "timeout" || status == "blocked") {
        tail = ccbridge::pane_tail(ccbridge::capture(name));
    }
    Value tail_value = Value::array();
    for (const auto& line : tail) {
        tail_value.push_back(line);
    }
    output.finish(status, acked, tail_value);

    std::string response;
    for (const auto& part : final_parts) {
        response += part;
    }
    if (core::text::strip(response).empty()) {
        response.clear();
        for (const auto& part : text_parts) {
            response += part;
        }
    }

    Value result = Value::object();
    result["name"] = name;
    result["harness"] = harness_name;
    result["cwd"] = text_of(core::json::member(&job, "cwd"));
    result["status"] = status;
    result["text"] = response;
    result["acked"] = acked;
    result["pane_tail"] = tail_value;
    result["tool_names"] = tool_names;
    result["duration_s"] = core::clock::round_to(epoch_now() - started, 1);
    result["output_path"] = output.path();
    result["output_session"] = output.session();
    record_latest(result, job);
    return result;
}

Value start_one(const Value& job, double settle, bool trust, double lock_wait)
{
    const std::string name = text_of(core::json::member(&job, "name"));
    const bool resuming = core::json::truthy(core::json::member(&job, "resume"))
        || core::json::truthy(core::json::member(&job, "continue"));
    bool trusted = false;
    bool restarted = false;
    {
        SessionLock lock(name, lock_wait);
        if (resuming) {
            ccbridge::retire_hub(name);
        }
        bool existing = ccbridge::hub_exists(name);
        if (existing && !resuming) {
            const Value live = ccbridge::fetch_sessions();
            const Value* info = core::json::member(&live, name);
            const Value* launch_args = core::json::member(info, "launch_args");
            if (launch_args != nullptr && resumes_saved_session(*launch_args)) {
                ccbridge::retire_hub(name);
                existing = false;
                restarted = true;
            }
        }
        websocket::Client peer(config::host(), config::port(), target_of(job));
        if (trust && !existing) {
            trusted = !trust_dialog_open(name, 4.0);
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(settle));
        peer.close();
    }

    Value result = job;
    result["trusted"] = trusted;
    result["restarted"] = restarted;
    record_latest(result, job);
    return result;
}

}
