#include "harness/event.hpp"
#include "harness/transcript.hpp"

#include <sqlite3.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ccb::Kind;
using ccb::KindSet;
using json = nlohmann::json;

namespace {

int failures = 0;

void expect(bool ok, const std::string& label) {
    std::printf("%-44s %s\n", label.c_str(), ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

std::string quote(const std::string& text) {
    std::string out = "'";
    for (char c : text) {
        if (c == '\'') out += '\'';
        out += c;
    }
    return out + "'";
}

class Db {
public:
    explicit Db(const fs::path& file) {
        if (sqlite3_open(file.string().c_str(), &db_) != SQLITE_OK) throw std::runtime_error("open failed");
    }
    ~Db() { sqlite3_close(db_); }
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    void exec(const std::string& sql) {
        char* error = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
            std::string message = error ? error : "unknown";
            sqlite3_free(error);
            throw std::runtime_error(message + " in " + sql);
        }
    }

private:
    sqlite3* db_ = nullptr;
};

void add_session(Db& db, const std::string& id, const std::string& directory, const std::string& parent,
                 long long updated) {
    db.exec("insert into session values (" + quote(id) + ", " + quote(directory) + ", 'title', " +
            (parent.empty() ? std::string("null") : quote(parent)) + ", null, " + std::to_string(updated) + ")");
}

void add_message(Db& db, const std::string& id, const std::string& session, long long created, long long updated,
                 const json& data) {
    db.exec("insert into message values (" + quote(id) + ", " + quote(session) + ", " + std::to_string(created) +
            ", " + std::to_string(updated) + ", " + quote(data.dump()) + ")");
}

void add_part(Db& db, const std::string& id, const std::string& message, const std::string& session,
              long long created, long long updated, const json& data) {
    db.exec("insert into part values (" + quote(id) + ", " + quote(message) + ", " + quote(session) + ", " +
            std::to_string(created) + ", " + std::to_string(updated) + ", " + quote(data.dump()) + ")");
}

void update_part(Db& db, const std::string& id, long long updated, const json& data) {
    db.exec("update part set time_updated = " + std::to_string(updated) + ", data = " + quote(data.dump()) +
            " where id = " + quote(id));
}

std::vector<ccb::Event> poll(ccb::Channel& channel) {
    return channel.poll(KindSet{Kind::Text, Kind::Thinking, Kind::Tool, Kind::Result, Kind::You, Kind::TurnEnd});
}

std::string summary(const std::vector<ccb::Event>& events) {
    json out = json::array();
    for (const auto& event : events) {
        out.push_back(std::string(ccb::kind_name(event.kind)) + ":" + event.text);
    }
    return out.dump();
}

}

int main() {
    const auto base = fs::temp_directory_path() /
                      ("opencode-channel-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = base / "opencode";
    const std::string cwd = (base / "proj").string();
    fs::create_directories(root);

    {
        Db db(root / "opencode.db");
        db.exec("create table session (id text primary key, directory text, title text, parent_id text, "
                "time_archived integer, time_updated integer)");
        db.exec("create table message (id text primary key, session_id text, time_created integer, "
                "time_updated integer, data text)");
        db.exec("create table part (id text primary key, message_id text, session_id text, time_created integer, "
                "time_updated integer, data text)");

        add_session(db, "ses_abc123", cwd, "", 1030);
        add_session(db, "ses_sub456", cwd, "ses_abc123", 1040);
        add_message(db, "msg_u1", "ses_abc123", 1000, 1001, json{{"role", "user"}});
        add_part(db, "prt_u1", "msg_u1", "ses_abc123", 1001, 1001, json{{"type", "text"}, {"text", "hello"}});
        add_message(db, "msg_a1", "ses_abc123", 1002, 1010,
                    json{{"role", "assistant"}, {"finish", "tool-calls"}, {"time", {{"created", 1002}, {"completed", 1010}}}});
        add_part(db, "prt_a1", "msg_a1", "ses_abc123", 1003, 1003, json{{"type", "text"}, {"text", "let me check"}});
        add_part(db, "prt_t1", "msg_a1", "ses_abc123", 1004, 1005,
                 json{{"type", "tool"}, {"tool", "bash"}, {"callID", "call_1"},
                      {"state", {{"status", "completed"}, {"input", {{"command", "ls"}}}, {"output", "a.txt"}}}});
        add_message(db, "msg_a2", "ses_abc123", 1011, 1020,
                    json{{"role", "assistant"}, {"finish", "stop"}, {"time", {{"created", 1011}, {"completed", 1020}}}});
        add_part(db, "prt_a2", "msg_a2", "ses_abc123", 1012, 1012, json{{"type", "text"}, {"text", "done"}});
    }

    ccb::TranscriptSpec spec;
    spec.kind = "opencode-sqlite";
    spec.root = root.string();
    auto transcript = ccb::make_transcript(spec);

    {
        auto channel = transcript->channel(cwd, 0.0, false, std::string("ses_abc123"));
        const auto first = poll(*channel);
        expect(summary(first) ==
                   json(std::vector<std::string>{"you:hello", "text:let me check", "tool:bash", "result:a.txt",
                                                 "text:done", "turn_end:"}).dump(),
               "history in part order, then turn end");
        expect(!first.empty() && first[2].input && (*first[2].input)["command"] == "ls",
               "tool event carries its input");
    }

    {
        Db db(root / "opencode.db");
        update_part(db, "prt_a2", 1030, json{{"type", "text"}, {"text", "done ok"}});
    }

    {
        auto channel = transcript->channel(cwd, 0.0, false, std::string("ses_abc123"));
        poll(*channel);
        Db db(root / "opencode.db");
        update_part(db, "prt_a2", 1040, json{{"type", "text"}, {"text", "done ok, all green"}});
        auto again = poll(*channel);
        expect(summary(again) == json(std::vector<std::string>{"text:, all green"}).dump(),
               "resumed poll emits only the new text");
    }

    {
        auto channel = transcript->channel(cwd, 0.0, true, std::nullopt);
        expect(poll(*channel).empty(), "at-end channel skips existing history");
        Db db(root / "opencode.db");
        add_message(db, "msg_u2", "ses_abc123", 1050, 1051, json{{"role", "user"}});
        add_part(db, "prt_u2", "msg_u2", "ses_abc123", 1051, 1051, json{{"type", "text"}, {"text", "next"}});
        expect(summary(poll(*channel)) == json(std::vector<std::string>{"you:next"}).dump(),
               "at-end channel emits the next prompt");
    }

    {
        auto sessions = transcript->sessions(cwd);
        expect(sessions.size() == 1 && sessions[0].session_id == "ses_abc123",
               "sessions list root sessions with messages");
        expect(transcript->find("ses_abc123").has_value(), "find returns a known session");
        expect(!transcript->find("ses_missing1").has_value(), "find misses an unknown session");
        transcript->check_selector("ses_abc123");
        bool refused = false;
        try {
            transcript->check_selector("../escape");
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        expect(refused, "check_selector refuses a path-like id");
    }

    {
        auto channel = transcript->channel(cwd, 0.0, false, std::nullopt);
        expect(!poll(*channel).empty(), "newest root session is chosen without an id");
    }

    fs::remove_all(base);
    std::printf("%s\n", failures == 0 ? "opencode channel: all ok" : "opencode channel: FAILURES");
    return failures == 0 ? 0 : 1;
}
