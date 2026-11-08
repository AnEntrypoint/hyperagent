#include "core/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

#include "core/files.hpp"
#include "core/text.hpp"

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

std::string python_dumps(const Value& value, PythonStyle style)
{
    std::string out;
    const bool pretty = style.indent >= 0;
    const std::string_view item_separator = pretty ? "," : ", ";
    const std::string_view key_separator = ": ";

    const auto newline = [&](int depth) {
        if (!pretty) {
            return;
        }
        out.push_back('\n');
        out.append(static_cast<std::size_t>(depth * style.indent), ' ');
    };

    const auto write_text = [&](std::string_view text) {
        out.push_back('"');
        for (std::size_t offset = 0; offset < text.size();) {
            const auto decoded = core::text::decode_at(text, offset);
            offset += decoded.size;
            const char32_t point = decoded.code_point;
            switch (point) {
            case '"': out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\n': out += "\\n"; continue;
            case '\r': out += "\\r"; continue;
            case '\t': out += "\\t"; continue;
            case '\b': out += "\\b"; continue;
            case '\f': out += "\\f"; continue;
            default: break;
            }
            const bool escape = point < 0x20 || (style.ensure_ascii && point >= 0x7f);
            if (!escape) {
                out.append(text.substr(offset - decoded.size, decoded.size));
                continue;
            }
            char escaped[16];
            if (point >= 0x10000) {
                const char32_t shifted = point - 0x10000;
                const unsigned high = 0xD800 + static_cast<unsigned>(shifted >> 10);
                const unsigned low = 0xDC00 + static_cast<unsigned>(shifted & 0x3FF);
                std::snprintf(escaped, sizeof escaped, "\\u%04x\\u%04x", high, low);
            } else {
                std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(point));
            }
            out += escaped;
        }
        out.push_back('"');
    };

    std::function<void(const Value&, int)> write = [&](const Value& node, int depth) {
        if (node.is_null()) {
            out += "null";
        } else if (node.is_boolean()) {
            out += node.get<bool>() ? "true" : "false";
        } else if (node.is_number_integer()) {
            out += std::to_string(node.get<std::int64_t>());
        } else if (node.is_number_unsigned()) {
            out += std::to_string(node.get<std::uint64_t>());
        } else if (node.is_number_float()) {
            out += float_repr(node.get<double>());
        } else if (node.is_string()) {
            write_text(node.get_ref<const std::string&>());
        } else if (node.is_array()) {
            if (node.empty()) {
                out += "[]";
                return;
            }
            out.push_back('[');
            bool first = true;
            for (const auto& item : node) {
                if (!first) {
                    out += item_separator;
                }
                first = false;
                newline(depth + 1);
                write(item, depth + 1);
            }
            newline(depth);
            out.push_back(']');
        } else {
            if (node.empty()) {
                out += "{}";
                return;
            }
            std::vector<std::pair<std::string, const Value*>> items;
            for (const auto& [key, item] : node.items()) {
                items.emplace_back(key, &item);
            }
            if (style.sort_keys) {
                std::stable_sort(items.begin(), items.end(),
                                 [](const auto& left, const auto& right) { return left.first < right.first; });
            }
            out.push_back('{');
            bool first = true;
            for (const auto& [key, item] : items) {
                if (!first) {
                    out += item_separator;
                }
                first = false;
                newline(depth + 1);
                write_text(key);
                out += key_separator;
                write(*item, depth + 1);
            }
            newline(depth);
            out.push_back('}');
        }
    };
    write(value, 0);
    return out;
}

namespace {

constexpr std::string_view decoded_marker{"\0\1", 2};
constexpr std::string_view escaped_marker{"\\u0000\\u0001"};

bool is_hex_digit(char symbol)
{
    return (symbol >= '0' && symbol <= '9') || (symbol >= 'a' && symbol <= 'f') || (symbol >= 'A' && symbol <= 'F');
}

bool is_hex4(std::string_view text)
{
    return text.size() == 4 && std::all_of(text.begin(), text.end(), is_hex_digit);
}

std::uint32_t hex4_value(std::string_view text)
{
    std::uint32_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return value;
}

bool is_surrogate(std::uint32_t unit)
{
    return unit >= 0xD800 && unit <= 0xDFFF;
}

bool is_high_surrogate(std::uint32_t unit)
{
    return unit >= 0xD800 && unit <= 0xDBFF;
}

bool is_low_surrogate(std::uint32_t unit)
{
    return unit >= 0xDC00 && unit <= 0xDFFF;
}

std::string token_literal(std::string_view payload)
{
    return "\"" + std::string(escaped_marker) + std::string(payload) + "\"";
}

bool is_digit(char symbol)
{
    return symbol >= '0' && symbol <= '9';
}

std::size_t digits_end(std::string_view text, std::size_t at)
{
    while (at < text.size() && is_digit(text[at])) {
        ++at;
    }
    return at;
}

std::size_t number_length(std::string_view text)
{
    std::size_t at = text.starts_with('-') ? 1 : 0;
    const std::size_t integral_end = digits_end(text, at);
    if (integral_end == at) {
        return 0;
    }
    at = integral_end;
    if (at + 1 < text.size() && text[at] == '.' && is_digit(text[at + 1])) {
        at = digits_end(text, at + 1);
    }
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
        std::size_t exponent = at + 1;
        if (exponent < text.size() && (text[exponent] == '+' || text[exponent] == '-')) {
            ++exponent;
        }
        const std::size_t exponent_end = digits_end(text, exponent);
        if (exponent_end > exponent) {
            at = exponent_end;
        }
    }
    return at;
}

