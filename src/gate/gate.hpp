#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace hyperagent::gate {

struct Request {
    std::string label;
    std::vector<std::string> command;
    std::string tree;
    std::string min_mb;
    bool exclusive = false;
    bool quick = false;
    bool force = false;
};

struct Started {
    long long ticket = 0;
    std::filesystem::path result;
    std::filesystem::path log;
};

int run(const std::string& label, const std::vector<std::string>& command);
Started start(const Request& request);
std::vector<std::string> list_lines();
std::vector<std::string> history_lines();

}
