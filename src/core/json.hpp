#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace hyperagent::core::json {

using Value = nlohmann::ordered_json;

struct Separators {
    std::string_view item;
    std::string_view key;
};

inline constexpr Separators compact{",", ":"};
inline constexpr Separators spaced{", ", ": "};

std::string float_repr(double value);
std::string dumps(const Value& value, Separators separators = compact);

struct PythonStyle {
    int indent = -1;
    bool sort_keys = false;
    bool ensure_ascii = true;
};

std::string python_dumps(const Value& value, PythonStyle style = {});
std::optional<Value> parse(std::string_view text);
std::optional<Value> parse_python(std::string_view text);
std::optional<Value> read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, const Value& value);

const Value* member(const Value* object, std::string_view key);
bool truthy(const Value* value);
bool is_string(const Value* value, std::string_view expected);
std::string py_str(const Value* value);
std::string str_or_empty(const Value* value);
Value or_else(const Value* value, Value fallback);

}
