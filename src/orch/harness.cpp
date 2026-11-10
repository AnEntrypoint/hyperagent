#include "orch/harness.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <sqlite3.h>

#include "core/files.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/text.hpp"

namespace hyperagent::orch::harness {

namespace {

namespace fs = std::filesystem;

using core::json::is_string;
using core::json::member;
using core::json::py_str;
using core::json::truthy;

constexpr std::array<std::string_view, 5> harness_names{"claude", "claude-fable", "codex", "freebuff", "opencode"};
constexpr std::string_view jsonl_suffix{".jsonl"};

const std::regex uuid_pattern("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}");
const std::regex name_pattern("[A-Za-z0-9][A-Za-z0-9 ._-]{0,127}");
const std::regex chat_id_pattern("[A-Za-z0-9][A-Za-z0-9._:-]{0,255}");
const std::regex state_version_pattern("state_([0-9]+)");
const std::regex opencode_session_pattern("ses_[A-Za-z0-9_]{1,124}");

bool is_known(std::string_view name)
{
    return std::ranges::find(harness_names, name) != harness_names.end();
}

bool is_claude_family(std::string_view name)
{
    return name == "claude" || name == "claude-fable";
}

void require_known(const std::string& harness)
{
    if (!is_known(harness)) {
        throw std::runtime_error("unknown harness '" + harness + "'; known: claude, claude-fable, codex, freebuff, opencode");
    }
}

bool contains(const std::string& text, std::string_view needle)
{
    return text.find(needle) != std::string::npos;
}

std::string home_dir()
{
    return core::paths::expand_user("~").string();
}

std::optional<std::string> env_nonempty(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

bool is_visible(const fs::path& path)
{
    return !path.filename().string().starts_with('.');
}

std::vector<fs::path> child_entries(const fs::path& directory)
{
    std::vector<fs::path> entries;
    std::error_code error;
    fs::directory_iterator iterator(directory, error);
    const fs::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
        if (is_visible(iterator->path())) {
            entries.push_back(iterator->path());
        }
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

std::string without_jsonl(const std::string& name)
{
    return name.substr(0, name.size() - jsonl_suffix.size());
}

Value session_row(const std::string& session_id, const fs::path& path, double mtime, std::uintmax_t size)
{
    Value row = Value::object();
    row["session_id"] = session_id;
    row["path"] = path.string();
    row["mtime"] = mtime;
    row["size"] = size;
    return row;
}

Value by_mtime(std::vector<Value> rows)
{
    std::stable_sort(rows.begin(), rows.end(), [](const Value& left, const Value& right) {
        return left.at("mtime").get<double>() < right.at("mtime").get<double>();
    });
    Value sorted = Value::array();
    for (auto& row : rows) {
        sorted.push_back(std::move(row));
    }
    return sorted;
}

std::string mangle_project_name(const std::string& absolute)
{
    std::string out;
    for (const char raw : absolute) {
        const auto byte = static_cast<unsigned char>(raw);
        const bool alnum = (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
        if (alnum) {
            out.push_back(raw);
        } else if ((byte & 0xC0) != 0x80) {
            out.push_back('-');
        }
    }
    return out;
}

fs::path claude_transcript_root()
{
    return fs::path(home_dir()) / ".claude" / "projects";
}

fs::path claude_project_dir(const std::string& cwd)
{
    return claude_transcript_root() / mangle_project_name(core::paths::absolute_path(cwd));
}

Value claude_sessions(const std::string& cwd)
{
    std::vector<Value> rows;
    for (const auto& path : child_entries(claude_project_dir(cwd))) {
        const std::string file = path.filename().string();
        if (!file.ends_with(jsonl_suffix)) {
            continue;
        }
        const std::string session_id = without_jsonl(file);
        if (session_id.empty() || session_id.starts_with("agent-")) {
            continue;
        }
        const auto info = core::files::stat(path);
        if (!info || info->size == 0) {
            continue;
        }
        rows.push_back(session_row(session_id, path, info->mtime_epoch, info->size));
    }
    return by_mtime(std::move(rows));
}

std::optional<Value> claude_find(const std::string& session_id)
{
    std::optional<fs::path> newest_path;
    std::optional<core::files::FileStat> newest_info;
    for (const auto& directory : child_entries(claude_transcript_root())) {
        const fs::path candidate = directory / (session_id + std::string(jsonl_suffix));
        const auto info = core::files::stat(candidate);
        if (info && (!newest_info || info->mtime_epoch > newest_info->mtime_epoch)) {
            newest_path = candidate;
            newest_info = info;
        }
    }
    if (!newest_info) {
        return std::nullopt;
    }
    return session_row(session_id, *newest_path, newest_info->mtime_epoch, newest_info->size);
}

std::string claude_selector_kind(const std::string& selector)
{
    if (fs::path(selector).is_absolute() && core::files::is_regular_file(selector)) {
        return "path";
    }
    if (std::regex_match(selector, uuid_pattern)) {
        return "id";
    }
    if (std::regex_match(selector, name_pattern)) {
        return "name";
    }
    throw std::invalid_argument("bad session selector '" + selector + "'");
}

fs::path codex_home()
{
    if (const auto configured = env_nonempty("CODEX_HOME")) {
        return fs::path(*configured);
    }
    return fs::path(home_dir()) / ".codex";
}

std::vector<fs::path> codex_thread_dbs()
{
    std::vector<std::pair<long long, fs::path>> dbs;
    for (const auto& entry : child_entries(codex_home())) {
        const std::string name = entry.filename().string();
        if (!name.starts_with("state_") || !name.ends_with(".sqlite")) {
            continue;
        }
        std::smatch match;
        const long long version = std::regex_search(name, match, state_version_pattern) ? std::stoll(match[1].str()) : -1;
        dbs.emplace_back(version, entry);
    }
    std::stable_sort(dbs.begin(), dbs.end(), [](const auto& left, const auto& right) {
        return left.first > right.first;
    });
    std::vector<fs::path> paths;
    for (const auto& db : dbs) {
        paths.push_back(db.second);
    }
    return paths;
}

using Connection = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

std::optional<Connection> open_readonly(const fs::path& db)
{
    sqlite3* raw = nullptr;
    const std::string uri = "file:" + db.string() + "?mode=ro";
    if (sqlite3_open_v2(uri.c_str(), &raw, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        sqlite3_close(raw);
        return std::nullopt;
    }
    return Connection(raw, &sqlite3_close);
}

std::optional<Statement> prepare(sqlite3* connection, const char* sql, const std::string& bound)
{
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(connection, sql, -1, &raw, nullptr) != SQLITE_OK) {
        sqlite3_finalize(raw);
        return std::nullopt;
    }
    Statement statement(raw, &sqlite3_finalize);
    if (sqlite3_bind_text(statement.get(), 1, bound.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        return std::nullopt;
    }
    return statement;
}

std::optional<std::string> column_text(sqlite3_stmt* statement, int index)
{
    if (sqlite3_column_type(statement, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
    if (text == nullptr) {
        return std::string();
    }
    return std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(statement, index)));
}

std::string first_truthy(std::initializer_list<std::optional<std::string>> values)
{
    for (const auto& value : values) {
        if (value && !value->empty()) {
            return *value;
        }
    }
    return std::string();
}

std::uintmax_t rollout_size(const std::optional<std::string>& rollout)
{
    if (rollout && !rollout->empty()) {
        if (const auto info = core::files::stat(*rollout)) {
            return info->size;
        }
    }
    return 0;
}

double thread_mtime(std::int64_t updated_ms)
{
    return static_cast<double>(updated_ms) / 1000.0;
}

std::optional<std::vector<Value>> thread_rows(const fs::path& db, const std::string& cwd)
{
    const auto connection = open_readonly(db);
    if (!connection) {
        return std::nullopt;
    }
    const auto statement = prepare(connection->get(),
        "select id, rollout_path, coalesce(updated_at_ms, created_at_ms, 0), title, name, first_user_message, preview "
        "from threads where cwd = ? and coalesce(archived, 0) = 0 and coalesce(has_user_event, 1) = 1",
        core::paths::absolute_path(cwd));
    if (!statement) {
        return std::nullopt;
    }
    std::vector<Value> rows;
    int status = SQLITE_DONE;
    while ((status = sqlite3_step(statement->get())) == SQLITE_ROW) {
        sqlite3_stmt* raw = statement->get();
        const std::string session_id = column_text(raw, 0).value_or("");
        const auto rollout = column_text(raw, 1);
        const auto updated_ms = static_cast<std::int64_t>(sqlite3_column_int64(raw, 2));
        Value row = Value::object();
        row["session_id"] = session_id;
        row["path"] = rollout.value_or("");
        row["mtime"] = thread_mtime(updated_ms);
        row["size"] = rollout_size(rollout);
        row["title"] = first_truthy({column_text(raw, 3), column_text(raw, 4), column_text(raw, 5), column_text(raw, 6)});
        rows.push_back(std::move(row));
    }
    if (status != SQLITE_DONE) {
        return std::nullopt;
    }
    return rows;
}

std::vector<Value> codex_database_rows(const std::string& cwd)
{
    std::vector<Value> found;
    std::unordered_map<std::string, std::size_t> position;
    for (const auto& db : codex_thread_dbs()) {
        const auto rows = thread_rows(db, cwd);
        if (!rows) {
            continue;
        }
        for (const auto& row : *rows) {
            const std::string session_id = row.at("session_id").get<std::string>();
            const auto slot = position.find(session_id);
            if (slot == position.end()) {
                position.emplace(session_id, found.size());
                found.push_back(row);
            } else {
                found[slot->second] = row;
            }
        }
    }
    return found;
}

std::optional<Value> codex_thread_lookup(const fs::path& db, const std::string& session_id)
{
    const auto connection = open_readonly(db);
    if (!connection) {
        return std::nullopt;
    }
    const auto statement = prepare(connection->get(),
        "select id, rollout_path, coalesce(updated_at_ms, created_at_ms, 0), cwd from threads where id = ?",
        session_id);
    if (!statement) {
        return std::nullopt;
    }
    if (sqlite3_step(statement->get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    sqlite3_stmt* raw = statement->get();
    const auto rollout = column_text(raw, 1);
    const auto cwd = column_text(raw, 3);
    Value row = Value::object();
    row["session_id"] = column_text(raw, 0).value_or("");
    row["path"] = rollout.value_or("");
    row["mtime"] = thread_mtime(static_cast<std::int64_t>(sqlite3_column_int64(raw, 2)));
    row["size"] = rollout_size(rollout);
    row["cwd"] = cwd ? Value(*cwd) : Value(nullptr);
    return row;
}

Value session_payload(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::string line;
    if (!stream || !std::getline(stream, line)) {
        return Value::object();
    }
    const auto parsed = core::json::parse(core::text::sanitize_utf8(line));
    if (!parsed || !parsed->is_object()) {
        return Value::object();
    }
    const Value* payload = member(&*parsed, "payload");
    if (payload == nullptr || !payload->is_object()) {
        return Value::object();
    }
    return *payload;
}

std::string codex_session_id(const fs::path& path)
{
    const Value payload = session_payload(path);
    const Value* id = member(&payload, "id");
    if (truthy(id)) {
        return py_str(id);
    }
    const std::string name = path.filename().string();
    std::smatch match;
    if (std::regex_search(name, match, uuid_pattern)) {
        return match.str(0);
    }
    return without_jsonl(name);
}

std::vector<fs::path> codex_rollout_paths()
{
    std::vector<fs::path> paths;
    for (const auto& year : child_entries(codex_home() / "sessions")) {
        for (const auto& month : child_entries(year)) {
            for (const auto& day : child_entries(month)) {
                for (const auto& file : child_entries(day)) {
                    const std::string name = file.filename().string();
                    if (name.starts_with("rollout-") && name.ends_with(jsonl_suffix)) {
                        paths.push_back(file);
                    }
                }
            }
        }
    }
    std::sort(paths.begin(), paths.end(), [](const fs::path& left, const fs::path& right) {
        return left.string() < right.string();
    });
    return paths;
}

std::vector<Value> codex_rollout_rows(const std::string& cwd)
{
    std::vector<Value> rows;
    const std::string wanted = core::paths::absolute_path(cwd);
    for (const auto& path : codex_rollout_paths()) {
        const Value payload = session_payload(path);
        if (!is_string(member(&payload, "cwd"), wanted)) {
            continue;
        }
        const auto info = core::files::stat(path);
        if (!info) {
            continue;
        }
        rows.push_back(session_row(codex_session_id(path), path, info->mtime_epoch, info->size));
    }
    return rows;
}

Value codex_sessions(const std::string& cwd)
{
    std::vector<Value> rows = codex_database_rows(cwd);
    std::unordered_set<std::string> seen;
    for (const auto& row : rows) {
        seen.insert(row.at("session_id").get<std::string>());
    }
    for (auto& row : codex_rollout_rows(cwd)) {
        if (seen.insert(row.at("session_id").get<std::string>()).second) {
            rows.push_back(std::move(row));
        }
    }
    return by_mtime(std::move(rows));
}

std::optional<Value> codex_find(const std::string& session_id)
{
    for (const auto& db : codex_thread_dbs()) {
        if (auto row = codex_thread_lookup(db, session_id)) {
            return row;
        }
    }
    for (const auto& path : codex_rollout_paths()) {
        if (codex_session_id(path) != session_id) {
            continue;
        }
        const auto info = core::files::stat(path);
        if (!info) {
            continue;
        }
        return session_row(session_id, path, info->mtime_epoch, info->size);
    }
    return std::nullopt;
}

std::string codex_selector_kind(const std::string& selector)
{
    if (std::regex_match(selector, uuid_pattern)) {
        return "id";
    }
    if (std::regex_match(selector, name_pattern)) {
        return "name";
    }
    throw std::invalid_argument("bad session selector '" + selector + "'");
}

fs::path freebuff_home()
{
    if (const auto configured = env_nonempty("FREEBUFF_HOME")) {
        return fs::path(*configured);
    }
    return fs::path(home_dir()) / ".config" / "manicode";
}

fs::path freebuff_project_dir(const std::string& cwd)
{
    return freebuff_home() / "projects" / fs::path(core::paths::absolute_path(cwd)).filename();
}

bool has_messages(const Value& data)
{
    if (data.is_array()) {
        return !data.empty();
    }
    if (!data.is_object()) {
        return false;
    }
    const Value* messages = member(&data, "messages");
    return messages != nullptr && messages->is_array() && !messages->empty();
}

Value freebuff_sessions(const std::string& cwd)
{
    std::vector<Value> rows;
    for (const auto& chat : child_entries(freebuff_project_dir(cwd) / "chats")) {
        const std::string chat_id = chat.filename().string();
        if (!std::regex_match(chat_id, chat_id_pattern)) {
            continue;
        }
        const fs::path path = chat / "chat-messages.json";
        const auto info = core::files::stat(path);
        if (!info) {
            continue;
        }
        const auto bytes = core::files::read_bytes(path);
        if (!bytes) {
            continue;
        }
        const auto data = core::json::parse(core::text::sanitize_utf8(*bytes));
        if (!data || !has_messages(*data)) {
            continue;
        }
        rows.push_back(session_row(chat_id, path, info->mtime_epoch, info->size));
    }
    return by_mtime(std::move(rows));
}

std::string freebuff_selector_kind(const std::string& selector)
{
    if (std::regex_match(selector, chat_id_pattern)) {
        return "id";
    }
    throw std::invalid_argument("bad conversation id '" + selector + "'");
}

fs::path opencode_database()
{
    return fs::path(home_dir()) / ".local" / "share" / "opencode" / "opencode.db";
}

constexpr const char* opencode_sessions_sql =
    "select s.id, coalesce(s.title, ''), s.time_updated from session s "
    "where s.directory = ? and s.parent_id is null and s.time_archived is null "
    "and exists (select 1 from message m where m.session_id = s.id) order by s.time_updated";
constexpr const char* opencode_find_sql = "select s.id, coalesce(s.title, ''), s.time_updated from session s where s.id = ?";

Value opencode_row(sqlite3_stmt* raw, const fs::path& database)
{
    Value row = Value::object();
    row["session_id"] = column_text(raw, 0).value_or("");
    row["path"] = database.string();
    row["mtime"] = thread_mtime(static_cast<std::int64_t>(sqlite3_column_int64(raw, 2)));
    row["size"] = 0;
    row["title"] = column_text(raw, 1).value_or("");
    return row;
}

Value opencode_sessions(const std::string& cwd)
{
    std::vector<Value> rows;
    const fs::path database = opencode_database();
    const auto connection = open_readonly(database);
    if (!connection) {
        return Value::array();
    }
    const auto statement = prepare(connection->get(), opencode_sessions_sql, core::paths::absolute_path(cwd));
    if (!statement) {
        return Value::array();
    }
    while (sqlite3_step(statement->get()) == SQLITE_ROW) {
        rows.push_back(opencode_row(statement->get(), database));
    }
    return by_mtime(std::move(rows));
}

std::optional<Value> opencode_find(const std::string& session_id)
{
    const fs::path database = opencode_database();
    const auto connection = open_readonly(database);
    if (!connection) {
        return std::nullopt;
    }
    const auto statement = prepare(connection->get(), opencode_find_sql, session_id);
    if (!statement || sqlite3_step(statement->get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    return opencode_row(statement->get(), database);
}

std::string opencode_selector_kind(const std::string& selector)
{
    if (std::regex_match(selector, opencode_session_pattern)) {
        return "id";
    }
    throw std::invalid_argument("bad session id '" + selector + "'");
}

constexpr std::string_view claude_busy_marker{"esc to interrupt"};
constexpr std::string_view claude_prompt_glyph{"❯"};
constexpr std::string_view claude_title_marker{"Claude Code"};
constexpr std::string_view codex_busy_marker{"esc to interrupt"};
constexpr std::string_view codex_prompt_marker{"Ask Codex to do anything"};
constexpr std::string_view codex_onboarding_marker{"to get started, describe a task"};
constexpr std::string_view freebuff_busy_marker{"Starting your model session"};
constexpr std::string_view freebuff_prompt_marker{"Enter a coding task"};
constexpr std::string_view freebuff_ready_marker{"for history · ? for help"};
constexpr std::string_view opencode_busy_marker{"esc interrupt"};
constexpr std::string_view opencode_prompt_marker{"Ask anything"};

}

std::vector<std::string> names()
{
    std::vector<std::string> out;
    for (const auto name : harness_names) {
        out.emplace_back(name);
    }
    return out;
}

bool known(const std::string& name)
{
    return is_known(name);
}

Value sessions(const std::string& harness, const std::string& cwd)
{
    require_known(harness);
    if (is_claude_family(harness)) {
        return claude_sessions(cwd);
    }
    if (harness == "codex") {
        return codex_sessions(cwd);
    }
    if (harness == "opencode") {
        return opencode_sessions(cwd);
    }
    return freebuff_sessions(cwd);
}

std::optional<Value> find(const std::string& harness, const std::string& session_id)
{
    require_known(harness);
    if (is_claude_family(harness)) {
        return claude_find(session_id);
    }
    if (harness == "codex") {
        return codex_find(session_id);
    }
    if (harness == "opencode") {
        return opencode_find(session_id);
    }
    return std::nullopt;
}

std::string selector_kind(const std::string& harness, const std::string& selector)
{
    require_known(harness);
    if (is_claude_family(harness)) {
        return claude_selector_kind(selector);
    }
    if (harness == "codex") {
        return codex_selector_kind(selector);
    }
    if (harness == "opencode") {
        return opencode_selector_kind(selector);
    }
    return freebuff_selector_kind(selector);
}

bool pane_busy(const std::string& harness, const std::string& pane)
{
    require_known(harness);
    if (is_claude_family(harness)) {
        return contains(pane, claude_busy_marker);
    }
    if (harness == "codex") {
        return contains(pane, codex_busy_marker);
    }
    if (harness == "opencode") {
        return contains(pane, opencode_busy_marker);
    }
    return contains(pane, freebuff_busy_marker);
}

bool pane_ready(const std::string& harness, const std::string& pane)
{
    require_known(harness);
    if (is_claude_family(harness)) {
        return contains(pane, claude_prompt_glyph) || contains(pane, claude_busy_marker)
            || contains(pane, claude_title_marker);
    }
    if (harness == "codex") {
        return contains(pane, codex_prompt_marker) || contains(pane, codex_onboarding_marker)
            || contains(pane, codex_busy_marker);
    }
    if (harness == "opencode") {
        return contains(pane, opencode_prompt_marker) || contains(pane, opencode_busy_marker);
    }
    return contains(pane, freebuff_prompt_marker) || contains(pane, freebuff_ready_marker)
        || contains(pane, freebuff_busy_marker);
}

}
