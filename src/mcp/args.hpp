#pragma once

#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::mcp {

using Value = core::json::Value;

class ToolFailure : public std::runtime_error {
public:
    ToolFailure(std::string type, const std::string& message);
    const std::string& type() const;

private:
    std::string type_;
};

class Silent : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::string exception_type(const std::exception& failure);
std::string failure_text(const std::exception& failure);
std::string python_type_name(const Value& value);
std::string python_repr(const Value* value);

std::string dumps_rpc(const Value& value);
std::string dumps_indented(const Value& value, bool sort_keys = false);

const Value* arg(const Value& args, std::string_view key);
std::string text_arg(const Value& args, std::string_view key);
std::optional<std::string> optional_text_arg(const Value& args, std::string_view key);
bool flag_arg(const Value& args, std::string_view key, bool fallback);
double number_arg(const Value& args, std::string_view key, double fallback);
long long int_arg(const Value& args, std::string_view key, long long fallback);
std::string text_of(const Value* value);
std::optional<std::string> optional_text(const Value* value);
double to_float(const Value* value);
long long to_int(const Value* value);
std::vector<std::string> text_list(const Value* value);

}
