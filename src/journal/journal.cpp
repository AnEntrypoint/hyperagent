#include "journal/journal.hpp"

#include <system_error>

#include "core/clock.hpp"
#include "core/files.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/text.hpp"

namespace hyperagent::journal {

namespace {

constexpr std::size_t buffer_limit = 32;
constexpr double flush_interval_seconds = 0.2;

std::optional<std::string> nonempty(const std::optional<std::string>& value)
{
    if (value && !value->empty()) {
        return value;
    }
    return std::nullopt;
}

}

Journal::Journal(std::string agent, std::string cwd, std::string harness,
                 std::optional<std::string> session_id, std::optional<Path> root)
    : agent_(std::move(agent))
    , cwd_(core::paths::absolute_path(cwd))
    , harness_(std::move(harness))
    , fallback_id_("peer-" + agent_)
    , session_id_(nonempty(session_id))
    , root_(root && !root->empty() ? core::paths::resolve(core::paths::expand_user(root->string()))
                                   : core::paths::journal_root())
    , path_(core::paths::session_path(cwd_, session_id_.value_or(fallback_id_), root_))
{
    core::files::ensure_parent_directory(path_);
}

Journal::~Journal()
{
    try {
        flush();
    } catch (...) {
    }
}

Path Journal::bind(const std::optional<std::string>& session_id)
{
    flush();
    const std::string wanted = nonempty(session_id).value_or(fallback_id_);
    const Path target = core::paths::session_path(cwd_, wanted, root_);
    core::files::ensure_parent_directory(target);
    if (target == path_) {
        session_id_ = nonempty(session_id);
        return target;
    }

    const std::string old_id = session_id_.value_or(fallback_id_);
    if (old_id != fallback_id_) {
        session_id_ = nonempty(session_id);
        path_ = target;
        flush();
        return target;
    }

    const Path previous = path_;
    if (const auto info = core::files::stat(previous); info && info->size > 0) {
        const auto previous_text = core::files::read_bytes(previous);
        const std::string rebound = rebind_text(core::text::sanitize_utf8(previous_text.value_or("")), old_id, wanted);
        if (std::filesystem::exists(target)) {
            core::files::append_bytes(target, rebound);
            std::error_code ignored;
            std::filesystem::remove(previous, ignored);
        } else {
            std::error_code ignored;
            std::filesystem::rename(previous, target, ignored);
            core::files::write_bytes(target, rebound);
        }
    }

    session_id_ = nonempty(session_id);
    path_ = target;
    flush();
    return target;
}

Value Journal::make_record(const std::string& kind, const std::string& text, Value fields, double epoch) const
{
    Value record = Value::object();
    record["ts"] = core::clock::iso_stamp(epoch);
    record["epoch"] = core::clock::round_to(epoch, 3);
    record["agent"] = agent_;
    record["harness"] = harness_;
    record["cwd"] = cwd_;
    record["session"] = session();
    record["kind"] = kind;
    if (!text.empty()) {
        record["text"] = text;
    }
    if (fields.is_object()) {
        for (const auto& [key, value] : fields.items()) {
            if (!value.is_null()) {
                record[key] = value;
            }
        }
    }
    return record;
}

Value Journal::append(const std::string& kind, const std::string& text, std::optional<double> epoch, Value fields)
{
    const double now = epoch ? *epoch : core::clock::now_epoch();
    Value record = make_record(kind, text, std::move(fields), now);
    buffer_.push_back(record);
    if (buffer_.size() >= buffer_limit || core::clock::now_epoch() - last_write_ >= flush_interval_seconds) {
        flush();
    }
    return record;
}

void Journal::flush()
{
    if (!buffer_.empty()) {
        write_buffered_records();
    }
    last_write_ = core::clock::now_epoch();
}

void Journal::write_buffered_records()
{
    std::string bytes;
    for (const auto& record : buffer_) {
        bytes += core::json::dumps(record) + "\n";
    }
    buffer_.clear();
    core::files::append_bytes(path_, bytes);
}

const Path& Journal::path() const
{
    return path_;
}

std::string Journal::session() const
{
    return session_id_.value_or(fallback_id_);
}

std::string Journal::rebind_text(const std::string& text, const std::string& old_id, const std::string& new_id)
{
    if (old_id == new_id) {
        return text;
    }
    return core::text::replace_all(text, "\"session\":\"" + old_id + "\"", "\"session\":\"" + new_id + "\"");
}

}
