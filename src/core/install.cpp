#include "core/install.hpp"

#include <cstdlib>

#include "core/files.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"

namespace hyperagent::core::install {

namespace {

std::string text_of(const core::json::Value* value)
{
    return value != nullptr && value->is_string() ? value->get<std::string>() : std::string();
}

bool complete(const Manifest& manifest)
{
    return !manifest.binary.empty() && !manifest.root.empty() && !manifest.parent.empty() && !manifest.host.empty()
        && !manifest.port.empty() && !manifest.orch_home.empty() && !manifest.journal.empty()
        && !manifest.templates.empty() && !manifest.ccbridge_bin.empty() && !manifest.mcp_timeout.empty()
        && !manifest.mcp_tool_timeout.empty();
}

}

std::filesystem::path manifest_path()
{
    if (const char* raw = std::getenv("HYPERAGENT_INSTALL_MANIFEST"); raw != nullptr && *raw != '\0') {
        return core::paths::resolve(core::paths::expand_user(raw));
    }
    return core::paths::user_data_dir() / "install.json";
}

std::optional<Manifest> read()
{
    const auto saved = core::json::read_file(manifest_path());
    if (!saved || !saved->is_object()) {
        return std::nullopt;
    }
    Manifest manifest;
    manifest.binary = text_of(core::json::member(&*saved, "binary"));
    manifest.root = text_of(core::json::member(&*saved, "root"));
    manifest.parent = text_of(core::json::member(&*saved, "parent"));
    manifest.host = text_of(core::json::member(&*saved, "host"));
    manifest.port = text_of(core::json::member(&*saved, "port"));
    manifest.orch_home = text_of(core::json::member(&*saved, "orch_home"));
    manifest.journal = text_of(core::json::member(&*saved, "journal"));
    manifest.templates = text_of(core::json::member(&*saved, "templates"));
    manifest.ccbridge_bin = text_of(core::json::member(&*saved, "ccbridge_bin"));
    manifest.mcp_timeout = text_of(core::json::member(&*saved, "mcp_timeout"));
    manifest.mcp_tool_timeout = text_of(core::json::member(&*saved, "mcp_tool_timeout"));
    if (!complete(manifest)) {
        return std::nullopt;
    }
    return manifest;
}

void write(const Manifest& manifest)
{
    core::json::Value saved = core::json::Value::object();
    saved["binary"] = manifest.binary;
    saved["root"] = manifest.root;
    saved["parent"] = manifest.parent;
    saved["host"] = manifest.host;
    saved["port"] = manifest.port;
    saved["orch_home"] = manifest.orch_home;
    saved["journal"] = manifest.journal;
    saved["templates"] = manifest.templates;
    saved["ccbridge_bin"] = manifest.ccbridge_bin;
    saved["mcp_timeout"] = manifest.mcp_timeout;
    saved["mcp_tool_timeout"] = manifest.mcp_tool_timeout;
    core::json::PythonStyle style;
    style.indent = 2;
    style.sort_keys = true;
    core::files::write_bytes(manifest_path(), core::json::python_dumps(saved, style) + "\n");
}

std::map<std::string, std::string> environment(const Manifest& manifest)
{
    return {
        {"CCBRIDGE_BIN", manifest.ccbridge_bin},
        {"CCBRIDGE_TEMPLATES", manifest.templates},
        {"HYPERAGENT_ROOT", manifest.root},
        {"MCP_TIMEOUT", manifest.mcp_timeout},
        {"MCP_TOOL_TIMEOUT", manifest.mcp_tool_timeout},
        {"ORCH_HOME", manifest.orch_home},
        {"ORCH_HOST", manifest.host},
        {"ORCH_JOURNAL", manifest.journal},
        {"ORCH_PARENT", manifest.parent},
        {"ORCH_PORT", manifest.port},
    };
}

std::optional<std::string> value(std::string_view key)
{
    const auto manifest = read();
    if (!manifest) {
        return std::nullopt;
    }
    const auto values = environment(*manifest);
    const auto found = values.find(std::string(key));
    if (found == values.end() || found->second.empty()) {
        return std::nullopt;
    }
    return found->second;
}

}
