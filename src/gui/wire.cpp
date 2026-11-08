#include "gui/wire.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "core/text.hpp"

namespace hyperagent::gui::wire {

namespace {

std::uint32_t byte_at(const std::string& raw, std::size_t offset)
{
    return static_cast<unsigned char>(raw[offset]);
}

bool is_encoded_surrogate(const std::string& raw, std::size_t offset)
{
    return offset + 2 < raw.size() && byte_at(raw, offset) == 0xED && byte_at(raw, offset + 1) >= 0xA0
        && byte_at(raw, offset + 1) <= 0xBF && (byte_at(raw, offset + 2) & 0xC0) == 0x80;
}

std::uint32_t encoded_surrogate(const std::string& raw, std::size_t offset)
{
    return 0xD000 | ((byte_at(raw, offset + 1) & 0x3F) << 6) | (byte_at(raw, offset + 2) & 0x3F);
}

void append_unit(std::string& out, std::uint32_t unit)
{
    char escaped[8];
    std::snprintf(escaped, sizeof escaped, "\\u%04x", unit);
    out += escaped;
}

}

std::string dumps_ascii(const Value& value)
{
    const std::string raw = core::json::dumps(value, core::json::spaced);
    std::string out;
    out.reserve(raw.size());
    for (std::size_t offset = 0; offset < raw.size();) {
        const auto byte = static_cast<unsigned char>(raw[offset]);
        if (byte >= 0x20 && byte < 0x7f) {
            out.push_back(raw[offset]);
            ++offset;
        } else if (byte < 0x80) {
            append_unit(out, byte);
            ++offset;
        } else if (is_encoded_surrogate(raw, offset)) {
            append_unit(out, encoded_surrogate(raw, offset));
            offset += 3;
        } else {
            const core::text::Decoded decoded = core::text::decode_at(raw, offset);
            if (decoded.code_point >= 0x10000) {
                const char32_t shifted = decoded.code_point - 0x10000;
                append_unit(out, 0xD800 + (shifted >> 10));
                append_unit(out, 0xDC00 + (shifted & 0x3FF));
            } else {
                append_unit(out, decoded.code_point);
            }
            offset += decoded.size;
        }
    }
    return out;
}

Value array_of(const std::vector<Value>& items)
{
    Value array = Value::array();
    for (const auto& item : items) {
        array.push_back(item);
    }
    return array;
}

double epoch_of(const Value& record)
{
    const Value* epoch = core::json::member(&record, "epoch");
    return epoch != nullptr && epoch->is_number() ? epoch->get<double>() : 0.0;
}

void sort_by_epoch(std::vector<Value>& records)
{
    std::stable_sort(records.begin(), records.end(),
                     [](const Value& left, const Value& right) { return epoch_of(left) < epoch_of(right); });
}

}
