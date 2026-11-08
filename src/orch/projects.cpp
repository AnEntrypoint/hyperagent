#include "orch/projects.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

#include "core/files.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "orch/ccbridge.hpp"
#include "orch/config.hpp"
#include "orch/harness.hpp"
#include "orch/state.hpp"

namespace hyperagent::orch::projects {

namespace {

constexpr std::string_view project_markers[] = {
    "AGENTS.md", "CLAUDE.md", "package.json", "pyproject.toml", "setup.py",
    "Cargo.toml", "go.mod", "CMakeLists.txt", "Makefile", "requirements.txt",
    "Gemfile", "composer.json", "pom.xml", "build.gradle", "meson.build", "deno.json",
};

constexpr std::string_view project_suffixes[] = {".sln", ".csproj", ".fsproj", ".xcodeproj", ".uproject", ".cabal"};

bool present(const std::optional<std::string>& value)
{
    return value && !value->empty();
}

std::string text_of(const Value* value)
{
    return core::json::str_or_empty(value);
}

std::string lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });
    return text;
}

std::string path_suffix(const std::string& name)
{
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot == name.size() - 1) {
        return "";
    }
    return name.substr(dot);
}

std::string base_name(const std::string& cwd)
{
    std::string trimmed = cwd;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    const auto slash = trimmed.rfind('/');
    return slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
}

bool is_project(const std::filesystem::path& path)
{
    std::error_code ignored;
    if (std::filesystem::exists(path / ".git", ignored)) {
        return true;
    }
    for (const auto marker : project_markers) {
        if (std::filesystem::exists(path / marker, ignored)) {
            return true;
        }
    }
    if (std::filesystem::is_directory(path / "src", ignored)) {
        return true;
    }
    std::filesystem::directory_iterator entries(path, ignored);
    if (ignored) {
        return false;
    }
    for (const auto& entry : entries) {
        const auto suffix = path_suffix(entry.path().filename().string());
        for (const auto known : project_suffixes) {
            if (suffix == known) {
                return true;
            }
        }
    }
    return false;
}

Value project_record(const std::string& name, const std::string& cwd, const std::string& harness)
{
    Value record = Value::object();
    record["name"] = name;
    record["cwd"] = cwd;
    record["harness"] = harness;
    return record;
}

std::optional<std::string> known_cwd(const Value& listing, const std::string& name)
{
    for (const auto& item : listing) {
        if (text_of(core::json::member(&item, "name")) == name) {
            return text_of(core::json::member(&item, "cwd"));
        }
    }
    return std::nullopt;
}

Value session_by_id(const Value& sessions, const std::string& session_id)
{
    for (const auto& item : sessions) {
        if (text_of(core::json::member(&item, "session_id")) == session_id) {
            return item;
        }
    }
    return Value(nullptr);
}

std::string errno_message(int code, const std::string& path)
{
    return "[Errno " + std::to_string(code) + "] " + std::strerror(code) + ": " + config::python_repr(path);
}

}

Value discover(bool include_all)
{
    const auto parent = config::parent_dir();
    if (!core::files::is_directory(parent)) {
        throw Error("parent path not found: " + parent.string());
    }
    std::vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
        entries.push_back(entry.path());
    }
    std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return lowered(left.filename().string()) < lowered(right.filename().string());
    });

    Value found = Value::array();
    std::map<std::string, bool> used;
    const auto root = config::root_dir();
    for (const auto& path : entries) {
        const std::string leaf = path.filename().string();
        if (!core::files::is_directory(path) || leaf.starts_with('.')) {
            continue;
        }
        if (core::paths::resolve(path) == root) {
            continue;
        }
        if (!include_all && !is_project(path)) {
            continue;
        }
        const std::string name = config::safe_name(leaf);
        std::string unique = name;
        int counter = 2;
        while (used.contains(unique)) {
            unique = name + "-" + std::to_string(counter);
            ++counter;
        }
        used[unique] = true;
        found.push_back(project_record(unique, core::paths::resolve(path).string(), std::string(config::default_harness)));
    }
    return found;
}

Value latest_session(const std::string& harness_name, const std::string& cwd)
{
    const Value found = harness::sessions(harness_name, cwd);
    if (found.empty()) {
        return Value(nullptr);
    }
    return found.back();
}

Value newest_project()
{
    Value best(nullptr);
    double best_mtime = 0.0;
    for (const auto& project : discover(true)) {
        const std::string cwd = text_of(core::json::member(&project, "cwd"));
        for (const auto& harness_name : harness::names()) {
            const Value session = latest_session(harness_name, cwd);
            if (session.is_null()) {
                continue;
            }
            const double mtime = session["mtime"].get<double>();
            if (best.is_null() || mtime > best_mtime) {
                best = project_record(text_of(core::json::member(&project, "name")), cwd, harness_name);
                best_mtime = mtime;
            }
        }
    }
    return best;
}

