#pragma once

#include <string>

#include "core/json.hpp"
#include "orch/orch.hpp"

namespace hyperagent::orch::turn {

using Value = core::json::Value;

Value run_job(const Value& job, const TurnOptions& options);
Value start_one(const Value& job, double settle, bool trust, double lock_wait);
Value session_created_after(const std::string& harness, const std::string& cwd, double since);
std::string websocket_target(const std::string& name, const std::string& cwd, const std::string& harness,
                             const std::string& resume, bool continue_last, bool fork);

}
