#include "mcp/outputs.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <utility>

#include "core/files.hpp"
#include "core/text.hpp"
#include "journal/records.hpp"
#include "mcp/args.hpp"
#include "mcp/events.hpp"
#include "orch/orch.hpp"

namespace hyperagent::mcp::outputs {

namespace {

namespace journal_api = hyperagent::journal;

std::vector<Value> python_slice_head(std::vector<Value> rows, long long limit)
{
    if (limit > 0 && static_cast<std::size_t>(limit) < rows.size()) {
        rows.resize(static_cast<std::size_t>(limit));
    } else if (limit < 0) {
        const auto drop = static_cast<std::size_t>(-limit);
        rows.resize(drop < rows.size() ? rows.size() - drop : 0);
    }
    return rows;
}

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

char32_t simple_lower(char32_t code_point)
{
    if (code_point >= 'A' && code_point <= 'Z') {
        return code_point + 0x20;
    }
    if (code_point >= 0xC0 && code_point <= 0xDE && code_point != 0xD7) {
        return code_point + 0x20;
    }
    if (code_point >= 0x391 && code_point <= 0x3A9 && code_point != 0x3A2) {
        return code_point + 0x20;
    }
    if (code_point >= 0x410 && code_point <= 0x42F) {
        return code_point + 0x20;
    }
    if (code_point >= 0x400 && code_point <= 0x40F) {
        return code_point + 0x50;
    }
    return code_point;
}

std::string lowercase(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t offset = 0; offset < text.size();) {
        const core::text::Decoded decoded = core::text::decode_at(text, offset);
        append_utf8(out, simple_lower(decoded.code_point));
        offset += decoded.size;
    }
    return out;
}

Value field(const Value& row, std::string_view key)
{
    const Value* found = core::json::member(&row, key);
    return found != nullptr ? *found : Value();
}

std::string header_text(const Value& row, const std::string& path)
{
    return "# " + core::json::py_str(core::json::member(&row, "agent")) + " "
        + core::json::py_str(core::json::member(&row, "session")) + " harness="
        + core::json::py_str(core::json::member(&row, "harness")) + " cwd="
        + core::json::py_str(core::json::member(&row, "cwd")) + " " + path;
}

}

std::optional<std::string> output_cwd(const Value& args)
{
    std::optional<std::string> cwd = optional_text_arg(args, "cwd");
    const std::optional<std::string> project = optional_text_arg(args, "project");
    if (!cwd && project) {
        orch::ProjectSelector selector;
        selector.project = project;
        selector.harness = optional_text_arg(args, "harness");
        const Value target = orch::project_target(selector);
        cwd = text_of(core::json::member(&target, "cwd"));
    }
    return cwd;
}

std::vector<std::string> output_kinds(const Value& args)
{
    const Value* raw = arg(args, "kinds");
    std::vector<std::string> wanted;
    if (raw == nullptr || raw->is_null()) {
        return wanted;
    }
    if (raw->is_string()) {
        const std::string text = raw->get<std::string>();
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t comma = std::min(text.find(',', start), text.size());
            const std::string item(core::text::strip(std::string_view(text).substr(start, comma - start)));
            if (!item.empty()) {
                wanted.push_back(item);
            }
            start = comma + 1;
        }
        return wanted;
    }
    if (raw->is_array()) {
        for (const auto& item : *raw) {
            if (core::json::truthy(&item)) {
                wanted.push_back(core::json::py_str(&item));
            }
        }
        return wanted;
    }
    return text_list(raw);
}

std::string list_outputs(const Value& args)
{
    const long long limit = int_arg(args, "limit", 50);
    const auto rows = journal_api::scan(std::nullopt, output_cwd(args), optional_text_arg(args, "agent"),
                                        optional_text_arg(args, "session_id"));
    Value out = Value::array();
    for (const auto& row : python_slice_head(rows, limit)) {
        out.push_back(row);
    }
    return dumps_indented(out);
}

