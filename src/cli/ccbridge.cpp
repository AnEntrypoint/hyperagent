#include "cli/ccbridge.hpp"

#include <sys/utsname.h>

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cli/https.hpp"
#include "core/clock.hpp"
#include "core/files.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/process.hpp"
#include "core/text.hpp"

namespace hyperagent::cli::ccbridge {

namespace fs = std::filesystem;

namespace {

const std::string repository = "AnEntrypoint/airc";
const std::string api_url = "https://api.github.com/repos/" + repository + "/releases/latest";
const std::string download_root = "https://github.com/" + repository + "/releases/download/";

struct Platform {
    std::string system;
    std::string machine;
};

Platform host_platform()
{
#ifdef _WIN32
    const char* architecture = std::getenv("PROCESSOR_ARCHITECTURE");
    return Platform{"Windows", architecture != nullptr ? architecture : ""};
#else
    utsname info{};
    if (::uname(&info) != 0) {
        throw std::runtime_error("uname failed");
    }
    return Platform{info.sysname, info.machine};
#endif
}

std::string release_target()
{
    static const std::map<std::pair<std::string, std::string>, std::string> targets{
        {{"Linux", "x86_64"}, "linux-x64"},
        {{"Linux", "aarch64"}, "linux-arm64"},
        {{"Darwin", "arm64"}, "macos-arm64"},
        {{"Darwin", "x86_64"}, "macos-x64"},
        {{"Windows", "AMD64"}, "windows-x64"},
        {{"Windows", "ARM64"}, "windows-arm64"},
    };
    const Platform platform = host_platform();
    const auto found = targets.find({platform.system, platform.machine});
    if (found == targets.end()) {
        throw std::runtime_error("no ccbridge release target for " + platform.system + " " + platform.machine);
    }
    return found->second;
}

bool is_windows()
{
    return host_platform().system == "Windows";
}

std::string asset_stem()
{
    return "ccbridge-" + release_target();
}

std::string asset_name()
{
    return asset_stem() + (is_windows() ? ".zip" : ".tar.gz");
}

fs::path binary_within(const fs::path& release_directory)
{
    return release_directory / asset_stem() / "bin" / (is_windows() ? "ccbridge.exe" : "ccbridge");
}

fs::path install_root()
{
    if (const char* override_root = std::getenv("CCBRIDGE_HOME"); override_root != nullptr && *override_root != '\0') {
        return core::paths::resolve(core::paths::expand_user(override_root));
    }
    return core::paths::repo_root() / ".orch" / "ccbridge";
}

std::optional<std::string> github_token()
{
    const char* token = std::getenv("GITHUB_TOKEN");
    if (token == nullptr || *token == '\0') {
        return std::nullopt;
    }
    return std::string(token);
}

https::Headers api_headers()
{
    https::Headers headers{
        {"Accept", "application/vnd.github+json"},
        {"X-GitHub-Api-Version", "2022-11-28"},
    };
    if (const auto token = github_token()) {
        headers.emplace_back("Authorization", "Bearer " + *token);
    }
    return headers;
}

bool is_rate_limited(const https::Response& response)
{
    if (response.status != 403 && response.status != 429) {
        return false;
    }
    return response.header("x-ratelimit-remaining") == "0" || !response.header("retry-after").empty()
        || response.body.find("rate limit") != std::string::npos;
}

[[noreturn]] void throw_rate_limited(const https::Response& response)
{
    std::string message = "GitHub API rate limit reached for " + api_url;
    const std::string reset = response.header("x-ratelimit-reset");
    long long reset_epoch = 0;
    if (!reset.empty() && std::from_chars(reset.data(), reset.data() + reset.size(), reset_epoch).ec == std::errc{}) {
        message += " (resets at " + core::clock::iso_stamp(static_cast<double>(reset_epoch)) + ")";
    }
    const std::string retry = response.header("retry-after");
    if (!retry.empty()) {
        message += " (retry after " + retry + " seconds)";
    }
    message += github_token()
        ? "; wait for the limit to reset"
        : "; set GITHUB_TOKEN to a token with public read access to raise the limit";
    throw std::runtime_error(message);
}

std::string latest_tag()
{
    const https::Response response = https::get_following_redirects(api_url, api_headers());
    if (is_rate_limited(response)) {
        throw_rate_limited(response);
    }
    if (response.status == 404) {
        throw std::runtime_error("no published release for " + repository);
    }
    if (response.status != 200) {
        throw std::runtime_error("GitHub API returned HTTP " + std::to_string(response.status) + " for " + api_url);
    }
    const auto document = core::json::parse(response.body);
    if (!document) {
        throw std::runtime_error("GitHub API returned a body that is not JSON");
    }
    const std::string tag = core::json::str_or_empty(core::json::member(&*document, "tag_name"));
    if (tag.empty() || tag.find_first_of("/\\") != std::string::npos || tag == "." || tag == "..") {
        throw std::runtime_error("latest release has no usable tag_name");
    }
    return tag;
}

fs::path fetch_asset(const std::string& tag, const std::string& name, const fs::path& into)
{
    const std::string url = download_root + tag + "/" + name;
    const https::Response response = https::get_following_redirects(url, {});
    if (response.status != 200) {
        throw std::runtime_error("download of " + name + " failed: HTTP " + std::to_string(response.status));
    }
    const fs::path archive = into / name;
    core::files::write_bytes(archive, response.body);
    return archive;
}

void unpack(const fs::path& archive, const fs::path& into)
{
    fs::create_directories(into);
    std::vector<std::string> command{"tar", "-xzf", archive.string(), "-C", into.string()};
    if (is_windows()) {
        command = {"tar", "-xf", archive.string(), "-C", into.string()};
    }
    const core::process::Result result = core::process::run_capture(command);
    if (result.exit_code != 0) {
        throw std::runtime_error("unpacking " + archive.string() + " failed: "
                                 + std::string(core::text::strip(result.standard_error)));
    }
}

fs::path install(const std::string& tag)
{
    const fs::path root = install_root();
    const fs::path destination = root / tag;
    const fs::path binary = binary_within(destination);
    if (core::files::is_regular_file(binary)) {
        return binary;
    }

    const fs::path archive = fetch_asset(tag, asset_name(), root / "downloads");
    const fs::path staging = root / (tag + ".partial");
    std::error_code ignored;
    fs::remove_all(staging, ignored);
    unpack(archive, staging);
    if (!core::files::is_regular_file(binary_within(staging))) {
        fs::remove_all(staging, ignored);
        throw std::runtime_error("release " + tag + " has no binary at " + binary_within(destination).string());
    }
    fs::remove_all(destination, ignored);
    fs::rename(staging, destination);
    return binary;
}

std::vector<long long> version_key(const std::string& tag)
{
    std::vector<long long> key;
    std::size_t index = 0;
    while (index < tag.size()) {
        if (std::isdigit(static_cast<unsigned char>(tag[index])) == 0) {
            ++index;
            continue;
        }
        std::size_t end = index;
        while (end < tag.size() && std::isdigit(static_cast<unsigned char>(tag[end])) != 0) {
            ++end;
        }
        long long value = 0;
        std::from_chars(tag.data() + index, tag.data() + end, value);
        key.push_back(value);
        index = end;
    }
    return key;
}

fs::path newest_installed()
{
    const fs::path root = install_root();
    std::optional<std::string> newest;
    std::error_code ignored;
    for (const auto& entry : fs::directory_iterator(root, ignored)) {
        const std::string name = entry.path().filename().string();
        if (name == "downloads" || name.ends_with(".partial") || !core::files::is_regular_file(binary_within(entry.path()))) {
            continue;
        }
        if (!newest || version_key(name) > version_key(*newest)) {
            newest = name;
        }
    }
    if (!newest) {
        throw std::runtime_error("no ccbridge installed under " + root.string() + "; run `hyperagent ccbridge install`");
    }
    return binary_within(root / *newest);
}

void run_guarded(const std::function<void()>& action)
{
    try {
        action();
    } catch (const std::exception& error) {
        std::cerr << "hyperagent ccbridge: " << error.what() << "\n";
        std::exit(1);
    }
}

}

void add_command(CLI::App& app)
{
    auto* command = app.add_subcommand("ccbridge", "install the newest ccbridge release (bin/ccbridge-latest)");
    command->require_subcommand(0, 1);

    auto* install_command = command->add_subcommand("install", "install the newest release if needed and print its binary");
    auto* latest_command = command->add_subcommand("latest", "print the newest release tag");
    auto* path_command = command->add_subcommand("path", "print the newest installed binary without network access");

    const auto install_and_print = []() {
        std::cout << install(latest_tag()).string() << "\n";
    };

    install_command->callback([install_and_print]() { run_guarded(install_and_print); });
    latest_command->callback([]() { run_guarded([]() { std::cout << latest_tag() << "\n"; }); });
    path_command->callback([]() { run_guarded([]() { std::cout << newest_installed().string() << "\n"; }); });
    command->callback([install_command, latest_command, path_command, install_and_print]() {
        if (!install_command->parsed() && !latest_command->parsed() && !path_command->parsed()) {
            run_guarded(install_and_print);
        }
    });
}

}
