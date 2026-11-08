#include "core/text.hpp"

namespace hyperagent::core::text {

namespace {

constexpr char32_t replacement_character = 0xFFFD;

void append_utf8(std::string& out, char32_t code_point)
{
    if (code_point < 0x80) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

}

Decoded decode_at(std::string_view bytes, std::size_t offset)
{
    const auto lead = static_cast<unsigned char>(bytes[offset]);
    if (lead < 0x80) {
        return {lead, 1};
    }

    std::size_t continuation = 0;
    char32_t code_point = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        continuation = 1;
        code_point = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        continuation = 2;
        code_point = lead & 0x0F;
        if (lead == 0xE0) {
            low = 0xA0;
        }
        if (lead == 0xED) {
            high = 0x9F;
        }
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        continuation = 3;
        code_point = lead & 0x07;
        if (lead == 0xF0) {
            low = 0x90;
        }
        if (lead == 0xF4) {
            high = 0x8F;
        }
    } else {
        return {replacement_character, 1};
    }

    for (std::size_t index = 0; index < continuation; ++index) {
        if (offset + 1 + index >= bytes.size()) {
            return {replacement_character, index + 1};
        }
        const auto next = static_cast<unsigned char>(bytes[offset + 1 + index]);
        const unsigned char lower = index == 0 ? low : 0x80;
        const unsigned char upper = index == 0 ? high : 0xBF;
        if (next < lower || next > upper) {
            return {replacement_character, index + 1};
        }
        code_point = (code_point << 6) | (next & 0x3F);
    }
    return {code_point, continuation + 1};
}

std::size_t code_point_count(std::string_view text)
{
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < text.size(); ++count) {
        offset += decode_at(text, offset).size;
    }
    return count;
}

std::string_view code_point_prefix(std::string_view text, std::size_t count)
{
    std::size_t offset = 0;
    for (std::size_t seen = 0; seen < count && offset < text.size(); ++seen) {
        offset += decode_at(text, offset).size;
    }
    return text.substr(0, offset);
}

std::string code_point_slice(std::string_view text, std::size_t begin, std::size_t end)
{
    std::size_t offset = 0;
    std::size_t index = 0;
    while (index < begin && offset < text.size()) {
        offset += decode_at(text, offset).size;
        ++index;
    }
    const std::size_t start = offset;
    while (index < end && offset < text.size()) {
        offset += decode_at(text, offset).size;
        ++index;
    }
    return std::string(text.substr(start, offset - start));
}

std::string pad_right(std::string_view text, std::size_t width)
{
    std::string out(text);
    const std::size_t count = code_point_count(text);
    if (count < width) {
        out.append(width - count, ' ');
    }
    return out;
}

std::string sanitize_utf8(std::string_view bytes)
{
    std::string out;
    out.reserve(bytes.size());
    for (std::size_t offset = 0; offset < bytes.size();) {
        const Decoded decoded = decode_at(bytes, offset);
        if (decoded.code_point == replacement_character) {
            append_utf8(out, replacement_character);
        } else {
            out.append(bytes.substr(offset, decoded.size));
        }
        offset += decoded.size;
    }
    return out;
}

std::string truncate(std::string_view text, long limit)
{
    if (limit <= 0 || code_point_count(text) <= static_cast<std::size_t>(limit)) {
        return std::string(text);
    }
    const auto kept = static_cast<std::size_t>(limit);
    const std::size_t cut = code_point_count(text) - kept;
    std::string out(code_point_prefix(text, kept));
    out += "\xE2\x80\xA6 [" + std::to_string(cut) + " chars cut]";
    return out;
}

std::string replace_all(std::string_view text, std::string_view from, std::string_view to)
{
    if (from.empty()) {
        return std::string(text);
    }
    std::string out;
    std::size_t position = 0;
    while (true) {
        const std::size_t found = text.find(from, position);
        if (found == std::string_view::npos) {
            out.append(text.substr(position));
            break;
        }
        out.append(text.substr(position, found - position));
        out.append(to);
        position = found + from.size();
    }
    return out;
}

bool is_python_space(char32_t code_point)
{
    if ((code_point >= 0x09 && code_point <= 0x0D) || (code_point >= 0x1C && code_point <= 0x20)) {
        return true;
    }
    switch (code_point) {
    case 0x85: case 0xA0: case 0x1680: case 0x2028: case 0x2029:
    case 0x202F: case 0x205F: case 0x3000:
        return true;
    default:
        return code_point >= 0x2000 && code_point <= 0x200A;
    }
}

std::string_view strip(std::string_view text)
{
    std::size_t first = std::string_view::npos;
    std::size_t last_end = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        const Decoded decoded = decode_at(text, offset);
        if (!is_python_space(decoded.code_point)) {
            if (first == std::string_view::npos) {
                first = offset;
            }
            last_end = offset + decoded.size;
        }
        offset += decoded.size;
    }
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, last_end - first);
}

std::string_view rstrip(std::string_view text)
{
    std::size_t last_end = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        const Decoded decoded = decode_at(text, offset);
        if (!is_python_space(decoded.code_point)) {
            last_end = offset + decoded.size;
        }
        offset += decoded.size;
    }
    return text.substr(0, last_end);
}

std::vector<std::string> splitlines(std::string_view text)
{
    std::vector<std::string> lines;
    std::string current;
    for (std::size_t offset = 0; offset < text.size();) {
        const Decoded decoded = decode_at(text, offset);
        const char32_t cp = decoded.code_point;
        const bool boundary = (cp >= 0x0A && cp <= 0x0D) || (cp >= 0x1C && cp <= 0x1E)
            || cp == 0x85 || cp == 0x2028 || cp == 0x2029;
        if (boundary) {
            lines.push_back(std::move(current));
            current.clear();
            if (cp == 0x0D && offset + 1 < text.size() && text[offset + 1] == '\n') {
                offset += 1;
            }
        } else {
            current.append(text.substr(offset, decoded.size));
        }
        offset += decoded.size;
    }
    if (!current.empty()) {
        lines.push_back(std::move(current));
    }
    return lines;
}

}
