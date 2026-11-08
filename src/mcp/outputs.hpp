#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::mcp::outputs {

using Value = core::json::Value;

std::optional<std::string> output_cwd(const Value& args);
std::vector<std::string> output_kinds(const Value& args);
std::string list_outputs(const Value& args);
std::string read_output(const Value& args);
std::string search_output(const Value& args);

}

namespace hyperagent::mcp::resources {

using Value = core::json::Value;

struct Content {
    std::string text;
    std::string mime;
};

Value list();
Value templates();
bool valid_uri(const std::string& uri);
Content read(const std::string& uri);
std::string uri_for_session(const Value& row);

}
