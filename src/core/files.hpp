#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hyperagent::core::files {

struct FileStat {
    std::uintmax_t size;
    double mtime_epoch;
};

std::optional<std::string> read_bytes(const std::filesystem::path& path);
std::optional<std::string> read_tail_bytes(const std::filesystem::path& path, std::uintmax_t max_bytes);
void write_bytes(const std::filesystem::path& path, std::string_view bytes);
void append_bytes(const std::filesystem::path& path, std::string_view bytes);
std::optional<FileStat> stat(const std::filesystem::path& path);
bool is_directory(const std::filesystem::path& path);
bool is_regular_file(const std::filesystem::path& path);
void ensure_parent_directory(const std::filesystem::path& path);

}
