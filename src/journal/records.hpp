#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::journal {

using Value = core::json::Value;
using Path = std::filesystem::path;

struct LocatedRecord {
    Path path;
    Value record;
};

struct LatestOutput {
    std::optional<Value> summary;
    std::optional<Value> record;
};

std::optional<Path> nonempty_root(const std::optional<Path>& root);

std::vector<Value> read_records(const Path& path, const std::vector<std::string>& kinds = {},
                                std::optional<std::size_t> limit = std::nullopt,
                                std::optional<double> since = std::nullopt);
std::vector<Value> tail_records(const Path& path, long limit = 40,
                                const std::vector<std::string>& kinds = {},
                                std::uintmax_t max_bytes = 1000000);
std::optional<Value> last_record(const Path& path, const std::vector<std::string>& kinds = {});

std::optional<Value> summarize(const Path& path);
std::vector<Value> scan(const std::optional<Path>& root = std::nullopt,
                        const std::optional<std::string>& cwd = std::nullopt,
                        const std::optional<std::string>& agent = std::nullopt,
                        const std::optional<std::string>& session_id = std::nullopt,
                        std::size_t limit = 0);
std::optional<Value> find_session(const std::optional<std::string>& agent = std::nullopt,
                                  const std::optional<std::string>& cwd = std::nullopt,
                                  const std::optional<std::string>& session_id = std::nullopt,
                                  const std::optional<Path>& root = std::nullopt);

std::optional<LocatedRecord> last_response(const std::string& cwd, const std::string& agent,
                                           const std::optional<Path>& root = std::nullopt);
LatestOutput latest_output(const std::string& cwd, const std::string& agent,
                           const std::optional<Path>& root = std::nullopt);
std::optional<LocatedRecord> latest_result(const std::string& cwd, const std::string& agent,
                                           const std::optional<Path>& root = std::nullopt);

std::string render_records(const std::vector<Value>& records, const std::string& header = "",
                           long max_chars = 20000, long per_record = 2000);

}