Value project_target(const ProjectSelector& selector)
{
    const std::string fallback_harness(config::default_harness);
    if (present(selector.cwd)) {
        const std::string name = config::check_name(present(selector.project)
            ? *selector.project
            : config::safe_name(base_name(*selector.cwd)));
        return project_record(name, config::resolve_cwd(selector.cwd),
                              selector.harness && !selector.harness->empty() ? *selector.harness : fallback_harness);
    }
    if (present(selector.project)) {
        const std::string name = config::check_name(*selector.project);
        const auto listing = discover(true);
        const auto state = state::read();
        const Value* entry = state::project_entry(state, name);
        const auto location = known_cwd(listing, name);
        std::string cwd = location.value_or(text_of(core::json::member(entry, "cwd")));
        if (cwd.empty()) {
            throw Error("unknown project " + config::python_repr(*selector.project) + "; see bin/orch projects");
        }
        std::string harness_name = present(selector.harness) ? *selector.harness
            : core::json::or_else(core::json::member(entry, "harness"), Value(fallback_harness)).get<std::string>();
        return project_record(name, config::resolve_cwd(cwd), harness_name);
    }
    auto target = state::last_project();
    if (!target) {
        const Value newest = newest_project();
        if (newest.is_null()) {
            throw Error("no project with a session to resume");
        }
        target = newest;
    }
    const std::string harness_name = present(selector.harness) ? *selector.harness
        : core::json::or_else(core::json::member(&*target, "harness"), Value(fallback_harness)).get<std::string>();
    return project_record(text_of(core::json::member(&*target, "name")),
                          config::resolve_cwd(text_of(core::json::member(&*target, "cwd"))), harness_name);
}

Value resume_target(const ProjectSelector& selector, const std::optional<std::string>& session_id)
{
    Value target = projects::project_target(selector);
    const std::string name = text_of(core::json::member(&target, "name"));
    const std::string harness_name = text_of(core::json::member(&target, "harness"));
    const std::string cwd = text_of(core::json::member(&target, "cwd"));
    const Value found = harness::sessions(harness_name, cwd);

    if (session_id) {
        const std::string selected = config::check_session_id(*session_id);
        Value chosen = session_by_id(found, selected);
        if (chosen.is_null()) {
            std::string kind;
            try {
                kind = harness::selector_kind(harness_name, selected);
            } catch (const std::invalid_argument& error) {
                throw Error(error.what());
            }
            if (kind == "name") {
                chosen = Value::object();
                chosen["session_id"] = selected;
                chosen["path"] = "";
                chosen["mtime"] = 0;
                chosen["size"] = 0;
                chosen["selector"] = kind;
            } else if (kind == "path") {
                const auto info = core::files::stat(selected);
                if (!info) {
                    throw Error(errno_message(ENOENT, selected));
                }
                chosen = Value::object();
                chosen["session_id"] = selected;
                chosen["path"] = selected;
                chosen["mtime"] = info->mtime_epoch;
                chosen["size"] = info->size;
                chosen["selector"] = kind;
            } else {
                const auto global_session = harness::find(harness_name, selected);
                if (!global_session) {
                    std::string recent;
                    const std::size_t first = found.size() > 5 ? found.size() - 5 : 0;
                    for (std::size_t index = first; index < found.size(); ++index) {
                        recent += (recent.empty() ? "" : ", ") + text_of(core::json::member(&found[index], "session_id"));
                    }
                    throw Error("session " + config::python_repr(selected) + " not found for " + cwd
                                + "; recent: " + (recent.empty() ? std::string("(none)") : recent));
                }
                chosen = *global_session;
                chosen["selector"] = kind;
                chosen["global"] = true;
            }
        }
        target["session"] = chosen;
        return target;
    }

    const Value state = state::read();
    const Value* entry = state::project_entry(state, name);
    const Value* remembered = core::json::member(entry, "session_id");
    if (core::json::truthy(remembered)) {
        const std::string remembered_id = remembered->get<std::string>();
        Value chosen = session_by_id(found, remembered_id);
        if (chosen.is_null()) {
            const auto global_session = harness::find(harness_name, remembered_id);
            if (global_session) {
                chosen = *global_session;
            }
        }
        if (!chosen.is_null()) {
            target["session"] = chosen;
            return target;
        }
    }
    if (found.empty()) {
        throw Error("no " + harness_name + " session transcript for " + cwd);
    }
    target["session"] = found.back();
    return target;
}

