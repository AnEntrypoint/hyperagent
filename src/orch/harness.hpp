#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::orch::harness {

using Value = core::json::Value;

std::vector<std::string> names();
bool known(const std::string& name);
Value sessions(const std::string& harness, const std::string& cwd);
std::optional<Value> find(const std::string& harness, const std::string& session_id);
std::string selector_kind(const std::string& harness, const std::string& selector);
bool pane_busy(const std::string& harness, const std::string& pane);
bool pane_ready(const std::string& harness, const std::string& pane);

}
