#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace hyperagent::gui {

struct Event {
    std::string name;
    std::string payload;
};

class Subscriber {
public:
    bool push(Event event);
    void close();
    std::optional<Event> next(std::chrono::seconds timeout);

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Event> queue_;
};

class Hub {
public:
    std::shared_ptr<Subscriber> subscribe();
    void unsubscribe(const std::shared_ptr<Subscriber>& subscriber);
    void publish(const std::string& name, const std::string& payload);

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<Subscriber>> subscribers_;
};

}
