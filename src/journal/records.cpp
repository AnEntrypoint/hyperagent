#include "journal/records.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

#include "core/clock.hpp"
#include "core/files.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/text.hpp"

namespace hyperagent::journal {

namespace {

using core::json::member;
using core::json::str_or_empty;
using core::json::truthy;

std::optional<std::string> nonempty(const std::optional<std::string>& value)
{
    if (value && !value->empty()) {
        return value;
    }
    return std::nullopt;
}

Value value_or_null(const Value* value)
{
    return value != nullptr ? *value : Value(nullptr);
}

bool kind_wanted(const Value& record, const std::vector<std::string>& kinds)
{
    if (kinds.empty()) {
        return true;
    }
    const Value* kind = member(&record, "kind");
    if (kind == nullptr || !kind->is_string()) {
        return false;
    }
    const auto& name = kind->get_ref<const std::string&>();
    return std::find(kinds.begin(), kinds.end(), name) != kinds.end();
}

bool is_agent(const Value& record, std::string_view agent)
{
    return core::json::is_string(member(&record, "agent"), agent);
}

std::string stem_of(const Path& path)
{
    const std::string name = path.filename().string();
    constexpr std::string_view suffix = ".jsonl";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return name.substr(0, name.size() - suffix.size());
    }
    return name;
}

std::vector<Path> journal_files_newest_first(const Path& directory)
{
    std::vector<std::pair<double, Path>> found;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ignored)) {
        const Path path = entry.path();
        if (path.extension() != ".jsonl" || !core::files::is_regular_file(path)) {
            continue;
        }
        if (const auto info = core::files::stat(path)) {
            found.emplace_back(info->mtime_epoch, path);
        }
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& left, const auto& right) { return left.first > right.first; });
    std::vector<Path> ordered;
    ordered.reserve(found.size());
    for (auto& entry : found) {
        ordered.push_back(std::move(entry.second));
    }
    return ordered;
}

Path base_root(const std::optional<Path>& root)
{
    if (const auto given = nonempty_root(root)) {
        return core::paths::resolve(core::paths::expand_user(given->string()));
    }
    return core::paths::journal_root();
}

}

std::optional<Path> nonempty_root(const std::optional<Path>& root)
{
    if (root && !root->empty()) {
        return root;
    }
    return std::nullopt;
}

std::vector<Value> read_records(const Path& path, const std::vector<std::string>& kinds,
                                std::optional<std::size_t> limit, std::optional<double> since)
{
    const auto bytes = core::files::read_bytes(path);
    if (!bytes) {
        return {};
    }
    std::string normalized = core::text::sanitize_utf8(*bytes);
    normalized = core::text::replace_all(normalized, "\r\n", "\n");
    normalized = core::text::replace_all(normalized, "\r", "\n");

    std::vector<Value> found;
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t end = normalized.find('\n', start);
        const std::string_view line = core::text::strip(std::string_view(normalized).substr(
            start, end == std::string::npos ? std::string::npos : end - start));
        start = end == std::string::npos ? normalized.size() + 1 : end + 1;
        if (line.empty()) {
            continue;
        }
        const auto record = core::json::parse(line);
        if (!record || !record->is_object() || !kind_wanted(*record, kinds)) {
            continue;
        }
        if (since) {
            const Value* epoch = member(&*record, "epoch");
            const double value = epoch != nullptr && epoch->is_number() && truthy(epoch) ? epoch->get<double>() : 0.0;
            if (value < *since) {
                continue;
            }
        }
        found.push_back(*record);
    }
    if (limit && *limit > 0 && found.size() > *limit) {
        found.erase(found.begin(), found.end() - static_cast<std::ptrdiff_t>(*limit));
    }
    return found;
}

std::vector<Value> tail_records(const Path& path, long limit, const std::vector<std::string>& kinds,
                                std::uintmax_t max_bytes)
{
    const auto info = core::files::stat(path);
    if (!info) {
        return {};
    }
    const auto chunk_bytes = core::files::read_tail_bytes(path, max_bytes);
    if (!chunk_bytes) {
        return {};
    }
    const std::string chunk = core::text::sanitize_utf8(*chunk_bytes);
    auto lines = core::text::splitlines(chunk);
    if (info->size > max_bytes && !lines.empty()) {
        lines.erase(lines.begin());
    }

    std::vector<Value> found;
    for (auto line = lines.rbegin(); line != lines.rend(); ++line) {
        const std::string_view stripped = core::text::strip(*line);
        if (stripped.empty()) {
            continue;
        }
        const auto record = core::json::parse(stripped);
        if (!record || !record->is_object() || !kind_wanted(*record, kinds)) {
            continue;
        }
        found.push_back(*record);
        if (static_cast<long>(found.size()) >= limit) {
            break;
        }
    }
    std::reverse(found.begin(), found.end());
    return found;
}

