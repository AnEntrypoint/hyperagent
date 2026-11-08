#include "core/files.hpp"

#include <sys/stat.h>

#include <fstream>
#include <iterator>
#include <system_error>

namespace hyperagent::core::files {

std::optional<std::string> read_bytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::optional<std::string> read_tail_bytes(const std::filesystem::path& path, std::uintmax_t max_bytes)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    stream.seekg(0, std::ios::end);
    const auto size = static_cast<std::uintmax_t>(stream.tellg());
    const std::uintmax_t start = size > max_bytes ? size - max_bytes : 0;
    stream.seekg(static_cast<std::streamoff>(start), std::ios::beg);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void ensure_parent_directory(const std::filesystem::path& path)
{
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
}

void write_bytes(const std::filesystem::path& path, std::string_view bytes)
{
    ensure_parent_directory(path);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void append_bytes(const std::filesystem::path& path, std::string_view bytes)
{
    ensure_parent_directory(path);
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::optional<FileStat> stat(const std::filesystem::path& path)
{
    struct ::stat info {};
    if (::stat(path.c_str(), &info) != 0) {
        return std::nullopt;
    }
    const double mtime = static_cast<double>(info.st_mtim.tv_sec) + static_cast<double>(info.st_mtim.tv_nsec) / 1e9;
    return FileStat{static_cast<std::uintmax_t>(info.st_size), mtime};
}

bool is_directory(const std::filesystem::path& path)
{
    std::error_code ignored;
    return std::filesystem::is_directory(path, ignored);
}

bool is_regular_file(const std::filesystem::path& path)
{
    std::error_code ignored;
    return std::filesystem::is_regular_file(path, ignored);
}

}
