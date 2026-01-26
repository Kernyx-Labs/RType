#include "protocol/UdpServer.hpp"
#include <iostream>
#include <cstring>

using namespace rtype::server;

UdpServer::UdpServer(asio::io_context& io, unsigned short port)
    : io_(io)
    , socket_(io_, asio::ip::udp::endpoint(asio::ip::udp::v4(), port))
{
    try {
        asio::socket_base::receive_buffer_size opt(1024 * 1024);
        socket_.set_option(opt);
    } catch (...) {}
}

UdpServer::~UdpServer() { stop(); }

void UdpServer::start() {
    std::cout << "[server] Listening UDP on " << socket_.local_endpoint().port() << "\n";
    running_ = true;
    doReceive();
}

void UdpServer::stop() {
    running_ = false;
    if (socket_.is_open()) {
        asio::error_code ec; socket_.close(ec);
    }
}

void UdpServer::doReceive() {
    socket_.async_receive_from(
        asio::buffer(buffer_), remote_,
        [this](std::error_code ec, std::size_t n) {
            if (!ec && n > 0) {
                if (handler_) handler_(remote_, buffer_.data(), n);
            }
            if (running_) doReceive();
        }
    );
}

void UdpServer::sendRaw(const asio::ip::udp::endpoint& to, const void* data, std::size_t size) {
    auto buf = std::make_shared<std::vector<char>>(static_cast<const char*>(data), static_cast<const char*>(data) + size);
    socket_.async_send_to(asio::buffer(*buf), to, [buf](std::error_code, std::size_t) {});
}
