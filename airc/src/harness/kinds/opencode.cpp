#include "harness/transcript.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ccb {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr std::size_t result_limit = 4000;
constexpr int busy_timeout_ms = 2000;
constexpr const char* session_columns =
    "select s.id, s.directory, coalesce(s.title, ''), s.time_updated, "
    "(select count(*) from message m where m.session_id = s.id) from session s ";

std::string safe_dump(const json& value) {
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string truncate_code_points(const std::string& text, std::size_t limit) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;
        if (count == limit) return text.substr(0, i);
        ++count;
    }
    return text;
}

std::string string_member(const json& object, const char* key) {
    if (!object.is_object()) return std::string();
    auto it = object.find(key);
    if (it == object.end() || !it->is_string()) return std::string();
    return it->get<std::string>();
}

const json& member_ref(const json& object, const char* key) {
    static const json absent;
    if (!object.is_object()) return absent;
    auto it = object.find(key);
    return it == object.end() ? absent : *it;
}

Event make_event(Kind kind, std::string text) {
    Event event;
    event.kind = kind;
    event.text = std::move(text);
    return event;
}

bool has_completed(const json& data) {
    const json& time = member_ref(data, "time");
    return time.is_object() && time.contains("completed");
}

std::string error_text(const json& error) {
    std::string message = string_member(member_ref(error, "data"), "message");
    if (!message.empty()) return message;
    std::string name = string_member(error, "name");
    return name.empty() ? safe_dump(error) : name;
}

std::string tool_output(const json& state) {
    std::string output = string_member(state, "output");
    return output.empty() ? string_member(state, "error") : output;
}

std::string absolute_cwd(const std::string& cwd) {
    std::error_code ec;
    fs::path absolute = fs::absolute(fs::path(cwd), ec).lexically_normal();
    if (!absolute.has_filename()) absolute = absolute.parent_path();
    return absolute.string();
}

bool valid_session_id(const std::string& id) {
    if (id.size() < 5 || id.size() > 128 || id.compare(0, 4, "ses_") != 0) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    });
}

using Connection = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

std::optional<Connection> open_database(const fs::path& db) {
    std::error_code ec;
    if (!fs::is_regular_file(db, ec)) return std::nullopt;
    sqlite3* raw = nullptr;
    const std::string uri = "file:" + db.string() + "?mode=ro";
    if (sqlite3_open_v2(uri.c_str(), &raw, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        sqlite3_close(raw);
        return std::nullopt;
    }
    sqlite3_busy_timeout(raw, busy_timeout_ms);
    return Connection(raw, &sqlite3_close);
}

std::optional<Statement> prepare(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
        sqlite3_finalize(raw);
        return std::nullopt;
    }
    return Statement(raw, &sqlite3_finalize);
}

void bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    sqlite3_bind_text(statement, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

std::string column_string(sqlite3_stmt* statement, int index) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
    if (text == nullptr) return std::string();
    return std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(statement, index)));
}

json parse_data(const std::string& text) {
    json data = json::parse(text, nullptr, false);
    return data.is_discarded() ? json::object() : data;
}

struct SessionRow {
    std::string id;
    std::string directory;
    std::string title;
    std::int64_t updated_ms = 0;
    std::int64_t messages = 0;
};

SessionRow read_session_row(sqlite3_stmt* statement) {
    SessionRow row;
    row.id = column_string(statement, 0);
    row.directory = column_string(statement, 1);
    row.title = column_string(statement, 2);
    row.updated_ms = sqlite3_column_int64(statement, 3);
    row.messages = sqlite3_column_int64(statement, 4);
    return row;
}

SessionEntry session_entry(const SessionRow& row, const fs::path& db) {
    SessionEntry entry;
    entry.session_id = row.id;
    entry.path = db.string();
    entry.mtime = static_cast<double>(row.updated_ms) / 1000.0;
    entry.title = row.title;
    entry.cwd = row.directory;
    return entry;
}

struct Row {
    std::string id;
    std::string message_id;
    json data;
    std::int64_t updated_ms = 0;
};

class OpencodeChannel : public Channel {
public:
    OpencodeChannel(fs::path db, std::string cwd, double since, bool at_end, std::optional<std::string> session_id)
        : Channel(std::move(cwd), since, at_end, std::move(session_id)), db_(std::move(db)) {}

