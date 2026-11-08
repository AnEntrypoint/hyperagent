#include <nlohmann/json.hpp>

#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr std::string_view paste_open = "\x1b[200~";
constexpr std::string_view paste_close = "\x1b[201~";
constexpr std::string_view model_name = "fake-claude";
constexpr std::string_view version_name = "0.0.0-fake";

struct Options {
    std::optional<std::string> resume;
    bool continue_last = false;
    bool fork_session = false;
};

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--resume" || arg == "-r") && i + 1 < argc) {
            options.resume = argv[++i];
        } else if (arg.rfind("--resume=", 0) == 0) {
            options.resume = arg.substr(std::string_view("--resume=").size());
        } else if (arg == "--continue" || arg == "-c") {
            options.continue_last = true;
        } else if (arg == "--fork-session") {
            options.fork_session = true;
        } else if (arg == "--model" && i + 1 < argc) {
            ++i;
        }
    }
    return options;
}

std::string random_hex(std::size_t digits)
{
    static std::mt19937_64 engine{std::random_device{}()};
    std::uniform_int_distribution<int> nibble(0, 15);
    std::string out;
    for (std::size_t i = 0; i < digits; ++i) {
        out += "0123456789abcdef"[nibble(engine)];
    }
    return out;
}

std::string new_uuid()
{
    std::string hex = random_hex(32);
    hex[12] = '4';
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" +
           hex.substr(20, 12);
}

std::string timestamp_now()
{
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    char buffer[40];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%S", &utc);
    char tail[8];
    std::snprintf(tail, sizeof tail, ".%03lldZ", static_cast<long long>(millis));
    return std::string(buffer) + tail;
}

std::string slug_of(const std::string& path)
{
    std::string out;
    for (const unsigned char c : path) {
        if (c >= 0x80) {
            if ((c & 0xC0) != 0x80) out += '-';
        } else {
            out += std::isalnum(c) ? static_cast<char>(c) : '-';
        }
    }
    return out;
}

void write_all(std::string_view text)
{
    while (!text.empty()) {
        const ssize_t written = ::write(STDOUT_FILENO, text.data(), text.size());
        if (written <= 0) return;
        text.remove_prefix(static_cast<std::size_t>(written));
    }
}

class Session {
public:
    Session(fs::path transcript, std::string session_id, std::string cwd)
        : transcript_(std::move(transcript)), session_id_(std::move(session_id)), cwd_(std::move(cwd))
    {
    }

    void draw(bool busy)
    {
        std::string out = "\x1b[2J\x1b[H";
        out += "Claude Code (fake harness)\r\n\r\n";
        out += "  model:   " + std::string(model_name) + "\r\n";
        out += "  cwd:     " + cwd_ + "\r\n";
        out += "  session: " + session_id_ + "\r\n\r\n";
        if (busy) {
            out += "  * Working... (esc to interrupt)\r\n\r\n";
        }
        out += "----------------------------------------\r\n";
        out += "\xe2\x9d\xaf " + input_ + "\r\n";
        out += "----------------------------------------\r\n";
        out += "  ? for shortcuts\r\n";
        write_all(out);
    }

    std::string& input()
    {
        return input_;
    }

    void run_turn(const std::string& prompt, long long pause_ms)
    {
        const auto step = std::chrono::milliseconds(pause_ms / 4 > 0 ? pause_ms / 4 : 1);
        append_user_text(prompt);
        draw(true);
        std::this_thread::sleep_for(step);

        const std::string message_id = "msg_" + random_hex(24);
        if (prompt.find("TOOL") != std::string::npos) {
            const std::string tool_id = "toolu_" + random_hex(24);
            append_assistant(message_id, 0, text_block("Running a tool first."), json(nullptr));
            std::this_thread::sleep_for(step);
            append_assistant(message_id, 1, tool_use_block(tool_id, "echo fake-tool-output"), json("tool_use"));
            std::this_thread::sleep_for(step);
            append_tool_result(tool_id, "fake-tool-output");
            std::this_thread::sleep_for(step);
            const std::string final_id = "msg_" + random_hex(24);
            append_assistant(final_id, 0, text_block("Tool finished for: " + prompt), json("end_turn"));
        } else {
            append_assistant(message_id, 0, text_block("fake reply to: " + prompt), json("end_turn"));
        }
        std::this_thread::sleep_for(step);
        append_turn_duration(pause_ms);
        draw(false);
    }

private:
    json base(const std::string& type) const
    {
        json record = json::object();
        record["parentUuid"] = parent_uuid_.empty() ? json(nullptr) : json(parent_uuid_);
        record["isSidechain"] = false;
        record["userType"] = "external";
        record["cwd"] = cwd_;
        record["sessionId"] = session_id_;
        record["version"] = std::string(version_name);
        record["type"] = type;
        record["uuid"] = new_uuid();
        record["timestamp"] = timestamp_now();
        return record;
    }

    void emit(const json& record)
    {
        std::ofstream out(transcript_, std::ios::app | std::ios::binary);
        out << record.dump() << '\n';
        out.flush();
        parent_uuid_ = record["uuid"].get<std::string>();
    }

    void append_user_text(const std::string& prompt)
    {
        json record = base("user");
        json message = json::object();
        message["role"] = "user";
        message["content"] = prompt;
        record["message"] = message;
        emit(record);
    }

    void append_tool_result(const std::string& tool_id, const std::string& output)
    {
        json block = json::object();
        block["type"] = "tool_result";
        block["tool_use_id"] = tool_id;
        block["content"] = output;
        json record = base("user");
        json message = json::object();
        message["role"] = "user";
        message["content"] = json::array({block});
        record["message"] = message;
        emit(record);
    }

