#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace hyperagent::core::update {

struct Asset {
    std::string file;
    std::string sha256;
    std::uintmax_t size = 0;
    std::string version;
    std::optional<std::int64_t> committed;
};

struct Manifest {
    std::string version;
    std::string commit;
    std::string built;
    std::string repo;
    std::map<std::string, Asset> assets;
};

struct Policy {
    std::string url;
    std::filesystem::path root;
    bool enabled = false;
    std::string disabled_reason;
    std::string current_version;
    std::string current_commit;
    std::optional<std::int64_t> current_committed;
};

struct Result {
    bool ok = false;
    bool updated = false;
    std::string from;
    std::string to;
    std::string commit;
    std::string status;
    std::string error;
};

std::string target();
Policy policy_from_environment();
Manifest parse_manifest(std::string_view text);
void verify_archive(const std::filesystem::path& archive, const Asset& asset);
void switch_version(const std::filesystem::path& unpacked, const std::filesystem::path& root,
                    const std::string& version);
Result check(const Policy& policy);
void start_background();

}