    std::vector<Event> poll(const KindSet& kinds) override {
        std::vector<Event> out;
        const KindSet want = wanted(kinds);
        auto connection = open_database(db_);
        if (!connection) return out;
        sqlite3* db = connection->get();
        if (!bound_) {
            bound_ = resolve(db);
            if (!bound_) return out;
            if (at_end_) {
                drain(db, want, out);
                out.clear();
                return out;
            }
        }
        drain(db, want, out);
        return out;
    }

private:
    std::optional<std::string> resolve(sqlite3* db) {
        if (session_id_ && !session_id_->empty()) return session_id_;
        auto statement = prepare(db, std::string(session_columns) +
            "where s.directory = ? and s.parent_id is null and s.time_archived is null order by s.time_updated desc");
        if (!statement) return std::nullopt;
        bind_text(statement->get(), 1, cwd_);
        while (sqlite3_step(statement->get()) == SQLITE_ROW) {
            SessionRow row = read_session_row(statement->get());
            if (static_cast<double>(row.updated_ms) / 1000.0 + 3.0 >= since_) return row.id;
        }
        return std::nullopt;
    }

    void drain(sqlite3* db, const KindSet& want, std::vector<Event>& out) {
        std::vector<Row> messages = read_messages(db);
        std::vector<Row> parts = read_parts(db);
        std::int64_t newest = watermark_;
        for (const auto& message : messages) {
            messages_[message.id] = message.data;
            newest = std::max(newest, message.updated_ms);
        }
        for (const auto& part : parts) {
            handle_part(db, part, want, out);
            newest = std::max(newest, part.updated_ms);
        }
        for (const auto& message : messages) {
            handle_end(message, want, out);
        }
        watermark_ = newest;
    }

    std::vector<Row> read_messages(sqlite3* db) {
        std::vector<Row> rows;
        auto statement = prepare(db,
            "select id, data, time_updated from message where session_id = ? and time_updated >= ? "
            "order by time_created, id");
        if (!statement) return rows;
        bind_text(statement->get(), 1, *bound_);
        sqlite3_bind_int64(statement->get(), 2, watermark_);
        while (sqlite3_step(statement->get()) == SQLITE_ROW) {
            Row row;
            row.id = column_string(statement->get(), 0);
            row.data = parse_data(column_string(statement->get(), 1));
            row.updated_ms = sqlite3_column_int64(statement->get(), 2);
            rows.push_back(std::move(row));
        }
        return rows;
    }

    std::vector<Row> read_parts(sqlite3* db) {
        std::vector<Row> rows;
        auto statement = prepare(db,
            "select id, message_id, data, time_updated from part where session_id = ? and time_updated >= ? "
            "order by time_created, id");
        if (!statement) return rows;
        bind_text(statement->get(), 1, *bound_);
        sqlite3_bind_int64(statement->get(), 2, watermark_);
        while (sqlite3_step(statement->get()) == SQLITE_ROW) {
            Row row;
            row.id = column_string(statement->get(), 0);
            row.message_id = column_string(statement->get(), 1);
            row.data = parse_data(column_string(statement->get(), 2));
            row.updated_ms = sqlite3_column_int64(statement->get(), 3);
            rows.push_back(std::move(row));
        }
        return rows;
    }

    json message_data(sqlite3* db, const std::string& id) {
        auto found = messages_.find(id);
        if (found != messages_.end()) return found->second;
        auto statement = prepare(db, "select data from message where id = ?");
        if (!statement) return json::object();
        bind_text(statement->get(), 1, id);
        if (sqlite3_step(statement->get()) != SQLITE_ROW) return json::object();
        json data = parse_data(column_string(statement->get(), 0));
        messages_[id] = data;
        return data;
    }

    void handle_part(sqlite3* db, const Row& part, const KindSet& want, std::vector<Event>& out) {
        const json message = message_data(db, part.message_id);
        const std::string role = string_member(message, "role");
        const std::string type = string_member(part.data, "type");
        if (role == "user") {
            if (type == "text" && done_ids_.insert("part:" + part.id).second && want.count(Kind::You)) {
                out.push_back(make_event(Kind::You, string_member(part.data, "text")));
            }
            return;
        }
        if (role != "assistant") return;
        if (type == "text") {
            emit_delta(part.id, string_member(part.data, "text"), Kind::Text, want, out);
        } else if (type == "reasoning") {
            emit_delta("reasoning:" + part.id, string_member(part.data, "text"), Kind::Thinking, want, out);
        } else if (type == "tool") {
            handle_tool(part.data, want, out);
        }
    }

