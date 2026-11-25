#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace hyperagent::core::http {

struct Response {
    std::uint16_t status;
    std::string body;
    std::map<std::string, std::string> headers;
};

Response request(const std::string& method, const std::string& host, const std::string& port,
                 const std::string& target, std::chrono::seconds timeout);

Response get(const std::string& host, const std::string& port, const std::string& target,
             std::chrono::seconds timeout);

std::string header(const Response& response, std::string_view name);

}