std::string read_output(const Value& args)
{
    const auto row = journal_api::find_session(optional_text_arg(args, "agent"), output_cwd(args),
                                               optional_text_arg(args, "session_id"));
    if (!row) {
        throw ToolFailure("ValueError", "no session output found");
    }
    const std::string path = core::json::py_str(core::json::member(&*row, "path"));
    const auto tail = static_cast<long>(int_arg(args, "tail", 40));
    const std::vector<std::string> kinds = output_kinds(args);
    std::vector<Value> records;
    const Value* since = arg(args, "since");
    if (since == nullptr || since->is_null()) {
        records = journal_api::tail_records(path, tail, kinds);
    } else {
        records = journal_api::read_records(path, kinds, static_cast<std::size_t>(std::max(0L, tail)),
                                            to_float(since));
    }
    if (text_arg(args, "format") == "json") {
        Value list = Value::array();
        for (const auto& record : records) {
            list.push_back(record);
        }
        Value out = Value::object();
        out["session"] = *row;
        out["records"] = list;
        return dumps_indented(out);
    }
    return journal_api::render_records(records, header_text(*row, path),
                                       static_cast<long>(int_arg(args, "max_chars", 20000)),
                                       static_cast<long>(int_arg(args, "per_record_chars", 2000)));
}

std::string search_output(const Value& args)
{
    const std::string query = text_arg(args, "query");
    if (query.empty()) {
        throw ToolFailure("ValueError", "query is required");
    }
    const Value* ignore = arg(args, "ignore_case");
    const bool ignore_case = ignore == nullptr ? true : core::json::truthy(ignore);
    const std::string needle = ignore_case ? lowercase(query) : query;
    const long long limit = int_arg(args, "limit", 50);
    const auto max_chars = static_cast<long>(int_arg(args, "max_chars", 500));
    const auto rows = journal_api::scan(std::nullopt, output_cwd(args), optional_text_arg(args, "agent"),
                                        optional_text_arg(args, "session_id"));
    Value found = Value::array();
    for (const auto& row : rows) {
        const std::string path = core::json::py_str(core::json::member(&row, "path"));
        for (const auto& record : journal_api::read_records(path)) {
            const std::string text = text_of(core::json::member(&record, "text"));
            const std::string haystack = ignore_case ? lowercase(text) : text;
            if (haystack.find(needle) == std::string::npos) {
                continue;
            }
            Value item = Value::object();
            item["agent"] = field(row, "agent");
            item["session"] = field(row, "session");
            item["kind"] = field(record, "kind");
            item["ts"] = field(record, "ts");
            item["text"] = core::text::truncate(text, max_chars);
            item["path"] = field(row, "path");
            found.push_back(item);
            if (static_cast<long long>(found.size()) >= limit) {
                return dumps_indented(found);
            }
        }
    }
    return dumps_indented(found);
}

}

namespace hyperagent::mcp::resources {

namespace {

namespace journal_api = hyperagent::journal;

constexpr std::string_view outputs_uri = "hyperagent://outputs";
constexpr std::string_view events_uri = "hyperagent://events";
constexpr std::string_view output_prefix = "hyperagent://outputs/";

std::string strip_slashes(std::string_view text)
{
    std::size_t first = 0;
    while (first < text.size() && text[first] == '/') {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && text[last - 1] == '/') {
        --last;
    }
    return std::string(text.substr(first, last - first));
}

std::string rstrip_slashes(std::string_view text)
{
    std::size_t last = text.size();
    while (last > 0 && text[last - 1] == '/') {
        --last;
    }
    return std::string(text.substr(0, last));
}

Value object_of(std::initializer_list<std::pair<std::string, Value>> entries)
{
    Value out = Value::object();
    for (const auto& [key, item] : entries) {
        out[key] = item;
    }
    return out;
}

std::vector<std::string> split_slash(const std::string& text)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t slash = text.find('/', start);
        parts.push_back(text.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos) {
            return parts;
        }
        start = slash + 1;
    }
}

}

std::string uri_for_session(const Value& row)
{
    return std::string(output_prefix) + core::json::py_str(core::json::member(&row, "project")) + "/"
        + core::json::py_str(core::json::member(&row, "session"));
}