std::string bridge_python_tokens(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 32);
    bool in_string = false;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::string_view rest = text.substr(at);
        if (!in_string) {
            if (rest.front() == '"') {
                in_string = true;
                out.push_back('"');
                at += 1;
            } else if (rest.starts_with("-Infinity")) {
                out += token_literal("-Infinity");
                at += 9;
            } else if (rest.starts_with("Infinity")) {
                out += token_literal("Infinity");
                at += 8;
            } else if (rest.starts_with("NaN")) {
                out += token_literal("NaN");
                at += 3;
            } else if (const std::size_t length = number_length(rest); length > 0) {
                const std::string literal(rest.substr(0, length));
                const double value = std::strtod(literal.c_str(), nullptr);
                if (std::isinf(value) && literal.find_first_of(".eE") != std::string::npos) {
                    out += token_literal(value < 0 ? "-Infinity" : "Infinity");
                } else {
                    out += literal;
                }
                at += length;
            } else {
                out.push_back(rest.front());
                at += 1;
            }
            continue;
        }
        if (rest.front() == '"') {
            in_string = false;
            out.push_back('"');
            at += 1;
        } else if (rest.size() >= 2 && rest[0] == '\\' && rest[1] != 'u') {
            out.append(rest.substr(0, 2));
            at += 2;
        } else if (rest.size() >= 6 && rest.starts_with("\\u") && is_hex4(rest.substr(2, 4))) {
            const std::uint32_t unit = hex4_value(rest.substr(2, 4));
            if (is_high_surrogate(unit) && rest.size() >= 12 && rest.substr(6, 2) == "\\u"
                && is_hex4(rest.substr(8, 4)) && is_low_surrogate(hex4_value(rest.substr(8, 4)))) {
                out.append(rest.substr(0, 12));
                at += 12;
            } else if (is_surrogate(unit)) {
                out.append(escaped_marker);
                out.append(rest.substr(2, 4));
                at += 6;
            } else {
                out.append(rest.substr(0, 6));
                at += 6;
            }
        } else {
            out.push_back(rest.front());
            at += 1;
        }
    }
    return out;
}

void append_surrogate(std::string& out, std::uint32_t unit)
{
    out.push_back(static_cast<char>(0xE0 | (unit >> 12)));
    out.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
}

std::string restore_surrogates(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t found = text.find(decoded_marker, at);
        if (found == std::string_view::npos) {
            out.append(text.substr(at));
            break;
        }
        out.append(text.substr(at, found - at));
        const std::size_t digits_at = found + decoded_marker.size();
        const std::string_view digits = text.substr(digits_at, 4);
        if (is_hex4(digits) && is_surrogate(hex4_value(digits))) {
            append_surrogate(out, hex4_value(digits));
            at = digits_at + 4;
        } else {
            out.append(decoded_marker);
            at = digits_at;
        }
    }
    return out;
}

std::optional<Value> special_value(std::string_view text)
{
    if (!text.starts_with(decoded_marker)) {
        return std::nullopt;
    }
    const std::string_view payload = text.substr(decoded_marker.size());
    if (payload == "NaN") {
        return Value(std::numeric_limits<double>::quiet_NaN());
    }
    if (payload == "Infinity") {
        return Value(std::numeric_limits<double>::infinity());
    }
    if (payload == "-Infinity") {
        return Value(-std::numeric_limits<double>::infinity());
    }
    return std::nullopt;
}

void restore_tokens(Value& value)
{
    if (value.is_string()) {
        const std::string& text = value.get_ref<const std::string&>();
        if (std::optional<Value> special = special_value(text)) {
            value = std::move(*special);
        } else if (text.find(decoded_marker) != std::string::npos) {
            value = restore_surrogates(text);
        }
    } else if (value.is_array()) {
        for (auto& item : value) {
            restore_tokens(item);
        }
    } else if (value.is_object()) {
        Value restored = Value::object();
        for (auto&& [key, item] : value.items()) {
            restore_tokens(item);
            restored[restore_surrogates(key)] = std::move(item);
        }
        value = std::move(restored);
    }
}

}

std::optional<Value> parse(std::string_view text)
{
    try {
        return Value::parse(text);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

std::optional<Value> parse_python(std::string_view text)
{
    try {
        const std::string bridged = bridge_python_tokens(text);
        Value value = Value::parse(bridged);
        restore_tokens(value);
        return value;
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
