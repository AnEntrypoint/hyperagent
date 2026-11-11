#pragma once

#include <optional>
#include <string>

#include "core/json.hpp"

namespace hyperagent::orch::state {

using Value = core::json::Value;

Value read();
void record_project(const std::string& name, const std::string& cwd,
                    const std::optional<std::string>& session_id = std::nullopt,
                    const std::optional<std::string>& harness = std::nullopt,
                    const std::optional<std::string>& model = std::nullopt);
std::optional<Value> last_project();
const Value* project_entry(const Value& state, const std::string& name);

}
