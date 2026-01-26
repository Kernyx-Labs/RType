#include "gameplay/GameSession.hpp"
#include "protocol/TcpServer.hpp"
#include <iostream>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include "rt/game/Components.hpp"
#include "rt/game/Systems.hpp"

using namespace rtype::server::gameplay;
using rtype::server::TcpServer;

static std::string makeKeyLocal(const asio::ip::udp::endpoint& ep) {
    return ep.address().to_string() + ":" + std::to_string(ep.port());
}

GameSession::GameSession(asio::io_context& io, SendFn sendFn, TcpServer* tcpServer)
    : io_(io), send_(std::move(sendFn)), rng_(std::random_device{}()), tcp_(tcpServer) {}

GameSession::~GameSession() { stop(); }

void GameSession::start() {
    running_ = true;
    gameThread_ = std::thread([this]{ gameLoop(); });
}

void GameSession::stop() {
    running_ = false;
    if (gameThread_.joinable()) gameThread_.join();
}

void GameSession::onTcpHello(const std::string& username, const std::string& ip) {
    auto e = reg_.create(); // create player
    reg_.emplace<rt::game::Transform>(e, rt::game::Transform{50.f, 100.f + static_cast<float>(pendingByIp_.size()) * 40.f});
    reg_.emplace<rt::game::Velocity>(e, rt::game::Velocity{0.f, 0.f});
    reg_.emplace<rt::game::NetType>(e, rt::game::NetType{rtype::net::EntityType::Player});
    reg_.emplace<rt::game::ColorRGBA>(e, rt::game::ColorRGBA{0x55AAFFFFu});
    reg_.emplace<rt::game::PlayerInput>(e, rt::game::PlayerInput{0, 150.f});
    reg_.emplace<rt::game::Shooter>(e, rt::game::Shooter{0.f, 0.15f, 320.f});
    reg_.emplace<rt::game::ChargeGun>(e, rt::game::ChargeGun{0.f, 2.0f, false});
    reg_.emplace<rt::game::Size>(e, rt::game::Size{20.f, 12.f});
    reg_.emplace<rt::game::Score>(e, rt::game::Score{0});

    std::lock_guard<std::mutex> lock(stateMutex_);
    playerInputBits_[e] = 0;
    playerLives_[e] = 4;
    playerScores_[e] = 0;
    playerNames_[e] = username.empty() ? (std::string("Player") + std::to_string(e)) : username;

    // If no host yet, assign this player as host
    if (hostId_ == 0) {
        hostId_ = e;
        std::cout << "[server] First player assigned as host: id=" << e << " name='" << playerNames_[e] << "'\n";
    }

    // store until UDP endpoint binds
    pendingByIp_[ip] = e;
}

void GameSession::bindUdpEndpoint(const asio::ip::udp::endpoint& ep, std::uint32_t playerId) {
    auto key = makeKey(ep);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        endpointToPlayerId_[key] = playerId;
        keyToEndpoint_[key] = ep;
        lastSeen_[key] = std::chrono::steady_clock::now();
    }
    // Broadcast outside the lock to avoid blocking I/O while holding the mutex
    broadcastRoster();
    broadcastLobbyStatus();
    std::cout << "[server] Player UDP bound: id=" << playerId << " from " << ep.address().to_string() << ":" << ep.port() << std::endl;
}

