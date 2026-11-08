#include "mcp/events.hpp"

#include <cstddef>

namespace hyperagent::mcp {

namespace {

constexpr std::size_t retained_events = 200;

}

void EventBook::append(const Value& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
    if (events_.size() > retained_events) {
        events_.erase(events_.begin(), events_.end() - retained_events);
    }
}

std::vector<Value> EventBook::recent(std::size_t count) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t start = events_.size() > count ? events_.size() - count : 0;
    return std::vector<Value>(events_.begin() + static_cast<std::ptrdiff_t>(start), events_.end());
}

EventBook& event_book()
{
    static EventBook book;
    return book;
}

}
