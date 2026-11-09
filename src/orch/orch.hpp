#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::orch {

using Value = core::json::Value;

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Timing {
    double timeout = 14400.0;
    double idle_exit = 120.0;
    double lock_wait = 14400.0;
};

struct TurnOptions {
    Timing timing;
    bool tools = false;
    bool trust = true;
};

struct ProjectSelector {
    std::optional<std::string> project;
    std::optional<std::string> cwd;
    std::optional<std::string> harness;
};

struct PromptRequest {
    std::string name;
    std::string text;
    std::optional<std::string> cwd;
    std::optional<std::string> harness;
    std::optional<std::string> session;
    bool resume = false;
    bool fork = false;
};

struct ResumeRequest {
    ProjectSelector target;
    std::optional<std::string> session;
    std::optional<std::string> prompt;
    bool continue_last = false;
    bool fork = false;
    double settle = 3.0;
    bool trust = true;
};

struct StartRequest {
    std::vector<std::string> names;
    std::optional<std::string> cwd;
    std::optional<std::string> harness;
    bool resume = false;
    bool fork = false;
    bool trust = true;
    double settle = 3.0;
    long concurrency = 8;
};

Value list_projects(bool include_all);
Value project_target(const ProjectSelector& selector);
Value list_sessions(const ProjectSelector& selector);
Value list_all_sessions();

void ensure_server();
Value server_up();
Value peer_status();
Value server_down();
Value stop_peer(const std::string& name);
std::vector<std::string> read_pane(const std::string& name);

Value start_peers(const StartRequest& request);
Value prompt_peer(const PromptRequest& request, const TurnOptions& options);
Value run_turns(const Value& entries, const TurnOptions& options);
Value resume_peer(const ResumeRequest& request, const TurnOptions& options);

std::string render_turn(const Value& result, bool tools);
int turn_exit_code(const Value& result);

Value list_output_sessions(const std::optional<std::string>& project, bool all, long limit);
Value read_output_session(const std::optional<std::string>& project, const std::optional<std::string>& agent,
                          const std::optional<std::string>& session_id, long tail,
                          const std::vector<std::string>& kinds);

}
