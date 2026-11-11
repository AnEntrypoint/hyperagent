#include "orch/state.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/clock.hpp"
#include "core/files.hpp"
#include "orch/config.hpp"

namespace hyperagent::orch::state {

namespace {

Value empty_state()
{
    Value state = Value::object();
    state["last_project"] = nullptr;
    state["projects"] = Value::object();
    return state;
}

class LockFile {
public:
    explicit LockFile(const std::filesystem::path& path)
    {
        core::files::ensure_parent_directory(path);
        descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR | O_APPEND, 0644);
        if (descriptor_ < 0 || ::flock(descriptor_, LOCK_EX) != 0) {
            throw std::runtime_error("cannot lock " + path.string());
        }
    }
    ~LockFile()
    {
        ::flock(descriptor_, LOCK_UN);
        ::close(descriptor_);
    }
    LockFile(const LockFile&) = delete;
    LockFile& operator=(const LockFile&) = delete;

private:
    int descriptor_ = -1;
};

}

Value read()
{
    const auto parsed = core::json::read_file(config::state_file());
    if (!parsed || !parsed->is_object()) {
        return empty_state();
    }
    Value state = *parsed;
    if (!state.contains("projects") || !state["projects"].is_object()) {
        state["projects"] = Value::object();
    }
    return state;
}

namespace {

void write_state(const Value& state)
{
    const auto target = config::state_file();
    auto temp = target;
    temp.replace_extension(".json.tmp");
    core::files::write_bytes(temp, core::json::python_dumps(state, {2, true, true}));
    std::filesystem::rename(temp, target);
}

void update(const std::function<void(Value&)>& change)
{
    LockFile lock(config::state_lock_file());
    Value state = read();
    change(state);
    write_state(state);
}

}

void record_project(const std::string& name, const std::string& cwd,
                    const std::optional<std::string>& session_id,
                    const std::optional<std::string>& harness,
                    const std::optional<std::string>& model)
{
    update([&](Value& state) {
        Value& entry = state["projects"][name];
        if (!entry.is_object()) {
            entry = Value::object();
        }
        const double now = core::clock::round_to(core::clock::now_epoch(), 3);
        entry["cwd"] = cwd;
        entry["touched"] = now;
        if (harness && !harness->empty()) {
            entry["harness"] = *harness;
        }
        if (model && *model == config::saved_model_reset) {
            entry.erase("model");
        } else if (model && !model->empty()) {
            entry["model"] = *model;
        }
        if (session_id && !session_id->empty()) {
            entry["session_id"] = *session_id;
            entry["resumed_at"] = now;
        }
        state["last_project"] = name;
    });
}

std::optional<Value> last_project()
{
    const Value state = read();
    std::vector<std::pair<std::string, Value>> ordered;
    for (const auto& [name, entry] : state["projects"].items()) {
        ordered.emplace_back(name, entry);
    }
    const auto touched = [](const std::pair<std::string, Value>& pair) {
        const Value* value = core::json::member(&pair.second, "touched");
        return value != nullptr && value->is_number() ? value->get<double>() : 0.0;
    };
    std::stable_sort(ordered.begin(), ordered.end(), [&](const auto& left, const auto& right) {
        return touched(left) > touched(right);
    });
    for (const auto& [name, entry] : ordered) {
        const Value* cwd = core::json::member(&entry, "cwd");
        if (!core::json::truthy(cwd)) {
            continue;
        }
        Value found = Value::object();
        found["name"] = name;
        found["cwd"] = *cwd;
        found["harness"] = core::json::or_else(core::json::member(&entry, "harness"),
                                               Value(std::string(config::default_harness)));
        return found;
    }
    return std::nullopt;
}

const Value* project_entry(const Value& state, const std::string& name)
{
    const Value* projects = core::json::member(&state, "projects");
    return core::json::member(projects, name);
}

}