std::optional<Value> last_record(const Path& path, const std::vector<std::string>& kinds)
{
    auto found = tail_records(path, 1, kinds);
    if (found.empty()) {
        return std::nullopt;
    }
    return found.front();
}

std::optional<Value> summarize(const Path& path)
{
    const auto info = core::files::stat(path);
    if (!info) {
        return std::nullopt;
    }
    const auto last = last_record(path);
    auto spoken = last_record(path, {"text"});
    if (!spoken) {
        spoken = last_record(path, {"tool", "you"});
    }
    const std::string stem = stem_of(path);
    const Value* last_value = last ? &*last : nullptr;
    const Value* spoken_value = spoken ? &*spoken : nullptr;

    Value row = Value::object();
    row["project"] = path.parent_path().filename().string();
    row["session"] = stem;
    row["agent"] = core::json::or_else(member(last_value, "agent"), Value(stem));
    row["harness"] = value_or_null(member(last_value, "harness"));
    row["cwd"] = value_or_null(member(last_value, "cwd"));
    row["path"] = path.string();
    row["bytes"] = info->size;
    row["mtime"] = core::clock::round_to(info->mtime_epoch, 3);
    row["last_ts"] = value_or_null(member(last_value, "ts"));
    row["last_kind"] = value_or_null(member(last_value, "kind"));
    row["last_text"] = core::text::truncate(str_or_empty(member(spoken_value, "text")), 500);
    return row;
}

std::vector<Value> scan(const std::optional<Path>& root, const std::optional<std::string>& cwd,
                        const std::optional<std::string>& agent, const std::optional<std::string>& session_id,
                        std::size_t limit)
{
    const Path base = base_root(root);
    std::vector<Path> directories;
    if (const auto wanted_cwd = nonempty(cwd)) {
        directories.push_back(core::paths::project_dir(*wanted_cwd, base));
    } else if (core::files::is_directory(base)) {
        std::error_code ignored;
        for (const auto& entry : std::filesystem::directory_iterator(base, ignored)) {
            if (core::files::is_directory(entry.path())) {
                directories.push_back(entry.path());
            }
        }
    }

    const auto wanted_agent = nonempty(agent);
    const auto wanted_session = nonempty(session_id);
    std::vector<Value> rows;
    for (const auto& directory : directories) {
        std::error_code ignored;
        if (!std::filesystem::is_directory(directory, ignored)) {
            continue;
        }
        for (const auto& path : journal_files_newest_first(directory)) {
            if (wanted_session && stem_of(path) != *wanted_session) {
                continue;
            }
            const auto row = summarize(path);
            if (!row) {
                continue;
            }
            if (wanted_agent && !core::json::is_string(&(*row)["agent"], *wanted_agent)) {
                continue;
            }
            rows.push_back(*row);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Value& left, const Value& right) {
        return left["mtime"].get<double>() > right["mtime"].get<double>();
    });
    if (limit > 0 && rows.size() > limit) {
        rows.resize(limit);
    }
    return rows;
}

std::optional<Value> find_session(const std::optional<std::string>& agent, const std::optional<std::string>& cwd,
                                  const std::optional<std::string>& session_id, const std::optional<Path>& root)
{
    auto rows = scan(root, cwd, agent, session_id, 1);
    if (rows.empty()) {
        return std::nullopt;
    }
    return rows.front();
}

