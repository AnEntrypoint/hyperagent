#include "orch/websocket.hpp"

#include <optional>
#include <stdexcept>
#include <utility>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

namespace hyperagent::orch::websocket {

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ws = beast::websocket;
using tcp = net::ip::tcp;

struct Client::Impl {
    net::io_context context;
    ws::stream<beast::tcp_stream> stream;
    beast::flat_buffer buffer;
    std::optional<std::string> pending;
    bool reading = false;
    bool closed = false;

    explicit Impl(const std::string& host, const std::string& port, const std::string& target)
        : stream(context)
    {
        tcp::resolver resolver(context);
        beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));
        beast::get_lowest_layer(stream).connect(resolver.resolve(host, port));
        stream.read_message_max(16U << 20U);
        stream.handshake(host + ":" + port, target);
        beast::get_lowest_layer(stream).expires_never();
        stream.text(true);
    }

    void start_read()
    {
        reading = true;
        stream.async_read(buffer, [this](beast::error_code failure, std::size_t) {
            reading = false;
            if (failure) {
                closed = true;
                buffer.consume(buffer.size());
                return;
            }
            if (stream.got_text()) {
                pending = beast::buffers_to_string(buffer.data());
            }
            buffer.consume(buffer.size());
        });
    }

    void drain_read()
    {
        while (reading) {
            context.restart();
            context.run_one();
        }
    }
};

Client::Client(const std::string& host, const std::string& port, const std::string& target)
    : impl_(std::make_unique<Impl>(host, port, target))
{
}

Client::~Client()
{
    try {
        close();
    } catch (...) {
    }
}

void Client::send_text(const std::string& text)
{
    if (impl_->closed) {
        throw std::runtime_error("websocket is closed");
    }
    const std::string payload = text;
    bool finished = false;
    beast::error_code failure;
    impl_->stream.async_write(net::buffer(payload), [&](beast::error_code ec, std::size_t) {
        finished = true;
        failure = ec;
    });
    while (!finished) {
        impl_->context.restart();
        impl_->context.run_one();
    }
    if (failure) {
        impl_->closed = true;
        throw std::runtime_error("websocket write failed: " + failure.message());
    }
}

Event Client::next(std::chrono::milliseconds wait)
{
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (true) {
        if (impl_->pending) {
            std::string text = std::move(*impl_->pending);
            impl_->pending.reset();
            return Event{EventKind::frame, std::move(text)};
        }
        if (impl_->closed) {
            return Event{EventKind::closed, ""};
        }
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) {
            return Event{EventKind::timeout, ""};
        }
        if (!impl_->reading) {
            impl_->start_read();
        }
        impl_->context.restart();
        impl_->context.run_for(remaining);
    }
}

void Client::close()
{
    if (impl_->closed) {
        return;
    }
    impl_->closed = true;
    if (impl_->reading) {
        beast::get_lowest_layer(impl_->stream).cancel();
        impl_->drain_read();
    }
    beast::get_lowest_layer(impl_->stream).expires_after(std::chrono::seconds(10));
    beast::error_code ignored;
    impl_->stream.close(ws::close_code::normal, ignored);
}

}
