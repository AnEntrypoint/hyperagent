#include "orch/command.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/json.hpp"
#include "core/text.hpp"
#include "journal/records.hpp"
#include "orch/ccbridge.hpp"
#include "orch/config.hpp"
#include "orch/help.hpp"
#include "orch/orch.hpp"
#include "orch/projects.hpp"

namespace hyperagent::orch {

namespace {

using core::json::PythonStyle;

std::string text_of(const Value* value)
{
    return core::json::str_or_empty(value);
}

std::optional<std::string> optional_of(const std::string& value)
{
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::string python_list_repr(const Value* items)
{
    std::string out = "[";
    if (items != nullptr && items->is_array()) {
        bool first = true;
        for (const auto& item : *items) {
            out += first ? "" : ", ";
            first = false;
            out += config::python_repr(core::json::py_str(&item));
        }
    }
    return out + "]";
}

std::string yes_no(bool value)
{
    return value ? "yes" : "no";
}

void print_line(const std::string& text)
{
    std::cout << text << "\n";
}

void print_error(const std::string& text)
{
    std::cerr << text << "\n";
}

std::string json_text(const Value& value)
{
    return core::json::python_dumps(value, {2, false, true});
}

std::string stamp_text(double epoch)
{
    const auto moment = static_cast<std::time_t>(std::floor(epoch));
    std::tm local{};
    localtime_r(&moment, &local);
    char buffer[64];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

int finish(int code)
{
    std::cout.flush();
    std::cerr.flush();
    return code;
}

int cmd_up()
{
    ensure_server();
    const Value sessions = ccbridge::fetch_sessions();
    print_line("airc server up at " + config::base_url() + " (" + std::to_string(sessions.size())
               + " sessions) binary=" + ccbridge::server_binary());
    return 0;
}

int cmd_down()
{
    const Value status = server_down();
    if (text_of(core::json::member(&status, "state")) == "no_pid_file") {
        const bool running = core::json::truthy(core::json::member(&status, "running"));
        print_line("no pid file at " + text_of(core::json::member(&status, "pid_file")) + "; server running="
                   + (running ? "True" : "False"));
        return running ? 3 : 0;
    }
    print_line("stopped airc server pid=" + core::json::py_str(core::json::member(&status, "pid")) + "; ended "
               + core::json::py_str(core::json::member(&status, "ended")) + " sessions");
    return 0;
}

int cmd_status(bool as_json, bool panes)
{
    ensure_server();
    const Value sessions = ccbridge::fetch_sessions();
    if (as_json) {
        print_line(core::json::python_dumps(sessions, {2, true, true}));
        return 0;
    }
    if (sessions.empty()) {
        print_line("no sessions");
        return 0;
    }
    std::vector<std::string> names;
    for (const auto& [name, info] : sessions.items()) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        const Value* info = core::json::member(&sessions, name);
        print_line(name + "  alive=" + yes_no(core::json::truthy(core::json::member(info, "alive")))
                   + " busy=" + yes_no(core::json::truthy(core::json::member(info, "busy")))
                   + " harness=" + core::json::py_str(core::json::member(info, "harness"))
                   + " clients=" + core::json::py_str(core::json::member(info, "clients"))
                   + " cwd=" + core::json::py_str(core::json::member(info, "cwd")));
        if (panes) {
            const Value* tail = core::json::member(info, "pane_tail");
            if (tail != nullptr && tail->is_array()) {
                for (const auto& line : *tail) {
                    print_line("  | " + core::json::py_str(&line));
                }
            }
        }
    }
    return 0;
}

int cmd_projects(bool include_all, bool as_json)
{
    const Value jobs = list_projects(include_all);
    if (as_json) {
        print_line(json_text(jobs));
    } else {
        for (const auto& job : jobs) {
            print_line(text_of(core::json::member(&job, "name")) + "\t" + text_of(core::json::member(&job, "cwd")));
        }
        print_error(std::to_string(jobs.size()) + " projects under " + config::parent_dir().string());
    }
    return 0;
}

int cmd_start(const StartRequest& request)
{
    const Value summary = start_peers(request);
    for (const auto& peer : *core::json::member(&summary, "peers")) {
        const std::string suffix = core::json::truthy(core::json::member(&peer, "trusted")) ? " trust=clear" : "";
        const std::string saved_model = text_of(core::json::member(&peer, "model"));
        const std::string model = saved_model.empty() ? "" : " model=" + saved_model;
        print_line(text_of(core::json::member(&peer, "name")) + ": " + text_of(core::json::member(&peer, "state"))
                   + " harness=" + text_of(core::json::member(&peer, "harness")) + " cwd="
                   + text_of(core::json::member(&peer, "cwd")) + model + suffix);
    }
    const auto alive = core::json::member(&summary, "alive")->get<std::size_t>();
    const auto total = core::json::member(&summary, "total")->get<std::size_t>();
    print_line(std::to_string(alive) + "/" + std::to_string(total) + " peers alive");
    return alive == total ? 0 : 3;
}

int cmd_prompt(const PromptRequest& request, const TurnOptions& options, bool as_json, bool tools)
{
    const Value result = prompt_peer(request, options);
    const std::string status = text_of(core::json::member(&result, "status"));
    if (as_json) {
        print_line(json_text(result));
    } else if (status == "turn_end") {
        print_line(std::string(core::text::strip(text_of(core::json::member(&result, "text")))));
    } else {
        print_line(render_turn(result, tools));
    }
    return turn_exit_code(result);
}

int cmd_fanout(const std::string& file, const TurnOptions& options, bool as_json, bool tools)
{
    const Value results = run_turns(projects::read_entries(file, "jobs"), options);
    if (as_json) {
        print_line(json_text(results));
    } else {
        std::string joined;
        for (const auto& result : results) {
            joined += (joined.empty() ? "" : "\n\n") + render_turn(result, tools);
        }
        print_line(joined);
    }
    std::set<std::string> statuses;
    for (const auto& result : results) {
        statuses.insert(text_of(core::json::member(&result, "status")));
    }
    if (statuses == std::set<std::string>{"turn_end"}) {
        return 0;
    }
    const bool stopped = statuses.contains("timeout") || statuses.contains("blocked");
    return stopped ? 2 : 3;
}

int cmd_resume(const ResumeRequest& request, const TurnOptions& options, bool as_json, bool tools)
{
    const Value outcome = resume_peer(request, options);
    const std::string mode = text_of(core::json::member(&outcome, "mode"));
    const int code = static_cast<int>(core::json::member(&outcome, "exit_code")->get<long>());
    const Value& payload = *core::json::member(&outcome, "payload");
    if (mode == "already_alive") {
        if (as_json) {
            print_line(json_text(payload));
        } else {
            print_line(text_of(core::json::member(&payload, "project")) + ": already alive cwd="
                       + text_of(core::json::member(&payload, "cwd")) + " session="
                       + text_of(core::json::member(&payload, "session_id")));
        }
        return code;
    }
    if (mode == "prompted") {
        const Value& result = *core::json::member(&outcome, "result");
        if (as_json) {
            print_line(json_text(payload));
        } else if (text_of(core::json::member(&result, "status")) == "turn_end") {
            print_line(std::string(core::text::strip(text_of(core::json::member(&result, "text")))));
        } else {
            print_line(render_turn(result, tools));
        }
        return code;
    }
    if (as_json) {
        print_line(json_text(payload));
    } else {
        print_line(text_of(core::json::member(&payload, "project")) + ": " + text_of(core::json::member(&payload, "state"))
                   + " session=" + text_of(core::json::member(&payload, "source_session_id")) + " launch="
                   + python_list_repr(core::json::member(&payload, "launch_args")) + " cwd="
                   + text_of(core::json::member(&payload, "cwd")));
    }
    return code;
}

int cmd_sessions(const std::optional<ProjectSelector>& selector, bool as_json)
{
    const Value rows = selector ? list_sessions(*selector) : list_all_sessions();
    if (as_json) {
        print_line(json_text(rows));
    } else {
        for (const auto& row : rows) {
            print_line(text_of(core::json::member(&row, "project")) + "\t" + text_of(core::json::member(&row, "harness"))
                       + "\t" + text_of(core::json::member(&row, "session_id")) + "\t"
                       + stamp_text(core::json::member(&row, "mtime")->get<double>()) + "\t"
                       + core::json::py_str(core::json::member(&row, "size")) + "\t"
                       + text_of(core::json::member(&row, "path")));
        }
        print_error(std::to_string(rows.size()) + " sessions");
    }
    return rows.empty() ? 3 : 0;
}

int cmd_stop(const std::string& name)
{
    const Value status = stop_peer(name);
    const std::string state = text_of(core::json::member(&status, "state"));
    if (state == "server_down") {
        print_line("server down; " + name + " has no hub");
        return 0;
    }
    if (state == "stopped") {
        print_line("stopped " + name);
        return 0;
    }
    if (state == "no_hub") {
        print_line("no hub " + name);
        return 0;
    }
    print_line("stop " + name + " failed: HTTP " + core::json::py_str(core::json::member(&status, "http_status")));
    return 3;
}

int cmd_pane(const std::string& name, long lines)
{
    const auto all = read_pane(name);
    const auto size = static_cast<long>(all.size());
    long begin = 0;
    if (lines > 0) {
        begin = std::max<long>(0, size - lines);
    } else if (lines < 0) {
        begin = std::min<long>(-lines, size);
    }
    for (long index = begin; index < size; ++index) {
        print_line(all[static_cast<std::size_t>(index)]);
    }
    return all.empty() ? 3 : 0;
}

int cmd_outputs(const std::optional<std::string>& name, bool all, long limit, bool as_json)
{
    const Value rows = list_output_sessions(name, all, limit);
    if (as_json) {
        print_line(json_text(rows));
    } else {
        for (const auto& row : rows) {
            print_line(core::json::py_str(core::json::member(&row, "agent")) + "\t"
                       + core::json::py_str(core::json::member(&row, "session")) + "\t"
                       + core::json::py_str(core::json::member(&row, "last_kind")) + "\t"
                       + core::json::py_str(core::json::member(&row, "bytes")) + "\t"
                       + core::json::py_str(core::json::member(&row, "last_ts")) + "\t"
                       + core::json::py_str(core::json::member(&row, "path")));
        }
        print_error(std::to_string(rows.size()) + " session outputs");
    }
    return rows.empty() ? 3 : 0;
}

int cmd_output(const std::optional<std::string>& name, const std::optional<std::string>& agent,
               const std::optional<std::string>& session_id, long tail, const std::vector<std::string>& kinds,
               bool as_json)
{
    const Value outcome = read_output_session(name, agent, session_id, tail, kinds);
    const Value& records = *core::json::member(&outcome, "records");
    if (as_json) {
        print_line(json_text(records));
        return 0;
    }
    const Value& row = *core::json::member(&outcome, "row");
    const std::string header = "# " + core::json::py_str(core::json::member(&row, "agent")) + " "
        + core::json::py_str(core::json::member(&row, "session")) + " "
        + core::json::py_str(core::json::member(&row, "path"));
    print_line(journal::render_records(
        std::vector<Value>(records.begin(), records.end()), header));
    return 0;
}

int guarded(const std::function<int()>& work)
{
    try {
        return finish(work());
    } catch (const Error& error) {
        print_error(error.what());
        return finish(1);
    } catch (const std::exception& error) {
        print_error(error.what());
        return finish(1);
    }
}

void exit_with(int code)
{
    if (code != 0) {
        std::exit(code);
    }
}

}

void add_command(CLI::App& app)
{
    auto* orch = app.add_subcommand("orch", "orchestrator CLI for airc harness peers");
    orch->require_subcommand(1);

    {
        auto* command = orch->add_subcommand("up", "start the airc server if it is not running");
        command->callback([]() { exit_with(guarded(cmd_up)); });
    }
    {
        auto* command = orch->add_subcommand("down", "stop the airc server and every session it owns");
        command->callback([]() { exit_with(guarded(cmd_down)); });
    }
    {
        auto status = std::make_shared<std::pair<bool, bool>>(false, false);
        auto* command = orch->add_subcommand("status", "list live sessions with ready/busy state");
        command->add_flag("--json", status->first);
        command->add_flag("--panes", status->second);
        command->callback([status]() {
            exit_with(guarded([status]() { return cmd_status(status->first, status->second); }));
        });
    }
    {
        auto flags = std::make_shared<std::pair<bool, bool>>(false, false);
        auto* command = orch->add_subcommand("projects", "list projects in the parent path except this one");
        command->add_flag("--all", flags->first);
        command->add_flag("--json", flags->second);
        command->callback([flags]() {
            exit_with(guarded([flags]() { return cmd_projects(flags->first, flags->second); }));
        });
    }
    {
        auto request = std::make_shared<StartRequest>();
        auto no_trust = std::make_shared<bool>(false);
        auto* command = orch->add_subcommand("start", "start one or more named project peers");
        command->add_option("names", request->names, "project names");
        command->add_option("--cwd", request->cwd);
        command->add_option("--harness", request->harness, "harness for the peer; defaults to the project's remembered harness");
        command->add_option("--model", request->model, "model for the peer; saved with the project and reused on every launch, \"default\" forgets it");
        command->add_option("--settle", request->settle);
        command->add_option("--concurrency", request->concurrency);
        command->add_flag("--resume", request->resume, "resume each project's latest session when it is not running");
        command->add_flag("--fork", request->fork, "fork the resumed session instead of reusing its id");
        command->add_flag("--no-trust", *no_trust);
        command->callback([request, no_trust]() {
            request->trust = !*no_trust;
            exit_with(guarded([request]() { return cmd_start(*request); }));
        });
    }
    {
        auto name = std::make_shared<std::string>();
        auto text = std::make_shared<std::vector<std::string>>();
        auto request = std::make_shared<PromptRequest>();
        auto options = std::make_shared<TurnOptions>();
        auto as_json = std::make_shared<bool>(false);
        auto tools = std::make_shared<bool>(false);
        auto no_trust = std::make_shared<bool>(false);
        auto session = std::make_shared<std::string>();
        auto* command = orch->add_subcommand("prompt", "start or prompt one session and wait for its turn");
        command->add_option("name", *name)->required();
        command->add_option("text", *text)->required();
        command->add_option("--cwd", request->cwd);
        command->add_option("--harness", request->harness, "harness for the peer; defaults to the project's remembered harness");
        command->add_option("--model", request->model, "model for the peer; saved with the project and reused on every launch, \"default\" forgets it");
        command->add_option("--timeout", options->timing.timeout);
        command->add_option("--idle-exit", options->timing.idle_exit);
        command->add_option("--lock-wait", options->timing.lock_wait);
        command->add_flag("--tools", *tools);
        command->add_flag("--json", *as_json);
        command->add_flag("--resume", request->resume, "resume the project's latest session when it is not running");
        command->add_option("--session", *session, "resume this harness session id before prompting");
        command->add_flag("--fork", request->fork, "fork the resumed session instead of reusing its id");
        command->add_flag("--no-trust", *no_trust);
        command->callback([=]() {
            request->name = *name;
            std::string joined;
            for (const auto& part : *text) {
                joined += (joined.empty() ? "" : " ") + part;
            }
            request->text = joined;
            request->session = optional_of(*session);
            options->tools = *tools;
            options->trust = !*no_trust;
            exit_with(guarded([=]() { return cmd_prompt(*request, *options, *as_json, *tools); }));
        });
    }
    {
        auto file = std::make_shared<std::string>();
        auto options = std::make_shared<TurnOptions>();
        auto as_json = std::make_shared<bool>(false);
        auto tools = std::make_shared<bool>(false);
        auto no_trust = std::make_shared<bool>(false);
        auto* command = orch->add_subcommand("fanout", "prompt many sessions concurrently and collect answers");
        command->add_option("file", *file)->required();
        command->add_option("--timeout", options->timing.timeout);
        command->add_option("--idle-exit", options->timing.idle_exit);
        command->add_option("--lock-wait", options->timing.lock_wait);
        command->add_flag("--tools", *tools);
        command->add_flag("--json", *as_json);
        command->add_flag("--no-trust", *no_trust);
        command->callback([=]() {
            options->tools = *tools;
            options->trust = !*no_trust;
            exit_with(guarded([=]() { return cmd_fanout(*file, *options, *as_json, *tools); }));
        });
    }
    {
        auto request = std::make_shared<ResumeRequest>();
        auto name = std::make_shared<std::string>();
        auto session = std::make_shared<std::string>();
        auto prompt = std::make_shared<std::vector<std::string>>();
        auto options = std::make_shared<TurnOptions>();
        auto as_json = std::make_shared<bool>(false);
        auto tools = std::make_shared<bool>(false);
        auto no_trust = std::make_shared<bool>(false);
        auto* command = orch->add_subcommand("resume", "resume a project's session; default is the last project");
        command->add_option("name", *name, "project name; omitted resumes the last project");
        command->add_option("--session", *session, "specific harness session id");
        command->add_option("--cwd", request->target.cwd, "project directory when the name is not discovered");
        command->add_flag("--continue", request->continue_last, "use the harness's continue flag instead of a session id");
        command->add_flag("--fork", request->fork, "fork the session instead of reusing its id");
        command->add_option("--prompt", *prompt, "prompt to send after resuming");
        command->add_option("--harness", request->target.harness, "harness to resume; defaults to the project's remembered harness");
        command->add_option("--model", request->model, "model for the peer; saved with the project and reused on every launch, \"default\" forgets it");
        command->add_option("--timeout", options->timing.timeout);
        command->add_option("--idle-exit", options->timing.idle_exit);
        command->add_option("--lock-wait", options->timing.lock_wait);
        command->add_option("--settle", request->settle);
        command->add_flag("--tools", *tools);
        command->add_flag("--json", *as_json);
        command->add_flag("--no-trust", *no_trust);
        command->callback([=]() {
            request->target.project = optional_of(*name);
            request->session = optional_of(*session);
            if (!prompt->empty()) {
                std::string joined;
                for (const auto& part : *prompt) {
                    joined += (joined.empty() ? "" : " ") + part;
                }
                request->prompt = joined;
            }
            options->tools = *tools;
            options->trust = !*no_trust;
            request->trust = !*no_trust;
            exit_with(guarded([=]() { return cmd_resume(*request, *options, *as_json, *tools); }));
        });
    }
    {
        auto selector = std::make_shared<ProjectSelector>();
        auto name = std::make_shared<std::string>();
        auto all = std::make_shared<bool>(false);
        auto as_json = std::make_shared<bool>(false);
        auto* command = orch->add_subcommand("sessions", "list resumable harness sessions");
        command->add_option("name", *name, "project name; omitted uses the last project");
        command->add_option("--cwd", selector->cwd, "project directory when the name is not discovered");
        command->add_option("--harness", selector->harness, "harness to list; defaults to the project's remembered harness");
        command->add_flag("--all", *all, "list sessions for every discovered project and harness");
        command->add_flag("--json", *as_json);
        command->callback([=]() {
            selector->project = optional_of(*name);
            exit_with(guarded([=]() {
                return cmd_sessions(*all ? std::nullopt : std::optional<ProjectSelector>(*selector), *as_json);
            }));
        });
    }
    {
        auto name = std::make_shared<std::string>();
        auto* command = orch->add_subcommand("stop", "stop one session");
        command->add_option("name", *name)->required();
        command->callback([name]() { exit_with(guarded([name]() { return cmd_stop(*name); })); });
    }
    {
        auto name = std::make_shared<std::string>();
        auto lines = std::make_shared<long>(40);
        auto* command = orch->add_subcommand("pane", "print the tail of a session's terminal pane");
        command->add_option("name", *name)->required();
        command->add_option("--lines", *lines);
        command->callback([name, lines]() { exit_with(guarded([name, lines]() { return cmd_pane(*name, *lines); })); });
    }
    {
        auto name = std::make_shared<std::string>();
        auto all = std::make_shared<bool>(false);
        auto limit = std::make_shared<long>(50);
        auto as_json = std::make_shared<bool>(false);
        auto* command = orch->add_subcommand("outputs", "list journaled session output files");
        command->add_option("name", *name, "project name; omitted uses the last project");
        command->add_flag("--all", *all);
        command->add_option("--limit", *limit);
        command->add_flag("--json", *as_json);
        command->callback([=]() {
            exit_with(guarded([=]() { return cmd_outputs(optional_of(*name), *all, *limit, *as_json); }));
        });
    }
    {
        auto name = std::make_shared<std::string>();
        auto agent = std::make_shared<std::string>();
        auto session = std::make_shared<std::string>();
        auto tail = std::make_shared<long>(40);
        auto kinds = std::make_shared<std::vector<std::string>>();
        auto as_json = std::make_shared<bool>(false);
        auto* command = orch->add_subcommand("output", "read one journaled session output");
        command->add_option("name", *name, "project name; omitted searches every project");
        command->add_option("--agent", *agent);
        command->add_option("--session", *session);
        command->add_option("--tail", *tail);
        command->add_option("--kind", *kinds);
        command->add_flag("--json", *as_json);
        command->callback([=]() {
            exit_with(guarded([=]() {
                return cmd_output(optional_of(*name), optional_of(*agent), optional_of(*session), *tail, *kinds,
                                  *as_json);
            }));
        });
    }

    for (auto* command : orch->get_subcommands([](CLI::App*) { return true; })) {
        command->formatter(help::make_formatter());
    }
}

}
