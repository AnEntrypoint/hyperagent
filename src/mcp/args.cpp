#include "mcp/args.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/text.hpp"
#include "orch/config.hpp"
#include "orch/orch.hpp"

namespace hyperagent::mcp {

namespace {

std::string ascii_escape(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (std::size_t offset = 0; offset < raw.size();) {
        const auto byte = static_cast<unsigned char>(raw[offset]);
        if (byte >= 0x20 && byte < 0x7f) {
            out.push_back(raw[offset]);
            ++offset;
            continue;
        }
        if (byte < 0x80) {
            char escaped[16];
            std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(byte));
            out += escaped;
            ++offset;
            continue;
        }
        const core::text::Decoded decoded = core::text::decode_at(raw, offset);
        const auto emit = [&out](char32_t unit) {
            char escaped[16];
            std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(unit));
            out += escaped;
        };
        if (decoded.code_point >= 0x10000) {
            const char32_t shifted = decoded.code_point - 0x10000;
            emit(0xD800 + (shifted >> 10));
            emit(0xDC00 + (shifted & 0x3FF));
        } else {
            emit(decoded.code_point);
        }
        offset += decoded.size;
    }
    return out;
}

bool is_space(char symbol)
{
    return symbol == ' ' || symbol == '\t' || symbol == '\n' || symbol == '\r' || symbol == '\f' || symbol == '\v';
}

std::string_view trim(std::string_view text)
{
    std::size_t first = 0;
    while (first < text.size() && is_space(text[first])) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && is_space(text[last - 1])) {
        --last;
    }
    return text.substr(first, last - first);
}

}

ToolFailure::ToolFailure(std::string type, const std::string& message)
    : std::runtime_error(message)
    , type_(std::move(type))
{
}

const std::string& ToolFailure::type() const
{
    return type_;
}

std::string exception_type(const std::exception& failure)
{
    if (const auto* tool = dynamic_cast<const ToolFailure*>(&failure)) {
        return tool->type();
    }
    if (dynamic_cast<const orch::Error*>(&failure) != nullptr) {
        return "SystemExit";
    }
    return "RuntimeError";
}

std::string failure_text(const std::exception& failure)
{
    return exception_type(failure) + ": " + failure.what();
}

std::string python_type_name(const Value& value)
{
    if (value.is_null()) {
        return "NoneType";
    }
    if (value.is_boolean()) {
        return "bool";
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return "int";
    }
    if (value.is_number_float()) {
        return "float";
    }
    if (value.is_string()) {
        return "str";
    }
    if (value.is_array()) {
        return "list";
    }
    return "dict";
}

std::string python_repr(const Value* value)
{
    if (value == nullptr || value->is_null()) {
        return "None";
    }
    if (value->is_boolean()) {
        return value->get<bool>() ? "True" : "False";
    }
    if (value->is_string()) {
        return orch::config::python_repr(value->get<std::string>());
    }
    return core::json::dumps(*value);
}

std::string dumps_rpc(const Value& value)
{
    return ascii_escape(core::json::dumps(value, core::json::compact));
}

std::string dumps_indented(const Value& value, bool sort_keys)
{
    core::json::PythonStyle style;
    style.indent = 2;
    style.sort_keys = sort_keys;
    style.ensure_ascii = true;
    return core::json::python_dumps(value, style);
}

const Value* arg(const Value& args, std::string_view key)
{
    return core::json::member(&args, key);
}

std::string text_arg(const Value& args, std::string_view key)
{
    return text_of(arg(args, key));
}

std::string text_of(const Value* value)
{
    return core::json::truthy(value) ? core::json::py_str(value) : std::string();
}

std::optional<std::string> optional_text_arg(const Value& args, std::string_view key)
{
    return optional_text(arg(args, key));
}

std::optional<std::string> optional_text(const Value* value)
{
    std::string text = text_of(value);
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

bool flag_arg(const Value& args, std::string_view key, bool fallback)
{
    const Value* value = arg(args, key);
    if (value == nullptr || value->is_null()) {
        return fallback;
    }
    return core::json::truthy(value);
}

double number_arg(const Value& args, std::string_view key, double fallback)
{
    const Value* value = arg(args, key);
    if (value == nullptr || value->is_null()) {
        return fallback;
    }
    return to_float(value);
}

long long int_arg(const Value& args, std::string_view key, long long fallback)
{
    const Value* value = arg(args, key);
    if (!core::json::truthy(value)) {
        return fallback;
    }
    return to_int(value);
}

double to_float(const Value* value)
{
    if (value->is_boolean()) {
        return value->get<bool>() ? 1.0 : 0.0;
    }
    if (value->is_number()) {
        return value->get<double>();
    }
    if (value->is_string()) {
        const std::string raw = value->get<std::string>();
        const std::string_view body = trim(raw);
        const std::string owned(body);
        char* end = nullptr;
        const double parsed = std::strtod(owned.c_str(), &end);
        if (owned.empty() || end != owned.c_str() + owned.size()) {
            throw ToolFailure("ValueError", "could not convert string to float: " + python_repr(value));
        }
        return parsed;
    }
    throw ToolFailure("TypeError", "float() argument must be a string or a real number, not '"
                                       + python_type_name(*value) + "'");
}

long long to_int(const Value* value)
{
    if (value->is_boolean()) {
        return value->get<bool>() ? 1 : 0;
    }
    if (value->is_number_integer()) {
        return value->get<long long>();
    }
    if (value->is_number_unsigned()) {
        return static_cast<long long>(value->get<unsigned long long>());
    }
    if (value->is_number_float()) {
        const double number = value->get<double>();
        if (std::isnan(number)) {
            throw ToolFailure("ValueError", "cannot convert float NaN to integer");
        }
        if (std::isinf(number)) {
            throw ToolFailure("OverflowError", "cannot convert float infinity to integer");
        }
        return static_cast<long long>(number);
    }
    if (value->is_string()) {
        const std::string raw = value->get<std::string>();
        const std::string_view body = trim(raw);
        std::size_t index = 0;
        bool negative = false;
        if (index < body.size() && (body[index] == '+' || body[index] == '-')) {
            negative = body[index] == '-';
            ++index;
        }
        bool digits = index < body.size();
        long long result = 0;
        for (std::size_t cursor = index; cursor < body.size(); ++cursor) {
            const char symbol = body[cursor];
            if (symbol < '0' || symbol > '9') {
                digits = false;
                break;
            }
            result = result * 10 + (symbol - '0');
        }
        if (!digits) {
            throw ToolFailure("ValueError", "invalid literal for int() with base 10: " + python_repr(value));
        }
        return negative ? -result : result;
    }
    throw ToolFailure("TypeError", "int() argument must be a string, a bytes-like object or a real number, not '"
                                       + python_type_name(*value) + "'");
}

std::vector<std::string> text_list(const Value* value)
{
    std::vector<std::string> items;
    if (!core::json::truthy(value)) {
        return items;
    }
    if (value->is_array()) {
        for (const auto& item : *value) {
            items.push_back(core::json::py_str(&item));
        }
        return items;
    }
    if (value->is_object()) {
        for (const auto& [key, item] : value->items()) {
            items.push_back(key);
        }
        return items;
    }
    if (value->is_string()) {
        const std::string raw = value->get<std::string>();
        for (std::size_t offset = 0; offset < raw.size();) {
            const auto decoded = core::text::decode_at(raw, offset);
            items.push_back(raw.substr(offset, decoded.size));
            offset += decoded.size;
        }
        return items;
    }
    throw ToolFailure("TypeError", "'" + python_type_name(*value) + "' object is not iterable");
}

}
