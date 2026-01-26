#pragma once
#include <asio.hpp>
#include <unordered_set>
#include <memory>
#include <functional>
#include <mutex>
#include "common/Protocol.hpp"

namespace rtype::server { class UdpServer; }

namespace rtype::server {

class TcpServer : public std::enable_shared_from_this<TcpServer> {
public:
    using IssueTokenFn = std::function<std::uint32_t(const std::string& name)>;
    using OnHelloFn = std::function<void(const std::string& name, const std::string& ip)>;

    TcpServer(asio::io_context& io, unsigned short tcpPort);

    void start();
    void stop();

    // Configuration: optional token issuer and UDP port to advertise
    void setIssueToken(IssueTokenFn fn) { issueToken_ = std::move(fn); }
    void setUdpPort(unsigned short udpPort) { udpPort_ = udpPort; }
    void setOnHello(OnHelloFn fn) { onHello_ = std::move(fn); }

    // notify clients
    void broadcastStartGame();

private:
    using SocketPtr = std::shared_ptr<asio::ip::tcp::socket>;

    void doAccept();
    void startSession(SocketPtr sock);
    void sendHeader(SocketPtr sock, rtype::net::MsgType t, std::uint16_t size = 0);

private:
    asio::ip::tcp::acceptor acceptor_;
    std::unordered_set<SocketPtr> clients_;
    std::mutex clientsMutex_;  // Protects clients_ from concurrent access
    bool running_{false};

    IssueTokenFn issueToken_{};
    OnHelloFn onHello_{};
    unsigned short udpPort_{0};
};

} // namespace rtype::server
