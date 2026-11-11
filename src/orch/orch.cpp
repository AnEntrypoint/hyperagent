#include "orch/orch.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <semaphore>
#include <thread>
#include <utility>
#include <vector>

#include "core/clock.hpp"
#include "core/files.hpp"
#include "core/json.hpp"
#include "core/text.hpp"
#include "journal/journal.hpp"
#include "journal/records.hpp"
#include "orch/ccbridge.hpp"
#include "orch/config.hpp"
#include "orch/harness.hpp"
#include "orch/projects.hpp"
#include "orch/state.hpp"
#include "orch/turn.hpp"

namespace hyperagent::orch {

namespace {

std::string text_of(const Value* value)
{
    return core::json::str_or_empty(value);
}

bool present(const std::optional<std::string>& value)
{
    return value && !value->empty();
}

bool peer_present(const std::string& name)
{
    return ccbridge::hub_exists(name);
}

template <typename Work>
std::vector<Value> run_concurrently(std::size_t count, std::size_t limit, Work work)
{
    std::vector<Value> results(count);
    std::vector<std::exception_ptr> failures(count);
    std::counting_semaphore<> gate(static_cast<std::ptrdiff_t>(std::max<std::size_t>(1, limit)));
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

Value session_row(const std::string& project, const std::string& cwd, const std::string& harness_name, const Value& item)
{
    Value row = Value::object();
    row["project"] = project;
    row["cwd"] = cwd;
    row["harness"] = harness_name;
    for (const auto& [key, value] : item.items()) {
        row[key] = value;
    }
    return row;
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

}

Value list_projects(bool include_all)
{
    return projects::discover(include_all);
}

Value project_target(const ProjectSelector& selector)
{
    return projects::project_target(selector);
}

Value list_sessions(const ProjectSelector& selector)
{
    const Value target = projects::project_target(selector);
    const std::string name = text_of(core::json::member(&target, "name"));
    const std::string cwd = text_of(core::json::member(&target, "cwd"));
    const std::string harness_name = text_of(core::json::member(&target, "harness"));
    Value rows = Value::array();
    for (const auto& item : harness::sessions(harness_name, cwd)) {
        rows.push_back(session_row(name, cwd, harness_name, item));
    }
    return rows;
}

Value list_all_sessions()
{
    Value rows = Value::array();
    for (const auto& project : projects::discover(true)) {
        const std::string cwd = text_of(core::json::member(&project, "cwd"));
        for (const auto& harness_name : harness::names()) {
            for (const auto& item : harness::sessions(harness_name, cwd)) {
                rows.push_back(session_row(text_of(core::json::member(&project, "name")), cwd, harness_name, item));
            }
        }
    }
    return rows;
}

void ensure_server()
{
    ccbridge::ensure_server();
}

Value server_up()
{
    ensure_server();
    Value status = Value::object();
    status["url"] = config::base_url();
    status["sessions"] = ccbridge::fetch_sessions().size();
    status["binary"] = ccbridge::server_binary();
    return status;
}

Value peer_status()
{
    ensure_server();
    return ccbridge::fetch_sessions();
}

Value server_down()
{
    Value sessions = Value::object();
    if (ccbridge::server_alive()) {
        sessions = ccbridge::fetch_sessions();
    }
    Value status = Value::object();
    const auto pid_file = config::orch_dir() / "server.pid";
    if (!core::files::is_regular_file(pid_file)) {
        status["state"] = "no_pid_file";
        status["pid_file"] = pid_file.string();
        status["running"] = !sessions.empty();
        return status;
    }
    const auto pid_text = std::string(core::text::strip(core::files::read_bytes(pid_file).value_or("0")));
    ccbridge::stop_server_process();
    status["state"] = "stopped";
    status["pid"] = std::stol(pid_text);
    status["ended"] = sessions.size();
    return status;
}

Value stop_peer(const std::string& name)
{
    config::check_name(name);
    Value status = Value::object();
    if (!ccbridge::server_alive()) {
        status["state"] = "server_down";
        return status;
    }
    const auto code = ccbridge::delete_session(name);
    if (code == 200) {
        status["state"] = "stopped";
    } else if (code == 404) {
        status["state"] = "no_hub";
    } else {
        status["state"] = "failed";
    }
    status["http_status"] = code;
    return status;
}

std::vector<std::string> read_pane(const std::string& name)
{
    config::check_name(name);
    std::vector<std::string> lines;
    for (const auto& line : core::text::splitlines(ccbridge::capture(name))) {
        if (!core::text::strip(line).empty()) {
            lines.emplace_back(core::text::rstrip(line));
        }
    }
    return lines;
}

Value start_peers(const StartRequest& request)
{
    std::vector<std::string> names;
    for (const auto& name : request.names) {
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    if (request.fork && !request.resume) {
        throw Error("--fork needs --resume");
    }
    if (names.empty()) {
        throw Error("start needs one or more project names; see `build/hyperagent orch projects`");
    }
    if (present(request.cwd) && names.size() > 1) {
        throw Error("--cwd applies to a single name");
    }
    ensure_server();

    std::vector<Value> jobs;
    for (const auto& name : names) {
        Value entry = Value::object();
        entry["name"] = name;
        if (names.size() == 1 && present(request.cwd)) {
            entry["cwd"] = *request.cwd;
        }
        if (present(request.harness)) {
            entry["harness"] = *request.harness;
        }
        if (present(request.model)) {
            entry["model"] = *request.model;
        }
        jobs.push_back(projects::normalize(entry, false));
    }
    projects::fill_cwds(jobs);

    std::vector<std::string> unknown;
    for (const auto& job : jobs) {
        if (!core::json::truthy(core::json::member(&job, "cwd"))) {
            unknown.push_back(text_of(core::json::member(&job, "name")));
        }
    }
    if (!unknown.empty()) {
        throw Error("unknown project(s): " + join(unknown, ", ") + "; see build/hyperagent orch projects or pass --cwd");
    }

    if (request.resume) {
        for (auto& job : jobs) {
            const std::string name = text_of(core::json::member(&job, "name"));
            if (peer_present(name)) {
                continue;
            }
            const Value session = projects::latest_session(text_of(core::json::member(&job, "harness")),
                                                           text_of(core::json::member(&job, "cwd")));
            if (!session.is_null()) {
                job["resume"] = text_of(core::json::member(&session, "session_id"));
                if (request.fork) {
                    job["fork"] = true;
                }
            }
        }
    }

    const std::size_t limit = request.concurrency > 0 ? static_cast<std::size_t>(request.concurrency) : jobs.size();
    const auto started = run_concurrently(jobs.size(), limit, [&](std::size_t index) {
        return turn::start_one(jobs[index], request.settle, request.trust, config::default_lock_wait);
    });

    const Value sessions = ccbridge::fetch_sessions();
    Value peers = Value::array();
    std::size_t alive = 0;
    for (const auto& job : started) {
        const std::string name = text_of(core::json::member(&job, "name"));
        const Value* info = core::json::member(&sessions, name);
        const bool up = info != nullptr && core::json::truthy(core::json::member(info, "alive"));
        alive += up ? 1 : 0;
        Value peer = Value::object();
        peer["name"] = name;
        peer["harness"] = text_of(core::json::member(&job, "harness"));
        peer["model"] = text_of(core::json::member(&job, "model"));
        const std::string cwd = text_of(core::json::member(&job, "cwd"));
        peer["cwd"] = cwd.empty() ? config::root_dir().string() : cwd;
        peer["state"] = up ? "started" : "not alive";
        peer["trusted"] = core::json::truthy(core::json::member(&job, "trusted"));
        peers.push_back(peer);
    }
    Value summary = Value::object();
    summary["peers"] = peers;
    summary["alive"] = alive;
    summary["total"] = started.size();
    return summary;
}

Value prompt_peer(const PromptRequest& request, const TurnOptions& options)
{
    ensure_server();
    Value entry = Value::object();
    entry["name"] = request.name;
    if (present(request.cwd)) {
        entry["cwd"] = *request.cwd;
    }
    if (present(request.harness)) {
        entry["harness"] = *request.harness;
    }
    if (present(request.model)) {
        entry["model"] = *request.model;
    }
    entry["prompt"] = request.text;
    Value job = projects::normalize(entry, true);
    std::vector<Value> jobs{job};
    projects::fill_cwds(jobs);
    job = jobs.front();
    if (!core::json::truthy(core::json::member(&job, "cwd"))) {
        throw Error("unknown project " + config::python_repr(request.name)
                    + "; see build/hyperagent orch projects or pass --cwd");
    }

    if (present(request.session)) {
        job["resume"] = config::check_session_id(*request.session);
        std::string kind;
        try {
            kind = harness::selector_kind(text_of(core::json::member(&job, "harness")), *request.session);
        } catch (const std::exception&) {
            kind.clear();
        }
        if (kind == "name" || kind == "path") {
            job["record_latest"] = true;
        }
    } else if (request.resume && !peer_present(request.name)) {
        const Value session = projects::latest_session(text_of(core::json::member(&job, "harness")),
                                                       text_of(core::json::member(&job, "cwd")));
        if (!session.is_null()) {
            job["resume"] = text_of(core::json::member(&session, "session_id"));
        }
    }
    if (request.fork) {
        if (!core::json::truthy(core::json::member(&job, "resume")) && !core::json::truthy(core::json::member(&job, "continue"))) {
            throw Error("--fork needs --resume or --session");
        }
        job["fork"] = true;
    }

    Value result = turn::run_job(job, options);
    std::vector<Value> results{result};
    projects::live_cwds(results);
    return results.front();
}

Value run_turns(const Value& entries, const TurnOptions& options)
{
    ensure_server();
    std::vector<Value> jobs;
    for (const auto& entry : entries) {
        jobs.push_back(projects::normalize(entry, true));
    }
    projects::fill_cwds(jobs);
    std::vector<std::string> unknown;
    for (const auto& job : jobs) {
        if (!core::json::truthy(core::json::member(&job, "cwd"))) {
            unknown.push_back(text_of(core::json::member(&job, "name")));
        }
    }
    if (!unknown.empty()) {
        throw Error("unknown project(s): " + join(unknown, ", ") + "; see build/hyperagent orch projects or give each job a cwd");
    }
    auto results = run_concurrently(jobs.size(), jobs.size(), [&](std::size_t index) {
        return turn::run_job(jobs[index], options);
    });
    projects::live_cwds(results);
    Value out = Value::array();
    for (const auto& result : results) {
        out.push_back(result);
    }
    return out;
}

Value resume_peer(const ResumeRequest& request, const TurnOptions& options)
{
    const std::string prompt = request.prompt.value_or("");
    const std::optional<std::string> session_id = present(request.session)
        ? std::optional<std::string>(config::check_session_id(*request.session)) : std::nullopt;
    const Value target = projects::resume_target(request.target, session_id);
    const Value& source = *core::json::member(&target, "session");
    const std::string source_id = text_of(core::json::member(&source, "session_id"));
    const std::string name = text_of(core::json::member(&target, "name"));
    const std::string harness_name = text_of(core::json::member(&target, "harness"));
    const std::string cwd = text_of(core::json::member(&target, "cwd"));
    const bool global = core::json::truthy(core::json::member(&source, "global"));

    Value job = Value::object();
    job["name"] = name;
    job["harness"] = harness_name;
    job["harness_explicit"] = present(request.target.harness);
    if (present(request.model)) {
        job["model"] = config::check_model(*request.model);
    }
    job["cwd"] = cwd;
    job["resume"] = source_id;
    const std::string selector = text_of(core::json::member(&source, "selector"));
    if (selector == "name" || selector == "path") {
        job["record_latest"] = true;
    }
    if (request.continue_last) {
        job.erase("resume");
        job["continue"] = true;
    }
    if (request.fork) {
        job["fork"] = true;
    }
    if (!prompt.empty()) {
        job["prompt"] = prompt;
    }
    std::vector<Value> jobs{job};
    projects::fill_cwds(jobs);
    job = jobs.front();
    ensure_server();

    Value outcome = Value::object();
    if (peer_present(name) && !session_id && !request.continue_last && !request.fork) {
        const Value sessions = ccbridge::fetch_sessions();
        const Value* info = core::json::member(&sessions, name);
        const Value* live_harness = core::json::member(info, "harness");
        const Value* live_model = core::json::member(info, "model");
        const bool same_harness = info == nullptr || live_harness == nullptr || live_harness->is_null()
            || text_of(live_harness) == harness_name;
        const bool same_model = !present(request.model) || live_model == nullptr || live_model->is_null()
            || text_of(live_model) == *request.model;
        if (same_harness && same_model) {
            Value payload = Value::object();
            payload["state"] = "already_alive";
            payload["project"] = name;
            payload["cwd"] = cwd;
            payload["harness"] = harness_name;
            payload["model"] = text_of(core::json::member(&job, "model"));
            payload["session_id"] = source_id;
            const Value* launch = core::json::member(info, "launch_args");
            payload["launch_args"] = launch != nullptr ? *launch : Value::array();
            outcome["mode"] = "already_alive";
            outcome["exit_code"] = 0;
            outcome["payload"] = payload;
            return outcome;
        }
    }

    if (!prompt.empty()) {
        Value result = turn::run_job(job, options);
        std::vector<Value> results{result};
        projects::live_cwds(results);
        result = results.front();
        const std::string forked = text_of(core::json::member(&result, "forked_session"));
        const std::string resolved = text_of(core::json::member(&result, "resolved_session"));
        Value payload = Value::object();
        payload["session_id"] = !forked.empty() ? forked : (!resolved.empty() ? resolved : source_id);
        payload["source_session_id"] = source_id;
        payload["fork"] = request.fork;
        payload["global"] = global;
        payload["result"] = result;
        outcome["mode"] = "prompted";
        outcome["exit_code"] = turn_exit_code(result);
        outcome["payload"] = payload;
        outcome["result"] = result;
        return outcome;
    }

    const Value started = turn::start_one(job, request.settle, request.trust, config::default_lock_wait);
    const Value sessions = ccbridge::fetch_sessions();
    const Value* info = core::json::member(&sessions, name);
    const bool alive = info != nullptr && core::json::truthy(core::json::member(info, "alive"));
    const std::string forked = text_of(core::json::member(&started, "forked_session"));
    const std::string resolved = text_of(core::json::member(&started, "resolved_session"));
    Value payload = Value::object();
    payload["state"] = alive ? "resumed" : "not alive";
    payload["project"] = name;
    payload["cwd"] = cwd;
    payload["harness"] = harness_name;
    payload["model"] = text_of(core::json::member(&job, "model"));
    payload["session_id"] = !forked.empty() ? forked : (!resolved.empty() ? resolved : source_id);
    payload["source_session_id"] = source_id;
    payload["fork"] = request.fork;
    payload["global"] = global;
    const Value* launch = core::json::member(info, "launch_args");
    payload["launch_args"] = launch != nullptr ? *launch : Value::array();
    payload["trusted"] = core::json::truthy(core::json::member(&started, "trusted"));
    outcome["mode"] = "started";
    outcome["exit_code"] = alive ? 0 : 3;
    outcome["payload"] = payload;
    return outcome;
}

std::string render_turn(const Value& result, bool tools)
{
    const std::string status = text_of(core::json::member(&result, "status"));
    const std::string duration = core::json::float_repr(core::json::member(&result, "duration_s")->get<double>());
    std::vector<std::string> lines;
    lines.push_back("=== " + text_of(core::json::member(&result, "name")) + " [" + status + "] " + duration
                    + "s harness=" + text_of(core::json::member(&result, "harness"))
                    + " cwd=" + text_of(core::json::member(&result, "cwd")) + " ===");
    const std::string text(core::text::strip(text_of(core::json::member(&result, "text"))));
    lines.push_back(text.empty() ? "(no text)" : text);
    const Value* names = core::json::member(&result, "tool_names");
    if (tools && names != nullptr && !names->empty()) {
        std::vector<std::string> items;
        for (const auto& item : *names) {
            items.push_back(core::json::py_str(&item));
        }
        lines.push_back("[orch] tools: " + join(items, ", "));
    }
    if (status != "turn_end") {
        if (!core::json::truthy(core::json::member(&result, "acked"))) {
            lines.push_back("[orch] prompt was not echoed into the transcript");
        }
        const Value* tail = core::json::member(&result, "pane_tail");
        if (tail != nullptr && !tail->empty()) {
            lines.push_back("[orch] pane tail:");
            for (const auto& line : *tail) {
                lines.push_back("| " + core::json::py_str(&line));
            }
        }
    }
    const std::string output = text_of(core::json::member(&result, "output_path"));
    if (!output.empty()) {
        lines.push_back("[orch] output " + output);
    }
    return join(lines, "\n");
}

int turn_exit_code(const Value& result)
{
    const std::string status = text_of(core::json::member(&result, "status"));
    if (status == "turn_end") {
        return 0;
    }
    return status == "timeout" || status == "blocked" ? 2 : 3;
}

Value list_output_sessions(const std::optional<std::string>& project, bool all, long limit)
{
    std::optional<std::string> cwd;
    if (!all) {
        ProjectSelector selector;
        selector.project = project;
        const Value target = projects::project_target(selector);
        cwd = text_of(core::json::member(&target, "cwd"));
    }
    Value rows = Value::array();
    for (const auto& row : journal::scan(std::nullopt, cwd, std::nullopt, std::nullopt,
                                         limit > 0 ? static_cast<std::size_t>(limit) : 0)) {
        rows.push_back(row);
    }
    return rows;
}

Value read_output_session(const std::optional<std::string>& project, const std::optional<std::string>& agent,
                          const std::optional<std::string>& session_id, long tail,
                          const std::vector<std::string>& kinds)
{
    std::optional<std::string> cwd;
    if (present(project)) {
        ProjectSelector selector;
        selector.project = project;
        const Value target = projects::project_target(selector);
        cwd = text_of(core::json::member(&target, "cwd"));
    }
    const auto row = journal::find_session(agent, cwd, session_id);
    if (!row) {
        throw Error("no session output found");
    }
    Value records = Value::array();
    for (const auto& record : journal::tail_records(row->at("path").get<std::string>(), tail, kinds)) {
        records.push_back(record);
    }
    Value out = Value::object();
    out["row"] = *row;
    out["records"] = records;
    return out;
}

}
