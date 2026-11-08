#include "cli/https.hpp"

#include <openssl/ssl.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <cctype>
#include <cstddef>
#include <stdexcept>

namespace hyperagent::cli::https {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace {

constexpr int max_redirects = 10;
constexpr std::uint64_t max_body_bytes = 512ULL * 1024ULL * 1024ULL;

struct Url {
    std::string host;
    std::string port;
    std::string target;
};

bool is_redirect(std::uint16_t status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

std::string lowercase(std::string_view text)
{
    std::string out(text);
    for (auto& character : out) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return out;
}

Url parse_url(const std::string& url)
{
    constexpr std::string_view scheme = "https://";
    if (!std::string_view(url).starts_with(scheme)) {
        throw std::runtime_error("only https URLs are supported: " + url);
    }
    const std::string rest = url.substr(scheme.size());
    const std::size_t slash = rest.find('/');
    const std::string authority = rest.substr(0, slash);
    const std::string target = slash == std::string::npos ? "/" : rest.substr(slash);
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        return Url{authority.substr(0, colon), authority.substr(colon + 1), target};
    }
    return Url{authority, "443", target};
}

std::string resolve_location(const std::string& current, const std::string& location)
{
    if (std::string_view(location).starts_with("https://")) {
        return location;
    }
    if (location.starts_with('/')) {
        const Url base = parse_url(current);
        return "https://" + base.host + (base.port == "443" ? "" : ":" + base.port) + location;
    }
    throw std::runtime_error("unsupported redirect location: " + location);
}

Response perform(const Url& url, const Headers& headers, std::chrono::seconds timeout)
{
    net::io_context context;
    ssl::context tls(ssl::context::tls_client);
    tls.set_default_verify_paths();
    tls.set_verify_mode(ssl::verify_peer);

    tcp::resolver resolver(context);
    ssl::stream<beast::tcp_stream> stream(context, tls);
    beast::get_lowest_layer(stream).expires_after(timeout);
    if (SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str()) != 1) {
        throw std::runtime_error("cannot set TLS server name for " + url.host);
    }
    stream.set_verify_callback(ssl::host_name_verification(url.host));

    beast::get_lowest_layer(stream).connect(resolver.resolve(url.host, url.port));
    stream.handshake(ssl::stream_base::client);

    http::request<http::empty_body> request{http::verb::get, url.target, 11};
    request.set(http::field::host, url.host);
    request.set(http::field::user_agent, "hyperagent-ccbridge-latest");
    request.set(http::field::connection, "close");
    for (const auto& [name, value] : headers) {
        request.set(name, value);
    }
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response_parser<http::string_body> parser;
    parser.body_limit(max_body_bytes);
    http::read(stream, buffer, parser);
    const auto& message = parser.get();

    Response response{static_cast<std::uint16_t>(message.result_int()), message.body(), {}};
    for (const auto& field : message) {
        response.headers.emplace_back(lowercase(field.name_string()), std::string(field.value()));
    }

    beast::error_code ignored;
    stream.shutdown(ignored);
    return response;
}

}

std::string Response::header(std::string_view name) const
{
    const std::string wanted = lowercase(name);
    for (const auto& [field, value] : headers) {
        if (field == wanted) {
            return value;
        }
    }
    return {};
}

Response get_following_redirects(const std::string& url, const Headers& headers, std::chrono::seconds timeout)
{
    std::string current = url;
    for (int hop = 0; hop <= max_redirects; ++hop) {
        Response response = perform(parse_url(current), hop == 0 ? headers : Headers{}, timeout);
        const std::string location = response.header("location");
        if (!is_redirect(response.status) || location.empty()) {
            return response;
        }
        current = resolve_location(current, location);
    }
    throw std::runtime_error("too many redirects fetching " + url);
}

}