    void append_assistant(const std::string& message_id, long long index, json block, json stop_reason)
    {
        json record = base("assistant");
        record["apiBlockIndex"] = index;
        json message = json::object();
        message["id"] = message_id;
        message["type"] = "message";
        message["role"] = "assistant";
        message["model"] = std::string(model_name);
        message["content"] = json::array({block});
        message["stop_reason"] = stop_reason;
        record["message"] = message;
        emit(record);
    }

    void append_turn_duration(long long duration_ms)
    {
        json record = base("system");
        record["subtype"] = "turn_duration";
        record["durationMs"] = duration_ms;
        emit(record);
    }

    static json text_block(const std::string& text)
    {
        json block = json::object();
        block["type"] = "text";
        block["text"] = text;
        return block;
    }

    static json tool_use_block(const std::string& id, const std::string& command)
    {
        json block = json::object();
        block["type"] = "tool_use";
        block["id"] = id;
        block["name"] = "Bash";
        block["input"] = json{{"command", command}};
        return block;
    }

    fs::path transcript_;
    std::string session_id_;
    std::string cwd_;
    std::string input_;
    std::string parent_uuid_;
};

std::vector<std::string> consume_input(std::string& inbox, std::string& input, bool& pasting)
{
    std::vector<std::string> submitted;
    std::size_t at = 0;
    while (at < inbox.size()) {
        const std::string_view rest(inbox.data() + at, inbox.size() - at);
        const unsigned char c = static_cast<unsigned char>(rest[0]);
        if (c == 0x1b) {
            if (rest.substr(0, paste_open.size()) == paste_open) {
                pasting = true;
                at += paste_open.size();
                continue;
            }
            if (rest.substr(0, paste_close.size()) == paste_close) {
                pasting = false;
                at += paste_close.size();
                continue;
            }
            if (paste_open.substr(0, rest.size()) == rest || paste_close.substr(0, rest.size()) == rest) {
                break;
            }
            if (rest.size() > 1 && rest[1] == '[') {
                std::size_t end = 2;
                while (end < rest.size() && !(rest[end] >= 0x40 && rest[end] <= 0x7e)) ++end;
                if (end >= rest.size()) break;
                at += end + 1;
                continue;
            }
            at += 1;
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (pasting) {
                input += '\n';
            } else if (!input.empty()) {
                submitted.push_back(input);
                input.clear();
            }
            at += 1;
            continue;
        }
        if (c == 0x7f || c == 0x08) {
            while (!input.empty() && (static_cast<unsigned char>(input.back()) & 0xC0) == 0x80) input.pop_back();
            if (!input.empty()) input.pop_back();
            at += 1;
            continue;
        }
        if (c == 0x03) {
            input.clear();
            at += 1;
            continue;
        }
        if (c < 0x20 && c != '\t') {
            at += 1;
            continue;
        }
        input += static_cast<char>(c);
        at += 1;
    }
    inbox.erase(0, at);
    return submitted;
}

long long turn_pause_ms()
{
    const char* value = std::getenv("FAKE_CLAUDE_TURN_MS");
    if (value == nullptr || *value == '\0') return 1500;
    return std::max<long long>(4, std::atoll(value));
}

fs::path newest_transcript(const fs::path& dir)
{
    fs::path best;
    fs::file_time_type best_time{};
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".jsonl") continue;
        const auto stamp = entry.last_write_time(ec);
        if (best.empty() || stamp > best_time) {
            best = entry.path();
            best_time = stamp;
        }
    }
    return best;
}

}

int main(int argc, char** argv)
{
    const Options options = parse_options(argc, argv);
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        std::fprintf(stderr, "fake claude: HOME is not set\n");
        return 1;
    }
    const std::string cwd = fs::current_path().lexically_normal().string();
    const fs::path project_dir = fs::path(home) / ".claude" / "projects" / slug_of(cwd);
    std::error_code ec;
    fs::create_directories(project_dir, ec);

    std::string session_id;
    fs::path transcript;
    if (options.resume) {
        session_id = *options.resume;
        transcript = project_dir / (session_id + ".jsonl");
        if (!fs::is_regular_file(transcript)) {
            std::printf("No conversation found with session ID: %s\n", session_id.c_str());
            return 1;
        }
    } else if (options.continue_last) {
        transcript = newest_transcript(project_dir);
        if (transcript.empty()) {
            std::printf("No conversation found to continue\n");
            return 1;
        }
        session_id = transcript.stem().string();
    } else {
        session_id = new_uuid();
        transcript = project_dir / (session_id + ".jsonl");
    }
    if (options.fork_session) {
        const fs::path parent = transcript;
        session_id = new_uuid();
        transcript = project_dir / (session_id + ".jsonl");
        if (fs::is_regular_file(parent)) {
            fs::copy_file(parent, transcript, fs::copy_options::overwrite_existing, ec);
        }
    }

    termios saved{};
    const bool have_terminal = ::tcgetattr(STDIN_FILENO, &saved) == 0;
    if (have_terminal) {
        termios raw = saved;
        ::cfmakeraw(&raw);
        raw.c_oflag |= OPOST | ONLCR;
        ::tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    Session session(transcript, session_id, cwd);
    session.draw(false);
    std::string inbox;
    bool pasting = false;
    char buffer[4096];
    for (;;) {
        const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof buffer);
        if (count <= 0) break;
        inbox.append(buffer, static_cast<std::size_t>(count));
        for (const auto& prompt : consume_input(inbox, session.input(), pasting)) {
            session.run_turn(prompt, turn_pause_ms());
        }
        session.draw(false);
    }

    if (have_terminal) {
        ::tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    return 0;
}
