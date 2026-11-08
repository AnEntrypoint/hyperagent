#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "orch/orch.hpp"

namespace hyperagent::orch::projects {

using Value = core::json::Value;

Value discover(bool include_all);
Value latest_session(const std::string& harness, const std::string& cwd);
Value project_target(const ProjectSelector& selector);
Value resume_target(const ProjectSelector& selector, const std::optional<std::string>& session_id);
void fill_cwds(std::vector<Value>& jobs);
void live_cwds(std::vector<Value>& results);
Value normalize(const Value& entry, bool require_prompt);
Value read_entries(const std::string& path, const std::string& key);
std::string check_harness(const std::string& name);
Value newest_project();

}