void GameSession::onUdpPacket(const asio::ip::udp::endpoint& from, const char* data, std::size_t size) {
    auto key = makeKey(from);

    // If endpoint not bound, check for pending player from TCP
    bool needsBind = false;
    std::uint32_t playerIdToBind = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (endpointToPlayerId_.find(key) == endpointToPlayerId_.end()) {
            auto ip = from.address().to_string();
            auto it = pendingByIp_.find(ip);
            if (it != pendingByIp_.end()) {
                playerIdToBind = it->second;
                pendingByIp_.erase(it);
                needsBind = true;
            } else {
                return;
            }
        }
    }

    if (needsBind) {
        bindUdpEndpoint(from, playerIdToBind);
    }

    if (size < sizeof(rtype::net::Header)) return;
    const auto* header = reinterpret_cast<const rtype::net::Header*>(data);
    if (header->version != rtype::net::ProtocolVersion) return;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastSeen_[key] = std::chrono::steady_clock::now();
    }

    const char* payload = data + sizeof(rtype::net::Header);
    std::size_t payloadSize = size - sizeof(rtype::net::Header);

    if (header->type == rtype::net::MsgType::Input) {
        if (payloadSize >= sizeof(rtype::net::InputPacket)) {
            auto* in = reinterpret_cast<const rtype::net::InputPacket*>(payload);
            std::uint32_t playerId = 0;
            bool found = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                auto it = endpointToPlayerId_.find(key);
                if (it != endpointToPlayerId_.end()) {
                    playerId = it->second;
                    playerInputBits_[playerId] = in->bits;
                    found = true;
                }
            }
            // Update ECS component outside the lock
            if (found) {
                if (auto* pi = reg_.get<rt::game::PlayerInput>(playerId))
                    pi->bits = in->bits;
            }
        }
        return;
    }

    if (header->type == rtype::net::MsgType::LobbyConfig) {
        if (payloadSize >= sizeof(rtype::net::LobbyConfigPayload)) {
            bool shouldBroadcast = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                auto it = endpointToPlayerId_.find(key);
                if (it != endpointToPlayerId_.end() && it->second == hostId_) {
                    auto* cfg = reinterpret_cast<const rtype::net::LobbyConfigPayload*>(payload);
                    lobbyBaseLives_ = std::clamp<std::uint8_t>(cfg->baseLives, 1, 6);
                    lobbyDifficulty_ = std::clamp<std::uint8_t>(cfg->difficulty, 0, 2);
                    std::cout << "[server] Host changed lobby: difficulty=" << (int)lobbyDifficulty_
                              << " baseLives=" << (int)lobbyBaseLives_ << std::endl;
                    shouldBroadcast = true;
                }
            }
            if (shouldBroadcast) {
                broadcastLobbyStatus();
            }
        }
        return;
    }

    if (header->type == rtype::net::MsgType::StartMatch) {
        bool shouldStart = false;
        std::vector<std::uint32_t> playerIds;
        std::uint8_t baseLives = 0;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = endpointToPlayerId_.find(key);
            if (it != endpointToPlayerId_.end() && it->second == hostId_ && !gameStarted_) {
                gameStarted_ = true;
                shouldStart = true;
                baseLives = lobbyBaseLives_;

                // Collect player IDs and reset their state
                for (auto& [pid, lives] : playerLives_) {
                    playerIds.push_back(pid);
                    lives = baseLives;
                    playerScores_[pid] = 0;
                }
                lastTeamScore_ = 0;
            }
        }

        if (shouldStart) {
            std::cout << "[server] Host started the match!" << std::endl;

            // Reset all players for new game (ECS operations outside the lock)
            int playerIndex = 0;
            for (std::uint32_t pid : playerIds) {
                // Reset player position
                if (auto* t = reg_.get<rt::game::Transform>(pid)) {
                    t->x = 50.f;
                    t->y = 100.f + static_cast<float>(playerIndex) * 40.f;
                }

                // Reset velocity
                if (auto* v = reg_.get<rt::game::Velocity>(pid)) {
                    v->vx = 0.f;
                    v->vy = 0.f;
                }

                // Reset score component
                if (auto* sc = reg_.get<rt::game::Score>(pid)) {
                    sc->value = 0;
                }

                // Brief spawn invincibility at start
                if (auto* inv = reg_.get<rt::game::Invincible>(pid)) {
                    inv->timeLeft = 1.0f;
                } else {
                    reg_.emplace<rt::game::Invincible>(pid, rt::game::Invincible{1.0f});
                }

                playerIndex++;
            }

            // Make sure game world is clean before starting
            cleanupGameWorld();
            lastKnownEntityIds_.clear(); // Reset entity tracking

            std::cout << "[server] Game initialized for " << playerIds.size() << " players\n";

            broadcastRoster();
            broadcastLobbyStatus();

            // Send initial score update
            rtype::net::Header scoreHdr{};
            scoreHdr.version = rtype::net::ProtocolVersion;
            scoreHdr.type = rtype::net::MsgType::ScoreUpdate;
            scoreHdr.size = sizeof(rtype::net::ScoreUpdatePayload);
            rtype::net::ScoreUpdatePayload scorePayload{ 0, 0 };
            std::vector<char> scoreOut(sizeof(scoreHdr) + sizeof(scorePayload));
            std::memcpy(scoreOut.data(), &scoreHdr, sizeof(scoreHdr));
            std::memcpy(scoreOut.data() + sizeof(scoreHdr), &scorePayload, sizeof(scorePayload));

            std::vector<asio::ip::udp::endpoint> endpoints;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                for (const auto& [_, ep] : keyToEndpoint_) {
                    endpoints.push_back(ep);
                }
            }
            for (const auto& ep : endpoints) {
                send_(ep, scoreOut.data(), scoreOut.size());
            }
        }
        return;
    }

    if (header->type == rtype::net::MsgType::Disconnect) {
        removeClient(key);
        return;
    }
}

