#include "gate/host.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "core/paths.hpp"

#if defined(_WIN32)
#include <process.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace hyperagent::gate::host {

#if defined(_WIN32)

namespace {

[[noreturn]] void unsupported()
{
    throw std::runtime_error("gate tickets need a POSIX host");
}

}

long long current_pid()
{
    return static_cast<long long>(::_getpid());
}

bool alive(long long)
{
    unsupported();
}

long long spawn_detached(const std::vector<std::string>&, const std::map<std::string, std::string>&,
                         const std::filesystem::path&)
{
    unsupported();
}

int run_sampled(const std::vector<std::string>&, long long, long long&)
{
    unsupported();
}

long long tree_rss_kb(long long)
{
    return 0;
}

void stop_on_signal(const std::filesystem::path&)
{
}

std::filesystem::path executable()
{
    return core::paths::executable_path();
}

#else

namespace {

char cleanup_path[4096] = {};

void on_stop(int signal_number)
{
    ::unlink(cleanup_path);
    ::_exit(signal_number == SIGINT ? 130 : 143);
}

void on_hangup(int)
{
}

std::vector<std::string> merged_environment(const std::map<std::string, std::string>& overrides)
{
    std::map<std::string, std::string> merged;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string text(*entry);
        const auto equals = text.find('=');
        if (equals != std::string::npos) {
            merged[text.substr(0, equals)] = text.substr(equals + 1);
        }
    }
    for (const auto& [key, value] : overrides) {
        merged[key] = value;
    }
    std::vector<std::string> out;
    for (const auto& [key, value] : merged) {
        out.push_back(key + "=" + value);
    }
    return out;
}

std::vector<char*> pointers_to(std::vector<std::string>& items)
{
    std::vector<char*> pointers;
    pointers.reserve(items.size() + 1);
    for (auto& item : items) {
        pointers.push_back(item.data());
    }
    pointers.push_back(nullptr);
    return pointers;
}

bool all_digits(const std::string& text)
{
    return !text.empty()
        && std::all_of(text.begin(), text.end(), [](unsigned char symbol) { return std::isdigit(symbol) != 0; });
}

}

long long current_pid()
{
    return static_cast<long long>(::getpid());
}

bool alive(long long pid)
{
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

std::filesystem::path executable()
{
    return core::paths::executable_path();
}

long long spawn_detached(const std::vector<std::string>& argv, const std::map<std::string, std::string>& overrides,
                         const std::filesystem::path& log)
{
    std::vector<std::string> arguments = argv;
    std::vector<std::string> environment = merged_environment(overrides);
    std::vector<char*> argument_pointers = pointers_to(arguments);
    std::vector<char*> environment_pointers = pointers_to(environment);
    const std::string log_path = log.string();

    int ends[2];
    if (::pipe(ends) != 0) {
        throw std::runtime_error("pipe failed");
    }
    ::fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(ends[1], F_SETFD, FD_CLOEXEC);

    const pid_t first = ::fork();
    if (first < 0) {
        ::close(ends[0]);
        ::close(ends[1]);
        throw std::runtime_error("fork failed");
    }
    if (first == 0) {
        const pid_t second = ::fork();
        if (second == 0) {
            ::setsid();
            const int input = ::open("/dev/null", O_RDONLY);
            const int sink = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (input >= 0) {
                ::dup2(input, STDIN_FILENO);
            }
            if (sink >= 0) {
                ::dup2(sink, STDOUT_FILENO);
                ::dup2(sink, STDERR_FILENO);
            }
            ::execve(argument_pointers.front(), argument_pointers.data(), environment_pointers.data());
            ::_exit(127);
        }
        if (second > 0) {
            const ssize_t written = ::write(ends[1], &second, sizeof second);
            if (written != static_cast<ssize_t>(sizeof second)) {
                ::_exit(0);
            }
        }
        ::_exit(0);
    }

    ::close(ends[1]);
    pid_t detached = 0;
    const ssize_t received = ::read(ends[0], &detached, sizeof detached);
    ::close(ends[0]);
    while (::waitpid(first, nullptr, 0) < 0 && errno == EINTR) {
    }
    if (received != static_cast<ssize_t>(sizeof detached)) {
        throw std::runtime_error("gate could not start " + argv.front());
    }
    return static_cast<long long>(detached);
}

int run_sampled(const std::vector<std::string>& argv, long long sample_ms, long long& peak_kb)
{
    std::vector<std::string> arguments = argv;
    std::vector<char*> pointers = pointers_to(arguments);

    const pid_t child = ::fork();
    if (child < 0) {
        throw std::runtime_error("fork failed");
    }
    if (child == 0) {
        ::execvp(pointers.front(), pointers.data());
        ::_exit(127);
    }

    using clock = std::chrono::steady_clock;
    const auto interval = std::chrono::milliseconds(sample_ms);
    auto next_sample = clock::now() + interval;
    int status = 0;
    for (;;) {
        const pid_t done = ::waitpid(child, &status, WNOHANG);
        if (done == child) {
            break;
        }
        if (done < 0 && errno != EINTR) {
            return 1;
        }
        if (clock::now() >= next_sample) {
            peak_kb = std::max(peak_kb, tree_rss_kb(static_cast<long long>(child)));
            next_sample += interval;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

long long tree_rss_kb(long long root)
{
    std::map<long long, std::vector<long long>> children;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", ignored)) {
        const std::string name = entry.path().filename().string();
        if (!all_digits(name)) {
            continue;
        }
        std::ifstream stat_file(entry.path() / "stat");
        std::stringstream buffer;
        buffer << stat_file.rdbuf();
        const std::string stat = buffer.str();
        const auto closing = stat.rfind(')');
        if (closing == std::string::npos) {
            continue;
        }
        std::istringstream fields(stat.substr(closing + 1));
        std::string state;
        long long parent = 0;
        fields >> state >> parent;
        children[parent].push_back(std::stoll(name));
    }

    long long total = 0;
    std::vector<long long> stack{root};
    while (!stack.empty()) {
        const long long pid = stack.back();
        stack.pop_back();
        std::ifstream status_file("/proc/" + std::to_string(pid) + "/status");
        std::string line;
        while (std::getline(status_file, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                std::istringstream value(line.substr(6));
                long long kb = 0;
                value >> kb;
                total += kb;
                break;
            }
        }
        const auto found = children.find(pid);
        if (found != children.end()) {
            stack.insert(stack.end(), found->second.begin(), found->second.end());
        }
    }
    return total;
}

void stop_on_signal(const std::filesystem::path& cleanup)
{
    std::strncpy(cleanup_path, cleanup.c_str(), sizeof cleanup_path - 1);
    struct sigaction stop{};
    stop.sa_handler = on_stop;
    sigemptyset(&stop.sa_mask);
    ::sigaction(SIGINT, &stop, nullptr);
    ::sigaction(SIGTERM, &stop, nullptr);
    struct sigaction hangup{};
    hangup.sa_handler = on_hangup;
    sigemptyset(&hangup.sa_mask);
    ::sigaction(SIGHUP, &hangup, nullptr);
}

#endif

}
