#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::mcp {

using Value = core::json::Value;

class EventBook {
public:
    void append(const Value& event);
    std::vector<Value> recent(std::size_t count) const;

private:
    mutable std::mutex mutex_;
    std::vector<Value> events_;
};

EventBook& event_book();

}
