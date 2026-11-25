#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "core/install.hpp"
#include "core/ports.hpp"

namespace {

void require(bool condition, const std::string& reason)
{
    if (!condition) {
        std::cerr << "INSTALL_UNIT_FAIL " << reason << "\n";
        std::exit(1);
    }
}

void set_manifest_path(const std::string& value)
{
#if defined(_WIN32)
    ::_putenv_s("HYPERAGENT_INSTALL_MANIFEST", value.c_str());
#else
    ::setenv("HYPERAGENT_INSTALL_MANIFEST", value.c_str(), 1);
#endif
}

}

int main()
{
    namespace fs = std::filesystem;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    const fs::path scratch = fs::temp_directory_path() / "hyperagent-install-unit";
    fs::remove_all(scratch);
    fs::create_directories(scratch);
    const fs::path manifest_path = scratch / "install.json";
    set_manifest_path(manifest_path.string());

    hyperagent::core::install::Manifest manifest;
    manifest.binary = (scratch / "hyperagent").string();
    manifest.root = scratch.string();
    manifest.parent = (scratch / "projects").string();
    manifest.host = "127.0.0.1";
    manifest.port = "8787";
    manifest.orch_home = (scratch / "state").string();
    manifest.journal = (scratch / "journal").string();
    manifest.templates = (scratch / "templates").string();
    manifest.ccbridge_bin = (scratch / "ccbridge").string();
    manifest.mcp_timeout = "60000";
    manifest.mcp_tool_timeout = "14400000";
    hyperagent::core::install::write(manifest);
    const auto loaded = hyperagent::core::install::read();
    require(loaded.has_value(), "manifest round trip");
    require(loaded->port == manifest.port, "manifest port");
    require(loaded->templates == manifest.templates, "manifest templates");

    const auto values = hyperagent::core::install::environment(*loaded);
    require(values.size() == 10, "full MCP environment");
    require(values.at("ORCH_PARENT") == manifest.parent, "parent environment");
    require(values.at("MCP_TOOL_TIMEOUT") == manifest.mcp_tool_timeout, "tool timeout environment");
    require(values.find("ORCH_CLAUDE_MODEL") == values.end(), "no model override key");
    require(hyperagent::core::install::value("ORCH_PORT") == manifest.port, "manifest value lookup");

    net::io_context context;
    tcp::acceptor occupied(context, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
    const auto occupied_port = occupied.local_endpoint().port();
    const auto chosen_port = hyperagent::core::ports::available_port(occupied_port);
    require(chosen_port != 0 && chosen_port != occupied_port, "occupied port fallback");
    std::uint16_t free_candidate = 0;
    {
        tcp::acceptor probe(context, tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
        free_candidate = probe.local_endpoint().port();
    }
    const auto free_port = hyperagent::core::ports::available_port(free_candidate);
    require(free_port == free_candidate, "free port selection");

    fs::remove_all(scratch);
    std::cout << "INSTALL_UNIT_OK\n";
    return 0;
}
