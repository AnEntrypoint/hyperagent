#include "core/update.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <openssl/evp.h>

#include "core/files.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/platform.hpp"
#include "core/process.hpp"
#include "core/text.hpp"

namespace hyperagent::core::update {

namespace {

namespace fs = std::filesystem;

constexpr const char* default_url = "https://github.com/AnEntrypoint/hyperagent/releases/download/latest/manifest.json";
constexpr std::chrono::minutes check_interval{15};
constexpr std::chrono::minutes retry_interval{10};
constexpr std::chrono::minutes lock_stale{30};
constexpr const char* state_name = "update.json";
constexpr const char* lock_name = "update.lock";
constexpr const char* staging_name = "update-staging";
constexpr const char* versions_name = "versions";
constexpr const char* current_name = "current";
constexpr std::size_t failure_detail_limit = 240;

struct State {
    std::int64_t checked_ms = 0;
    std::string etag;
    std::string version;
    std::string manifest;
    std::string asset_sha;
};

struct Fetched {
    bool not_modified = false;
    std::string body;
    std::string etag;
};

class Cleanup {
public:
    explicit Cleanup(fs::path path) : path_(std::move(path)) {}
    ~Cleanup()
    {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    Cleanup(const Cleanup&) = delete;
    Cleanup& operator=(const Cleanup&) = delete;

private:
    fs::path path_;
};

std::string lowercase(std::string_view value)
{
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char symbol) { return static_cast<char>(std::tolower(symbol)); });
    return out;
}

std::string member_text(const json::Value& object, std::string_view key)
{
    return json::str_or_empty(json::member(&object, key));
}

bool plain_file_name(std::string_view name)
{
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\") == std::string_view::npos;
}

std::string url_base(const std::string& url)
{
    const std::size_t slash = url.rfind('/');
    return slash == std::string::npos ? url : url.substr(0, slash);
}

std::int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string sha256_hex(const std::string& bytes)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_Digest(bytes.data(), bytes.size(), digest, &length, EVP_sha256(), nullptr);
    std::string hex;
    for (unsigned int index = 0; index < length; ++index) {
        hex.push_back(digits[digest[index] >> 4]);
        hex.push_back(digits[digest[index] & 0x0F]);
    }
    return hex;
}

process::Result run_checked(const std::vector<std::string>& argv, const std::string& what)
{
    process::Result result = process::run_capture(argv);
    if (result.exit_code != 0) {
        std::string detail(text::strip(result.standard_error));
        if (detail.size() > failure_detail_limit) {
            detail.resize(failure_detail_limit);
        }
        throw std::runtime_error(what + " failed (exit " + std::to_string(result.exit_code) + ")"
                                 + (detail.empty() ? std::string() : ": " + detail));
    }
    return result;
}

std::string etag_of(const std::string& headers)
{
    std::string etag;
    std::istringstream lines(headers);
    for (std::string line; std::getline(lines, line);) {
        const std::string_view stripped = text::strip(line);
        if (stripped.starts_with("HTTP/")) {
            etag.clear();
        } else if (lowercase(stripped.substr(0, 5)) == "etag:") {
            etag = std::string(text::strip(stripped.substr(5)));
        }
    }
    return etag;
}

Fetched fetch_manifest(const std::string& url, const std::string& etag, const fs::path& scratch)
{
    fs::create_directories(scratch);
    const fs::path headers = scratch / "manifest.headers";
    const fs::path body = scratch / "manifest.json";
    std::vector<std::string> argv{"curl", "-fsSL", "--max-time", "60", "-D", headers.string(),
                                  "-o", body.string(), "-w", "%{http_code}"};
    if (!etag.empty()) {
        argv.push_back("-H");
        argv.push_back("If-None-Match: " + etag);
    }
    argv.push_back(url);
    const process::Result response = run_checked(argv, "fetch " + url);

    Fetched fetched;
    fetched.not_modified = std::string(text::strip(response.standard_output)) == "304";
    if (!fetched.not_modified) {
        fetched.body = files::read_bytes(body).value_or(std::string());
        fetched.etag = etag_of(files::read_bytes(headers).value_or(std::string()));
    }
    return fetched;
}

void download(const std::string& url, const fs::path& destination)
{
    run_checked({"curl", "-fsSL", "--max-time", "900", "-o", destination.string(), url}, "download " + url);
}

void unpack(const fs::path& archive, const fs::path& destination)
{
    fs::create_directories(destination);
    run_checked({"tar", "-xf", archive.string(), "-C", destination.string()}, "unpack " + archive.filename().string());
}

State read_state(const fs::path& root)
{
    State state;
    const auto saved = json::read_file(root / state_name);
    if (!saved || !saved->is_object()) {
        return state;
    }
    if (const json::Value* checked = json::member(&*saved, "checked_ms"); checked != nullptr && checked->is_number()) {
        state.checked_ms = checked->get<std::int64_t>();
    }
    state.etag = member_text(*saved, "etag");
    state.version = member_text(*saved, "version");
    state.manifest = member_text(*saved, "manifest");
    state.asset_sha = member_text(*saved, "assetSha");
    return state;
}

