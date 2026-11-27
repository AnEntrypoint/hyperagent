#include "cli/setup.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/files.hpp"
#include "core/install.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/platform.hpp"
#include "core/ports.hpp"
#include "core/process.hpp"
#include "orch/ccbridge.hpp"
#include "orch/config.hpp"

namespace hyperagent::cli::setup {

namespace {

namespace fs = std::filesystem;
using core::json::Value;

#if defined(_WIN32)
constexpr const char* home_variable = "USERPROFILE";
#else
constexpr const char* home_variable = "HOME";
#endif

struct Options {
    std::string parent;
    std::string home;
    std::string journal;
    std::string templates;
    std::string port;
    bool check = false;
};

std::optional<std::uint16_t> parse_port(const std::string& text)
{
    try {
        std::size_t used = 0;
        const long value = std::stol(text, &used);
        if (used == text.size() && value > 0 && value <= 65535) {
            return static_cast<std::uint16_t>(value);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

fs::path resolved(const std::string& text)
{
    return core::paths::resolve(core::paths::expand_user(text));
}

fs::path claude_config_path()
{
    const char* raw = std::getenv(home_variable);
    if (raw == nullptr || *raw == '\0') {
        return {};
    }
    return fs::path(raw) / ".claude.json";
}

fs::path project_mcp_path()
{
    return core::paths::repo_root() / ".mcp.json";
}

fs::path find_on_path(const std::string& name)
{
    const char* raw = std::getenv("PATH");
    std::stringstream directories(raw == nullptr ? "" : raw);
    std::vector<std::string> suffixes{std::string(core::platform::executable_suffix)};
#if defined(_WIN32)
    suffixes.emplace_back(".cmd");
    suffixes.emplace_back(".bat");
    suffixes.emplace_back("");
#endif
    std::string directory;
    while (std::getline(directories, directory, core::platform::path_separator)) {
        if (directory.empty()) {
            continue;
        }
        for (const auto& suffix : suffixes) {
            const auto candidate = fs::path(directory) / (name + suffix);
            std::error_code ignored;
            if (fs::is_regular_file(candidate, ignored) && core::platform::is_executable(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

core::install::Manifest make_manifest(const Options& options)
{
    core::install::Manifest manifest;
    manifest.binary = core::paths::executable_path().string();
    if (manifest.binary.empty()) {
        throw std::runtime_error("cannot resolve the hyperagent executable");
    }
    manifest.root = core::paths::repo_root().string();
    manifest.host = orch::config::host();
    const std::string preferred = options.port.empty() ? orch::config::port() : options.port;
    const auto preferred_port = parse_port(preferred);
    if (!preferred_port) {
        throw std::runtime_error("bad airc port " + preferred);
    }
    manifest.port = std::to_string(core::ports::available_port(*preferred_port));
    manifest.orch_home = options.home.empty() ? core::paths::orch_home().string() : resolved(options.home).string();
    manifest.parent = options.parent.empty() ? orch::config::parent_dir().string() : resolved(options.parent).string();
    if (!options.journal.empty()) {
        manifest.journal = resolved(options.journal).string();
    } else if (!options.home.empty()) {
        manifest.journal = (fs::path(manifest.orch_home) / "projects").string();
    } else {
        manifest.journal = core::paths::journal_root().string();
    }
    manifest.templates = options.templates.empty()
        ? orch::config::templates_dir().string() : resolved(options.templates).string();
    manifest.ccbridge_bin = orch::ccbridge::ccbridge_binary();
    if (manifest.ccbridge_bin.empty()) {
        throw std::runtime_error("cannot resolve ccbridge");
    }
    manifest.mcp_timeout = orch::config::startup_timeout();
    manifest.mcp_tool_timeout = orch::config::tool_timeout();
    return manifest;
}

void create_directories(const core::install::Manifest& manifest)
{
    for (const auto& directory : {manifest.orch_home, manifest.parent, manifest.journal, manifest.templates}) {
        fs::create_directories(directory);
    }
}

void write_project_mcp(const core::install::Manifest& manifest)
{
    const auto values = core::install::environment(manifest);
    Value environment = Value::object();
    for (const auto& [name, value] : values) {
        environment[name] = value;
    }
    Value arguments = Value::array();
    arguments.push_back("mcp");
    Value server = Value::object();
    server["command"] = core::paths::is_shipped() ? manifest.binary : std::string("build/hyperagent");
    server["args"] = arguments;
    server["env"] = environment;
    server["timeout"] = std::stoll(manifest.mcp_tool_timeout);
    Value servers = Value::object();
    servers["hyperagent"] = server;
    Value root = Value::object();
    root["mcpServers"] = servers;
    core::json::PythonStyle style;
    style.indent = 2;
    style.sort_keys = true;
    core::files::write_bytes(project_mcp_path(), core::json::python_dumps(root, style) + "\n");
}

bool register_with_claude(const fs::path& claude, const core::install::Manifest& manifest)
{
    core::process::run_capture({claude.string(), "mcp", "remove", "--scope", "user", "hyperagent"});
    std::vector<std::string> arguments{claude.string(), "mcp", "add", "--scope", "user"};
    for (const auto& [name, value] : core::install::environment(manifest)) {
        arguments.emplace_back("-e");
        arguments.emplace_back(name + "=" + value);
    }
    arguments.emplace_back("--");
    arguments.emplace_back("hyperagent");
    arguments.emplace_back(manifest.binary);
    arguments.emplace_back("mcp");
    const auto added = core::process::run_capture(arguments);
    if (added.exit_code != 0) {
        std::cerr << "claude mcp add failed: " << added.standard_error << added.standard_output;
        return false;
    }
    std::cout << "registered mcp server hyperagent with claude: " << manifest.binary << " mcp\n";
    return true;
}

void run_setup(const Options& options)
{
    try {
        const auto manifest = make_manifest(options);
        create_directories(manifest);
        core::install::write(manifest);
        write_project_mcp(manifest);
        const auto claude = find_on_path("claude");
        if (claude.empty()) {
            std::cout << "claude CLI not found on PATH: skipped mcp registration\n";
        } else if (!register_with_claude(claude, manifest)) {
            std::exit(1);
        }
        std::cout << "setup ok parent=" << manifest.parent << " port=" << manifest.port
                  << " home=" << manifest.orch_home << " journal=" << manifest.journal
                  << " templates=" << manifest.templates << "\n";
        std::cout << "restart Claude Code so the MCP registration takes effect\n";
    } catch (const std::exception& failure) {
        std::cerr << "SETUP_FAIL " << failure.what() << "\n";
        std::exit(1);
    }
}

int fail(const std::string& reason)
{
    std::cout << "DOCTOR_FAIL " << reason << "\n";
    return 1;
}

bool expected_string(const Value* value, const std::string& expected)
{
    return value != nullptr && value->is_string() && value->get_ref<const std::string&>() == expected;
}

bool args_have_mcp(const Value* arguments)
{
    if (arguments == nullptr || !arguments->is_array()) {
        return false;
    }
    for (const auto& item : *arguments) {
        if (item.is_string() && item.get_ref<const std::string&>() == "mcp") {
            return true;
        }
    }
    return false;
}

bool environment_exact(const Value* environment, const std::map<std::string, std::string>& expected)
{
    if (environment == nullptr || !environment->is_object()) {
        return false;
    }
    for (const auto& [name, value] : expected) {
        if (!expected_string(core::json::member(environment, name), value)) {
            return false;
        }
    }
    return true;
}

bool server_matches(const Value* server, const core::install::Manifest& manifest, bool project)
{
    if (server == nullptr || !server->is_object()) {
        return false;
    }
    const std::string expected_command = project && !core::paths::is_shipped()
        ? std::string("build/hyperagent") : manifest.binary;
    if (!expected_string(core::json::member(server, "command"), expected_command)
        || !args_have_mcp(core::json::member(server, "args"))
        || !environment_exact(core::json::member(server, "env"), core::install::environment(manifest))) {
        return false;
    }
    if (!project) {
        return true;
    }
    const Value* timeout = core::json::member(server, "timeout");
    if (timeout == nullptr || !timeout->is_number_integer()) {
        return false;
    }
    try {
        return timeout->get<std::int64_t>() == std::stoll(manifest.mcp_tool_timeout);
    } catch (const std::exception&) {
        return false;
    }
}

bool template_has_model_override(const fs::path& file)
{
    const auto text = core::files::read_bytes(file);
    if (!text) {
        return true;
    }
    const auto parsed = core::json::parse(*text);
    if (!parsed) {
        return true;
    }
    const Value* arguments = core::json::member(&*parsed, "argv");
    if (arguments == nullptr || !arguments->is_array()) {
        return false;
    }
    for (const auto& item : *arguments) {
        if (!item.is_string()) {
            continue;
        }
        const std::string& argument = item.get_ref<const std::string&>();
        if (argument == "--model" || argument.rfind("--model=", 0) == 0) {
            return true;
        }
    }
    return false;
}

bool templates_valid(const fs::path& directory)
{
    std::error_code ignored;
    if (!fs::is_directory(directory, ignored)) {
        return false;
    }
    bool found = false;
    for (const auto& entry : fs::directory_iterator(directory, ignored)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        found = true;
        if (template_has_model_override(entry.path())) {
            return false;
        }
    }
    return found;
}

bool model_environment_clean()
{
    if (orch::config::claude_model_setting()) {
        return false;
    }
    for (const char* name : {"ORCH_CLAUDE_MODEL", "AIRC_CLAUDE_MODEL"}) {
        const char* raw = std::getenv(name);
        if (raw != nullptr && *raw != '\0'
            && std::string(raw) != std::string(orch::config::saved_model_reset)) {
            return false;
        }
    }
    return true;
}

bool mcp_config_matches(const core::install::Manifest& manifest, const fs::path& file, bool project)
{
    const auto saved = core::json::read_file(file);
    if (!saved) {
        return false;
    }
    const Value* servers = core::json::member(&*saved, "mcpServers");
    const Value* server = core::json::member(servers, "hyperagent");
    return server_matches(server, manifest, project);
}

int run_doctor(const Options& options)
{
    const auto manifest = core::install::read();
    if (!manifest) {
        return fail("install manifest missing");
    }
    const fs::path home = options.home.empty() ? fs::path(manifest->orch_home) : resolved(options.home);
    const fs::path parent = options.parent.empty() ? fs::path(manifest->parent) : resolved(options.parent);
    const fs::path journal = options.journal.empty() ? fs::path(manifest->journal) : resolved(options.journal);
    const fs::path templates = options.templates.empty()
        ? fs::path(manifest->templates) : resolved(options.templates);
    const std::string port_text = options.port.empty() ? manifest->port : options.port;
    const auto port = parse_port(port_text);
    if (!port) {
        return fail("bad persisted port " + port_text);
    }
    std::error_code ignored;
    if (!fs::path(manifest->binary).is_absolute() || !fs::is_regular_file(manifest->binary, ignored)
        || !core::platform::is_executable(manifest->binary)) {
        return fail("installed binary missing " + manifest->binary);
    }
    for (const auto& directory : {manifest->root, parent.string(), home.string(), journal.string()}) {
        if (!fs::is_directory(directory, ignored)) {
            return fail("missing directory " + directory);
        }
    }
    if (!fs::is_regular_file(manifest->ccbridge_bin, ignored)) {
        return fail("ccbridge binary missing " + manifest->ccbridge_bin);
    }
    if (!templates_valid(templates)) {
        return fail("missing or model-overriding harness templates " + templates.string());
    }
    if (!model_environment_clean()) {
        return fail("conflicting model override");
    }
    const bool ccbridge_alive = orch::ccbridge::server_alive();
    const auto probed_port = core::ports::available_port(*port);
    if (probed_port != *port && !ccbridge_alive) {
        return fail("port " + port_text + " is occupied by another server");
    }
    const auto claude = find_on_path("claude");
    if (claude.empty()) {
        return fail("claude CLI not found on PATH");
    }
    const auto listed = core::process::run_capture({claude.string(), "mcp", "get", "hyperagent"});
    if (listed.exit_code != 0) {
        return fail("mcp registration missing");
    }
    const auto saved_mcp = claude_config_path();
    if (saved_mcp.empty() || !mcp_config_matches(*manifest, saved_mcp, false)) {
        return fail("stale user MCP environment");
    }
    if (!mcp_config_matches(*manifest, project_mcp_path(), true)) {
        return fail("stale project MCP environment");
    }
    std::cout << "ok binary=" << manifest->binary << " ccbridge=" << manifest->ccbridge_bin
              << " parent=" << parent.string() << " port=" << port_text << " home=" << home.string()
              << " journal=" << journal.string() << " templates=" << templates.string() << "\n";
    std::cout << "ok mcp registration and long tool timeout\n";
    std::cout << "model override none\n";
    std::cout << "DOCTOR_OK\n";
    return 0;
}

void add_options(CLI::App* command, Options& options)
{
    command->add_option("--parent", options.parent, "directory whose projects become peers");
    command->add_option("--home", options.home, "state directory for the airc server");
    command->add_option("--journal", options.journal, "root of the per-session output journal");
    command->add_option("--templates", options.templates, "directory of ccbridge harness templates");
    command->add_option("--port", options.port, "preferred airc port; an occupied port falls back to an OS-assigned one");
}

}

void add_command(CLI::App& app)
{
    auto setup_options = std::make_shared<Options>();
    auto* setup = app.add_subcommand("setup", "create the portable install manifest, directories and MCP registration");
    add_options(setup, *setup_options);
    setup->add_flag("--check", setup_options->check, "verify the installation without changing it");
    setup->callback([setup_options]() {
        if (setup_options->check) {
            const int code = run_doctor(*setup_options);
            if (code != 0) {
                std::exit(code);
            }
            return;
        }
        run_setup(*setup_options);
    });

    auto doctor_options = std::make_shared<Options>();
    auto* doctor = app.add_subcommand("doctor", "verify the portable installation and MCP registration");
    add_options(doctor, *doctor_options);
    doctor->callback([doctor_options]() {
        const int code = run_doctor(*doctor_options);
        if (code != 0) {
            std::exit(code);
        }
    });
}

}