void fill_cwds(std::vector<Value>& jobs)
{
    const auto listing = discover(true);
    const auto state = state::read();
    const Value* projects_state = core::json::member(&state, "projects");

    Value live = Value::object();
    try {
        live = ccbridge::fetch_sessions();
    } catch (const std::exception&) {
    }

    for (auto& job : jobs) {
        const std::string name = text_of(core::json::member(&job, "name"));
        if (!core::json::truthy(core::json::member(&job, "cwd"))) {
            const Value* info = core::json::member(&live, name);
            std::string cwd = text_of(core::json::member(info, "cwd"));
            if (cwd.empty()) {
                cwd = known_cwd(listing, name).value_or("");
            }
            if (cwd.empty()) {
                const Value* entry = core::json::member(projects_state, name);
                cwd = text_of(core::json::member(entry, "cwd"));
            }
            job["cwd"] = cwd.empty() ? Value(nullptr) : Value(cwd);
        }
        const Value* entry = core::json::member(projects_state, name);
        const Value* explicit_flag = core::json::member(&job, "harness_explicit");
        if (!core::json::truthy(explicit_flag) && core::json::truthy(core::json::member(entry, "harness"))) {
            job["harness"] = check_harness(text_of(core::json::member(entry, "harness")));
        }
    }
    for (const auto& job : jobs) {
        if (!core::json::truthy(core::json::member(&job, "cwd"))) {
            continue;
        }
        const bool keep_source = core::json::truthy(core::json::member(&job, "fork"))
            || core::json::truthy(core::json::member(&job, "record_latest"));
        const std::string resume = text_of(core::json::member(&job, "resume"));
        const std::optional<std::string> session_id = keep_source || resume.empty()
            ? std::nullopt : std::optional<std::string>(resume);
        const std::string harness_name = text_of(core::json::member(&job, "harness"));
        state::record_project(text_of(core::json::member(&job, "name")), text_of(core::json::member(&job, "cwd")),
                              session_id, harness_name.empty() ? std::nullopt : std::optional<std::string>(harness_name));
    }
}

void live_cwds(std::vector<Value>& results)
{
    Value live = Value::object();
    try {
        live = ccbridge::fetch_sessions();
    } catch (const std::exception&) {
        return;
    }
    for (auto& result : results) {
        const Value* info = core::json::member(&live, text_of(core::json::member(&result, "name")));
        const std::string cwd = text_of(core::json::member(info, "cwd"));
        if (!cwd.empty()) {
            result["cwd"] = cwd;
        }
    }
}

Value normalize(const Value& entry, bool require_prompt)
{
    if (!entry.is_object()) {
        throw Error("bad entry " + core::json::python_dumps(entry));
    }
    const std::string name = config::check_name(core::json::str_or_empty(core::json::member(&entry, "name")));
    const std::string harness_raw = core::json::str_or_empty(core::json::member(&entry, "harness"));
    const std::string harness_name = check_harness(harness_raw.empty() ? std::string(config::default_harness) : harness_raw);

    Value job = Value::object();
    job["name"] = name;
    job["harness"] = harness_name;
    job["harness_explicit"] = !harness_raw.empty();
    if (core::json::truthy(core::json::member(&entry, "cwd"))) {
        job["cwd"] = config::resolve_cwd(core::json::py_str(core::json::member(&entry, "cwd")));
    }
    const Value* prompt = core::json::member(&entry, "prompt");
    if (require_prompt && !core::json::truthy(prompt)) {
        throw Error("entry " + config::python_repr(name) + " has no prompt");
    }
    if (core::json::truthy(prompt)) {
        job["prompt"] = core::json::py_str(prompt);
    }
    const Value* session_value = core::json::member(&entry, "session_id");
    if (!core::json::truthy(session_value)) {
        session_value = core::json::member(&entry, "resume");
    }
    if (core::json::truthy(session_value)) {
        job["resume"] = config::check_session_id(core::json::py_str(session_value));
    }
    if (core::json::truthy(core::json::member(&entry, "continue"))) {
        if (job.contains("resume")) {
            throw Error("entry " + config::python_repr(name) + ": resume and continue are mutually exclusive");
        }
        job["continue"] = true;
    }
    if (core::json::truthy(core::json::member(&entry, "fork"))) {
        job["fork"] = true;
    }
    return job;
}

Value read_entries(const std::string& path, const std::string& key)
{
    const auto data = core::json::read_file(path);
    if (!data) {
        throw Error(path + ": not valid JSON");
    }
    const Value* entries = data->is_object() && !key.empty() ? core::json::member(&*data, key) : &*data;
    if (entries == nullptr || !entries->is_array()) {
        throw Error(path + ": expected a list or an object with a " + config::python_repr(key) + " list");
    }
    return *entries;
}

std::string check_harness(const std::string& name)
{
    if (!harness::known(name)) {
        std::string known;
        for (const auto& item : harness::names()) {
            known += (known.empty() ? "" : ", ") + item;
        }
        throw Error("unknown harness " + config::python_repr(name) + "; known: " + known);
    }
    return name;
}

}