void GameSession::gameLoop() {
    using clock = std::chrono::steady_clock;
    const double tickRate = 60.0;   // Game runs at 60 Hz
    const double dt = 1.0 / tickRate;
    auto next = clock::now();
    float elapsed = 0.f;

    reg_.addSystem(std::make_unique<rt::game::InputSystem>());
    reg_.addSystem(std::make_unique<rt::game::ShootingSystem>());
    reg_.addSystem(std::make_unique<rt::game::ChargeShootingSystem>());
    reg_.addSystem(std::make_unique<rt::game::FormationSystem>(&elapsed));
    reg_.addSystem(std::make_unique<rt::game::MovementSystem>());
    reg_.addSystem(std::make_unique<rt::game::EnemyShootingSystem>(rng_));
    reg_.addSystem(std::make_unique<rt::game::DespawnOffscreenSystem>(-50.f));
    reg_.addSystem(std::make_unique<rt::game::DespawnOutOfBoundsSystem>(-50.f, 1000.f, -50.f, 600.f));
    reg_.addSystem(std::make_unique<rt::game::CollisionSystem>());
    reg_.addSystem(std::make_unique<rt::game::InvincibilitySystem>());
    reg_.addSystem(std::make_unique<rt::game::PowerupSpawnSystem>(rng_, &lastTeamScore_));
    reg_.addSystem(std::make_unique<rt::game::PowerupCollisionSystem>());
    reg_.addSystem(std::make_unique<rt::game::InfiniteFireSystem>());
    reg_.addSystem(std::make_unique<rt::game::FormationSpawnSystem>(rng_, &elapsed));

    while (running_) {
        next += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(dt));
        elapsed += static_cast<float>(dt);
        tickCount_++;

        bool isGameStarted = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            isGameStarted = gameStarted_;
        }

        // Only run game systems if the match has started
        if (isGameStarted) {
            reg_.update(static_cast<float>(dt));

            for (auto& [e, inp] : reg_.storage<rt::game::PlayerInput>().data()) {
                (void)inp;
                if (auto* hf = reg_.get<rt::game::HitFlag>(e)) {
                    if (hf->value) {
                        std::uint8_t lives = 0;
                        {
                            std::lock_guard<std::mutex> lock(stateMutex_);
                            lives = playerLives_[e];
                            if (lives > 0) {
                                lives = static_cast<std::uint8_t>(lives - 1);
                                playerLives_[e] = lives;
                            }
                        }
                        if (lives > 0 || lives == 0) {
                            broadcastLivesUpdate(e, lives);
                        }
                        if (auto* t = reg_.get<rt::game::Transform>(e)) {
                            constexpr float kStartX = 50.f;
                            constexpr float kWorldH = 600.f;
                            constexpr float kTopMargin = 56.f;
                            constexpr float kBottomMargin = 10.f;
                            float y = t->y;
                            float maxY = kWorldH - kBottomMargin - 12.f;
                            if (y < kTopMargin) y = kTopMargin;
                            if (y > maxY) y = maxY;
                            t->x = kStartX; t->y = y;
                        }
                        if (auto* v = reg_.get<rt::game::Velocity>(e)) { v->vx = 0.f; v->vy = 0.f; }
                        if (auto* inv = reg_.get<rt::game::Invincible>(e)) {
                            inv->timeLeft = std::max(inv->timeLeft, 1.0f);
                        } else {
                            reg_.emplace<rt::game::Invincible>(e, rt::game::Invincible{1.0f});
                        }
                        hf->value = false;
                    }
                }

                // Handle life pickups
                if (auto* lp = reg_.get<rt::game::LifePickup>(e)) {
                    if (lp->pending) {
                        std::uint8_t lives = 0;
                        {
                            std::lock_guard<std::mutex> lock(stateMutex_);
                            lives = playerLives_[e];
                            if (lives < 10) { // Cap at 10 lives
                                lives = static_cast<std::uint8_t>(lives + 1);
                                playerLives_[e] = lives;
                            }
                        }
                        broadcastLivesUpdate(e, lives);
                        lp->pending = false; // Mark as processed
                    }
                }
            }

            std::int32_t teamScore = 0;
            for (auto& [e, inp] : reg_.storage<rt::game::PlayerInput>().data()) {
                (void)inp;
                if (auto* sc = reg_.get<rt::game::Score>(e)) {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    playerScores_[e] = sc->value;
                    teamScore += sc->value;
                }
            }

            bool shouldBroadcastScore = false;
            std::vector<asio::ip::udp::endpoint> endpoints;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                if (teamScore != lastTeamScore_) {
                    lastTeamScore_ = teamScore;
                    shouldBroadcastScore = true;
                    for (const auto& [_, ep] : keyToEndpoint_) {
                        endpoints.push_back(ep);
                    }
                }
            }

            if (shouldBroadcastScore) {
                rtype::net::Header hdr{};
                hdr.version = rtype::net::ProtocolVersion;
                hdr.type = rtype::net::MsgType::ScoreUpdate;
                hdr.size = sizeof(rtype::net::ScoreUpdatePayload);
                rtype::net::ScoreUpdatePayload p{ 0, teamScore };
                std::vector<char> out(sizeof(hdr) + sizeof(p));
                std::memcpy(out.data(), &hdr, sizeof(hdr));
                std::memcpy(out.data() + sizeof(hdr), &p, sizeof(p));
                for (const auto& ep : endpoints) {
                    send_(ep, out.data(), out.size());
                }
            }
        }

        checkTimeouts();

        // Broadcast state at regular tick intervals (every N ticks) to ensure
        // state snapshots are always aligned with completed game tick boundaries.
        // This eliminates desync between game logic and network updates.
        if (tickCount_ % kBroadcastEveryNTicks == 0) {
            // Detect destroyed entities by comparing before/after entity sets
            std::unordered_set<std::uint32_t> currentEntityIds;
            std::unordered_set<std::uint32_t> playerIds;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                for (const auto& [_, pid] : endpointToPlayerId_) {
                    playerIds.insert(pid);
                }
            }
            for (auto& [e, nt] : reg_.storage<rt::game::NetType>().data()) {
                currentEntityIds.insert(e);
            }
            // Send Despawn for entities that disappeared (excluding players)
            for (std::uint32_t id : lastKnownEntityIds_) {
                if (currentEntityIds.find(id) == currentEntityIds.end() && playerIds.find(id) == playerIds.end()) {
                    broadcastDespawn(id);
                }
            }
            lastKnownEntityIds_ = currentEntityIds;
            broadcastState();
        }

        std::this_thread::sleep_until(next);
    }
}

