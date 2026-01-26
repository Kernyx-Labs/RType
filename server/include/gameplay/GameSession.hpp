#pragma once
#include <asio.hpp>
#include <thread>
#include <mutex>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <random>
#include <string>
#include <chrono>
#include <functional>
#include "common/Protocol.hpp"
#include "rt/ecs/Registry.hpp"

// Forward declaration to avoid including heavy headers in the interface
namespace rt { namespace game { class FormationSpawnSystem; } }

namespace rtype::server {
class TcpServer;
}

namespace rtype::server::gameplay {

class GameSession {
public:
    using SendFn = std::function<void(const asio::ip::udp::endpoint&, const void*, std::size_t)>;

    GameSession(asio::io_context& io, SendFn sendFn, rtype::server::TcpServer* tcpServer);
    ~GameSession();

    void start();
    void stop();
    void onUdpPacket(const asio::ip::udp::endpoint& from, const char* data, std::size_t size);
    void onTcpHello(const std::string& username, const std::string& ip);

private:
    void gameLoop();
    void checkTimeouts();
    void removeClient(const std::string& key);
    void broadcastState();
    void broadcastDespawn(std::uint32_t entityId);
    void broadcastRoster();
    void broadcastLivesUpdate(std::uint32_t id, std::uint8_t lives);
    void broadcastLobbyStatus();
    void maybeStartGame();
    void cleanupGameWorld();

    static std::string makeKey(const asio::ip::udp::endpoint& ep);

    void bindUdpEndpoint(const asio::ip::udp::endpoint& ep, std::uint32_t playerId);

private:
    asio::io_context& io_;
    SendFn send_;

    std::thread gameThread_;
    bool running_ = false;

    // Tick-synchronized state broadcasting
    std::uint32_t tickCount_ = 0;
    static constexpr std::uint32_t kBroadcastEveryNTicks = 3;  // 60Hz / 3 = 20Hz state updates

    // Mutex for protecting shared state accessed by both I/O and game loop threads
    // Lock ordering: Always acquire stateMutex_ before any operations on shared state
    mutable std::mutex stateMutex_;

    // Shared state protected by stateMutex_
    std::unordered_map<std::string, std::uint32_t> endpointToPlayerId_; // key "ip:port"
    std::unordered_map<std::string, asio::ip::udp::endpoint> keyToEndpoint_;
    std::unordered_map<std::uint32_t, std::uint8_t> playerInputBits_;
    std::unordered_map<std::uint32_t, std::string> playerNames_;
    std::unordered_map<std::uint32_t, std::uint8_t> playerLives_;
    std::unordered_map<std::uint32_t, std::int32_t> playerScores_;
    std::int32_t lastTeamScore_ = 0;
    std::unordered_map<std::string, std::uint32_t> pendingByIp_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastSeen_;
    std::uint32_t hostId_ = 0;
    bool gameStarted_ = false;
    std::uint8_t lobbyBaseLives_ = 4;
    std::uint8_t lobbyDifficulty_ = 1;

    // ECS Registry (separate synchronization if needed)
    rt::ecs::Registry reg_;
    std::mt19937 rng_;
    std::unordered_set<std::uint32_t> lastKnownEntityIds_; // Track entities from previous tick to detect deletions

    rtype::server::TcpServer* tcp_ = nullptr;
};

} // namespace rtype::server::gameplay