std::optional<LocatedRecord> last_response(const std::string& cwd, const std::string& agent,
                                           const std::optional<Path>& root)
{
    const Path directory = core::paths::project_dir(cwd, base_root(root));
    if (!core::files::is_directory(directory)) {
        return std::nullopt;
    }
    for (const auto& path : journal_files_newest_first(directory)) {
        const auto records = tail_records(path, 4000, {"text", "tool", "you", "instruct", "result"});
        std::vector<Value> mine;
        for (const auto& record : records) {
            if (is_agent(record, agent)) {
                mine.push_back(record);
            }
        }
        if (mine.empty()) {
            continue;
        }

        std::size_t start = 0;
        for (std::size_t index = mine.size(); index-- > 0;) {
            if (core::json::is_string(member(&mine[index], "kind"), "instruct")) {
                start = index + 1;
                break;
            }
        }
        const std::vector<Value> segment(mine.begin() + static_cast<std::ptrdiff_t>(start), mine.end());

        std::optional<std::size_t> last_tool;
        for (std::size_t index = 0; index < segment.size(); ++index) {
            if (core::json::is_string(member(&segment[index], "kind"), "tool")) {
                last_tool = index;
            }
        }
        const std::size_t tail_start = last_tool ? *last_tool + 1 : 0;
        std::vector<const Value*> tail;
        for (std::size_t index = tail_start; index < segment.size(); ++index) {
            if (core::json::is_string(member(&segment[index], "kind"), "text")) {
                tail.push_back(&segment[index]);
            }
        }
        if (tail.empty()) {
            for (const auto& record : segment) {
                if (core::json::is_string(member(&record, "kind"), "text")) {
                    tail.push_back(&record);
                }
            }
        }

        std::string text;
        for (const Value* record : tail) {
            text += str_or_empty(member(record, "text"));
        }
        if (!core::text::strip(text).empty()) {
            Value response = Value::object();
            response["kind"] = "text";
            response["ts"] = value_or_null(member(tail.back(), "ts"));
            response["text"] = text;
            response["session"] = path.stem().string();
            return LocatedRecord{path, response};
        }
    }
    return std::nullopt;
}

LatestOutput latest_output(const std::string& cwd, const std::string& agent, const std::optional<Path>& root)
{
    if (const auto response = last_response(cwd, agent, root)) {
        return LatestOutput{summarize(response->path), std::optional<Value>(response->record)};
    }
    const Path directory = core::paths::project_dir(cwd, base_root(root));
    if (!core::files::is_directory(directory)) {
        return {};
    }
    for (const auto& path : journal_files_newest_first(directory)) {
        const auto records = tail_records(path, 400, {"tool", "result"});
        for (auto record = records.rbegin(); record != records.rend(); ++record) {
            if (is_agent(*record, agent)) {
                return LatestOutput{summarize(path), std::optional<Value>(*record)};
            }
        }
    }
    return {};
}

std::optional<LocatedRecord> latest_result(const std::string& cwd, const std::string& agent,
                                           const std::optional<Path>& root)
{
    const Path directory = core::paths::project_dir(cwd, base_root(root));
    if (!core::files::is_directory(directory)) {
        return std::nullopt;
    }
    for (const auto& path : journal_files_newest_first(directory)) {
        const auto records = tail_records(path, 400, {"result"}, 65536);
        for (auto record = records.rbegin(); record != records.rend(); ++record) {
            if (is_agent(*record, agent)) {
                return LocatedRecord{path, *record};
            }
        }
    }
    return std::nullopt;
}

std::string render_records(const std::vector<Value>& records, const std::string& header, long max_chars,
                           long per_record)
{
    std::vector<std::string> lines;
    if (!header.empty()) {
        lines.push_back(header);
    }
    for (const auto& record : records) {
        std::string body = str_or_empty(member(&record, "text"));
        const Value* status = member(&record, "status");
        if (body.empty() && truthy(status)) {
            body = "status=" + core::json::py_str(status) + " duration="
                + core::json::py_str(member(&record, "duration_s")) + "s";
        }
        body = core::text::replace_all(core::text::truncate(core::text::replace_all(body, "\r", ""), per_record),
                                       "\n", "\n  ");

        const std::string value = str_or_empty(member(&record, "ts"));
        const std::string clock = core::text::code_point_count(value) >= 19
            ? core::text::code_point_slice(value, 11, 19) : value;
        std::string kind = str_or_empty(member(&record, "kind"));
        if (kind.empty()) {
            kind = "?";
        }
        const std::string line = clock + " " + core::text::pad_right(kind, 7) + " "
            + str_or_empty(member(&record, "agent")) + "  " + body;
        lines.emplace_back(core::text::rstrip(line));
    }

    std::string text;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0) {
            text += "\n";
        }
        text += lines[index];
    }
    const std::size_t count = core::text::code_point_count(text);
    if (max_chars > 0 && count > static_cast<std::size_t>(max_chars)) {
        text = std::string(core::text::code_point_prefix(text, static_cast<std::size_t>(max_chars)))
            + "\n\xE2\x80\xA6 [" + std::to_string(count - static_cast<std::size_t>(max_chars)) + " chars cut]";
    }
    return text;
}

}
