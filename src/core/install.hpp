#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace hyperagent::core::install {

struct Manifest {
    std::string binary;
    std::string root;
    std::string parent;
    std::string host;
    std::string port;
    std::string orch_home;
    std::string journal;
    std::string templates;
    std::string ccbridge_bin;
    std::string mcp_timeout;
    std::string mcp_tool_timeout;
};

std::filesystem::path manifest_path();
std::optional<Manifest> read();
void write(const Manifest& manifest);
std::map<std::string, std::string> environment(const Manifest& manifest);
std::optional<std::string> value(std::string_view key);

}
