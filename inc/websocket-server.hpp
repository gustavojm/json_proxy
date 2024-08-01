#pragma once

#include <algorithm>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <vector>
#include <websocket-server.hpp>

namespace beast = boost::beast;
// from <boost/beast.hpp>
namespace http = beast::http;
// from <boost/beast/http.hpp>
namespace websocket = beast::websocket;
// from <boost/beast/websocket.hpp>
namespace net = boost::asio;
// from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;
// from <boost/asio/ip/tcp.hpp>

//------------------------------------------------------------------------------

// Report a failure
inline void fail(beast::error_code ec, char const *what) {
    SPDLOG_ERROR("{}: {}", what, ec.message());
}

// Echoes back all received WebSocket messages
class session : public std::enable_shared_from_this<session> {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;

  public:
    // Take ownership of the socket
    explicit session(tcp::socket &&socket) : ws_(std::move(socket)) {
    }

    // Get on the correct executor
    void run() {
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session. Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        net::dispatch(ws_.get_executor(), beast::bind_front_handler(&session::on_run, shared_from_this()));
    }

    // Start the asynchronous operation
    void on_run() {
        // Set suggested timeout settings for the websocket
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        ws_.set_option(websocket::stream_base::decorator([](websocket::response_type &res) {
            res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-server-async");
        }));
        // Accept the websocket handshake
        ws_.async_accept(beast::bind_front_handler(&session::on_accept, shared_from_this()));
    }

    void do_send_hello([[maybe_unused]] beast::error_code ec, [[maybe_unused]] std::size_t bytes_transferred) {
        beast::flat_buffer buffer;
        auto mutable_buffer = buffer.prepare(1000);

        std::string str = "Hello, World!";
        std::copy(str.begin(), str.end(), boost::asio::buffer_cast<char *>(mutable_buffer));

        buffer.commit(str.size());

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        ws_.text(true);
        ws_.async_write(buffer.data(), beast::bind_front_handler(&session::do_send_hello, shared_from_this()));
    }

    void on_accept(beast::error_code ec) {
        if (ec)
            return fail(ec, "accept");

        // Send a message
        do_send_hello(ec, 1);
    }

    void do_read() {
        // Read a message into our buffer
        ws_.async_read(buffer_, beast::bind_front_handler(&session::on_read, shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        // This indicates that the session was closed
        if (ec == websocket::error::closed)
            return;

        if (ec)
            return fail(ec, "read");

        // Echo the message
        ws_.text(ws_.got_text());
        ws_.async_write(buffer_.data(), beast::bind_front_handler(&session::on_write, shared_from_this()));
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        // Clear the buffer
        buffer_.consume(buffer_.size());

        // Do another read
        do_read();
    }
};

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener> {
    net::io_context &ioc_;
    tcp::acceptor acceptor_;

  public:
    listener(net::io_context &ioc, tcp::endpoint endpoint) : ioc_(ioc), acceptor_(ioc) {
        beast::error_code ec;

        // Open the acceptor
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            fail(ec, "open");
            return;
        }

        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            fail(ec, "set_option");
            return;
        }

        // Bind to the server address
        acceptor_.bind(endpoint, ec);
        if (ec) {
            fail(ec, "bind");
            return;
        }

        // Start listening for connections
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            fail(ec, "listen");
            return;
        }
    }

    // Start accepting incoming connections
    void run() {
        do_accept();
    }

  private:
    void do_accept() {
        // The new connection gets its own strand
        acceptor_.async_accept(net::make_strand(ioc_), beast::bind_front_handler(&listener::on_accept, shared_from_this()));
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (ec) {
            fail(ec, "accept");
        } else {
            // Create the session and run it
            std::make_shared<session>(std::move(socket))->run();
        }

        // Accept another connection
        do_accept();
    }
};

void websocket_init();
