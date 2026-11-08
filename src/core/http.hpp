#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace hyperagent::core::http {

struct Response {
    std::uint16_t status;
    std::string body;
};

Response request(const std::string& method, const std::string& host, const std::string& port,
                 const std::string& target, std::chrono::seconds timeout);

Response get(const std::string& host, const std::string& port, const std::string& target,
             std::chrono::seconds timeout);

}
