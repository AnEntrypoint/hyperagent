#include "mcp/peers.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <semaphore>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <thread>
#include <utility>
#include <vector>

#include "core/files.hpp"
#include "core/text.hpp"
#include "journal/records.hpp"
#include "mcp/args.hpp"
#include "mcp/outputs.hpp"
#include "mcp/turns.hpp"
#include "orch/ccbridge.hpp"
#include "orch/config.hpp"
#include "orch/harness.hpp"
#include "orch/orch.hpp"
#include "orch/projects.hpp"
#include "orch/turn.hpp"

namespace hyperagent::mcp::peers {

namespace {

namespace journal_api = hyperagent::journal;

constexpr long long default_concurrency = 8;
constexpr double start_settle_seconds = 3.0;
constexpr double default_wait_seconds = 50.0;

orch::TurnOptions turn_options(double timeout, double idle, bool tools, bool trust)
{
    orch::TurnOptions options;
    options.timing.timeout = timeout;
    options.timing.idle_exit = idle;
    options.timing.lock_wait = orch::config::default_lock_wait;
    options.tools = tools;
    options.trust = trust;
    return options;
}

std::string join(const std::vector<std::string>& items, std::string_view separator)
{
    std::string out;
    for (const auto& item : items) {
        out += out.empty() ? "" : std::string(separator);
        out += item;
    }
    return out;
}

Value field(const Value& row, std::string_view key)
{
    const Value* found = core::json::member(&row, key);
    return found != nullptr ? *found : Value();
}

Value or_chain(const Value& first, const Value& second, const Value& fallback)
{
    if (core::json::truthy(&first)) {
        return first;
    }
    if (core::json::truthy(&second)) {
        return second;
    }
    return fallback;
}

Value merged(Value base, const Value& session)
{
    for (const auto& [key, item] : session.items()) {
        base[key] = item;
    }
    return base;
}

Value prepare_job(const std::string& name, const std::string& prompt, bool resume,
                  const std::optional<std::string>& harness, const std::optional<std::string>& model, bool fork)
{
    Value entry = Value::object();
    entry["name"] = name;
    entry["prompt"] = prompt;
    if (harness) {
        entry["harness"] = *harness;
    }
    if (model) {
        entry["model"] = *model;
    }
    if (fork) {
        entry["fork"] = true;
    }
    std::vector<Value> jobs{orch::projects::normalize(entry, true)};
    orch::projects::fill_cwds(jobs);
    Value job = jobs.front();
    if (!core::json::truthy(core::json::member(&job, "cwd"))) {
        throw ToolFailure("ValueError", "unknown project " + orch::config::python_repr(core::json::py_str(
                                            core::json::member(&job, "name"))) + "; call list_projects first");
    }
    if (resume && !orch::ccbridge::hub_exists(name)) {
        const Value session = orch::projects::latest_session(core::json::py_str(core::json::member(&job, "harness")),
                                                             core::json::py_str(core::json::member(&job, "cwd")));
        if (session.is_object() && !session.empty()) {
            job["resume"] = field(session, "session_id");
        }
    }
    return job;
}

template <typename Work>
std::vector<Value> concurrently(std::size_t count, std::ptrdiff_t limit, Work work)
{
    std::vector<Value> results(count);
    std::vector<std::exception_ptr> failures(count);
    std::counting_semaphore<> gate(std::max<std::ptrdiff_t>(1, limit));
    std::vector<std::thread> threads;
    threads.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        threads.emplace_back([&, index]() {
            gate.acquire();
            try {
                results[index] = work(index);
            } catch (...) {
                failures[index] = std::current_exception();
            }
            gate.release();
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (const auto& failure : failures) {
        if (failure) {
            std::rethrow_exception(failure);
        }
    }
    return results;
}

std::string page_of_lines(const std::vector<std::string>& lines, long long count)
{
    const auto total = static_cast<long long>(lines.size());
    long long start = 0;
    if (count > 0) {
        start = std::max(0LL, total - count);
    } else if (count < 0) {
        start = std::min(total, -count);
    }
    std::vector<std::string> window(lines.begin() + static_cast<std::ptrdiff_t>(start), lines.end());
    const std::string joined = join(window, "\n");
    return joined.empty() ? "(empty pane)" : joined;
}

}

std::string list_projects(const Value& args)
{
    return dumps_indented(orch::list_projects(flag_arg(args, "all", false)));
}

std::string start_projects(const Value& args)
{
    std::vector<std::string> names;
    for (const auto& name : text_list(arg(args, "names"))) {
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    if (names.empty()) {
        throw ToolFailure("ValueError", "names must contain at least one project name");
    }
    orch::ensure_server();
    const std::optional<std::string> harness = optional_text_arg(args, "harness");
    const std::optional<std::string> model = optional_text_arg(args, "model");
    std::vector<Value> jobs;
    for (const auto& name : names) {
        Value entry = Value::object();
        entry["name"] = name;
        if (harness) {
            entry["harness"] = *harness;
        }
        if (model) {
            entry["model"] = *model;
        }
        jobs.push_back(orch::projects::normalize(entry, false));
    }
    orch::projects::fill_cwds(jobs);
    std::vector<std::string> missing;
    for (const auto& job : jobs) {
        if (!core::json::truthy(core::json::member(&job, "cwd"))) {
            missing.push_back(core::json::py_str(core::json::member(&job, "name")));
        }
    }
    if (!missing.empty()) {
        throw ToolFailure("ValueError", "unknown project(s): " + join(missing, ", ") + "; call list_projects");
    }
    if (flag_arg(args, "resume", false)) {
        for (auto& job : jobs) {
            const std::string name = core::json::py_str(core::json::member(&job, "name"));
            if (orch::ccbridge::hub_exists(name)) {
                continue;
            }
            const Value session = orch::projects::latest_session(core::json::py_str(core::json::member(&job, "harness")),
                                                                 core::json::py_str(core::json::member(&job, "cwd")));
            if (session.is_object() && !session.empty()) {
                job["resume"] = field(session, "session_id");
                if (flag_arg(args, "fork", false)) {
                    job["fork"] = true;
                }
            }
        }
    }
    const bool trust = flag_arg(args, "trust", true);
    const long long limit = int_arg(args, "concurrency", default_concurrency);
    const auto started = concurrently(jobs.size(), static_cast<std::ptrdiff_t>(limit), [&](std::size_t index) {
        return orch::turn::start_one(jobs[index], start_settle_seconds, trust, orch::config::default_lock_wait);
    });
    const Value sessions = orch::ccbridge::fetch_sessions();
    std::vector<std::string> lines;
    for (const auto& job : started) {
        const std::string name = core::json::py_str(core::json::member(&job, "name"));
        const Value info = field(sessions, name);
        const bool alive = core::json::truthy(core::json::member(&info, "alive"));
        std::string suffix;
        if (core::json::truthy(core::json::member(&job, "model"))) {
            suffix += " model=" + core::json::py_str(core::json::member(&job, "model"));
        }
        if (core::json::truthy(core::json::member(&job, "trusted"))) {
            suffix += " trust=accepted";
        }
        if (core::json::truthy(core::json::member(&job, "restarted"))) {
            suffix += " restarted=stale-resume";
        }
        if (core::json::truthy(core::json::member(&job, "resume"))) {
            suffix += " resume=" + core::json::py_str(core::json::member(&job, "resume"));
        }
        lines.push_back(name + ": " + (alive ? "started" : "not alive") + " harness="
                        + core::json::py_str(core::json::member(&job, "harness")) + " cwd="
                        + core::json::py_str(core::json::member(&job, "cwd")) + suffix);
    }
    return join(lines, "\n");
}

std::string instruct(const Value& args)
{
    const std::string name = text_arg(args, "name");
    const std::string prompt = text_arg(args, "prompt");
    if (core::text::strip(prompt).empty()) {
        throw ToolFailure("ValueError", "prompt is required");
    }
    orch::ensure_server();
    const Value job = prepare_job(name, prompt, flag_arg(args, "resume", false), optional_text_arg(args, "harness"),
                                  optional_text_arg(args, "model"), flag_arg(args, "fork", false));
    const auto options = turn_options(number_arg(args, "timeout_s", orch::config::default_timeout),
                                      number_arg(args, "idle_exit_s", orch::config::default_idle_exit),
                                      flag_arg(args, "tools", false), flag_arg(args, "trust", true));
    const bool tools = flag_arg(args, "tools", false);
    if (flag_arg(args, "wait", false)) {
        return orch::render_turn(orch::turn::run_job(job, options), tools);
    }
    Value out = Value::object();
    out["turn_id"] = turn_book().start(job, options);
    out["name"] = field(job, "name");
    out["harness"] = field(job, "harness");
    out["model"] = field(job, "model");
    out["state"] = "running";
    return dumps_indented(out);
}

std::string instruct_many(const Value& args)
{
    const Value* entries = arg(args, "jobs");
    if (entries == nullptr || !entries->is_array() || entries->empty()) {
        throw ToolFailure("ValueError", "jobs must contain at least one entry");
    }
    const double timeout = number_arg(args, "timeout_s", orch::config::default_timeout);
    const double idle = number_arg(args, "idle_exit_s", orch::config::default_idle_exit);
    const bool tools = flag_arg(args, "tools", false);
    const bool trust = flag_arg(args, "trust", true);
    struct Prepared {
        Value job;
        double timeout;
        double idle;
    };
    std::vector<Prepared> prepared;
    orch::ensure_server();
    for (const auto& entry : *entries) {
        if (!entry.is_object()) {
            throw ToolFailure("AttributeError", "'" + python_type_name(entry) + "' object has no attribute 'get'");
        }
        const std::string name = text_arg(entry, "name");
        const std::string prompt = text_arg(entry, "prompt");
        if (core::text::strip(prompt).empty()) {
            throw ToolFailure("ValueError", "job " + orch::config::python_repr(name) + " has no prompt");
        }
        std::optional<std::string> harness = optional_text_arg(entry, "harness");
        if (!harness) {
            harness = optional_text_arg(args, "harness");
        }
        std::optional<std::string> model = optional_text_arg(entry, "model");
        if (!model) {
            model = optional_text_arg(args, "model");
        }
        Value job = prepare_job(name, prompt, flag_arg(args, "resume", false), harness, model,
                                flag_arg(args, "fork", false));
        prepared.push_back({std::move(job), number_arg(entry, "timeout_s", timeout),
                            number_arg(entry, "idle_exit_s", idle)});
    }
    if (flag_arg(args, "wait", false)) {
        const auto results = concurrently(prepared.size(), static_cast<std::ptrdiff_t>(prepared.size()),
                                          [&](std::size_t index) {
                                              return orch::turn::run_job(prepared[index].job,
                                                                         turn_options(prepared[index].timeout,
                                                                                      prepared[index].idle, tools,
                                                                                      trust));
                                          });
        std::vector<std::string> rendered;
        for (const auto& result : results) {
            rendered.push_back(orch::render_turn(result, tools));
        }
        return join(rendered, "\n\n");
    }
    Value turns = Value::array();
    for (const auto& item : prepared) {
        turns.push_back(turn_book().start(item.job, turn_options(item.timeout, item.idle, tools, trust)));
    }
    Value out = Value::object();
    out["turns"] = turns;
    out["state"] = "running";
    return dumps_indented(out);
}

std::string turn_result(const Value& args)
{
    const std::string turn_id = text_arg(args, "turn_id");
    return dumps_indented(turn_book().collect(turn_id, number_arg(args, "wait_s", default_wait_seconds)));
}

std::string resume_project(const Value& args)
{
    const auto project = optional_text_arg(args, "project");
    const auto session_id = optional_text_arg(args, "session_id");
    const auto cwd = optional_text_arg(args, "cwd");
    const auto harness = optional_text_arg(args, "harness");
    const auto model = optional_text_arg(args, "model");
    const std::string prompt = text_arg(args, "prompt");
    const bool fork = flag_arg(args, "fork", false);

    orch::ProjectSelector selector;
    selector.project = project;
    selector.cwd = cwd;
    selector.harness = harness;
    const Value target = orch::projects::resume_target(selector, session_id);
    const Value session = field(target, "session");
    const std::string target_session = core::json::py_str(core::json::member(&session, "session_id"));
    const bool global = core::json::truthy(core::json::member(&session, "global"));

    Value job = Value::object();
    job["name"] = field(target, "name");
    job["harness"] = field(target, "harness");
    job["harness_explicit"] = harness.has_value();
    if (model) {
        job["model"] = orch::config::check_model(*model);
    }
    job["cwd"] = field(target, "cwd");
    job["resume"] = target_session;
    if (fork) {
        job["fork"] = true;
    }
    const std::string selector_kind = core::json::py_str(core::json::member(&session, "selector"));
    if (selector_kind == "name" || selector_kind == "path") {
        job["record_latest"] = true;
    }
    std::vector<Value> jobs{job};
    orch::projects::fill_cwds(jobs);
    job = jobs.front();
    const std::string name = core::json::py_str(core::json::member(&job, "name"));

    orch::ensure_server();
    const bool present = orch::ccbridge::hub_exists(name);
    std::vector<Value> live_args;
    Value live_harness;
    Value live_model;
    if (present && orch::ccbridge::server_alive()) {
        const Value sessions = orch::ccbridge::fetch_sessions();
        const Value live = field(sessions, name);
        if (core::json::truthy(&live)) {
            const Value launch = field(live, "launch_args");
            if (launch.is_array()) {
                for (const auto& item : launch) {
                    live_args.push_back(item);
                }
            }
            live_harness = field(live, "harness");
            live_model = field(live, "model");
        }
    }
    const Value* live_id = live_args.size() > 1 ? &live_args[1] : nullptr;
    const bool same_harness = live_harness.is_null()
        || core::json::py_str(&live_harness) == core::json::py_str(core::json::member(&job, "harness"));
    const bool live_id_matches = live_id != nullptr && live_id->is_string() && live_id->get<std::string>() == target_session;
    const bool listed = std::any_of(live_args.begin(), live_args.end(), [&](const Value& item) {
        return item.is_string() && item.get<std::string>() == target_session;
    });
    const bool same_model = !model || live_model.is_null() || core::json::py_str(&live_model) == *model;
    const bool matches_live = present && same_harness && same_model && !fork
        && (!session_id || target_session == session_id.value() || live_id_matches || listed);

    if (matches_live && prompt.empty()) {
        Value launch = Value::array();
        for (const auto& item : live_args) {
            launch.push_back(item);
        }
        Value out = Value::object();
        out["state"] = "already_alive";
        out["project"] = name;
        out["cwd"] = field(job, "cwd");
        out["harness"] = field(job, "harness");
        out["model"] = field(job, "model");
        out["session_id"] = live_id != nullptr ? *live_id : Value();
        out["requested_session_id"] = target_session;
        out["fork"] = fork;
        out["global"] = global;
        out["launch_args"] = launch;
        return dumps_indented(out);
    }
    if (matches_live) {
        job.erase("resume");
    }

    const double timeout = number_arg(args, "timeout_s", orch::config::default_timeout);
    const double idle = number_arg(args, "idle_exit_s", orch::config::default_idle_exit);
    const bool tools = flag_arg(args, "tools", false);
    const bool trust = flag_arg(args, "trust", true);
    if (!prompt.empty()) {
        job["prompt"] = prompt;
        const auto options = turn_options(timeout, idle, tools, trust);
        if (flag_arg(args, "wait", false)) {
            const Value result = orch::turn::run_job(job, options);
            Value out = Value::object();
            out["state"] = "prompted";
            out["project"] = name;
            out["cwd"] = field(job, "cwd");
            out["harness"] = field(job, "harness");
        out["model"] = field(job, "model");
            out["session_id"] = or_chain(field(result, "forked_session"), field(result, "resolved_session"),
                                         Value(target_session));
            out["source_session_id"] = target_session;
            out["fork"] = fork;
            out["global"] = global;
            out["result"] = result;
            return dumps_indented(out);
        }
        Value out = Value::object();
        out["state"] = "running";
        out["project"] = name;
        out["cwd"] = field(job, "cwd");
        out["harness"] = field(job, "harness");
        out["model"] = field(job, "model");
        out["session_id"] = target_session;
        out["fork"] = fork;
        out["global"] = global;
        out["turn_id"] = turn_book().start(job, options);
        return dumps_indented(out);
    }

    const Value started = orch::turn::start_one(job, start_settle_seconds, trust, orch::config::default_lock_wait);
    const Value sessions = orch::ccbridge::fetch_sessions();
    const Value info = field(sessions, name);
    const bool alive = core::json::truthy(core::json::member(&info, "alive"));
    Value out = Value::object();
    out["state"] = alive ? "resumed" : "not_alive";
    out["project"] = name;
    out["cwd"] = field(job, "cwd");
    out["harness"] = field(job, "harness");
    out["model"] = field(job, "model");
    out["session_id"] = or_chain(field(started, "forked_session"), field(started, "resolved_session"),
                                 Value(target_session));
    out["source_session_id"] = target_session;
    out["fork"] = fork;
    out["global"] = global;
    out["launch_args"] = or_chain(field(info, "launch_args"), Value(), Value::array());
    return dumps_indented(out);
}

std::string list_project_sessions(const Value& args)
{
    Value rows = Value::array();
    if (flag_arg(args, "all", false)) {
        for (const auto& project : orch::list_projects(true)) {
            for (const auto& harness_name : orch::harness::names()) {
                for (const auto& session : orch::harness::sessions(harness_name, text_of(core::json::member(&project, "cwd")))) {
                    Value base = Value::object();
                    base["project"] = field(project, "name");
                    base["cwd"] = field(project, "cwd");
                    base["harness"] = harness_name;
                    rows.push_back(merged(base, session));
                }
            }
        }
        return dumps_indented(rows);
    }
    orch::ProjectSelector selector;
    selector.project = optional_text_arg(args, "project");
    selector.cwd = optional_text_arg(args, "cwd");
    selector.harness = optional_text_arg(args, "harness");
    const Value target = orch::project_target(selector);
    for (const auto& session : orch::harness::sessions(text_of(core::json::member(&target, "harness")),
                                                      text_of(core::json::member(&target, "cwd")))) {
        Value base = Value::object();
        base["project"] = field(target, "name");
        base["cwd"] = field(target, "cwd");
        base["harness"] = field(target, "harness");
        rows.push_back(merged(base, session));
    }
    return dumps_indented(rows);
}

bool is_pane_rule_or_blank(std::string line)
{
    for (const std::string_view glyph : {std::string_view("─"), std::string_view(" "), std::string_view(" ")}) {
        for (std::size_t at = line.find(glyph); at != std::string::npos; at = line.find(glyph)) {
            line.erase(at, glyph.size());
        }
    }
    return line.empty();
}

std::string rstrip_pane_line(std::string line)
{
    while (true) {
        if (line.ends_with(" ")) {
            line.pop_back();
        } else if (line.ends_with(" ")) {
            line.resize(line.size() - 2);
        } else {
            return line;
        }
    }
}

Value compact_peer(const Value& info, long long pane_lines)
{
    static const std::vector<std::string> fields{"alive", "busy", "harness", "model", "cwd", "clients", "launch_args",
                                                 "output_path", "output_session"};
    Value row = Value::object();
    for (const auto& key : fields) {
        if (const Value* value = core::json::member(&info, key)) {
            row[key] = *value;
        }
    }
    if (pane_lines > 0) {
        std::vector<std::string> content;
        for (auto& line : text_list(core::json::member(&info, "pane_tail"))) {
            if (!is_pane_rule_or_blank(line)) {
                content.push_back(rstrip_pane_line(std::move(line)));
            }
        }
        const std::size_t first = content.size() > static_cast<std::size_t>(pane_lines)
                                      ? content.size() - static_cast<std::size_t>(pane_lines)
                                      : 0;
        Value tail = Value::array();
        for (std::size_t index = first; index < content.size(); ++index) {
            tail.push_back(content[index]);
        }
        row["pane_tail"] = tail;
    }
    return row;
}

std::string peer_status(const Value& args)
{
    const long long pane_lines = int_arg(args, "pane_lines", 0);
    Value sessions = orch::peer_status();
    Value status = Value::object();
    for (auto iterator = sessions.begin(); iterator != sessions.end(); ++iterator) {
        Value& info = iterator.value();
        std::optional<Value> row;
        try {
            row = journal_api::find_session(iterator.key(), optional_text(core::json::member(&info, "cwd")));
        } catch (const std::exception&) {
            row = std::nullopt;
        }
        if (row) {
            info["output_path"] = field(*row, "path");
            info["output_session"] = field(*row, "session");
        }
        status[iterator.key()] = compact_peer(info, pane_lines);
    }
    return dumps_indented(status, true);
}

std::string read_pane(const Value& args)
{
    const std::string name = text_arg(args, "name");
    const std::vector<std::string> lines = orch::read_pane(name);
    return page_of_lines(lines, int_arg(args, "lines", 40));
}

std::string stop_peer(const Value& args)
{
    const std::string name = text_arg(args, "name");
    const Value status = orch::stop_peer(name);
    const std::string state = core::json::py_str(core::json::member(&status, "state"));
    if (state == "server_down") {
        return "server down; " + name + " has no hub";
    }
    if (state == "stopped") {
        return "stopped " + name;
    }
    if (state == "no_hub") {
        return "no hub " + name;
    }
    throw ToolFailure("RuntimeError", "stop " + name + " failed: HTTP "
                                          + core::json::py_str(core::json::member(&status, "http_status")));
}

std::string server_down(const Value&)
{
    const std::size_t running = orch::ccbridge::server_alive() ? orch::ccbridge::fetch_sessions().size() : 0;
    const auto pid_file = orch::config::orch_dir() / "server.pid";
    if (!core::files::is_regular_file(pid_file)) {
        return "no pid file; running sessions=" + std::to_string(running);
    }
    const Value pid_text(std::string(core::text::strip(core::files::read_bytes(pid_file).value_or(""))));
    const long long pid = to_int(&pid_text);
    ::kill(static_cast<pid_t>(pid), SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline && orch::ccbridge::server_alive()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::error_code ignored;
    std::filesystem::remove(pid_file, ignored);
    return "stopped airc server pid=" + std::to_string(pid) + "; ended " + std::to_string(running) + " sessions";
}

}
