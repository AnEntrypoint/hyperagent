#include "core/paths.hpp"

#include <cstdlib>
#include <system_error>
#include <vector>

#if defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#endif

#include "core/text.hpp"

namespace hyperagent::core::paths {

namespace {

std::optional<std::string> environment(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

bool is_repo_root(const std::filesystem::path& directory)
{
    std::error_code ignored;
    return std::filesystem::exists(directory / "CMakeLists.txt", ignored)
        && std::filesystem::exists(directory / "AGENTS.md", ignored);
}

std::optional<std::filesystem::path> shipped_prefix()
{
    const auto executable = executable_path();
    if (executable.empty()) {
        return std::nullopt;
    }
    const auto prefix = executable.parent_path().parent_path();
    std::error_code ignored;
    if (std::filesystem::is_directory(prefix / "share" / "hyperagent" / "gui", ignored)) {
        return prefix;
    }
    return std::nullopt;
}

}

std::filesystem::path executable_path()
{
    std::error_code ignored;
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    const auto resolved = std::filesystem::weakly_canonical(buffer, ignored);
    return ignored ? std::filesystem::path(buffer) : resolved;
#else
    const auto resolved = std::filesystem::read_symlink("/proc/self/exe", ignored);
    return ignored ? std::filesystem::path() : resolved;
#endif
}

bool is_shipped()
{
    return shipped_prefix().has_value();
}

std::filesystem::path user_data_dir()
{
    const auto home = environment("HOME").value_or("");
#if defined(__APPLE__)
    return std::filesystem::path(home) / "Library" / "Application Support" / "hyperagent";
#else
    if (const auto data_home = environment("XDG_DATA_HOME")) {
        return std::filesystem::path(*data_home) / "hyperagent";
    }
    return std::filesystem::path(home) / ".local" / "share" / "hyperagent";
#endif
}

std::filesystem::path repo_root()
{
    if (const auto prefix = shipped_prefix()) {
        return *prefix;
    }
    if (const auto override_root = environment("HYPERAGENT_ROOT")) {
        return resolve(*override_root);
    }
    const auto executable = executable_path();
    if (!executable.empty()) {
        for (auto directory = executable.parent_path();; directory = directory.parent_path()) {
            if (is_repo_root(directory)) {
                return directory;
            }
            if (directory == directory.parent_path()) {
                break;
            }
        }
    }
    return std::filesystem::current_path();
}

std::filesystem::path data_root()
{
    if (const auto prefix = shipped_prefix()) {
        return *prefix / "share" / "hyperagent";
    }
    return repo_root();
}

std::filesystem::path expand_user(std::string_view text)
{
    if (text == "~" || text.starts_with("~/")) {
        if (const auto home = environment("HOME")) {
            return std::filesystem::path(*home) / std::string(text.substr(1));
        }
    }
    return std::filesystem::path(std::string(text));
}

std::string absolute_path(std::string_view text)
{
    std::filesystem::path expanded = expand_user(text);
    if (!expanded.is_absolute()) {
        expanded = std::filesystem::current_path() / expanded;
    }

    std::vector<std::string> parts;
    const std::string joined = expanded.string();
    std::size_t start = 0;
    while (start <= joined.size()) {
        const std::size_t end = joined.find('/', start);
        const std::string part = joined.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    std::string out;
    for (const auto& part : parts) {
        out += "/" + part;
    }
    return out.empty() ? "/" : out;
}

std::filesystem::path resolve(const std::filesystem::path& path)
{
    std::error_code ignored;
    auto resolved = std::filesystem::weakly_canonical(path, ignored);
    return ignored ? path : resolved;
}

std::string slug(std::string_view cwd)
{
    const std::string absolute = absolute_path(cwd);
    std::string out;
    out.reserve(absolute.size());
    for (std::size_t offset = 0; offset < absolute.size();) {
        const auto decoded = text::decode_at(absolute, offset);
        const bool keep = decoded.size == 1
            && ((decoded.code_point >= '0' && decoded.code_point <= '9')
                || (decoded.code_point >= 'A' && decoded.code_point <= 'Z')
                || (decoded.code_point >= 'a' && decoded.code_point <= 'z'));
        out.push_back(keep ? static_cast<char>(decoded.code_point) : '-');
        offset += decoded.size;
    }
    return out;
}

std::filesystem::path orch_home()
{
    if (const auto raw = environment("ORCH_HOME")) {
        return resolve(*raw);
    }
    if (is_shipped()) {
        return user_data_dir() / ".orch";
    }
    return repo_root() / ".orch";
}

std::filesystem::path journal_root()
{
    if (const auto raw = environment("ORCH_JOURNAL")) {
        return resolve(expand_user(*raw));
    }
    return orch_home() / "projects";
}

std::filesystem::path project_dir(std::string_view cwd, const std::optional<std::filesystem::path>& root)
{
    const std::filesystem::path base = root ? resolve(expand_user(root->string())) : journal_root();
    return base / slug(cwd);
}

std::filesystem::path session_path(std::string_view cwd, std::string_view session_id,
                                   const std::optional<std::filesystem::path>& root)
{
    return project_dir(cwd, root) / (std::string(session_id) + ".jsonl");
}

}
