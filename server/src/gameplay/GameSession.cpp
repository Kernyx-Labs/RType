#include "gameplay/GameSession.hpp"
#include "protocol/TcpServer.hpp"
#include "rt/game/Components.hpp"
#include "rt/game/Systems.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

using namespace rtype::server::gameplay;
using rtype::server::TcpServer;

static std::string makeKeyLocal(const asio::ip::udp::endpoint &ep) {
  return ep.address().to_string() + ":" + std::to_string(ep.port());
}

GameSession::GameSession(asio::io_context &io, SendFn sendFn,
                         TcpServer *tcpServer)
    : io_(io), send_(std::move(sendFn)), rng_(std::random_device{}()),
      lastPingTime_(std::chrono::steady_clock::now()), tcp_(tcpServer) {}

GameSession::~GameSession() { stop(); }

void GameSession::start() {
  running_ = true;
  gameThread_ = std::thread([this] { gameLoop(); });
}

void GameSession::stop() {
  running_ = false;
  if (gameThread_.joinable())
    gameThread_.join();
}

void GameSession::onTcpHello(const std::string &username,
                             const std::string &ip) {
  reg_.withLock([&](auto &reg) {
    // Cap strictly at 5 players
    std::size_t count = 0;
    for (auto &[pid, ip] : reg.template storage<rt::game::IsPlayer>().data()) {
      count++;
    }
    if (count >= 5) {
      std::cout << "[server] Connection rejected: Server full (5/5 players)\n";
      return;
    }

    // Reuse ship IDs: find first unused 0..4
    std::vector<bool> usedShips(5, false);
    for (auto &[pid, st] : reg.template storage<rt::game::ShipType>().data()) {
      if (st.value < 5)
        usedShips[st.value] = true;
    }
    int assignedShip = -1;
    for (int i = 0; i < 5; ++i) {
      if (!usedShips[i]) {
        assignedShip = i;
        break;
      }
    }
    if (assignedShip == -1)
      assignedShip = 4; // Should not happen given count check

    std::lock_guard<std::mutex> lock(stateMutex_);
    auto e = reg.create(); // create player
    reg.template emplace<rt::game::Transform>(
        e, rt::game::Transform{
               50.f, 100.f + static_cast<float>(pendingByIp_.size()) * 40.f});
    reg.template emplace<rt::game::Velocity>(e, rt::game::Velocity{0.f, 0.f});
    reg.template emplace<rt::game::NetType>(
        e, rt::game::NetType{rtype::net::EntityType::Player});
    reg.template emplace<rt::game::IsPlayer>(e, rt::game::IsPlayer{});
    reg.template emplace<rt::game::ShipType>(
        e, rt::game::ShipType{(std::uint8_t)assignedShip});
    reg.template emplace<rt::game::ColorRGBA>(e,
                                              rt::game::ColorRGBA{0x55AAFFFFu});
    reg.template emplace<rt::game::PlayerInput>(
        e, rt::game::PlayerInput{0, 150.f});
    reg.template emplace<rt::game::Shooter>(
        e, rt::game::Shooter{0.f, 0.15f, 320.f});
    reg.template emplace<rt::game::ChargeGun>(
        e, rt::game::ChargeGun{0.f, 2.0f, false});
    reg.template emplace<rt::game::Size>(e, rt::game::Size{20.f, 12.f});
    reg.template emplace<rt::game::Score>(e, rt::game::Score{0});

    reg.template emplace<rt::game::Name>(
        e, rt::game::Name{username.empty()
                              ? (std::string("Player") + std::to_string(e))
                              : username});
    reg.template emplace<rt::game::Lives>(e, rt::game::Lives{4});

    // If no host yet, assign this player as host
    if (hostId_ == 0) {
      hostId_ = e;
      std::string name = "Player";
      if (auto *n = reg.template get<rt::game::Name>(e))
        name = n->value;
      std::cout << "[server] First player assigned as host: id=" << e
                << " name='" << name << "'\n";
    }

    // store until UDP endpoint binds
    pendingByIp_[ip] = e;
  });
}

