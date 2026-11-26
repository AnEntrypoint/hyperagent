#pragma once

#include <string>

#include "core/json.hpp"

namespace hyperagent::mcp::ops {

using Value = core::json::Value;

std::string cycle(const Value& args);
std::string queue(const Value& args);
std::string sent(const Value& args);
std::string specs(const Value& args);
std::string gate_run(const Value& args);
std::string gate_list(const Value& args);
std::string gate_history(const Value& args);

}
