#include "gui/server.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>

#include "core/clock.hpp"
#include "core/files.hpp"
#include "gui/constants.hpp"

namespace hyperagent::gui {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

std::string event_stream_header()
{
    return "HTTP/1.0 200 OK\r\n"
           "Server: orch-gui\r\n"
           "Date: " + core::clock::http_date() + "\r\n"
           "Content-Type: text/event-stream\r\n"
           "Cache-Control: no-store\r\n"
           "\r\n";
}

std::string route_of(std::string_view target)
{
    std::string_view rest = target;
    if (rest.starts_with("//")) {
        const auto first = rest.find_first_not_of('/');
        rest = first == std::string_view::npos ? std::string_view("/") : rest.substr(first - 1);
    }
    if (const auto colon = rest.find(':'); colon != std::string_view::npos && colon > 0
        && std::isalpha(static_cast<unsigned char>(rest.front()))
        && std::all_of(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(colon), [](char symbol) {
               return std::isalnum(static_cast<unsigned char>(symbol)) || symbol == '+' || symbol == '-' || symbol == '.';
           })) {
        rest = rest.substr(colon + 1);
    }
    if (rest.starts_with("//")) {
        const auto slash = rest.find('/', 2);
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
    }
    if (const auto hash = rest.find('#'); hash != std::string_view::npos) {
        rest = rest.substr(0, hash);
    }
    if (const auto query = rest.find('?'); query != std::string_view::npos) {
        rest = rest.substr(0, query);
    }
    return std::string(rest);
}

std::string relative_of(const std::string& route)
{
    const auto first = route.find_first_not_of('/');
    return first == std::string::npos ? std::string{} : route.substr(first);
}

std::string content_type_of(const std::string& name)
{
    static const std::map<std::string, std::string, std::less<>> types{
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "text/javascript"},
        {".mjs", "text/javascript"},
        {".json", "application/json"},
        {".map", "application/json"},
        {".svg", "image/svg+xml"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".ico", "image/vnd.microsoft.icon"},
        {".txt", "text/plain"},
        {".xml", "text/xml"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".wasm", "application/wasm"},
    };
    const auto dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) {
        return "application/octet-stream";
    }
    std::string extension = name.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char symbol) { return static_cast<char>(std::tolower(symbol)); });
    const auto found = types.find(extension);
    return found == types.end() ? "application/octet-stream" : found->second;
}

bool write_all(tcp::socket& socket, std::string_view bytes)
{
    beast::error_code ec;
    net::write(socket, net::buffer(bytes.data(), bytes.size()), ec);
    return !ec;
}

}

Server::Server(std::filesystem::path public_dir, JournalFeed& feed, ReloadWatcher& reloads)
    : public_dir_(std::filesystem::weakly_canonical(public_dir)),
      feed_(feed),
      reloads_(reloads),
      acceptor_(context_)
{
}

void Server::listen(const std::string& host, std::uint16_t port)
{
    tcp::endpoint endpoint = *tcp::resolver(context_).resolve(host, std::to_string(port)).begin();
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(net::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
}

void Server::accept_forever()
{
    while (true) {
        Socket socket(context_);
        beast::error_code ec;
        acceptor_.accept(socket, ec);
        if (ec) {
            continue;
        }
        std::thread([this, connection = std::move(socket)]() mutable { handle(std::move(connection)); }).detach();
    }
}

void Server::handle(Socket socket)
{
    beast::flat_buffer buffer;
    http::request_parser<http::empty_body> parser;
    parser.skip(true);
    beast::error_code ec;
    http::read_header(socket, buffer, parser, ec);
    if (ec) {
        return;
    }
    const auto& request = parser.get();
    if (request.method() != http::verb::get) {
        send_error(socket, http::status::not_implemented,
                   "Unsupported method ('" + std::string(request.method_string()) + "')",
                   "Server does not support this operation");
    } else {
        const std::string route = route_of(request.target());
        if (route == "/api/events") {
            stream(socket, feed_.hub(), [this] { return feed_.backfill(); });
        } else if (route == "/api/reload") {
            stream(socket, reloads_.hub(), nullptr);
        } else {
            serve_static(socket, route == "/" ? "index.html" : relative_of(route));
        }
    }
    beast::error_code ignored;
    socket.shutdown(Socket::shutdown_both, ignored);
}

void Server::serve_static(Socket& socket, const std::string& relative)
{
    std::error_code ec;
    const std::filesystem::path target = std::filesystem::weakly_canonical(public_dir_ / relative, ec);
    if (ec || !is_inside_public(target) || !std::filesystem::is_regular_file(target, ec)) {
        send_error(socket, http::status::not_found, "Not Found", "Nothing matches the given URI");
        return;
    }
    const auto body = core::files::read_bytes(target);
    if (!body) {
        send_error(socket, http::status::not_found, "Not Found", "Nothing matches the given URI");
        return;
    }
    http::response<http::string_body> response{http::status::ok, 10};
    response.set(http::field::server, "orch-gui");
    response.set(http::field::date, core::clock::http_date());
    response.set(http::field::content_type, content_type_of(target.filename().string()));
    response.body() = *body;
    response.prepare_payload();
    response.set(http::field::cache_control, "no-store");
    beast::error_code write_error;
    http::write(socket, response, write_error);
}

bool Server::is_inside_public(const std::filesystem::path& target) const
{
    const auto relative = target.lexically_relative(public_dir_);
    return !relative.empty() && *relative.begin() != "..";
}

void Server::stream(Socket& socket, Hub& hub, const std::function<std::string()>& initial)
{
    if (!write_all(socket, event_stream_header())) {
        return;
    }
    const auto subscriber = hub.subscribe();
    bool connected = true;
    if (initial) {
        connected = send_event(socket, "snapshot", initial());
    }
    connected = connected && write_all(socket, "retry: 1000\n\n");
    while (connected) {
        const auto event = subscriber->next(constants::keepalive_interval);
        if (!event) {
            connected = write_all(socket, ": keepalive\n\n");
        } else if (event->name == "close") {
            break;
        } else {
            connected = send_event(socket, event->name, event->payload);
        }
    }
    hub.unsubscribe(subscriber);
}

bool Server::send_event(Socket& socket, const std::string& name, const std::string& payload)
{
    return write_all(socket, "event: " + name + "\ndata: " + payload + "\n\n");
}

void Server::send_error(Socket& socket, http::status status, const std::string& message,
                        const std::string& explanation)
{
    const auto code = std::to_string(static_cast<unsigned>(status));
    const std::string body = "<!DOCTYPE HTML>\n<html lang=\"en\">\n    <head>\n        <meta charset=\"utf-8\">\n"
                             "        <title>Error response</title>\n    </head>\n    <body>\n"
                             "        <h1>Error response</h1>\n        <p>Error code: " + code
                             + "</p>\n        <p>Message: " + message + ".</p>\n"
                             "        <p>Error code explanation: " + code + " - " + explanation
                             + ".</p>\n    </body>\n</html>\n";
    http::response<http::string_body> response{status, 10};
    response.reason(message);
    response.set(http::field::server, "orch-gui");
    response.set(http::field::date, core::clock::http_date());
    response.set(http::field::connection, "close");
    response.set(http::field::content_type, "text/html;charset=utf-8");
    response.body() = body;
    response.prepare_payload();
    beast::error_code ec;
    http::write(socket, response, ec);
}

}