void GameSession::checkTimeouts() {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    const auto timeout = seconds(10);
    std::vector<std::string> toRemove;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto& [key, last] : lastSeen_) {
            if (now - last > timeout)
                toRemove.push_back(key);
        }
    }
    for (auto& key : toRemove)
        removeClient(key);
}

void GameSession::removeClient(const std::string& key) {
    std::uint32_t id = 0;
    bool wasHost = false;
    std::vector<asio::ip::udp::endpoint> endpoints;
    bool shouldStopGame = false;
    bool allPlayersLeft = false;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto it = endpointToPlayerId_.find(key);
        if (it == endpointToPlayerId_.end()) return;
        id = it->second;

        wasHost = (id == hostId_);

        endpointToPlayerId_.erase(it);
        keyToEndpoint_.erase(key);
        lastSeen_.erase(key);
        playerInputBits_.erase(id);
        playerLives_.erase(id);
        playerScores_.erase(id);
        playerNames_.erase(id);

        // Collect endpoints for broadcasting
        for (auto& [_, ep] : keyToEndpoint_) {
            endpoints.push_back(ep);
        }

        // Reassign host if needed
        if (wasHost && !endpointToPlayerId_.empty()) {
            hostId_ = endpointToPlayerId_.begin()->second;
            std::cout << "[server] New host assigned: id=" << hostId_ << std::endl;
        } else if (endpointToPlayerId_.empty()) {
            hostId_ = 0;
            gameStarted_ = false;
            allPlayersLeft = true;
        }

        // Check if we should stop the game
        if (endpointToPlayerId_.size() > 0 && endpointToPlayerId_.size() < 2 && gameStarted_) {
            shouldStopGame = true;
            gameStarted_ = false;
        }
    }

    // Destroy entity outside the lock
    try { reg_.destroy(id); } catch (...) {}

    // Send despawn message
    rtype::net::Header hdr{};
    hdr.size = sizeof(std::uint32_t);
    hdr.type = rtype::net::MsgType::Despawn;
    hdr.version = rtype::net::ProtocolVersion;
    std::vector<char> out(sizeof(hdr) + sizeof(std::uint32_t));
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    std::memcpy(out.data() + sizeof(hdr), &id, sizeof(id));
    for (auto& ep : endpoints) {
        send_(ep, out.data(), out.size());
    }

    std::cout << "[server] Removed disconnected client: " << key << " (id=" << id << ")\n";

    if (allPlayersLeft) {
        cleanupGameWorld();
        std::cout << "[server] All players left. Game world cleaned up.\n";
    }

    broadcastRoster();
    broadcastLobbyStatus();

    // If game was running and not enough players remain, stop the game
    if (shouldStopGame) {
        std::cout << "[server] Not enough players to continue. Stopping game.\n";
        rtype::net::Header rtm{0, rtype::net::MsgType::ReturnToMenu, rtype::net::ProtocolVersion};
        for (auto& ep : endpoints) {
            send_(ep, &rtm, sizeof(rtm));
        }
        cleanupGameWorld();
        broadcastLobbyStatus();
    }
}