void write_state(const fs::path& root, const State& state)
{
    json::Value saved = json::Value::object();
    saved["checked_ms"] = state.checked_ms;
    saved["etag"] = state.etag;
    saved["version"] = state.version;
    saved["manifest"] = state.manifest;
    saved["assetSha"] = state.asset_sha;
    json::write_file(root / state_name, saved);
}

bool acquire_lock(const fs::path& lock)
{
    std::error_code ec;
    if (fs::create_directory(lock, ec)) {
        return true;
    }
    const fs::file_time_type modified = fs::last_write_time(lock, ec);
    if (ec || fs::file_time_type::clock::now() - modified < lock_stale) {
        return false;
    }
    fs::remove_all(lock, ec);
    if (ec) {
        return false;
    }
    return fs::create_directory(lock, ec);
}

std::string linked_version(const fs::path& root)
{
    std::error_code ec;
    const fs::path current = root / current_name;
    if (!fs::is_symlink(current, ec)) {
        return {};
    }
    return fs::read_symlink(current, ec).filename().string();
}

void prune(const fs::path& root, const std::string& version, const std::string& previous)
{
    const fs::path versions = root / versions_name;
    std::vector<fs::path> stale;
    for (const auto& entry : fs::directory_iterator(versions)) {
        const std::string name = entry.path().filename().string();
        if (name != version && name != previous) {
            stale.push_back(entry.path());
        }
    }
    for (const auto& entry : fs::directory_iterator(root)) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("previous-") && name != "previous-" + previous) {
            stale.push_back(entry.path());
        }
    }
    for (const auto& path : stale) {
        fs::remove_all(path);
    }
}

void apply(const Policy& policy, Result& result)
{
    const fs::path staging = policy.root / staging_name;
    const fs::path lock = policy.root / lock_name;
    if (!acquire_lock(lock)) {
        result.status = "another update is running";
        return;
    }
    const Cleanup unlock(lock);
    const Cleanup cleanup(staging);

    State state = read_state(policy.root);
    const Fetched fetched = fetch_manifest(policy.url, state.etag, staging);
    if (fetched.not_modified) {
        state.checked_ms = now_ms();
        write_state(policy.root, state);
        result.status = "already current";
        return;
    }

    const Manifest manifest = parse_manifest(fetched.body);
    result.commit = manifest.commit;
    state.checked_ms = now_ms();
    state.etag = fetched.etag;
    state.manifest = manifest.version;
    if (!policy.current_commit.empty() && manifest.commit.starts_with(policy.current_commit)) {
        write_state(policy.root, state);
        result.status = "already current";
        return;
    }

    const std::string name = target();
    const auto found = manifest.assets.find(name);
    if (found == manifest.assets.end()) {
        throw std::runtime_error("the manifest has no build for " + name);
    }
    const Asset& asset = found->second;
    if (!plain_file_name(asset.file)) {
        throw std::runtime_error("the manifest names " + asset.file + ", which is not a file name");
    }
    result.to = asset.version;
    if (asset.committed && policy.current_committed && *asset.committed <= *policy.current_committed) {
        write_state(policy.root, state);
        result.status = "kept " + policy.current_version + ": the published " + asset.version + " is not newer";
        return;
    }
    if (!state.asset_sha.empty() && lowercase(state.asset_sha) == lowercase(asset.sha256)) {
        state.version = asset.version;
        write_state(policy.root, state);
        result.status = "already current";
        return;
    }

    fs::create_directories(staging);
    const fs::path archive = staging / asset.file;
    download(url_base(policy.url) + "/" + asset.file, archive);
    verify_archive(archive, asset);

    const fs::path unpacked = staging / "unpacked";
    unpack(archive, unpacked);
    const fs::path tree = unpacked / ("hyperagent-" + name);
    if (!fs::is_regular_file(tree / "bin" / ("hyperagent" + std::string(platform::executable_suffix)))) {
        throw std::runtime_error("the archive " + asset.file + " has no bin/hyperagent");
    }
    switch_version(tree, policy.root, asset.version);

    state.version = asset.version;
    state.asset_sha = asset.sha256;
    write_state(policy.root, state);
    result.updated = true;
    result.status = "updated";
}

std::optional<std::int64_t> committed_time()
{
    if (const auto seconds = std::strtoll(HYPERAGENT_COMMIT_TIME, nullptr, 10); seconds > 0) {
        return static_cast<std::int64_t>(seconds);
    }
    return std::nullopt;
}

}

std::string target()
{
#if defined(_WIN32)
    const char* operating = "windows";
#elif defined(__APPLE__)
    const char* operating = "macos";
#else
    const char* operating = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* machine = "arm64";
#else
    const char* machine = "x64";
#endif
    return std::string(machine) + "-" + operating;
}

