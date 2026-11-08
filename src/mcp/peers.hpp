#pragma once

#include <string>

#include "core/json.hpp"

namespace hyperagent::mcp::peers {

using Value = core::json::Value;

std::string list_projects(const Value& args);
std::string start_projects(const Value& args);
std::string instruct(const Value& args);
std::string instruct_many(const Value& args);
std::string turn_result(const Value& args);
std::string resume_project(const Value& args);
std::string list_project_sessions(const Value& args);
std::string peer_status(const Value& args);
std::string read_pane(const Value& args);
std::string stop_peer(const Value& args);
std::string server_down(const Value& args);

}