    void emit_delta(const std::string& key, const std::string& text, Kind kind, const KindSet& want,
                    std::vector<Event>& out) {
        std::string piece = delta(key, text, text_len_);
        if (!piece.empty() && want.count(kind)) out.push_back(make_event(kind, std::move(piece)));
    }

    void handle_tool(const json& data, const KindSet& want, std::vector<Event>& out) {
        const std::string call = string_member(data, "callID");
        const json& state = member_ref(data, "state");
        const std::string status = string_member(state, "status");
        if (call.empty()) return;
        if (done_ids_.insert("tool:" + call).second && want.count(Kind::Tool)) {
            Event event = make_event(Kind::Tool, string_member(data, "tool"));
            const json& input = member_ref(state, "input");
            if (!input.is_null()) event.input = input;
            out.push_back(std::move(event));
        }
        const bool finished = status == "completed" || status == "error";
        if (finished && done_ids_.insert("result:" + call).second && want.count(Kind::Result)) {
            out.push_back(make_event(Kind::Result, truncate_code_points(tool_output(state), result_limit)));
        }
    }

    void handle_end(const Row& message, const KindSet& want, std::vector<Event>& out) {
        if (string_member(message.data, "role") != "assistant") return;
        if (!has_completed(message.data) || string_member(message.data, "finish") == "tool-calls") return;
        if (!done_ids_.insert("end:" + message.id).second) return;
        const json& error = member_ref(message.data, "error");
        if (!error.is_null() && want.count(Kind::Result)) {
            out.push_back(make_event(Kind::Result, truncate_code_points(error_text(error), result_limit)));
        }
        if (want.count(Kind::TurnEnd)) out.push_back(make_event(Kind::TurnEnd, std::string()));
    }

    fs::path db_;
    std::optional<std::string> bound_;
    std::int64_t watermark_ = 0;
    std::map<std::string, json> messages_;
};

class OpencodeTranscript : public Transcript {
public:
    explicit OpencodeTranscript(fs::path root) : db_(root / "opencode.db") {}

    std::unique_ptr<Channel> channel(const std::string& cwd, double since, bool at_end,
                                     std::optional<std::string> session_id) const override {
        return std::make_unique<OpencodeChannel>(db_, cwd, since, at_end, std::move(session_id));
    }

    std::vector<SessionEntry> sessions(const std::string& cwd) const override {
        std::vector<SessionEntry> found;
        auto connection = open_database(db_);
        if (!connection) return found;
        auto statement = prepare(connection->get(), std::string(session_columns) +
            "where s.directory = ? and s.parent_id is null and s.time_archived is null order by s.time_updated");
        if (!statement) return found;
        bind_text(statement->get(), 1, absolute_cwd(cwd));
        while (sqlite3_step(statement->get()) == SQLITE_ROW) {
            SessionRow row = read_session_row(statement->get());
            if (row.messages > 0) found.push_back(session_entry(row, db_));
        }
        return found;
    }

    std::optional<SessionEntry> find(const std::string& session_id) const override {
        if (!valid_session_id(session_id)) return std::nullopt;
        auto connection = open_database(db_);
        if (!connection) return std::nullopt;
        auto statement = prepare(connection->get(), std::string(session_columns) + "where s.id = ?");
        if (!statement) return std::nullopt;
        bind_text(statement->get(), 1, session_id);
        if (sqlite3_step(statement->get()) != SQLITE_ROW) return std::nullopt;
        return session_entry(read_session_row(statement->get()), db_);
    }

    void check_selector(const std::string& selector) const override {
        if (valid_session_id(selector)) return;
        throw std::invalid_argument("bad session id '" + selector + "'");
    }

private:
    fs::path db_;
};

}

std::unique_ptr<Transcript> make_opencode_transcript(const TranscriptSpec& spec) {
    return std::make_unique<OpencodeTranscript>(resolve_root(spec));
}

}
