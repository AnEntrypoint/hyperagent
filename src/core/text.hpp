#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hyperagent::core::text {

struct Decoded {
    char32_t code_point;
    std::size_t size;
};

Decoded decode_at(std::string_view bytes, std::size_t offset);
std::size_t code_point_count(std::string_view text);
std::string_view code_point_prefix(std::string_view text, std::size_t count);
std::string code_point_slice(std::string_view text, std::size_t begin, std::size_t end);
std::string pad_right(std::string_view text, std::size_t width);
std::string sanitize_utf8(std::string_view bytes);
std::string truncate(std::string_view text, long limit);
std::string replace_all(std::string_view text, std::string_view from, std::string_view to);
std::string_view strip(std::string_view text);
std::string_view rstrip(std::string_view text);
std::vector<std::string> splitlines(std::string_view text);
bool is_python_space(char32_t code_point);

}