void GameSession::broadcastDespawn(std::uint32_t entityId) {
    rtype::net::Header hdr{};
    hdr.size = sizeof(std::uint32_t);
    hdr.type = rtype::net::MsgType::Despawn;
    hdr.version = rtype::net::ProtocolVersion;
    std::vector<char> out(sizeof(hdr) + sizeof(std::uint32_t));
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    std::memcpy(out.data() + sizeof(hdr), &entityId, sizeof(entityId));

    std::vector<asio::ip::udp::endpoint> endpoints;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (const auto& [_, ep] : keyToEndpoint_) {
            endpoints.push_back(ep);
        }
    }
    for (const auto& ep : endpoints) {
        send_(ep, out.data(), out.size());
    }
}

void GameSession::broadcastState() {
    rtype::net::Header hdr{};
    hdr.version = rtype::net::ProtocolVersion;
    hdr.type = rtype::net::MsgType::State;

    // Slightly larger snapshot budget; still below common MTU (~1500)
    constexpr std::size_t kMaxUdpBytes   = 1400;
    constexpr std::size_t kHeaderBytes   = sizeof(rtype::net::Header);
    constexpr std::size_t kStateHdrBytes = sizeof(rtype::net::StateHeader);
    constexpr std::size_t kEntBytes      = sizeof(rtype::net::PackedEntity);

    const std::size_t maxEntities = (kMaxUdpBytes > (kHeaderBytes + kStateHdrBytes))
        ? ((kMaxUdpBytes - (kHeaderBytes + kStateHdrBytes)) / kEntBytes)
        : 0;

    std::vector<rtype::net::PackedEntity> players;
    std::vector<rtype::net::PackedEntity> bullets;
    std::vector<rtype::net::PackedEntity> enemies;
    std::vector<rtype::net::PackedEntity> powerups;
    players.reserve(16); bullets.reserve(64); enemies.reserve(64); powerups.reserve(16);

    auto& types = reg_.storage<rt::game::NetType>().data();
    for (auto& [e, nt] : types) {
        auto* tr = reg_.get<rt::game::Transform>(e);
        auto* ve = reg_.get<rt::game::Velocity>(e);
        auto* co = reg_.get<rt::game::ColorRGBA>(e);
        if (!tr || !ve || !co) continue;
        rtype::net::PackedEntity pe{};
        pe.id = e;
        pe.type = nt.type;
        pe.x = tr->x; pe.y = tr->y;
        pe.vx = ve->vx; pe.vy = ve->vy;
        pe.rgba = co->rgba;
        switch (nt.type) {
            case rtype::net::EntityType::Player: players.push_back(pe); break;
            case rtype::net::EntityType::Bullet: bullets.push_back(pe); break;
            case rtype::net::EntityType::Powerup: powerups.push_back(pe); break;
            case rtype::net::EntityType::Enemy:
            default: enemies.push_back(pe); break;
        }
    }

    // Copy endpoints under lock for broadcasting
    std::vector<asio::ip::udp::endpoint> endpoints;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (const auto& [key, ep] : keyToEndpoint_) {
            endpoints.push_back(ep);
        }
    }

    // Split across two datagrams to avoid crowding out enemies when bullets spike.
    auto sendBatch = [&](const std::vector<rtype::net::PackedEntity>& batch) {
        rtype::net::StateHeader sh{};
        sh.count = static_cast<std::uint16_t>(batch.size());
        std::size_t payloadSize = sizeof(rtype::net::StateHeader) + batch.size() * sizeof(rtype::net::PackedEntity);
        hdr.size = static_cast<std::uint16_t>(payloadSize);
        std::vector<char> out(sizeof(rtype::net::Header) + payloadSize);
        std::memcpy(out.data(), &hdr, sizeof(hdr));
        std::memcpy(out.data() + sizeof(hdr), &sh, sizeof(sh));
        auto* arr = reinterpret_cast<rtype::net::PackedEntity*>(out.data() + sizeof(hdr) + sizeof(sh));
        for (std::uint16_t i = 0; i < sh.count; ++i) arr[i] = batch[i];
        for (const auto& ep : endpoints) send_(ep, out.data(), out.size());
    };

    // Packet A: players + enemies (authoritative for presence)
    std::vector<rtype::net::PackedEntity> a;
    a.reserve(std::min<std::size_t>(players.size() + enemies.size(), maxEntities));
    auto appendLimitedA = [&](const std::vector<rtype::net::PackedEntity>& src) {
        for (const auto& pe : src) { if (a.size() >= maxEntities) break; a.push_back(pe); }
    };
    appendLimitedA(players);
    appendLimitedA(enemies);
    sendBatch(a);

    // Packet B: bullets + powerups (may be many; send as much as fits)
    std::vector<rtype::net::PackedEntity> b;
    b.reserve(std::min<std::size_t>(bullets.size() + powerups.size(), maxEntities));
    auto appendLimitedB = [&](const std::vector<rtype::net::PackedEntity>& src) {
        for (const auto& pe : src) { if (b.size() >= maxEntities) break; b.push_back(pe); }
    };
    appendLimitedB(bullets);
    appendLimitedB(powerups);
    if (!b.empty()) sendBatch(b);
}

