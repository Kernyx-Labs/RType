#include "protocol/TcpServer.hpp"
#include <array>
#include <iostream>

using namespace rtype::server;

TcpServer::TcpServer(asio::io_context& io, unsigned short tcpPort)
: acceptor_(io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), tcpPort)) {}

void TcpServer::start() {
    running_ = true;
    std::cout << "[server] Listening TCP on " << acceptor_.local_endpoint().port() << "\n";
    doAccept();
}

void TcpServer::stop() {
    running_ = false;
    asio::error_code ec;
    acceptor_.close(ec);

    // Lock to safely iterate and clear clients_
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& c : clients_) {
        if (c && c->is_open()) c->close(ec);
    }
    clients_.clear();
}

void TcpServer::broadcastStartGame() {
    // Lock to safely iterate clients_
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& c : clients_) {
        if (c && c->is_open())
            sendHeader(c, rtype::net::MsgType::StartGame);
    }
}

void TcpServer::doAccept() {
    auto sock = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
    acceptor_.async_accept(*sock, [self = shared_from_this(), sock](std::error_code ec) {
        if (!ec && self->running_) {
            {
                std::lock_guard<std::mutex> lock(self->clientsMutex_);
                self->clients_.insert(sock);
            }
            self->startSession(sock);
        }
        if (self->running_) self->doAccept();
    });
}

void TcpServer::startSession(SocketPtr sock) {
    sendHeader(sock, rtype::net::MsgType::TcpWelcome);
    // Read Hello with username (<=15 bytes)
    auto hdrBuf = std::make_shared<std::array<char, sizeof(rtype::net::Header)>>();
    asio::async_read(*sock, asio::buffer(*hdrBuf), [self = shared_from_this(), sock, hdrBuf](std::error_code ec, std::size_t) {
        if (ec) {
            std::lock_guard<std::mutex> lock(self->clientsMutex_);
            self->clients_.erase(sock);
            return;
        }
        auto hdr = *reinterpret_cast<rtype::net::Header*>(hdrBuf->data());
        if (hdr.version != rtype::net::ProtocolVersion || hdr.type != rtype::net::MsgType::Hello) {
            return;
        }
        std::size_t payloadSize = std::min<std::size_t>(hdr.size, 64);
        auto payload = std::make_shared<std::vector<char>>(payloadSize);
        asio::async_read(*sock, asio::buffer(*payload), [self, sock, payload](std::error_code, std::size_t) {
            std::string uname(payload->data(), payload->data() + std::min<std::size_t>(payload->size(), 15));
            while (!uname.empty() && (uname.back() == '\0' || uname.back() == ' ')) uname.pop_back();
            // Inform upper layer about the declared username and client IP
            if (self->onHello_) {
                try {
                    auto ep = sock->remote_endpoint();
                    self->onHello_(uname, ep.address().to_string());
                } catch (...) {
                    self->onHello_(uname, std::string{});
                }
            }
            std::uint32_t token = self->issueToken_ ? self->issueToken_(uname) : 0u;
            rtype::net::HelloAckPayload hp{ static_cast<std::uint16_t>(self->udpPort_), token };
            self->sendHeader(sock, rtype::net::MsgType::HelloAck, sizeof(hp));
            auto pbuf = std::make_shared<std::array<char, sizeof(hp)>>();
            std::memcpy(pbuf->data(), &hp, sizeof(hp));
            asio::async_write(*sock, asio::buffer(*pbuf), [pbuf](std::error_code, std::size_t){});
        });
    });

    // clean on closure
    sock->async_wait(asio::ip::tcp::socket::wait_error,
        [self = shared_from_this(), sock](std::error_code) {
            std::lock_guard<std::mutex> lock(self->clientsMutex_);
            self->clients_.erase(sock);
        });
}

void TcpServer::sendHeader(SocketPtr sock, rtype::net::MsgType t, std::uint16_t size) {
    rtype::net::Header hdr{size, t, rtype::net::ProtocolVersion};
    auto buf = std::make_shared<std::array<char, sizeof(hdr)>>();
    std::memcpy(buf->data(), &hdr, sizeof(hdr));

    asio::async_write(*sock, asio::buffer(*buf), [buf](std::error_code, std::size_t) {});
}
