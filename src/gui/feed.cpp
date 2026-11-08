#include "gui/feed.hpp"

#include <chrono>
#include <set>
#include <string_view>
#include <thread>
#include <utility>

#include "core/files.hpp"
#include "gui/constants.hpp"
#include "gui/wire.hpp"

namespace hyperagent::gui {

namespace {

constexpr std::string_view journal_suffix = ".jsonl";

std::string journal_stem(const std::string& name)
{
    return name.size() > journal_suffix.size() ? name.substr(0, name.size() - journal_suffix.size()) : name;
}

std::vector<std::pair<std::filesystem::path, std::string>> discover_journals(const std::filesystem::path& root)
{
    std::vector<std::pair<std::filesystem::path, std::string>> found;
    if (!core::files::is_directory(root)) {
        return found;
    }
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!name.ends_with(journal_suffix) || !core::files::is_regular_file(it->path())) {
            continue;
        }
        found.emplace_back(it->path(), it->path().parent_path().filename().string() + "/" + journal_stem(name));
    }
    return found;
}

}

JournalFeed::JournalFeed(std::filesystem::path root, std::filesystem::path server_log)
    : root_(std::move(root)), server_log_(std::move(server_log))
{
}

void JournalFeed::discover(bool initial)
{
    auto found = discover_journals(root_);
    found.emplace_back(server_log_, "airc/server");

    std::set<std::filesystem::path> wanted;
    for (const auto& entry : found) {
        wanted.insert(entry.first);
    }
    std::erase_if(tails_, [&wanted](const std::unique_ptr<Tail>& tail) { return !wanted.contains(tail->path()); });

    std::set<std::filesystem::path> known;
    for (const auto& tail : tails_) {
        known.insert(tail->path());
    }
    for (auto& [path, source] : found) {
        if (known.contains(path)) {
            continue;
        }
        auto tail = std::make_unique<Tail>(path, source, path == server_log_ ? RecordKind::log : RecordKind::journal);
        if (initial) {
            tail->start_at_end();
        }
        tails_.push_back(std::move(tail));
    }
}

void JournalFeed::run()
{
    auto next_discover = std::chrono::steady_clock::time_point{};
    bool initial = true;
    while (true) {
        std::vector<wire::Value> batch;
        {
            std::lock_guard lock(mutex_);
            if (std::chrono::steady_clock::now() >= next_discover) {
                discover(initial);
                initial = false;
                next_discover = std::chrono::steady_clock::now() + constants::discover_interval;
            }
            for (const auto& tail : tails_) {
                auto fresh = tail->read_new();
                batch.insert(batch.end(), std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()));
            }
        }
        if (!batch.empty()) {
            wire::sort_by_epoch(batch);
            hub_.publish("records", wire::dumps_ascii(wire::array_of(batch)));
        }
        std::this_thread::sleep_for(constants::poll_interval);
    }
}

std::string JournalFeed::backfill()
{
    std::vector<wire::Value> records;
    {
        std::lock_guard lock(mutex_);
        for (const auto& tail : tails_) {
            auto recent = tail->last_records(constants::backfill_per_source);
            records.insert(records.end(), std::make_move_iterator(recent.begin()), std::make_move_iterator(recent.end()));
        }
    }
    wire::sort_by_epoch(records);
    if (records.size() > constants::backfill_total) {
        records.erase(records.begin(), records.end() - static_cast<std::ptrdiff_t>(constants::backfill_total));
    }
    return wire::dumps_ascii(wire::array_of(records));
}

Hub& JournalFeed::hub()
{
    return hub_;
}

}
