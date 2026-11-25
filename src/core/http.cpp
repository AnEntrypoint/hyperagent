#include "core/http.hpp"

#include <algorithm>
#include <cctype>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace hyperagent::core::http {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

std::string lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });
    return out;
}

}

Response request(const std::string& method, const std::string& host, const std::string& port,
                 const std::string& target, std::chrono::seconds timeout)
{
    net::io_context context;
    tcp::resolver resolver(context);
    beast::tcp_stream stream(context);

    stream.expires_after(timeout);
    stream.connect(resolver.resolve(host, port));

    http::request<http::empty_body> outgoing{http::string_to_verb(method), target, 11};
    outgoing.set(http::field::host, host + ":" + port);
    outgoing.set(http::field::user_agent, "hyperagent");
    outgoing.set(http::field::connection, "close");
    http::write(stream, outgoing);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    beast::error_code ignored;
    stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    Response result{static_cast<std::uint16_t>(response.result_int()), response.body(), {}};
    for (const auto& field : response) {
        const std::string_view name(field.name_string().data(), field.name_string().size());
        const std::string_view text(field.value().data(), field.value().size());
        result.headers[lower(name)] = std::string(text);
    }
    return result;
}

Response get(const std::string& host, const std::string& port, const std::string& target,
             std::chrono::seconds timeout)
{
    return request("GET", host, port, target, timeout);
}

std::string header(const Response& response, std::string_view name)
{
    const auto found = response.headers.find(lower(name));
    return found == response.headers.end() ? std::string() : found->second;
}

}
