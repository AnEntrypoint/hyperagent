#include "orch/ccbridge.hpp"

#include <csignal>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <filesystem>
#include <mutex>
#include <stdexcept>

#include "core/files.hpp"
#include "core/http.hpp"
#include "core/json.hpp"
#include "core/process.hpp"
#include "core/text.hpp"
#include "orch/config.hpp"
#include "orch/orch.hpp"

namespace hyperagent::orch::ccbridge {

namespace {

std::string stripped_file_text(const std::filesystem::path& path)
{
    const auto bytes = core::files::read_bytes(path);
    return bytes ? std::string(core::text::strip(*bytes)) : std::string();
}

std::vector<std::string> environment_with(const std::vector<std::pair<std::string, std::optional<std::string>>>& changes)
{
    std::vector<std::string> out;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string text(*entry);
        const auto equals = text.find('=');
        const std::string key = text.substr(0, equals);
        bool replaced = false;
        for (const auto& [name, value] : changes) {
            if (name == key) {
                replaced = true;
            }
        }
        if (!replaced) {
            out.push_back(text);
        }
    }
    for (const auto& [name, value] : changes) {
        if (value) {
            out.push_back(name + "=" + *value);
        }
    }
    return out;
}

pid_t spawn_detached(const std::string& binary)
{
    namespace fs = std::filesystem;
    const auto dir = config::orch_dir();
    fs::create_directories(dir);
    const int log = ::open((dir / "server.log").c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log < 0) {
        throw Error("cannot open " + (dir / "server.log").string());
    }

    std::vector<std::pair<std::string, std::optional<std::string>>> changes;
    changes.emplace_back("AIRC_CLAUDE_SKIP_PERMISSIONS",
                         config::skip_permissions_setting() == "1" ? std::optional<std::string>("1") : std::nullopt);
    changes.emplace_back("AIRC_CLAUDE_MODEL", config::claude_model_setting());
    if (std::getenv("CCBRIDGE_TEMPLATES") == nullptr) {
        changes.emplace_back("CCBRIDGE_TEMPLATES", (config::root_dir() / "airc" / "templates").string());
    }

    auto env_text = environment_with(changes);
    std::vector<char*> envp;
    for (auto& entry : env_text) {
        envp.push_back(entry.data());
    }
    envp.push_back(nullptr);

    std::string host = config::host();
    std::string port = config::port();
    std::vector<std::string> args{binary, "serve", "--host", host, "--port", port};
    std::vector<char*> argv;
    for (auto& argument : args) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const std::string root = config::root_dir().string();
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(log);
        throw Error("fork failed");
    }
    if (child == 0) {
        ::setsid();
        if (::chdir(root.c_str()) != 0) {
            ::_exit(127);
        }
        ::dup2(log, STDOUT_FILENO);
        ::dup2(log, STDERR_FILENO);
        ::close(log);
        ::execve(binary.c_str(), argv.data(), envp.data());
        ::_exit(127);
    }
    ::close(log);
    return child;
}

void record_server_process(pid_t server, const std::string& binary)
{
    const auto dir = config::orch_dir();
    core::files::write_bytes(dir / "server.pid", std::to_string(server));
    core::files::write_bytes(dir / "server.binary", binary);
}

class ServerStartLock {
public:
    ServerStartLock()
    {
        const auto path = config::orch_dir() / "server.lock";
        std::filesystem::create_directories(path.parent_path());
        descriptor_ = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
        if (descriptor_ < 0 || ::flock(descriptor_, LOCK_EX) != 0) {
            throw Error("cannot lock " + path.string());
        }
    }
    ~ServerStartLock()
    {
        ::flock(descriptor_, LOCK_UN);
        ::close(descriptor_);
    }
    ServerStartLock(const ServerStartLock&) = delete;
    ServerStartLock& operator=(const ServerStartLock&) = delete;

private:
    int descriptor_ = -1;
};

constexpr int slow_server_probe_seconds = 5;

bool server_answers()
{
    return server_alive() || server_alive(slow_server_probe_seconds);
}

}