void GameSession::broadcastRoster() {
    rtype::net::RosterHeader rh{};
    std::vector<rtype::net::PlayerEntry> entries;
    std::vector<asio::ip::udp::endpoint> endpoints;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        entries.reserve(endpointToPlayerId_.size());

        for (const auto& [key, pid] : endpointToPlayerId_) {
            rtype::net::PlayerEntry pe{};
            pe.id = pid;

            auto itl = playerLives_.find(pid);
            std::uint8_t lives = (itl != playerLives_.end()) ? itl->second : 0;
            if (lives > 10) lives = 10;
            pe.lives = lives;

            auto itn = playerNames_.find(pid);
            std::string name = (itn != playerNames_.end()) ? itn->second : (std::string("Player") + std::to_string(pid));
            std::memset(pe.name, 0, sizeof(pe.name));
            std::strncpy(pe.name, name.c_str(), sizeof(pe.name) - 1);

            entries.push_back(pe);
        }

        for (const auto& [_, ep] : keyToEndpoint_) {
            endpoints.push_back(ep);
        }
    }

    rh.count = static_cast<std::uint8_t>(entries.size());

    rtype::net::Header hdr{};
    hdr.version = rtype::net::ProtocolVersion;
    hdr.type = rtype::net::MsgType::Roster;
    hdr.size = static_cast<std::uint16_t>(sizeof(rh) + entries.size() * sizeof(rtype::net::PlayerEntry));

    std::vector<char> out(sizeof(hdr) + hdr.size);
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    std::memcpy(out.data() + sizeof(hdr), &rh, sizeof(rh));
    if (!entries.empty())
        std::memcpy(out.data() + sizeof(hdr) + sizeof(rh), entries.data(), entries.size() * sizeof(rtype::net::PlayerEntry));

    for (const auto& ep : endpoints)
        send_(ep, out.data(), out.size());
}