void GameSession::bindUdpEndpoint(const asio::ip::udp::endpoint &ep,
                                  std::uint32_t playerId) {
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
  std::cout << "[server] Player UDP bound: id=" << playerId << " from "
            << ep.address().to_string() << ":" << ep.port() << std::endl;
}

void GameSession::onUdpPacket(const asio::ip::udp::endpoint &from,
                              const char *data, std::size_t size) {
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

  if (size < sizeof(rtype::net::Header))
    return;
  const auto *header = reinterpret_cast<const rtype::net::Header *>(data);
  if (header->version != rtype::net::ProtocolVersion)
    return;

  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    lastSeen_[key] = std::chrono::steady_clock::now();
  }

  const char *payload = data + sizeof(rtype::net::Header);
  std::size_t payloadSize = size - sizeof(rtype::net::Header);

  if (header->type == rtype::net::MsgType::Input) {
    if (payloadSize >= sizeof(rtype::net::InputPacket)) {
      auto *in = reinterpret_cast<const rtype::net::InputPacket *>(payload);
      std::uint32_t playerId = 0;
      bool found = false;
      {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto it = endpointToPlayerId_.find(key);
        if (it != endpointToPlayerId_.end()) {
          playerId = it->second;
          found = true;
        }
      }
      if (found) {
        reg_.withLock([&](auto &reg) {
          if (auto *pi = reg.template get<rt::game::PlayerInput>(playerId)) {
            pi->bits = in->bits;
          }
        });
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
          auto *cfg =
              reinterpret_cast<const rtype::net::LobbyConfigPayload *>(payload);
          lobbyBaseLives_ = std::clamp<std::uint8_t>(cfg->baseLives, 1, 6);
          lobbyDifficulty_ = std::clamp<std::uint8_t>(cfg->difficulty, 0, 2);
          std::cout << "[server] Host changed lobby: difficulty="
                    << (int)lobbyDifficulty_
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
      if (it != endpointToPlayerId_.end() && it->second == hostId_ &&
          !gameStarted_) {
        gameStarted_ = true;
        shouldStart = true;
        baseLives = lobbyBaseLives_;

        // Collect player IDs from map directly (safe under stateMutex_)
        for (const auto &[_, pid] : endpointToPlayerId_) {
          playerIds.push_back(pid);
        }
        lastTeamScore_ = 0;
      }
    }

    if (shouldStart) {
      std::cout << "[server] Host started the match!" << std::endl;

      reg_.withLock([&](auto &reg) {
        // Reset Lives and Score
        for (auto &[pid, l] : reg.template storage<rt::game::Lives>().data()) {
          l.value = baseLives;
        }
        for (auto &[pid, s] : reg.template storage<rt::game::Score>().data()) {
          s.value = 0;
        }

        // Reset all players for new game
        int playerIndex = 0;
        std::sort(playerIds.begin(), playerIds.end());
        for (std::uint32_t pid : playerIds) {
          if (auto *t = reg.template get<rt::game::Transform>(pid)) {
            t->x = 50.f;
            t->y = 100.f + static_cast<float>(playerIndex) * 40.f;
          }
          if (auto *v = reg.template get<rt::game::Velocity>(pid)) {
            v->vx = 0.f;
            v->vy = 0.f;
          }
          if (auto *sc = reg.template get<rt::game::Score>(pid)) {
            sc->value = 0;
          }
          if (auto *inv = reg.template get<rt::game::Invincible>(pid)) {
            inv->timeLeft = 1.0f;
          } else {
            reg.template emplace<rt::game::Invincible>(
                pid, rt::game::Invincible{1.0f});
          }
          playerIndex++;
        }

        cleanupGameWorld(reg);
        lastKnownEntityIds_.clear();
      });

      std::cout << "[server] Game initialized for " << playerIds.size()
                << " players\n";

      broadcastRoster();
      broadcastLobbyStatus();

      // Send initial score update
      rtype::net::Header scoreHdr{};
      scoreHdr.version = rtype::net::ProtocolVersion;
      scoreHdr.type = rtype::net::MsgType::ScoreUpdate;
      scoreHdr.size = sizeof(rtype::net::ScoreUpdatePayload);
      rtype::net::ScoreUpdatePayload scorePayload{0, 0};
      std::vector<char> scoreOut(sizeof(scoreHdr) + sizeof(scorePayload));
      std::memcpy(scoreOut.data(), &scoreHdr, sizeof(scoreHdr));
      std::memcpy(scoreOut.data() + sizeof(scoreHdr), &scorePayload,
                  sizeof(scorePayload));

      std::vector<asio::ip::udp::endpoint> endpoints;
      {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (const auto &[_, ep] : keyToEndpoint_) {
          endpoints.push_back(ep);
        }
      }
      for (const auto &ep : endpoints) {
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
  const double tickRate = 60.0; // Game runs at 60 Hz
  const double dt = 1.0 / tickRate;
  auto next = clock::now();
  float elapsed = 0.f;

  reg_.withLock([&](auto &reg) {
    reg.template addSystem(std::make_unique<rt::game::InputSystem>());
    reg.template addSystem(std::make_unique<rt::game::ShootingSystem>());
    reg.template addSystem(std::make_unique<rt::game::ChargeShootingSystem>());
    reg.template addSystem(
        std::make_unique<rt::game::FormationSystem>(&elapsed));
    reg.template addSystem(std::make_unique<rt::game::MovementSystem>());
    reg.template addSystem(
        std::make_unique<rt::game::EnemyShootingSystem>(rng_));
    reg.template addSystem(
        std::make_unique<rt::game::DespawnOffscreenSystem>(-50.f));
    reg.template addSystem(std::make_unique<rt::game::DespawnOutOfBoundsSystem>(
        -50.f, 1000.f, -50.f, 600.f));
    reg.template addSystem(std::make_unique<rt::game::CollisionSystem>());
    reg.template addSystem(std::make_unique<rt::game::InvincibilitySystem>());
    reg.template addSystem(
        std::make_unique<rt::game::PowerupSpawnSystem>(rng_, &lastTeamScore_));
    reg.template addSystem(
        std::make_unique<rt::game::PowerupCollisionSystem>());
    reg.template addSystem(std::make_unique<rt::game::InfiniteFireSystem>());
    reg.template addSystem(
        std::make_unique<rt::game::FormationSpawnSystem>(rng_, &elapsed));
  });

  while (running_) {
    next += std::chrono::duration_cast<clock::duration>(
        std::chrono::duration<double>(dt));
    elapsed += static_cast<float>(dt);
    tickCount_++;

    // Ping mechanism (every 1 second)
    auto now = clock::now();
    if (now - lastPingTime_ >= std::chrono::seconds(1)) {
      lastPingTime_ = now;
      rtype::net::Header ph{};
      ph.version = rtype::net::ProtocolVersion;
      ph.type = rtype::net::MsgType::Ping;
      ph.size = 0;

      std::vector<asio::ip::udp::endpoint> eps;
      {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (auto &[_, ep] : keyToEndpoint_)
          eps.push_back(ep);
      }
      for (auto &ep : eps)
        send_(ep, &ph, sizeof(ph));
    }

    bool isGameStarted = false;
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      isGameStarted = gameStarted_;
    }

    // Only run game systems if the match has started
    if (isGameStarted) {
      reg_.withLock([&](auto &reg) {
        reg.update(static_cast<float>(dt));

        for (auto &[e, inp] :
             reg.template storage<rt::game::PlayerInput>().data()) {
          (void)inp;
          if (auto *hf = reg.template get<rt::game::HitFlag>(e)) {
            if (hf->value) {
              std::uint8_t lives = 0;
              if (auto *l = reg.template get<rt::game::Lives>(e)) {
                if (l->value > 0) {
                  l->value--;
                  lives = l->value;
                }
              }
              if (lives > 0 || lives == 0) { // Keep logic
                broadcastLivesUpdate(e, lives);
              }
              if (auto *t = reg.template get<rt::game::Transform>(e)) {
                constexpr float kStartX = 50.f;
                constexpr float kWorldH = 600.f;
                constexpr float kTopMargin = 56.f;
                constexpr float kBottomMargin = 10.f;
                float y = t->y;
                float maxY = kWorldH - kBottomMargin - 12.f;
                if (y < kTopMargin)
                  y = kTopMargin;
                if (y > maxY)
                  y = maxY;
                t->x = kStartX;
                t->y = y;
              }
              if (auto *v = reg.template get<rt::game::Velocity>(e)) {
                v->vx = 0.f;
                v->vy = 0.f;
              }
              if (auto *inv = reg.template get<rt::game::Invincible>(e)) {
                inv->timeLeft = std::max(inv->timeLeft, 1.0f);
              } else {
                reg.template emplace<rt::game::Invincible>(
                    e, rt::game::Invincible{1.0f});
              }
              hf->value = false;
            } // This closes 'if (hf->value)'
          }

          // Handle life pickups
          if (auto *lp = reg.template get<rt::game::LifePickup>(e)) {
            if (lp->pending) {
              std::uint8_t lives = 0;
              if (auto *l = reg.template get<rt::game::Lives>(e)) {
                lives = l->value;
                if (lives < 10) {
                  lives++;
                  l->value = lives;
                }
              }
              broadcastLivesUpdate(e, lives);
              lp->pending = false; // Mark as processed
            }
          }
        }

        std::int32_t teamScore = 0;
        for (auto &[e, inp] :
             reg.template storage<rt::game::PlayerInput>().data()) {
          (void)inp;
          if (auto *sc = reg.template get<rt::game::Score>(e)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            // playerScores_[e] = sc->value; // Removed
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
            for (const auto &[_, ep] : keyToEndpoint_) {
              endpoints.push_back(ep);
            }
          }
        }

        if (shouldBroadcastScore) {
          rtype::net::Header hdr{};
          hdr.version = rtype::net::ProtocolVersion;
          hdr.type = rtype::net::MsgType::ScoreUpdate;
          hdr.size = sizeof(rtype::net::ScoreUpdatePayload);
          rtype::net::ScoreUpdatePayload p{0, teamScore};
          std::vector<char> out(sizeof(hdr) + sizeof(p));
          std::memcpy(out.data(), &hdr, sizeof(hdr));
          std::memcpy(out.data() + sizeof(hdr), &p, sizeof(p));
          for (const auto &ep : endpoints) {
            send_(ep, out.data(), out.size());
          }
        }
      });
    }

    checkTimeouts();

    // Broadcast state at regular tick intervals (every N ticks) to ensure
    // state snapshots are always aligned with completed game tick
    // boundaries. This eliminates desync between game logic and network
    // updates.
    if (tickCount_ % kBroadcastEveryNTicks == 0) {
      // Detect destroyed entities by comparing before/after entity sets
      std::unordered_set<std::uint32_t> currentEntityIds;
      std::unordered_set<std::uint32_t> playerIds;
      {
        std::lock_guard<std::mutex> lock(stateMutex_);
        for (const auto &[_, pid] : endpointToPlayerId_) {
          playerIds.insert(pid);
        }
      }
      // Access storage under lock
      reg_.withLock([&](auto &reg) {
        for (auto &[e, nt] : reg.template storage<rt::game::NetType>().data()) {
          currentEntityIds.insert(e);
        }
      });
      // Send Despawn for entities that disappeared (excluding players)
      for (std::uint32_t id : lastKnownEntityIds_) {
        if (currentEntityIds.find(id) == currentEntityIds.end() &&
            playerIds.find(id) == playerIds.end()) {
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
    for (auto &[key, last] : lastSeen_) {
      if (now - last > timeout)
        toRemove.push_back(key);
    }
  }
  for (auto &key : toRemove)
    removeClient(key);
}

void GameSession::removeClient(const std::string &key) {
  std::uint32_t id = 0;
  bool wasHost = false;
  std::vector<asio::ip::udp::endpoint> endpoints;
  bool shouldStopGame = false;
  bool allPlayersLeft = false;

  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = endpointToPlayerId_.find(key);
    if (it == endpointToPlayerId_.end())
      return;
    id = it->second;

    wasHost = (id == hostId_);

    endpointToPlayerId_.erase(it);
    keyToEndpoint_.erase(key);
    lastSeen_.erase(key);
    // Maps removed

    // Collect endpoints for broadcasting
    for (auto &[_, ep] : keyToEndpoint_) {
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
    if (endpointToPlayerId_.size() > 0 && endpointToPlayerId_.size() < 2 &&
        gameStarted_) {
      shouldStopGame = true;
      gameStarted_ = false;
    }
  }

  // Destroy entity outside the lock
  reg_.withLock([&](auto &reg) {
    try {
      reg.destroy(id);
    } catch (...) {
    }
  });

  // Send despawn message
  rtype::net::Header hdr{};
  hdr.size = sizeof(std::uint32_t);
  hdr.type = rtype::net::MsgType::Despawn;
  hdr.version = rtype::net::ProtocolVersion;
  std::vector<char> out(sizeof(hdr) + sizeof(std::uint32_t));
  std::memcpy(out.data(), &hdr, sizeof(hdr));
  std::memcpy(out.data() + sizeof(hdr), &id, sizeof(id));
  for (auto &ep : endpoints) {
    send_(ep, out.data(), out.size());
  }

  std::cout << "[server] Removed disconnected client: " << key << " (id=" << id
            << ")\n";

  if (allPlayersLeft) {
    reg_.withLock([&](auto &reg) { cleanupGameWorld(reg); });
    std::cout << "[server] All players left. Game world cleaned up.\n";
  }

  broadcastRoster();
  broadcastLobbyStatus();

  // If game was running and not enough players remain, stop the game
  if (shouldStopGame) {
    std::cout << "[server] Not enough players to continue. Stopping game.\n";
    rtype::net::Header rtm{0, rtype::net::MsgType::ReturnToMenu,
                           rtype::net::ProtocolVersion};
    for (auto &ep : endpoints) {
      send_(ep, &rtm, sizeof(rtm));
    }
    reg_.withLock([&](auto &reg) { cleanupGameWorld(reg); });
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
    for (const auto &[_, ep] : keyToEndpoint_) {
      endpoints.push_back(ep);
    }
  }
  for (const auto &ep : endpoints) {
    send_(ep, out.data(), out.size());
  }
}

void GameSession::broadcastState() {
  rtype::net::Header hdr{};
  hdr.version = rtype::net::ProtocolVersion;
  hdr.type = rtype::net::MsgType::State;

  // Slightly larger snapshot budget; still below common MTU (~1500)
  constexpr std::size_t kMaxUdpBytes = 1400;
  constexpr std::size_t kHeaderBytes = sizeof(rtype::net::Header);
  constexpr std::size_t kStateHdrBytes = sizeof(rtype::net::StateHeader);
  constexpr std::size_t kEntBytes = sizeof(rtype::net::PackedEntity);

  const std::size_t maxEntities =
      (kMaxUdpBytes > (kHeaderBytes + kStateHdrBytes))
          ? ((kMaxUdpBytes - (kHeaderBytes + kStateHdrBytes)) / kEntBytes)
          : 0;

  std::vector<rtype::net::PackedEntity> players;
  std::vector<rtype::net::PackedEntity> bullets;
  std::vector<rtype::net::PackedEntity> enemies;
  std::vector<rtype::net::PackedEntity> powerups;
  players.reserve(16);
  bullets.reserve(64);
  enemies.reserve(64);
  powerups.reserve(16);

  reg_.withLock([&](auto &reg) {
    auto &types = reg.template storage<rt::game::NetType>().data();
    for (auto &[e, nt] : types) {
      auto *tr = reg.template get<rt::game::Transform>(e);
      auto *ve = reg.template get<rt::game::Velocity>(e);
      auto *co = reg.template get<rt::game::ColorRGBA>(e);
      if (!tr || !ve || !co)
        continue;
      rtype::net::PackedEntity pe{};
      pe.id = e;
      pe.type = nt.type;
      pe.x = tr->x;
      pe.y = tr->y;
      pe.vx = ve->vx;
      pe.vy = ve->vy;
      pe.rgba = co->rgba;
      switch (nt.type) {
      case rtype::net::EntityType::Player:
        players.push_back(pe);
        break;
      case rtype::net::EntityType::Bullet:
        bullets.push_back(pe);
        break;
      case rtype::net::EntityType::Powerup:
        powerups.push_back(pe);
        break;
      case rtype::net::EntityType::Enemy:
      default:
        enemies.push_back(pe);
        break;
      }
    }
  });

  // Copy endpoints under lock for broadcasting
  std::vector<asio::ip::udp::endpoint> endpoints;
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (const auto &[key, ep] : keyToEndpoint_) {
      endpoints.push_back(ep);
    }
  }

  // Split across two datagrams to avoid crowding out enemies when bullets
  // spike.
  auto sendBatch = [&](const std::vector<rtype::net::PackedEntity> &batch) {
    rtype::net::StateHeader sh{};
    sh.count = static_cast<std::uint16_t>(batch.size());
    std::size_t payloadSize = sizeof(rtype::net::StateHeader) +
                              batch.size() * sizeof(rtype::net::PackedEntity);
    hdr.size = static_cast<std::uint16_t>(payloadSize);
    std::vector<char> out(sizeof(rtype::net::Header) + payloadSize);
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    std::memcpy(out.data() + sizeof(hdr), &sh, sizeof(sh));
    auto *arr = reinterpret_cast<rtype::net::PackedEntity *>(
        out.data() + sizeof(hdr) + sizeof(sh));
    for (std::uint16_t i = 0; i < sh.count; ++i)
      arr[i] = batch[i];
    for (const auto &ep : endpoints)
      send_(ep, out.data(), out.size());
  };

  // Packet A: players + enemies (authoritative for presence)
  std::vector<rtype::net::PackedEntity> a;
  a.reserve(
      std::min<std::size_t>(players.size() + enemies.size(), maxEntities));
  auto appendLimitedA = [&](const std::vector<rtype::net::PackedEntity> &src) {
    for (const auto &pe : src) {
      if (a.size() >= maxEntities)
        break;
      a.push_back(pe);
    }
  };
  appendLimitedA(players);
  appendLimitedA(enemies);
  sendBatch(a);

  // Packet B: bullets + powerups (may be many; send as much as fits)
  std::vector<rtype::net::PackedEntity> b;
  b.reserve(
      std::min<std::size_t>(bullets.size() + powerups.size(), maxEntities));
  auto appendLimitedB = [&](const std::vector<rtype::net::PackedEntity> &src) {
    for (const auto &pe : src) {
      if (b.size() >= maxEntities)
        break;
      b.push_back(pe);
    }
  };
  appendLimitedB(bullets);
  appendLimitedB(powerups);
  if (!b.empty())
    sendBatch(b);
}

void GameSession::broadcastRoster() {
  rtype::net::RosterHeader rh{};
  std::vector<rtype::net::PlayerEntry> entries;
  std::vector<asio::ip::udp::endpoint> endpoints;

  std::vector<std::pair<std::uint32_t, std::string>> playerIdsAndNames;

  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (const auto &[key, pid] : endpointToPlayerId_) {
      // We can't access registry here to get Name/Lives!
      // So we just collect IDs.
      playerIdsAndNames.push_back({pid, key});
    }

    for (const auto &[_, ep] : keyToEndpoint_) {
      endpoints.push_back(ep);
    }
  }

  // Now access registry to get details
  reg_.withLock([&](auto &reg) {
    entries.reserve(playerIdsAndNames.size());
    for (const auto &[pid, key] : playerIdsAndNames) {
      rtype::net::PlayerEntry pe{};
      pe.id = pid;

      std::uint8_t lives = 0;
      if (auto *l = reg.template get<rt::game::Lives>(pid))
        lives = l->value;
      if (lives > 10)
        lives = 10;
      pe.lives = lives;

      std::string name = "Player" + std::to_string(pid);
      if (auto *n = reg.template get<rt::game::Name>(pid))
        name = n->value;
      std::memset(pe.name, 0, sizeof(pe.name));
      std::strncpy(pe.name, name.c_str(), sizeof(pe.name) - 1);

      std::uint8_t shipId = 0;
      if (auto *s = reg.template get<rt::game::ShipType>(pid))
        shipId = s->value;
      pe.shipId = shipId;

      entries.push_back(pe);
    }
  });

  rh.count = static_cast<std::uint8_t>(entries.size());

  rtype::net::Header hdr{};
  hdr.version = rtype::net::ProtocolVersion;
  hdr.type = rtype::net::MsgType::Roster;
  hdr.size = static_cast<std::uint16_t>(
      sizeof(rh) + entries.size() * sizeof(rtype::net::PlayerEntry));

  std::vector<char> out(sizeof(hdr) + hdr.size);
  std::memcpy(out.data(), &hdr, sizeof(hdr));
  std::memcpy(out.data() + sizeof(hdr), &rh, sizeof(rh));
  if (!entries.empty())
    std::memcpy(out.data() + sizeof(hdr) + sizeof(rh), entries.data(),
                entries.size() * sizeof(rtype::net::PlayerEntry));

  for (const auto &ep : endpoints)
    send_(ep, out.data(), out.size());
}

void GameSession::broadcastLivesUpdate(std::uint32_t id, std::uint8_t lives) {
  rtype::net::Header hdr{};
  hdr.version = rtype::net::ProtocolVersion;
  hdr.type = rtype::net::MsgType::LivesUpdate;
  hdr.size = sizeof(rtype::net::LivesUpdatePayload);
  rtype::net::LivesUpdatePayload p{id, lives};
  std::vector<char> out(sizeof(hdr) + sizeof(p));
  std::memcpy(out.data(), &hdr, sizeof(hdr));
  std::memcpy(out.data() + sizeof(hdr), &p, sizeof(p));

  std::vector<asio::ip::udp::endpoint> endpoints;
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (const auto &[_, ep] : keyToEndpoint_) {
      endpoints.push_back(ep);
    }
  }
  for (const auto &ep : endpoints)
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

    for (const auto &[_, ep] : keyToEndpoint_) {
      endpoints.push_back(ep);
    }
  }

  std::vector<char> out(sizeof(hdr) + sizeof(payload));
  std::memcpy(out.data(), &hdr, sizeof(hdr));
  std::memcpy(out.data() + sizeof(hdr), &payload, sizeof(payload));

  for (const auto &ep : endpoints)
    send_(ep, out.data(), out.size());
}

void GameSession::maybeStartGame() {
  // This function is now deprecated - game starts only via StartMatch
  // message Kept for backwards compatibility but does nothing
}

void GameSession::cleanupGameWorld(rt::ecs::Registry &reg) {
  // Clean up all non-player entities (enemies, bullets, powerups,
  // formations)
  std::vector<rt::ecs::Entity> toDestroy;

  // Find all entities that are not players
  for (auto &[e, nt] : reg.storage<rt::game::NetType>().data()) {
    if (nt.type != rtype::net::EntityType::Player) {
      toDestroy.push_back(e);
    }
  }

  // Also destroy formations (they may not have NetType component)
  for (auto &[e, f] : reg.storage<rt::game::Formation>().data()) {
    (void)f;
    toDestroy.push_back(e);
  }

  // Destroy all collected entities
  for (auto e : toDestroy) {
    try {
      reg.destroy(e);
    } catch (...) {
    }
  }

  // Reset team score
  lastTeamScore_ = 0;

  std::cout << "[server] Game world cleaned: " << toDestroy.size()
            << " entities removed\n";
}

std::string GameSession::makeKey(const asio::ip::udp::endpoint &ep) {
  return makeKeyLocal(ep);
}
