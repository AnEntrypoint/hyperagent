#include "gui/hub.hpp"

#include <utility>

#include "gui/constants.hpp"

namespace hyperagent::gui {

bool Subscriber::push(Event event)
{
    std::lock_guard lock(mutex_);
    if (queue_.size() >= constants::subscriber_depth) {
        return false;
    }
    queue_.push_back(std::move(event));
    ready_.notify_one();
    return true;
}

void Subscriber::close()
{
    std::lock_guard lock(mutex_);
    queue_.clear();
    queue_.push_back(Event{"close", ""});
    ready_.notify_one();
}

std::optional<Event> Subscriber::next(std::chrono::seconds timeout)
{
    std::unique_lock lock(mutex_);
    if (!ready_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
        return std::nullopt;
    }
    Event event = std::move(queue_.front());
    queue_.pop_front();
    return event;
}

std::shared_ptr<Subscriber> Hub::subscribe()
{
    auto subscriber = std::make_shared<Subscriber>();
    std::lock_guard lock(mutex_);
    subscribers_.push_back(subscriber);
    return subscriber;
}

void Hub::unsubscribe(const std::shared_ptr<Subscriber>& subscriber)
{
    std::lock_guard lock(mutex_);
    std::erase(subscribers_, subscriber);
}

void Hub::publish(const std::string& name, const std::string& payload)
{
    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<Subscriber>> evicted;
    for (const auto& subscriber : subscribers_) {
        if (!subscriber->push(Event{name, payload})) {
            evicted.push_back(subscriber);
        }
    }
    for (const auto& subscriber : evicted) {
        std::erase(subscribers_, subscriber);
        subscriber->close();
    }
}

}
