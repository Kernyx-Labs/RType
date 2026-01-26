#include "Screens.hpp"
#include "common/Protocol.hpp"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <iostream>
#include <unordered_set>

namespace client { namespace ui {

void Screens::handleNetPacket(const char* data, std::size_t n) {
    if (!data || n < sizeof(rtype::net::Header)) return;
    const auto* h = reinterpret_cast<const rtype::net::Header*>(data);
    if (h->version != rtype::net::ProtocolVersion) return;
    if (h->type == rtype::net::MsgType::State) {
        const char* p = data + sizeof(rtype::net::Header);
        if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::StateHeader)) return;
        auto* sh = reinterpret_cast<const rtype::net::StateHeader*>(p);
        p += sizeof(rtype::net::StateHeader);
        std::size_t count = sh->count;
        if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::StateHeader) + count * sizeof(rtype::net::PackedEntity)) return;
        // Reconciliation: update or insert all received entities; mark as seen
        auto* arr = reinterpret_cast<const rtype::net::PackedEntity*>(p);
        std::unordered_set<unsigned> seenIds;
        seenIds.reserve(count);
        double nowSec = GetTime();
        for (std::size_t i = 0; i < count; ++i) {
            PackedEntity e{};
            e.id = arr[i].id;
            e.type = static_cast<unsigned char>(arr[i].type);
            e.x = arr[i].x; e.y = arr[i].y; e.vx = arr[i].vx; e.vy = arr[i].vy;
            e.rgba = arr[i].rgba;
            _entityById[e.id] = e;
            _missedById[e.id] = 0;
            _lastSeenAt[e.id] = nowSec;
            seenIds.insert(e.id);
        }
        // Increment miss counters for any id not seen in this snapshot
        std::vector<unsigned> toErase;
        toErase.reserve(_entityById.size());
        for (const auto& kv : _entityById) {
            unsigned id = kv.first;
            if (seenIds.find(id) == seenIds.end()) {
                int missed = (_missedById.count(id) ? _missedById[id] : 0) + 1;
                _missedById[id] = missed;
                double lastSeen = (_lastSeenAt.count(id) ? _lastSeenAt[id] : nowSec);
                double elapsed = nowSec - lastSeen;
                unsigned char type = kv.second.type;
                double ttl = (type == 2 /* Enemy */) ? _expireSecondsEnemy : _expireSecondsDefault;
                if (missed >= _missThreshold && elapsed >= ttl) toErase.push_back(id);
            }
        }
        for (unsigned id : toErase) {
            _entityById.erase(id);
            _missedById.erase(id);
            _lastSeenAt.erase(id);
        }
        // Rebuild render list with a stable ordering: players, bullets, powerups, enemies
        _entities.clear();
        _entities.reserve(_entityById.size());
        auto appendByType = [&](unsigned char type) {
            for (const auto& kv : _entityById) {
                if (kv.second.type == type) _entities.push_back(kv.second);
            }
        };
        appendByType(1); // Player
        appendByType(3); // Bullet
        appendByType(4); // Powerup (if used)
        appendByType(2); // Enemy
    } else if (h->type == rtype::net::MsgType::Despawn) {
        // Server explicitly told us to remove an entity - do it immediately
        const char* p = data + sizeof(rtype::net::Header);
        if (n < sizeof(rtype::net::Header) + sizeof(std::uint32_t)) return;
        std::uint32_t entityId;
        std::memcpy(&entityId, p, sizeof(entityId));
        _entityById.erase(entityId);
        _missedById.erase(entityId);
        _lastSeenAt.erase(entityId);
        // Rebuild render list immediately
        _entities.clear();
        _entities.reserve(_entityById.size());
        auto appendByType = [&](unsigned char type) {
            for (const auto& kv : _entityById) {
                if (kv.second.type == type) _entities.push_back(kv.second);
            }
        };
        appendByType(1); // Player
        appendByType(3); // Bullet
        appendByType(4); // Powerup
        appendByType(2); // Enemy
    } else if (h->type == rtype::net::MsgType::Roster) {
        const char* p = data + sizeof(rtype::net::Header);
        if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::RosterHeader)) return;
        auto* rh = reinterpret_cast<const rtype::net::RosterHeader*>(p);
        p += sizeof(rtype::net::RosterHeader);
        std::size_t count = rh->count;
        if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::RosterHeader) + count * sizeof(rtype::net::PlayerEntry)) return;
        _otherPlayers.clear();
        std::string unameTrunc = _username.substr(0, 15);
        for (std::size_t i = 0; i < count; ++i) {
            auto* pe = reinterpret_cast<const rtype::net::PlayerEntry*>(p + i * sizeof(rtype::net::PlayerEntry));
            std::string name(pe->name, pe->name + strnlen(pe->name, sizeof(pe->name)));
            int lives = std::clamp<int>(pe->lives, 0, 10);
            if (name == unameTrunc) { _playerLives = lives; _selfId = pe->id; continue; }
            _otherPlayers.push_back({pe->id, name, lives});
        }
        if (_otherPlayers.size() > 3) _otherPlayers.resize(3);
    } else if (h->type == rtype::net::MsgType::LivesUpdate) {
        const char* p = data + sizeof(rtype::net::Header);
        if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::LivesUpdatePayload)) return;
        auto* lu = reinterpret_cast<const rtype::net::LivesUpdatePayload*>(p);
        unsigned id = lu->id;
        int lives = std::clamp<int>(lu->lives, 0, 10);
        if (id == _selfId) { _playerLives = lives; _gameOver = (_playerLives <= 0); }
        else { for (auto& op : _otherPlayers) { if (op.id == id) { op.lives = lives; break; } } }
    } else if (h->type == rtype::net::MsgType::ScoreUpdate) {
        const char* p = data + sizeof(rtype::net::Header);
        if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::ScoreUpdatePayload)) return;
        auto* su = reinterpret_cast<const rtype::net::ScoreUpdatePayload*>(p);
        _score = su->score;
    } else if (h->type == rtype::net::MsgType::ReturnToMenu) {
        _serverReturnToMenu = true;
    } else if (h->type == rtype::net::MsgType::LobbyStatus) {
        const char* p = data + sizeof(rtype::net::Header);
        if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::LobbyStatusPayload)) return;
        auto* ls = reinterpret_cast<const rtype::net::LobbyStatusPayload*>(p);
        _hostId = ls->hostId;
        _lobbyBaseLives = std::clamp<int>(ls->baseLives, 1, 6);
        _lobbyDifficulty = std::clamp<int>(ls->difficulty, 0, 2);
        _lobbyStarted = (ls->started != 0);
    } else if (h->type == rtype::net::MsgType::GameOver) {
        _gameOver = true;
    }
}

} } // namespace client::ui
