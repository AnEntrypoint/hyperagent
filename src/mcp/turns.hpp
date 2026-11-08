#pragma once

#include <string>

#include "core/json.hpp"
#include "orch/orch.hpp"

namespace hyperagent::mcp {

using Value = core::json::Value;

class TurnBook {
public:
    std::string start(const Value& job, const orch::TurnOptions& options);
    Value collect(const std::string& turn_id, double wait_seconds);
};

TurnBook& turn_book();

}
