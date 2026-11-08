#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::gui {

using Value = core::json::Value;

enum class RecordKind { journal, log };

Value journal_record(const std::string& source, std::uintmax_t position, std::string_view line);
Value log_record(const std::string& source, std::uintmax_t position, std::string_view line);
std::uintmax_t last_line_break(const std::filesystem::path& path);

class Tail {
public:
    Tail(std::filesystem::path path, std::string source, RecordKind kind);

    void start_at_end();
    std::vector<Value> read_new();
    std::vector<Value> last_records(std::size_t count) const;
    const std::filesystem::path& path() const;

private:
    std::filesystem::path path_;
    std::string source_;
    RecordKind kind_;
    std::uintmax_t read_offset_ = 0;
    std::uintmax_t complete_end_ = 0;
    std::string carry_;
};

}
