#include "core/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "core/files.hpp"

namespace hyperagent::core::json {

namespace {

void write_string(std::string& out, std::string_view text)
{
    out.push_back('"');
    for (const char raw : text) {
        const auto byte = static_cast<unsigned char>(raw);
        switch (byte) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (byte < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", byte);
                out += escaped;
            } else {
                out.push_back(raw);
            }
        }
    }
    out.push_back('"');
}

void write_value(std::string& out, const Value& value, Separators separators)
{
    if (value.is_null()) {
        out += "null";
    } else if (value.is_boolean()) {
        out += value.get<bool>() ? "true" : "false";
    } else if (value.is_number_integer()) {
        out += std::to_string(value.get<std::int64_t>());
    } else if (value.is_number_unsigned()) {
        out += std::to_string(value.get<std::uint64_t>());
    } else if (value.is_number_float()) {
        out += float_repr(value.get<double>());
    } else if (value.is_string()) {
        write_string(out, value.get_ref<const std::string&>());
    } else if (value.is_array()) {
        out.push_back('[');
        bool first = true;
        for (const auto& item : value) {
            if (!first) {
                out += separators.item;
            }
            first = false;
            write_value(out, item, separators);
        }
        out.push_back(']');
    } else {
        out.push_back('{');
        bool first = true;
        for (const auto& [key, item] : value.items()) {
            if (!first) {
                out += separators.item;
            }
            first = false;
            write_string(out, key);
            out += separators.key;
            write_value(out, item, separators);
        }
        out.push_back('}');
    }
}

}

std::string float_repr(double value)
{
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-Infinity" : "Infinity";
    }

    char buffer[64];
    const auto converted = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
    std::string_view scientific(buffer, static_cast<std::size_t>(converted.ptr - buffer));

    std::string out;
    if (scientific.front() == '-') {
        out.push_back('-');
        scientific.remove_prefix(1);
    }
    const std::size_t marker = scientific.find('e');
    std::string digits;
    for (const char symbol : scientific.substr(0, marker)) {
        if (symbol >= '0' && symbol <= '9') {
            digits.push_back(symbol);
        }
    }
    const int exponent = std::stoi(std::string(scientific.substr(marker + 1)));

    if (exponent >= -4 && exponent < 16) {
        if (exponent >= 0) {
            const auto integral_size = static_cast<std::size_t>(exponent) + 1;
            std::string integral = digits.substr(0, integral_size);
            integral.append(integral_size - integral.size(), '0');
            const std::string fraction = digits.size() > integral_size ? digits.substr(integral_size) : "0";
            out += integral + "." + fraction;
        } else {
            out += "0.";
            out.append(static_cast<std::size_t>(-exponent - 1), '0');
            out += digits;
        }
    } else {
        out.push_back(digits.front());
        if (digits.size() > 1) {
            out.push_back('.');
            out += digits.substr(1);
        }
        out.push_back('e');
        out.push_back(exponent < 0 ? '-' : '+');
        const int magnitude = exponent < 0 ? -exponent : exponent;
        if (magnitude < 10) {
            out.push_back('0');
        }
        out += std::to_string(magnitude);
    }
    return out;
}

std::string dumps(const Value& value, Separators separators)
{
    std::string out;
    write_value(out, value, separators);
    return out;
}

std::optional<Value> parse(std::string_view text)
{
    try {
        return Value::parse(text);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<Value> read_file(const std::filesystem::path& path)
{
    const auto bytes = files::read_bytes(path);
    if (!bytes) {
        return std::nullopt;
    }
    return parse(*bytes);
}

void write_file(const std::filesystem::path& path, const Value& value)
{
    files::write_bytes(path, dumps(value, spaced) + "\n");
}

const Value* member(const Value* object, std::string_view key)
{
    if (object == nullptr || !object->is_object()) {
        return nullptr;
    }
    const auto found = object->find(std::string(key));
    return found == object->end() ? nullptr : &*found;
}

bool truthy(const Value* value)
{
    if (value == nullptr || value->is_null()) {
        return false;
    }
    if (value->is_boolean()) {
        return value->get<bool>();
    }
    if (value->is_number_integer()) {
        return value->get<std::int64_t>() != 0;
    }
    if (value->is_number_unsigned()) {
        return value->get<std::uint64_t>() != 0;
    }
    if (value->is_number_float()) {
        return value->get<double>() != 0.0;
    }
    if (value->is_string()) {
        return !value->get_ref<const std::string&>().empty();
    }
    return !value->empty();
}

bool is_string(const Value* value, std::string_view expected)
{
    return value != nullptr && value->is_string() && value->get_ref<const std::string&>() == expected;
}

std::string py_str(const Value* value)
{
    if (value == nullptr || value->is_null()) {
        return "None";
    }
    if (value->is_boolean()) {
        return value->get<bool>() ? "True" : "False";
    }
    if (value->is_string()) {
        return value->get<std::string>();
    }
    if (value->is_number()) {
        return dumps(*value);
    }
    return dumps(*value, spaced);
}

std::string str_or_empty(const Value* value)
{
    return truthy(value) ? py_str(value) : std::string();
}

Value or_else(const Value* value, Value fallback)
{
    return truthy(value) ? *value : fallback;
}

}