Value list()
{
    Value items = Value::array();
    items.push_back(object_of({{"uri", std::string(outputs_uri)}, {"name", "session-output-index"},
                               {"description", "Every journaled peer session"},
                               {"mimeType", "application/json"}}));
    items.push_back(object_of({{"uri", std::string(events_uri)}, {"name", "monitor-events"},
                               {"description", "Idle and gone events from the peer monitor"},
                               {"mimeType", "application/x-ndjson"}}));
    for (const auto& row : journal_api::scan(std::nullopt, std::nullopt, std::nullopt, std::nullopt, 200)) {
        const std::string joined = text_of(core::json::member(&row, "harness")) + " "
            + text_of(core::json::member(&row, "cwd")) + " " + text_of(core::json::member(&row, "last_kind"));
        const std::string description(core::text::strip(joined));
        items.push_back(object_of({{"uri", uri_for_session(row)},
                                   {"name", core::json::py_str(core::json::member(&row, "agent")) + " "
                                       + core::json::py_str(core::json::member(&row, "session"))},
                                   {"description", description},
                                   {"mimeType", "application/x-ndjson"}}));
    }
    return items;
}

Value templates()
{
    Value items = Value::array();
    items.push_back(object_of({{"uriTemplate", "hyperagent://outputs/{project}/{session}"},
                               {"name", "session-output"},
                               {"description", "Raw journal for one peer session"},
                               {"mimeType", "application/x-ndjson"}}));
    items.push_back(object_of({{"uriTemplate", "hyperagent://outputs/{project}/{session}/view"},
                               {"name", "session-output-view"},
                               {"description", "Compact rendered journal for one peer session"},
                               {"mimeType", "text/plain"}}));
    return items;
}

bool valid_uri(const std::string& uri)
{
    const std::string bare = rstrip_slashes(uri);
    return bare == outputs_uri || bare == events_uri || std::string_view(bare).starts_with(output_prefix);
}

Content read(const std::string& uri)
{
    const std::string bare = rstrip_slashes(uri);
    if (bare == events_uri) {
        std::string text;
        bool first = true;
        for (const auto& event : event_book().recent(100)) {
            if (!first) {
                text += "\n";
            }
            first = false;
            text += core::json::dumps(event);
        }
        return {text, "application/x-ndjson"};
    }
    if (bare == outputs_uri) {
        Value rows = Value::array();
        for (const auto& row : journal_api::scan(std::nullopt, std::nullopt, std::nullopt, std::nullopt, 500)) {
            rows.push_back(row);
        }
        return {dumps_indented(rows), "application/json"};
    }
    if (!std::string_view(uri).starts_with(output_prefix)) {
        throw ToolFailure("ValueError", "unknown resource " + uri);
    }
    std::string rest = strip_slashes(std::string_view(uri).substr(output_prefix.size()));
    const bool view = rest.size() >= 5 && rest.compare(rest.size() - 5, 5, "/view") == 0;
    if (view) {
        rest.erase(rest.size() - 5);
    }
    const std::vector<std::string> parts = split_slash(rest);
    if (parts.size() != 2 || parts[0].empty() || parts[1].empty()) {
        throw ToolFailure("ValueError", "bad resource " + uri);
    }
    const std::string& project = parts[0];
    const std::string& session = parts[1];
    std::optional<Value> row;
    for (const auto& candidate : journal_api::scan(std::nullopt, std::nullopt, std::nullopt, session)) {
        if (core::json::py_str(core::json::member(&candidate, "project")) == project) {
            row = candidate;
            break;
        }
    }
    if (!row) {
        throw ToolFailure("ValueError", "no session output " + project + "/" + session);
    }
    const std::string path = core::json::py_str(core::json::member(&*row, "path"));
    if (view) {
        const std::string header = "# " + core::json::py_str(core::json::member(&*row, "agent")) + " " + session
            + " " + path;
        return {journal_api::render_records(journal_api::tail_records(path, 200), header), "text/plain"};
    }
    const auto bytes = core::files::read_bytes(path);
    if (!bytes) {
        const Value path_value(path);
        throw ToolFailure("OSError", "[Errno 2] No such file or directory: " + python_repr(&path_value));
    }
    return {core::text::sanitize_utf8(*bytes), "application/x-ndjson"};
}

}
