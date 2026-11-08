#include "gui/reload.hpp"

#include <thread>
#include <utility>

#include "gui/constants.hpp"
#include "gui/wire.hpp"

namespace hyperagent::gui {

ReloadWatcher::ReloadWatcher(std::filesystem::path public_dir) : public_dir_(std::move(public_dir))
{
}

ReloadWatcher::Stamps ReloadWatcher::snapshot() const
{
    Stamps stamps;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(public_dir_, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry_error;
        const bool regular = it->is_regular_file(entry_error);
        if (entry_error || !regular) {
            continue;
        }
        const auto stamp = it->last_write_time(entry_error);
        if (entry_error) {
            continue;
        }
        stamps[it->path().lexically_relative(public_dir_).generic_string()] = stamp;
    }
    return stamps;
}

void ReloadWatcher::run()
{
    Stamps seen = snapshot();
    while (true) {
        std::this_thread::sleep_for(constants::watch_interval);
        Stamps current = snapshot();
        std::vector<std::string> changed;
        for (const auto& [name, stamp] : current) {
            const auto previous = seen.find(name);
            if (previous == seen.end() || previous->second != stamp) {
                changed.push_back(name);
            }
        }
        seen = std::move(current);
        if (!changed.empty()) {
            publish(changed);
        }
    }
}

void ReloadWatcher::publish(const std::vector<std::string>& changed)
{
    wire::Value files = wire::Value::array();
    for (const auto& name : changed) {
        files.push_back(name);
    }
    wire::Value payload = wire::Value::object();
    payload["files"] = files;
    payload["restart"] = false;
    hub_.publish("change", wire::dumps_ascii(payload));
}

Hub& ReloadWatcher::hub()
{
    return hub_;
}

}