Policy policy_from_environment()
{
    Policy policy;
    policy.url = default_url;
    if (const char* override_url = std::getenv("HYPERAGENT_UPDATE_URL"); override_url != nullptr && *override_url != '\0') {
        policy.url = override_url;
    }
    policy.current_version = HYPERAGENT_VERSION;
    policy.current_commit = HYPERAGENT_COMMIT;
    policy.current_committed = committed_time();

    const fs::path executable = paths::executable_path();
    const fs::path versions = paths::resolve(paths::user_data_dir() / versions_name);
    const bool installed = !executable.empty()
        && paths::resolve(executable.parent_path().parent_path().parent_path()) == versions;
    if (!installed) {
        policy.disabled_reason = "this build does not run from " + versions.string();
        return policy;
    }
    policy.root = versions.parent_path();
    const char* auto_update = std::getenv("HYPERAGENT_AUTO_UPDATE");
    if (auto_update != nullptr && std::string_view(auto_update) == "0") {
        policy.disabled_reason = "HYPERAGENT_AUTO_UPDATE is 0";
    }
    policy.enabled = policy.disabled_reason.empty();
    return policy;
}

Manifest parse_manifest(std::string_view document)
{
    const auto parsed = json::parse(document);
    if (!parsed || !parsed->is_object()) {
        throw std::runtime_error("the manifest is not a JSON object");
    }
    Manifest manifest;
    manifest.version = member_text(*parsed, "version");
    manifest.commit = member_text(*parsed, "commit");
    manifest.built = member_text(*parsed, "built");
    manifest.repo = member_text(*parsed, "repo");
    if (manifest.version.empty()) {
        throw std::runtime_error("the manifest has no version");
    }
    const json::Value* assets = json::member(&*parsed, "assets");
    if (assets == nullptr || !assets->is_object()) {
        throw std::runtime_error("the manifest has no assets");
    }
    for (const auto& [name, entry] : assets->items()) {
        if (!entry.is_object()) {
            continue;
        }
        Asset asset;
        asset.file = member_text(entry, "file");
        asset.sha256 = member_text(entry, "sha256");
        asset.version = member_text(entry, "version");
        if (asset.version.empty()) {
            asset.version = manifest.version;
        }
        if (const json::Value* size = json::member(&entry, "size"); size != nullptr && size->is_number()) {
            asset.size = size->get<std::uintmax_t>();
        }
        if (const json::Value* committed = json::member(&entry, "committed"); committed != nullptr && committed->is_number()) {
            asset.committed = committed->get<std::int64_t>();
        }
        manifest.assets[name] = std::move(asset);
    }
    return manifest;
}

void verify_archive(const fs::path& archive, const Asset& asset)
{
    const auto bytes = files::read_bytes(archive);
    if (!bytes) {
        throw std::runtime_error("the archive " + asset.file + " is missing");
    }
    if (asset.size != 0 && bytes->size() != asset.size) {
        throw std::runtime_error("the archive " + asset.file + " is " + std::to_string(bytes->size())
                                 + " bytes, the manifest says " + std::to_string(asset.size));
    }
    if (asset.sha256.empty() || lowercase(sha256_hex(*bytes)) != lowercase(asset.sha256)) {
        throw std::runtime_error("sha256 of " + asset.file + " does not match the manifest; the installed build is kept");
    }
}

void switch_version(const fs::path& unpacked, const fs::path& root, const std::string& version)
{
    if (!plain_file_name(version)) {
        throw std::runtime_error("the version " + version + " is not a file name");
    }
    const fs::path versions = root / versions_name;
    const fs::path target_dir = versions / version;
    const std::string previous = linked_version(root);

    fs::create_directories(versions);
    fs::remove_all(target_dir);
    fs::rename(unpacked, target_dir);
    if (!previous.empty() && previous != version) {
        const fs::path previous_link = root / ("previous-" + previous);
        fs::remove(previous_link);
        fs::create_directory_symlink(versions / previous, previous_link);
    }

    const fs::path next = root / (std::string(current_name) + ".next");
    fs::remove(next);
    fs::create_directory_symlink(target_dir, next);
    fs::rename(next, root / current_name);
    prune(root, version, previous);
}

Result check(const Policy& policy)
{
    Result result;
    result.from = policy.current_version;
    result.to = policy.current_version;
    if (policy.root.empty()) {
        result.error = policy.disabled_reason;
        return result;
    }
    result.ok = true;
    try {
        apply(policy, result);
    } catch (const std::exception& error) {
        result.ok = false;
        result.error = error.what();
    }
    return result;
}

void start_background()
{
    const Policy policy = policy_from_environment();
    if (!policy.enabled) {
        return;
    }
    std::thread([policy]() {
        for (;;) {
            const Result result = check(policy);
            if (!result.ok) {
                std::cerr << "hyperagent update: " << result.error << "; retrying in 10 minutes\n";
            } else if (result.updated) {
                std::cerr << "hyperagent update: installed " << result.to << "; the next start runs it\n";
            }
            std::this_thread::sleep_for(result.ok ? check_interval : retry_interval);
        }
    }).detach();
}

}
