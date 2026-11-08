#pragma once

#include <string>

#include "core/json.hpp"

namespace hyperagent::mcp::tools {

using Value = core::json::Value;

Value descriptors();
bool known(const std::string& name);
bool is_synchronous(const std::string& name);
std::string call(const std::string& name, const Value& args);

}
