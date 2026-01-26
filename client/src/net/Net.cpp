#include "Screens.hpp"
#include <asio.hpp>
#include <vector>
#include <cstring>
#include <array>
#include <thread>
#include <chrono>
#include "common/Protocol.hpp"

namespace client { namespace ui {

namespace {
    struct UdpClientGlobals {
        std::unique_ptr<asio::io_context> io;
        std::unique_ptr<asio::ip::udp::socket> sock;
        asio::ip::udp::endpoint server;
    } g;
}

bool Screens::connectTcp() {
    try {
        _tcpIo = std::make_unique<asio::io_context>();
        _tcpSocket = std::make_unique<asio::ip::tcp::socket>(*_tcpIo);

        asio::ip::tcp::resolver resolver(*_tcpIo);
        // TCP port is UDP port + 1
        std::string tcpPort = std::to_string(std::stoi(_serverPort) + 1);
        auto results = resolver.resolve(asio::ip::tcp::v4(), _serverAddr, tcpPort);

        asio::connect(*_tcpSocket, results);

        // TCP connected
        std::array<char, sizeof(rtype::net::Header)> welcome{};
        asio::read(*_tcpSocket, asio::buffer(welcome));
        auto* hdr = reinterpret_cast<rtype::net::Header*>(welcome.data());
        if (hdr->type != rtype::net::MsgType::TcpWelcome) {
            logMessage("Expected TcpWelcome, got different message", "ERROR");
            return false;
        }

        //Hello + username via TCP
        rtype::net::Header hello{};
        hello.version = rtype::net::ProtocolVersion;
        hello.type = rtype::net::MsgType::Hello;
        hello.size = static_cast<std::uint16_t>(_username.size());
        std::vector<char> helloMsg(sizeof(hello) + _username.size());
        std::memcpy(helloMsg.data(), &hello, sizeof(hello));
        if (!_username.empty()) {
            std::memcpy(helloMsg.data() + sizeof(hello), _username.data(), _username.size());
        }
        asio::write(*_tcpSocket, asio::buffer(helloMsg));

        // Receive HelloAck with UDP port
        std::array<char, sizeof(rtype::net::Header) + sizeof(rtype::net::HelloAckPayload)> ackBuf{};
        asio::read(*_tcpSocket, asio::buffer(ackBuf));
        auto* ackHdr = reinterpret_cast<rtype::net::Header*>(ackBuf.data());
        if (ackHdr->type != rtype::net::MsgType::HelloAck) {
            logMessage("Expected HelloAck, got different message", "ERROR");
            return false;
        }

        auto* ackPayload = reinterpret_cast<rtype::net::HelloAckPayload*>(ackBuf.data() + sizeof(rtype::net::Header));
        _udpPort = ackPayload->udpPort;

        logMessage("TCP handshake complete, UDP port: " + std::to_string(_udpPort), "INFO");
        return true;
    } catch (const std::exception& e) {
        logMessage(std::string("TCP connection failed: ") + e.what(), "ERROR");
        disconnectTcp();
        return false;
    }
}

void Screens::disconnectTcp() {
    if (_tcpSocket && _tcpSocket->is_open()) {
        asio::error_code ec;
        _tcpSocket->close(ec);
    }
    _tcpSocket.reset();
    _tcpIo.reset();
    _udpPort = 0;
}

void Screens::leaveSession() {
    teardownNet();
    disconnectTcp();
    _connected = false;
    _entities.clear();
    _entityById.clear();
    _missedById.clear();
    _lastSeenAt.clear();
    _serverReturnToMenu = false;
}

void Screens::ensureNetSetup() {
    if (g.io) return;
    if (_udpPort == 0) {
        logMessage("UDP port not set - TCP handshake may have failed", "ERROR");
        return;
    }
    g.io = std::make_unique<asio::io_context>();
    g.sock = std::make_unique<asio::ip::udp::socket>(*g.io);
    g.sock->open(asio::ip::udp::v4());
    asio::ip::udp::resolver resolver(*g.io);
    g.server = *resolver.resolve(asio::ip::udp::v4(), _serverAddr, std::to_string(_udpPort)).begin();
    g.sock->non_blocking(true);
    _serverReturnToMenu = false;

   // Send UDP Hello with username to bind
    rtype::net::Header hdr{};
    hdr.version = rtype::net::ProtocolVersion;
    hdr.type = rtype::net::MsgType::Hello;
    hdr.size = static_cast<std::uint16_t>(_username.size());
    std::vector<char> out(sizeof(rtype::net::Header) + _username.size());
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    if (!_username.empty()) std::memcpy(out.data() + sizeof(hdr), _username.data(), _username.size());
    g.sock->send_to(asio::buffer(out), g.server);
}

void Screens::sendDisconnect() {
    if (!g.sock) return;
    rtype::net::Header hdr{};
    hdr.version = rtype::net::ProtocolVersion;
    hdr.type = rtype::net::MsgType::Disconnect;
    hdr.size = 0;
    std::array<char, sizeof(rtype::net::Header)> buf{};
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    asio::error_code ec;
    g.sock->send_to(asio::buffer(buf), g.server, 0, ec);
}

void Screens::teardownNet() {
    if (g.sock && g.sock->is_open()) {
        sendDisconnect();
        asio::error_code ec; g.sock->close(ec);
    }
    g.sock.reset();
    g.io.reset();
    _spriteRowById.clear();
    _nextSpriteRow = 0;
    _entities.clear();
    _entityById.clear();
    _missedById.clear();
    _lastSeenAt.clear();
}

void Screens::sendInput(std::uint8_t bits) {
    if (!g.sock) return;
    rtype::net::Header hdr{};
    hdr.version = rtype::net::ProtocolVersion;
    hdr.type = rtype::net::MsgType::Input;
    rtype::net::InputPacket ip{}; ip.sequence = 0; ip.bits = bits;
    hdr.size = sizeof(ip);
    std::array<char, sizeof(hdr) + sizeof(ip)> buf{};
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    std::memcpy(buf.data() + sizeof(hdr), &ip, sizeof(ip));
    g.sock->send_to(asio::buffer(buf), g.server);
}

void Screens::sendLobbyConfig(std::uint8_t difficulty, std::uint8_t baseLives) {
    if (!g.sock) return;
    rtype::net::Header hdr{};
    hdr.version = rtype::net::ProtocolVersion;
    hdr.type = rtype::net::MsgType::LobbyConfig;
    rtype::net::LobbyConfigPayload p{ baseLives, difficulty };
    hdr.size = sizeof(p);
    std::array<char, sizeof(hdr) + sizeof(p)> buf{};
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    std::memcpy(buf.data() + sizeof(hdr), &p, sizeof(p));
    g.sock->send_to(asio::buffer(buf), g.server);
}

void Screens::sendStartMatch() {
    if (!g.sock) return;
    rtype::net::Header hdr{};
    hdr.version = rtype::net::ProtocolVersion;
    hdr.type = rtype::net::MsgType::StartMatch;
    hdr.size = 0;
    std::array<char, sizeof(rtype::net::Header)> buf{};
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    g.sock->send_to(asio::buffer(buf), g.server);
}

void Screens::pumpNetworkOnce() {
    if (!g.sock) return;
    for (int i = 0; i < 8; ++i) {
        asio::ip::udp::endpoint from;
        std::array<char, 8192> in{};
        asio::error_code ec;
        std::size_t n = g.sock->receive_from(asio::buffer(in), from, 0, ec);
        if (ec == asio::error::would_block) break;
        if (ec || n < sizeof(rtype::net::Header)) break;
        handleNetPacket(in.data(), n);
    }
}

bool Screens::waitHelloAck(double timeoutSec) {
    double start = GetTime();
    while (GetTime() - start < timeoutSec) {
        asio::ip::udp::endpoint from;
        std::array<char, 8192> in{};
        asio::error_code ec;
        std::size_t n = g.sock->receive_from(asio::buffer(in), from, 0, ec);
        if (!ec && n >= sizeof(rtype::net::Header)) {
            auto* rh = reinterpret_cast<rtype::net::Header*>(in.data());
            if (rh->version == rtype::net::ProtocolVersion) {
                if (rh->type == rtype::net::MsgType::Roster ||
                    rh->type == rtype::net::MsgType::State ||
                    rh->type == rtype::net::MsgType::LivesUpdate ||
                    rh->type == rtype::net::MsgType::ScoreUpdate) {
                    handleNetPacket(in.data(), n);
                    return true;
                }
                handleNetPacket(in.data(), n);
            }
        } else if (ec && ec != asio::error::would_block && ec != asio::error::try_again) {
            logMessage(std::string("Receive error: ") + ec.message(), "ERROR");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

bool Screens::autoConnect(ScreenState& screen, MultiplayerForm& form) {
    bool canConnect = !form.username.empty() && !form.serverAddress.empty() && !form.serverPort.empty();
    if (!canConnect) {
        _statusMessage = std::string("Missing host/port/name for autoconnect.");
        return false;
    }
    try {
        _username = form.username;
        _serverAddr = form.serverAddress;
        _serverPort = form.serverPort;
        _selfId = 0;
        _playerLives = 4;
        _gameOver = false;
        _otherPlayers.clear();
        // TCP handshake to get UDP port
        disconnectTcp();
        if (!connectTcp()) {
            _statusMessage = std::string("TCP connection failed.");
            disconnectTcp();
            return false;
        }
        // Setup UDP connection
        teardownNet();
        ensureNetSetup();
        // Wait for roster/state packets
        bool ok = waitHelloAck(1.0);
        if (ok) {
            _statusMessage = std::string("Player Connected.");
            _connected = true;
            screen = ScreenState::Waiting;
            return true;
        } else {
            _statusMessage = std::string("Connection failed.");
            teardownNet();
            disconnectTcp();
            return false;
        }
    } catch (const std::exception& e) {
        logMessage(std::string("Exception: ") + e.what(), "ERROR");
        _statusMessage = std::string("Error: ") + e.what();
        teardownNet();
        disconnectTcp();
        return false;
    }
}

} } // namespace client::ui