void GameSession::broadcastLivesUpdate(std::uint32_t id, std::uint8_t lives) {
    rtype::net::Header hdr{};
    hdr.version = rtype::net::ProtocolVersion;
    hdr.type = rtype::net::MsgType::LivesUpdate;
    hdr.size = sizeof(rtype::net::LivesUpdatePayload);
    rtype::net::LivesUpdatePayload p{ id, lives };
    std::vector<char> out(sizeof(hdr) + sizeof(p));
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    std::memcpy(out.data() + sizeof(hdr), &p, sizeof(p));

    std::vector<asio::ip::udp::endpoint> endpoints;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (const auto& [_, ep] : keyToEndpoint_) {
            endpoints.push_back(ep);
        }
    }
    for (const auto& ep : endpoints)
        send_(ep, out.data(), out.size());
}

void GameSession::broadcastLobbyStatus() {
    rtype::net::Header hdr{};
    hdr.version = rtype::net::ProtocolVersion;
    hdr.type = rtype::net::MsgType::LobbyStatus;
    hdr.size = sizeof(rtype::net::LobbyStatusPayload);

    rtype::net::LobbyStatusPayload payload{};
    std::vector<asio::ip::udp::endpoint> endpoints;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        payload.hostId = hostId_;
        payload.baseLives = lobbyBaseLives_;
        payload.difficulty = lobbyDifficulty_;
        payload.started = gameStarted_ ? 1 : 0;
        payload.reserved = 0;

        for (const auto& [_, ep] : keyToEndpoint_) {
            endpoints.push_back(ep);
        }
    }

    std::vector<char> out(sizeof(hdr) + sizeof(payload));
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    std::memcpy(out.data() + sizeof(hdr), &payload, sizeof(payload));

    for (const auto& ep : endpoints)
        send_(ep, out.data(), out.size());
}

void GameSession::maybeStartGame() {
    // This function is now deprecated - game starts only via StartMatch message
    // Kept for backwards compatibility but does nothing
}

void GameSession::cleanupGameWorld() {
    // Clean up all non-player entities (enemies, bullets, powerups, formations)
    std::vector<rt::ecs::Entity> toDestroy;

    // Find all entities that are not players
    for (auto& [e, nt] : reg_.storage<rt::game::NetType>().data()) {
        if (nt.type != rtype::net::EntityType::Player) {
            toDestroy.push_back(e);
        }
    }

    // Also destroy formations (they may not have NetType component)
    for (auto& [e, f] : reg_.storage<rt::game::Formation>().data()) {
        (void)f;
        toDestroy.push_back(e);
    }

    // Destroy all collected entities
    for (auto e : toDestroy) {
        try {
            reg_.destroy(e);
        } catch (...) {}
    }

    // Reset team score
    lastTeamScore_ = 0;

    std::cout << "[server] Game world cleaned: " << toDestroy.size() << " entities removed\n";
}

std::string GameSession::makeKey(const asio::ip::udp::endpoint& ep) {
    return makeKeyLocal(ep);
}
