#include "gui/tail.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <utility>

#include "core/clock.hpp"
#include "core/files.hpp"
#include "core/json.hpp"
#include "core/text.hpp"
#include "gui/constants.hpp"

namespace hyperagent::gui {

namespace {

using Segment = std::pair<std::uintmax_t, std::string>;

bool is_blank(std::string_view bytes)
{
    return bytes.find_first_not_of(" \t\n\r\v\f") == std::string_view::npos;
}

std::optional<std::string> read_range(const std::filesystem::path& path, std::uintmax_t start, std::uintmax_t length)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }
    stream.seekg(static_cast<std::streamoff>(start));
    std::string bytes(length, '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(length));
    bytes.resize(static_cast<std::size_t>(stream.gcount()));
    return bytes;
}

Value make_record(RecordKind kind, const std::string& source, std::uintmax_t position, std::string_view line)
{
    return kind == RecordKind::log ? log_record(source, position, line) : journal_record(source, position, line);
}

std::vector<Segment> read_last_lines(const std::filesystem::path& path, std::uintmax_t end, std::size_t count)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::uintmax_t position = end;
    std::string block;
    std::size_t newlines = 0;
    while (position > 0 && newlines <= count) {
        const auto step = std::min<std::uintmax_t>(constants::read_block, position);
        position -= step;
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(position));
        std::string piece(step, '\0');
        stream.read(piece.data(), static_cast<std::streamsize>(step));
        piece.resize(static_cast<std::size_t>(stream.gcount()));
        newlines += static_cast<std::size_t>(std::count(piece.begin(), piece.end(), '\n'));
        block.insert(0, piece);
    }

    bool starts_mid_line = false;
    if (position > 0) {
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(position - 1));
        char symbol = '\0';
        stream.read(&symbol, 1);
        starts_mid_line = stream.gcount() != 1 || symbol != '\n';
    }

    std::vector<Segment> entries;
    std::uintmax_t cursor = position;
    std::size_t start = 0;
    for (std::size_t newline = block.find('\n'); newline != std::string::npos; newline = block.find('\n', start)) {
        entries.emplace_back(cursor, block.substr(start, newline - start));
        cursor += newline - start + 1;
        start = newline + 1;
    }
    if (starts_mid_line && !entries.empty()) {
        entries.erase(entries.begin());
    }

    std::vector<Segment> kept;
    for (auto& entry : entries) {
        if (!is_blank(entry.second)) {
            kept.push_back(std::move(entry));
        }
    }
    if (kept.size() > count) {
        kept.erase(kept.begin(), kept.end() - static_cast<std::ptrdiff_t>(count));
    }
    return kept;
}

}

Value journal_record(const std::string& source, std::uintmax_t position, std::string_view line)
{
    const std::string text = core::text::sanitize_utf8(line);
    std::optional<Value> parsed = core::json::parse(text);
    Value record;
    if (parsed && parsed->is_object()) {
        record = std::move(*parsed);
    } else {
        record = Value::object();
        record["kind"] = "malformed";
        record["text"] = text;
    }
    if (!record.contains("epoch")) {
        record["epoch"] = core::clock::now_epoch();
    }
    if (!record.contains("agent")) {
        record["agent"] = source.substr(0, source.find('/'));
    }
    record["src"] = source;
    record["id"] = source + "@" + std::to_string(position);
    return record;
}

Value log_record(const std::string& source, std::uintmax_t position, std::string_view line)
{
    Value record = Value::object();
    record["id"] = source + "@" + std::to_string(position);
    record["src"] = source;
    record["kind"] = "log";
    record["agent"] = "airc";
    record["session"] = "server.log";
    record["epoch"] = core::clock::now_epoch();
    record["text"] = core::text::sanitize_utf8(line);
    return record;
}

std::uintmax_t last_line_break(const std::filesystem::path& path)
{
    const auto info = core::files::stat(path);
    if (!info || info->size == 0) {
        return 0;
    }
    const std::uintmax_t size = info->size;
    const std::uintmax_t start = size > constants::read_block ? size - constants::read_block : 0;
    const auto block = read_range(path, start, size - start);
    if (!block) {
        return 0;
    }
    const std::size_t index = block->rfind('\n');
    return index == std::string::npos ? 0 : start + index + 1;
}

Tail::Tail(std::filesystem::path path, std::string source, RecordKind kind)
    : path_(std::move(path)), source_(std::move(source)), kind_(kind)
{
}

void Tail::start_at_end()
{
    read_offset_ = complete_end_ = last_line_break(path_);
}

std::vector<Value> Tail::read_new()
{
    const auto info = core::files::stat(path_);
    if (!info) {
        return {};
    }
    const std::uintmax_t size = info->size;
    if (size < read_offset_) {
        read_offset_ = 0;
        complete_end_ = 0;
        carry_.clear();
    }
    if (size <= read_offset_) {
        return {};
    }
    const auto chunk = read_range(path_, read_offset_, size - read_offset_);
    if (!chunk) {
        return {};
    }

    const std::string data = carry_ + *chunk;
    std::uintmax_t position = complete_end_;
    std::vector<Value> records;
    std::size_t start = 0;
    for (std::size_t newline = data.find('\n'); newline != std::string::npos; newline = data.find('\n', start)) {
        const std::string_view piece(data.data() + start, newline - start);
        if (!is_blank(piece)) {
            records.push_back(make_record(kind_, source_, position, piece));
        }
        position += piece.size() + 1;
        start = newline + 1;
    }
    carry_ = data.substr(start);
    read_offset_ = size;
    complete_end_ = position;
    return records;
}

std::vector<Value> Tail::last_records(std::size_t count) const
{
    if (complete_end_ == 0) {
        return {};
    }
    std::vector<Value> records;
    for (const auto& [position, line] : read_last_lines(path_, complete_end_, count)) {
        records.push_back(make_record(kind_, source_, position, line));
    }
    return records;
}

const std::filesystem::path& Tail::path() const
{
    return path_;
}

}
