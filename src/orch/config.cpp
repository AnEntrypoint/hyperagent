#include "orch/config.hpp"

#include <cstdlib>
#include <cstdio>
#include <stdexcept>

#include "core/paths.hpp"
#include "core/text.hpp"
#include "orch/orch.hpp"

namespace hyperagent::orch::config {

namespace {

std::string environment_or(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::string(value) : std::string(fallback);
}

bool is_alnum(char symbol)
{
    return (symbol >= 'A' && symbol <= 'Z') || (symbol >= 'a' && symbol <= 'z') || (symbol >= '0' && symbol <= '9');
}

}

std::string host()
{
    return environment_or("ORCH_HOST", "127.0.0.1");
}

std::string port()
{
    return environment_or("ORCH_PORT", "8787");
}

std::string base_url()
{
    return "http://" + host() + ":" + port();
}

std::filesystem::path orch_dir()
{
    return core::paths::orch_home();
}

std::filesystem::path state_file()
{
    return orch_dir() / "state.json";
}

std::filesystem::path state_lock_file()
{
    return orch_dir() / "state.lock";
}

std::filesystem::path locks_dir()
{
    return orch_dir() / "locks";
}

std::filesystem::path parent_dir()
{
    const char* raw = std::getenv("ORCH_PARENT");
    if (raw != nullptr && *raw != '\0') {
        return core::paths::resolve(core::paths::expand_user(raw));
    }
    return core::paths::resolve(core::paths::repo_root().parent_path());
}

std::filesystem::path root_dir()
{
    return core::paths::repo_root();
}

std::string skip_permissions_setting()
{
    return environment_or("ORCH_SKIP_PERMISSIONS", "1");
}

std::optional<std::string> claude_model_setting()
{
    const std::string model = environment_or("ORCH_CLAUDE_MODEL", "haiku");
    if (model.empty()) {
        return std::nullopt;
    }
    return model;
}

bool valid_name(std::string_view name)
{
    if (name.empty() || name.size() > 64 || !is_alnum(name.front())) {
        return false;
    }
    for (const char symbol : name) {
        if (!is_alnum(symbol) && symbol != '_' && symbol != '-') {
            return false;
        }
    }
    return true;
}

bool valid_session_id(std::string_view session_id)
{
    if (session_id.empty() || session_id.front() == '-') {
        return false;
    }
    for (const char symbol : session_id) {
        if (symbol == '\0' || symbol == '\n') {
            return false;
        }
    }
    return core::text::code_point_count(session_id) <= 4096;
}

std::string check_name(const std::string& name)
{
    if (!valid_name(name)) {
        throw Error("bad agent name " + python_repr(name) + ": use letters, digits, underscore, dash");
    }
    return name;
}

std::string check_session_id(const std::string& session_id)
{
    if (!valid_session_id(session_id)) {
        throw Error("bad session selector " + python_repr(session_id));
    }
    return session_id;
}

std::string safe_name(std::string_view raw)
{
    std::string out;
    bool in_gap = false;
    for (const char symbol : raw) {
        if (is_alnum(symbol) || symbol == '_' || symbol == '-') {
            out.push_back(symbol);
            in_gap = false;
        } else if (!in_gap) {
            out.push_back('-');
            in_gap = true;
        }
    }
    std::size_t begin = 0;
    std::size_t end = out.size();
    while (begin < end && out[begin] == '-') {
        ++begin;
    }
    while (end > begin && out[end - 1] == '-') {
        --end;
    }
    std::string trimmed = out.substr(begin, end - begin);
    if (trimmed.size() > 64) {
        trimmed.resize(64);
    }
    return trimmed.empty() ? "project" : trimmed;
}

std::string resolve_cwd(const std::optional<std::string>& cwd)
{
    std::filesystem::path path = cwd && !cwd->empty() ? core::paths::expand_user(*cwd) : root_dir();
    if (!path.is_absolute()) {
        path = root_dir() / path;
    }
    return core::paths::resolve(path).string();
}

std::string python_repr(std::string_view text)
{
    const bool has_single = text.find('\'') != std::string_view::npos;
    const bool has_double = text.find('"') != std::string_view::npos;
    const char quote = has_single && !has_double ? '"' : '\'';
    std::string out(1, quote);
    for (const char symbol : text) {
        const auto byte = static_cast<unsigned char>(symbol);
        if (symbol == quote || symbol == '\\') {
            out.push_back('\\');
            out.push_back(symbol);
        } else if (symbol == '\n') {
            out += "\\n";
        } else if (symbol == '\r') {
            out += "\\r";
        } else if (symbol == '\t') {
            out += "\\t";
        } else if (byte < 0x20 || byte == 0x7f) {
            char escaped[8];
            std::snprintf(escaped, sizeof escaped, "\\x%02x", static_cast<unsigned>(byte));
            out += escaped;
        } else {
            out.push_back(symbol);
        }
    }
    out.push_back(quote);
    return out;
}

}
