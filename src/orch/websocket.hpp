#pragma once

#include <chrono>
#include <memory>
#include <string>

namespace hyperagent::orch::websocket {

enum class EventKind {
    frame,
    timeout,
    closed,
};

struct Event {
    EventKind kind;
    std::string text;
};

class Client {
public:
    Client(const std::string& host, const std::string& port, const std::string& target);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void send_text(const std::string& text);
    Event next(std::chrono::milliseconds wait);
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