Value fetch_sessions()
{
    const auto response = core::http::get(config::host(), config::port(), "/sessions", std::chrono::seconds(10));
    if (response.status >= 400) {
        throw Error("GET /sessions answered " + std::to_string(response.status));
    }
    const auto parsed = core::json::parse(response.body);
    if (!parsed) {
        throw Error("GET /sessions returned invalid JSON");
    }
    return *parsed;
}

bool server_alive(int timeout_seconds)
{
    try {
        const auto response = core::http::get(config::host(), config::port(), "/sessions",
                                              std::chrono::seconds(timeout_seconds));
        return response.status == 200;
    } catch (const std::exception&) {
        return false;
    }
}

std::uint16_t delete_session(const std::string& name)
{
    return core::http::request("DELETE", config::host(), config::port(), "/sessions/" + name,
                               std::chrono::seconds(30))
        .status;
}

bool retire_hub(const std::string& name)
{
    if (!server_alive()) {
        return false;
    }
    return delete_session(name) == 200;
}

const Value* hub_info(const Value& sessions, const std::string& name)
{
    return core::json::member(&sessions, name);
}

bool hub_exists(const std::string& name)
{
    try {
        const Value sessions = fetch_sessions();
        const Value* info = hub_info(sessions, name);
        return info != nullptr && core::json::truthy(core::json::member(info, "alive"));
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<std::string> pane_tail(const std::string& pane, std::size_t count)
{
    std::vector<std::string> lines;
    for (const auto& line : core::text::splitlines(pane)) {
        const auto stripped = core::text::strip(line);
        if (!stripped.empty()) {
            lines.emplace_back(core::text::rstrip(line));
        }
    }
    if (lines.size() > count) {
        lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(count));
    }
    return lines;
}

std::string capture(const std::string& name)
{
    try {
        const Value sessions = fetch_sessions();
        const Value* info = hub_info(sessions, name);
        const Value* tail = core::json::member(info, "pane_tail");
        if (tail == nullptr || !tail->is_array()) {
            return "";
        }
        std::string pane;
        bool first = true;
        for (const auto& line : *tail) {
            if (!first) {
                pane += "\n";
            }
            first = false;
            pane += core::json::py_str(&line);
        }
        return pane;
    } catch (const std::exception&) {
        return "";
    }
}

std::string ccbridge_binary()
{
    if (const char* override_binary = std::getenv("CCBRIDGE_BIN"); override_binary != nullptr && *override_binary != '\0') {
        return override_binary;
    }
    return HYPERAGENT_CCBRIDGE;
}

std::string server_binary()
{
    const auto path = config::orch_dir() / "server.binary";
    if (!std::filesystem::is_regular_file(path)) {
        return "unknown";
    }
    return stripped_file_text(path);
}

std::string server_log_tail()
{
    const auto bytes = core::files::read_bytes(config::orch_dir() / "server.log");
    if (!bytes) {
        return "";
    }
    const auto lines = core::text::splitlines(*bytes);
    const std::size_t first = lines.size() > 15 ? lines.size() - 15 : 0;
    std::string out;
    for (std::size_t index = first; index < lines.size(); ++index) {
        if (index > first) {
            out += "\n";
        }
        out += lines[index];
    }
    return out;
}

void ensure_server()
{
    if (server_answers()) {
        return;
    }
    static std::mutex start_in_this_process;
    const std::lock_guard<std::mutex> one_thread(start_in_this_process);
    const ServerStartLock one_process;
    if (server_answers()) {
        return;
    }
    const std::string binary = ccbridge_binary();
    const pid_t child = spawn_detached(binary);
    bool child_exited = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    while (std::chrono::steady_clock::now() < deadline) {
        if (server_alive()) {
            record_server_process(child, binary);
            return;
        }
        int status = 0;
        if (::waitpid(child, &status, WNOHANG) == child) {
            child_exited = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!child_exited) {
        record_server_process(child, binary);
    }
    throw Error("airc server failed on " + config::base_url() + "\n" + server_log_tail());
}

void stop_server_process()
{
    const auto pid_file = config::orch_dir() / "server.pid";
    const pid_t pid = static_cast<pid_t>(std::stol(stripped_file_text(pid_file)));
    ::kill(pid, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline && server_alive()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::error_code ignored;
    std::filesystem::remove(pid_file, ignored);
}

}
