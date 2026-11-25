#include "core/ports.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp>

namespace hyperagent::core::ports {

std::uint16_t available_port(std::uint16_t preferred)
{
    namespace net = boost::asio;
    using tcp = net::ip::tcp;
    net::io_context context;
    const auto address = net::ip::make_address("127.0.0.1");
    boost::system::error_code failure;
    tcp::acceptor preferred_listener(context);
    preferred_listener.open(tcp::v4(), failure);
    if (!failure) {
        preferred_listener.bind(tcp::endpoint(address, preferred), failure);
        if (!failure) {
            return preferred;
        }
    }
    tcp::acceptor assigned_listener(context, tcp::endpoint(address, 0));
    return assigned_listener.local_endpoint().port();
}

}
