#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::orch::ccbridge {

using Value = core::json::Value;

Value fetch_sessions();
bool server_alive(int timeout_seconds = 1);
std::uint16_t delete_session(const std::string& name);
bool retire_hub(const std::string& name);
const Value* hub_info(const Value& sessions, const std::string& name);
bool hub_exists(const std::string& name);
std::string capture(const std::string& name);
std::vector<std::string> pane_tail(const std::string& pane, std::size_t count = 10);
void ensure_server();
std::string server_binary();
std::string server_log_tail();
std::string ccbridge_binary();
void stop_server_process();

}
