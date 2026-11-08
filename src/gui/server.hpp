#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http.hpp>

#include "gui/feed.hpp"
#include "gui/hub.hpp"
#include "gui/reload.hpp"

namespace hyperagent::gui {

class Server {
public:
    Server(std::filesystem::path public_dir, JournalFeed& feed, ReloadWatcher& reloads);

    void listen(const std::string& host, std::uint16_t port);
    void accept_forever();

private:
    using Socket = boost::asio::ip::tcp::socket;

    void handle(Socket socket);
    void serve_static(Socket& socket, const std::string& relative);
    void stream(Socket& socket, Hub& hub, const std::function<std::string()>& initial);
    bool send_event(Socket& socket, const std::string& name, const std::string& payload);
    void send_error(Socket& socket, boost::beast::http::status status, const std::string& message,
                    const std::string& explanation);
    bool is_inside_public(const std::filesystem::path& target) const;

    std::filesystem::path public_dir_;
    JournalFeed& feed_;
    ReloadWatcher& reloads_;
    boost::asio::io_context context_;
    boost::asio::ip::tcp::acceptor acceptor_;
};

}
