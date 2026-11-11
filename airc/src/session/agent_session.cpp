#include "session/agent_session.hpp"

#include "common/text.hpp"
#include "common/time.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ccb {

namespace {

constexpr std::string_view model_flag = "--model";
constexpr std::string_view model_flag_assignment = "--model=";

long long current_process_id() {
#if defined(_WIN32)
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(getpid());
#endif
}

std::string default_session_name() {
    const auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    return "ccb-" + std::to_string(current_process_id()) + "-" + std::to_string(epoch_ms % 10000000);
}

std::string absolute_directory(const std::string& cwd) {
    std::error_code error;
    std::filesystem::path path = std::filesystem::absolute(cwd, error);
    if (error) {
        return cwd;
    }
    path = path.lexically_normal();
    while (!path.has_filename() && path.has_parent_path() && path != path.root_path()) {
        path = path.parent_path();
    }
    return path.string();
}

}

AgentSession::AgentSession(std::shared_ptr<const Harness> harness, SessionOptions options)
    : harness_(std::move(harness)),
      options_(std::move(options)),
      name_(options_.name.value_or(default_session_name())),
      start_time_(now_seconds()) {
    options_.cwd = absolute_directory(options_.cwd);
}

AgentSession::~AgentSession() {
    stop();
}

AgentSession& AgentSession::start() {
    std::filesystem::create_directories(options_.cwd);
    if (!options_.launch_args.empty()) {
        stop();
    }
    std::vector<std::string> argv = launch_argv();
    std::lock_guard lock(screen_mutex_);
    screen_ = std::make_unique<Screen>(options_.cols, options_.rows, [this](std::string_view bytes) {
        if (pty_) {
            pty_->write(bytes);
        }
    });
    PtyOptions pty_options;
    pty_options.argv = std::move(argv);
    pty_options.env = harness_->child_environment();
    pty_options.cwd = options_.cwd;
    pty_options.cols = options_.cols;
    pty_options.rows = options_.rows;
    pty_options.on_output = [this](std::string_view bytes) {
        std::lock_guard output_lock(screen_mutex_);
        if (screen_) {
            screen_->feed(bytes);
        }
    };
    pty_ = Pty::spawn(std::move(pty_options));
    start_time_ = now_seconds();
    return *this;
}

void AgentSession::stop() {
    Pty* running = nullptr;
    {
        std::lock_guard lock(screen_mutex_);
        running = pty_.get();
    }
    if (running != nullptr) {
        running->terminate();
    }
    std::unique_ptr<Pty> retired_pty;
    std::unique_ptr<Screen> retired_screen;
    {
        std::lock_guard lock(screen_mutex_);
        retired_pty = std::move(pty_);
        retired_screen = std::move(screen_);
    }
}

bool AgentSession::alive() const {
    std::lock_guard lock(screen_mutex_);
    return pty_ && pty_->running();
}

std::string AgentSession::pane() const {
    std::lock_guard lock(screen_mutex_);
    return screen_ ? screen_->text() : std::string();
}

bool AgentSession::busy() const {
    return harness_->pane_busy(pane());
}

bool AgentSession::wait_ready(std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (harness_->pane_ready(pane())) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    return false;
}

void AgentSession::send(std::string_view text) {
    Pty* target = nullptr;
    {
        std::lock_guard lock(screen_mutex_);
        target = pty_.get();
    }
    if (target == nullptr) {
        return;
    }
    target->write(harness_->bracketed_paste(text));
    if (harness_->submit_delay > 0.0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(harness_->submit_delay));
    }
    target->write("\r");
}

std::vector<Event> AgentSession::events(const KindSet& kinds) {
    if (!channel_) {
        if (!harness_->transcript) {
            throw std::runtime_error("harness '" + harness_->name + "' has no transcript");
        }
        channel_ = harness_->transcript->channel(options_.cwd, start_time_, options_.at_end,
                                                 options_.session_id);
        channel_->set_pane_source([this] { return pane(); });
    }
    return channel_->poll(kinds);
}

const std::string& AgentSession::name() const {
    return name_;
}

const std::string& AgentSession::cwd() const {
    return options_.cwd;
}

const Harness& AgentSession::harness() const {
    return *harness_;
}

std::vector<std::string> AgentSession::launch_argv() const {
    std::vector<std::string> argv;
    const bool overriding = options_.model && !options_.model->empty();
    for (std::size_t index = 0; index < harness_->argv.size(); ++index) {
        const std::string& word = harness_->argv[index];
        if (overriding && word == model_flag) {
            ++index;
            continue;
        }
        if (overriding && word.starts_with(model_flag_assignment)) {
            continue;
        }
        argv.push_back(word);
    }
    if (overriding) {
        argv.emplace_back(model_flag);
        argv.push_back(*options_.model);
    }
    argv.insert(argv.end(), options_.launch_args.begin(), options_.launch_args.end());
    return argv;
}

std::optional<std::string> AgentSession::launch_model() const {
    const std::vector<std::string> argv = launch_argv();
    std::optional<std::string> model;
    for (std::size_t index = 0; index < argv.size(); ++index) {
        if (argv[index] == model_flag && index + 1 < argv.size()) {
            model = argv[index + 1];
        } else if (argv[index].starts_with(model_flag_assignment)) {
            model = argv[index].substr(model_flag_assignment.size());
        }
    }
    return model;
}

std::string AgentSession::command() const {
    std::string joined;
    for (const auto& part : launch_argv()) {
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += shell_quote(part);
    }
    return joined;
}

double AgentSession::start_time() const {
    return start_time_;
}

}
