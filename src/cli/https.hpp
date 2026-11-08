#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hyperagent::cli::https {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Response {
    std::uint16_t status;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    std::string header(std::string_view name) const;
};

Response get_following_redirects(const std::string& url, const Headers& headers,
                                 std::chrono::seconds timeout = std::chrono::seconds(60));

}
