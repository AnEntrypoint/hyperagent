#include "core/http.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace hyperagent::core::http {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

Response get(const std::string& host, const std::string& port, const std::string& target,
             std::chrono::seconds timeout)
{
    net::io_context context;
    tcp::resolver resolver(context);
    beast::tcp_stream stream(context);

    stream.expires_after(timeout);
    stream.connect(resolver.resolve(host, port));

    http::request<http::empty_body> request{http::verb::get, target, 11};
    request.set(http::field::host, host + ":" + port);
    request.set(http::field::user_agent, "hyperagent");
    request.set(http::field::connection, "close");
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    beast::error_code ignored;
    stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    return Response{static_cast<std::uint16_t>(response.result_int()), response.body()};
}

}
