# R-Type Codebase Analysis Findings

This document contains a comprehensive analysis of the R-Type codebase, identifying issues, architectural problems, and improvement opportunities across all components.

**Analysis Date:** January 2026
**Components Analyzed:** Engine, Server, Client, Common Protocol

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Critical Issues](#critical-issues)
3. [High Priority Issues](#high-priority-issues)
4. [Medium Priority Issues](#medium-priority-issues)
5. [Low Priority Issues](#low-priority-issues)
6. [Engine Analysis](#engine-analysis)
7. [Server Analysis](#server-analysis)
8. [Client Analysis](#client-analysis)
9. [Known Issues Root Causes](#known-issues-root-causes)
10. [Recommended Fix Order](#recommended-fix-order)

---

## Executive Summary

The codebase has a **solid ECS foundation** in the core engine but suffers from:
- **Critical thread safety issues** in the server (race conditions between ASIO and game loop)
- **Architectural coupling** between engine game layer and network protocol
- **Dead code and duplicate implementations** in the client
- **No proper multi-threading synchronization** despite using multiple threads

### Component Health Overview

| Component | Health | Key Issues |
|-----------|--------|------------|
| Engine Core (rt/ecs/) | Good | Clean ECS, minor performance concerns |
| Engine Game Layer (rt/game/) | Fair | Coupled to protocol, duplicate components |
| Server | Poor | Race conditions, duplicate state, no sync |
| Client Multiplayer | Poor | Dead code, no ECS usage, blocking calls |
| Client Singleplayer | Fair | Hybrid ECS approach, manual collision |

---

## Critical Issues

### CRIT-001: Server Thread Race Conditions

**Severity:** Critical
**Component:** Server
**Location:** `server/src/gameplay/GameSession.cpp`

**Description:**
The server runs two threads without any synchronization:
1. **ASIO I/O thread** — handles incoming packets, modifies player state
2. **Game loop thread** — runs at 60Hz, reads/writes the same state

**Affected shared state (NO mutex protection):**
- `endpointToPlayerId_` (line 62-64)
- `keyToEndpoint_` (line 62-64)
- `playerInputBits_` (line 99)
- `playerLives_` (line 234)
- `playerScores_` (line 285)
- `playerNames_` (line 60)
- `lastSeen_` (line 65)
- `pendingByIp_` (line 55)
- ECS Registry itself (all entity operations)

**Impact:**
- Data corruption causing "ghost" players
- Crashes from iterator invalidation
- Packet loss and connection drops
- Unpredictable game state

**Fix Required:**
```cpp
// Option A: Add mutex to all shared state access
std::mutex stateMutex_;
// Lock in packet handler and game loop

// Option B: Move all logic to single thread (ASIO strand)
// Option C: Use lock-free queues for inter-thread communication
```

---

### CRIT-002: Server TCP Client Cleanup Race

**Severity:** Critical
**Component:** Server
**Location:** `server/src/TcpServer.cpp:78-81`

**Description:**
```cpp
sock->async_wait(asio::ip::tcp::socket::wait_error,
    [self = shared_from_this(), sock](std::error_code) {
        self->clients_.erase(sock);  // NO SYNCHRONIZATION
    });
```

The `clients_` set is modified from async callbacks while `stop()` (line 20) iterates it.

**Impact:**
- Undefined behavior during server shutdown
- Lobby connection drops
- Potential crashes

---

### CRIT-003: Client Dead Code and ODR Violation

**Severity:** Critical
**Component:** Client
**Location:** `client/src/net/Net.cpp` vs `client/src/ui/Screens.cpp`

**Description:**
Two separate files define identical network globals:

```cpp
// Net.cpp (lines 13-28) - DEAD CODE, never called
struct UdpClientGlobals { ... } g;

// Screens.cpp (lines 18-28) - Actually used
struct UdpClientGlobals { ... } g;
```

The entire `Net.cpp` file is unreachable — all networking happens in `Screens.cpp`.

**Impact:**
- Two UDP sockets allocated in memory
- Maintenance confusion
- ODR (One Definition Rule) violation

**Fix Required:**
- Delete `Net.cpp` entirely or consolidate networking into single module

---

### CRIT-004: Game Tick and State Broadcast Desync

**Severity:** Critical
**Component:** Server
**Location:** `server/src/gameplay/GameSession.cpp:194-319`

**Description:**
```cpp
const double tickRate = 60.0;   // Game simulation at 60 Hz
const double stateHz_ = 20.0;   // State broadcast at 20 Hz (independent timer)
```

These timers are **not synchronized** — state snapshots don't align with game ticks.

**Impact:**
- Clients receive state from mid-tick
- Interpolation artifacts
- Perceived lag and stuttering

**Fix Required:**
- Send state every N game ticks (e.g., every 3rd tick for 20Hz)
- Or increase state rate to match tick rate

---

## High Priority Issues

### HIGH-001: Engine Game Layer Coupled to Protocol

**Severity:** High
**Component:** Engine
**Location:** `engine/src/rt/game/Components.hpp:4`

**Description:**
```cpp
#include "common/Protocol.hpp"  // Game logic knows about network!

struct NetType {
    rtype::net::EntityType type;  // Network enum in game component
};
```

Systems use `EntityType::Player` for logic instead of tag components.

**Impact:**
- Engine cannot be used standalone
- Violates "engine as separate project" principle
- Tight coupling prevents reuse

**Fix Required:**
```cpp
// Use pure game tags instead
struct IsPlayer {};
struct IsEnemy {};
struct IsBullet {};

// Only add NetType during serialization (server-side only)
```

---

### HIGH-002: Server Duplicate State Management

**Severity:** High
**Component:** Server
**Location:** `server/src/gameplay/GameSession.hpp:62-82`

**Description:**
```cpp
// These DUPLICATE what's already in ECS components!
std::unordered_map<std::uint32_t, std::uint8_t> playerLives_;
std::unordered_map<std::uint32_t, std::int32_t> playerScores_;
std::unordered_map<std::uint32_t, std::string> playerNames_;
std::unordered_map<std::uint32_t, std::uint8_t> playerInputBits_;
```

**Impact:**
- State divergence when maps and ECS components desync
- Double memory usage
- Maintenance burden

**Fix Required:**
- Use ECS as single source of truth
- Query components directly instead of maintaining parallel maps

---

### HIGH-003: Client Blocking Network Calls

**Severity:** High
**Component:** Client
**Location:** `client/src/ui/Screens.cpp:219, 249`

**Description:**
```cpp
bool ok = waitHelloAck(1.0);  // Blocks renderer for 1 second
// No progress indicator, window appears frozen
```

**Impact:**
- Poor user experience
- Window marked as "not responding" by OS
- No way to cancel connection attempt

**Fix Required:**
- Use async connection with progress callback
- Or run connection in background thread with UI feedback

---

### HIGH-004: Client Unbounded Entity Maps

**Severity:** High
**Component:** Client
**Location:** `client/include/client/ui/Screens.hpp:225-227`

**Description:**
```cpp
std::unordered_map<unsigned, PackedEntity> _entityById;
std::unordered_map<unsigned, int> _missedById;
std::unordered_map<unsigned, double> _lastSeenAt;
// No maximum entity count!
```

**Impact:**
- Malicious server can OOM client
- Memory grows unbounded

**Fix Required:**
- Add maximum entity limit (e.g., 1000)
- Reject state packets exceeding limit

---

### HIGH-005: No TCP/UDP Keepalive

**Severity:** High
**Component:** Server, Client
**Location:** `server/src/gameplay/GameSession.cpp:321-332`

**Description:**
```cpp
const auto timeout = seconds(10);  // Hardcoded, no keepalive
for (auto& [key, last] : lastSeen_) {
    if (now - last > timeout) toRemove.push_back(key);
}
```

No ping/pong heartbeat — connections silently die.

**Impact:**
- Players removed without warning after 10s of packet loss
- No distinction between network hiccup and disconnect
- Lobby connection drops

**Fix Required:**
- Implement Ping/Pong messages (protocol already has them)
- Use shorter timeout with keepalive

---

### HIGH-006: Missing Multi-Threading Support

**Severity:** High
**Component:** Server, Engine
**Location:** Multiple

**Description:**
The codebase uses multiple threads but lacks:
- Thread-safe data structures
- Synchronization primitives (mutexes, atomics)
- Clear thread ownership model
- Documentation of thread boundaries

**Current Threading Model:**
```
Server:
  - Main thread: ASIO io_context.run()
  - Game thread: gameLoop() running at 60Hz
  - Both access shared state unsafely

Client:
  - Single thread (raylib main loop)
  - Blocking network calls freeze UI
```

**Required Infrastructure:**
```cpp
// Option A: Proper synchronization
class ThreadSafeRegistry {
    std::mutex mutex_;
    rt::ecs::Registry reg_;
public:
    template<typename F>
    auto withLock(F&& f) {
        std::lock_guard lock(mutex_);
        return f(reg_);
    }
};

// Option B: Single-threaded with async I/O
// Move game loop into ASIO strand
asio::strand<asio::io_context::executor_type> gameStrand_;

// Option C: Message queues
ThreadSafeQueue<GameEvent> eventQueue_;
// I/O thread pushes events, game thread consumes
```

---

## Medium Priority Issues

### MED-001: Engine Duplicate Component Systems

**Severity:** Medium
**Component:** Engine
**Location:** `engine/src/rt/components/` vs `engine/src/rt/game/`

**Description:**
Two parallel component hierarchies exist:

**Set A (rt/components/):** Position, Velocity, Size, Controller, Player, Enemy
**Set B (rt/game/):** Transform, Velocity, NetType, PlayerInput, Shooter, etc.

```cpp
// Duplicate definitions!
// rt/components/Position.hpp
struct Position { float x = 0.f; float y = 0.f; };

// rt/game/Components.hpp
struct Transform { float x = 0.f; float y = 0.f; };
```

**Impact:**
- Code duplication
- Confusion about which to use
- Maintenance burden

---

### MED-002: Engine Systems Store External State

**Severity:** Medium
**Component:** Engine
**Location:** `engine/src/game_compat_Systems.cpp:39`, `engine/src/rt/game/Systems.hpp:31`

**Description:**
```cpp
// FormationSystem stores raw pointer to external state
class FormationSystem : public rt::ecs::System {
    float* t_;  // External state dependency!
};

// EnemyShootingSystem holds RNG reference
class EnemyShootingSystem : public rt::ecs::System {
    std::mt19937& rng_;  // Makes replays non-deterministic
};
```

**Impact:**
- Hidden dependencies
- Harder to test
- Non-deterministic behavior

---

### MED-003: Engine ComponentStorage Cache Unfriendly

**Severity:** Medium
**Component:** Engine
**Location:** `engine/src/rt/ecs/Storage.hpp`

**Description:**
```cpp
template <typename C>
class ComponentStorage : public IStorage {
    std::unordered_map<Entity, C> data_;  // Scattered memory!
};
```

**Impact:**
- Poor cache locality during iteration
- Performance degradation with many entities

**Better Approach:**
```cpp
// Contiguous storage with index mapping
std::vector<C> components_;
std::unordered_map<Entity, size_t> entityToIndex_;
```

---

### MED-004: Server Silent Error Handling

**Severity:** Medium
**Component:** Server
**Location:** Multiple locations

**Description:**
```cpp
// TcpServer.cpp - empty catch
try { ... } catch (...) {}

// main.cpp:37 - silent IP resolution failure
catch (...) {}

// Malformed packets silently ignored (GameSession.cpp:86-88)
if (bytes < sizeof(Header)) return;
```

**Impact:**
- Debugging difficulties
- Silent failures go unnoticed
- No error reporting to clients

---

### MED-005: Client Double-Free Risk

**Severity:** Medium
**Component:** Client
**Location:** `client/src/ui/Screens.cpp:125-143`

**Description:**
```cpp
// Destructor
~Screens() {
    if (_playerSheet.id != 0) UnloadTexture(_playerSheet);
    if (_enemySheet.id != 0) UnloadTexture(_enemySheet);
}

// unloadGraphics() - ALSO unloads textures
void unloadGraphics() {
    if (_playerSheet.id != 0) { UnloadTexture(_playerSheet); _playerSheet = {}; }
    // ...
}
```

If `unloadGraphics()` is skipped, destructor may double-free.

---

### MED-006: Client Singleplayer Hybrid ECS

**Severity:** Medium
**Component:** Client
**Location:** `client/src/ui/screens/Singleplayer.cpp`

**Description:**
Singleplayer uses ECS for entities but raw vectors for projectiles:

```cpp
// ECS entities
_spWorld->emplace<rt::components::Position>(player, {100.f, 100.f});

// But bullets are raw vectors (NOT ECS)
std::vector<SpBullet> _spBullets;      // line 125
std::vector<SpBullet> _spEnemyBullets; // line 127
std::vector<SpPowerup> _spPowerups;    // line 155
```

Manual collision detection duplicates engine's CollisionSystem.

**Impact:**
- Inconsistent architecture
- Duplicate collision logic
- Potential desync between ECS and parallel vectors

---

### MED-007: Server Entity Despawn Detection Bug

**Severity:** Medium
**Component:** Server
**Location:** `server/src/gameplay/GameSession.cpp:307-311`

**Description:**
```cpp
for (std::uint32_t id : lastKnownEntityIds_) {
    if (currentEntityIds.find(id) == currentEntityIds.end() &&
        playerIds.find(id) == playerIds.end()) {
        broadcastDespawn(id);
    }
}
```

Only entities with `NetType` component are tracked. Formations without `NetType` are deleted but despawn never broadcast.

**Impact:**
- Clients show orphaned formation followers
- Visual desync

---

## Low Priority Issues

### LOW-001: Magic Numbers Throughout Codebase

**Severity:** Low
**Component:** All
**Location:** Multiple

**Examples:**
```cpp
// Server
const double tickRate = 60.0;           // GameSession.cpp
const auto timeout = seconds(10);       // GameSession.cpp

// Engine
constexpr float kWorldH = 600.f;        // Systems.cpp
constexpr float kTopMargin = 56.f;      // Systems.cpp
constexpr float kEnemyH = 18.f;         // Systems.cpp

// Client
float ew = 27.0f, eh = 18.0f;           // Singleplayer.cpp (AABB)
double overheatDrain = 0.30;            // Singleplayer.cpp
int bossHealth = 50;                    // Singleplayer.cpp
```

**Fix:** Create configuration structures or constants files.

---

### LOW-002: Duplicate System Implementations

**Severity:** Low
**Component:** Engine
**Location:** `engine/src/Systems.cpp` vs `engine/src/game_compat_Systems.cpp`

**Description:**
InputSystem appears in both files with slightly different implementations.

---

### LOW-003: Inconsistent Logging

**Severity:** Low
**Component:** Server
**Location:** Multiple

**Description:**
```cpp
std::cout << "[Server] ..." << std::endl;  // Sometimes cout
std::cerr << "[Server] ..." << std::endl;  // Sometimes cerr
// No log levels, no timestamps, no structured logging
```

---

### LOW-004: Client String Truncation in Roster

**Severity:** Low
**Component:** Client
**Location:** `client/src/net/NetPackets.cpp:99`

**Description:**
```cpp
std::string unameTrunc = _username.substr(0, 15);
```

Server truncates to 15 chars; if client username is shorter, comparison may fail.

---

### LOW-005: No Input Validation on Port

**Severity:** Low
**Component:** Client
**Location:** `client/src/ui/screens/Multiplayer.cpp:27`

**Description:**
```cpp
int port = std::stoi(portStr);  // No bounds check, can throw
```

---

## Engine Analysis

### ECS Compliance Scorecard

| Aspect | Score | Notes |
|--------|-------|-------|
| Core ECS Design | 9/10 | Registry/Storage/System excellent |
| Component Purity | 8/10 | Game components have network coupling |
| System Separation | 7/10 | Duplicate systems, external state |
| Entity Decoupling | 9/10 | No cross-entity references |
| Network Decoupling | 4/10 | Game layer tightly coupled to protocol |
| Code Quality | 7/10 | Magic numbers, duplicate code |
| Performance | 7/10 | Hash map storage acceptable for scale |
| Testability | 6/10 | RNG/state dependencies |

**Overall: 7.1/10**

### ECS Violations Found

1. **FormationSystem** stores external state pointer (game_compat_Systems.cpp:39)
2. **EnemyShootingSystem** holds RNG reference (Systems.hpp:31)
3. **BossSpawnSystem** has mutable state (bossesSpawned_, bossActive_)
4. **Systems** use EntityType enum instead of tag components
5. **Hardcoded constants** in system implementations

### Decoupling Assessment

**Core ECS (rt/ecs/):** Fully decoupled, can be used standalone
**Game Layer (rt/game/):** Tightly coupled to Protocol.hpp

---

## Server Analysis

### Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                    NetworkManager                        │
│  ┌─────────────┐              ┌─────────────┐           │
│  │  TcpServer  │              │  UdpServer  │           │
│  │  (port+1)   │              │  (port)     │           │
│  └──────┬──────┘              └──────┬──────┘           │
│         │                            │                   │
│         └────────────┬───────────────┘                   │
│                      │                                   │
│              ┌───────▼───────┐                          │
│              │  GameSession  │  ← RACE CONDITIONS HERE  │
│              │  (60Hz loop)  │                          │
│              └───────┬───────┘                          │
│                      │                                   │
│              ┌───────▼───────┐                          │
│              │ ECS Registry  │                          │
│              └───────────────┘                          │
└─────────────────────────────────────────────────────────┘
```

### Thread Safety Summary

| Resource | Thread 1 (ASIO) | Thread 2 (Game) | Protected? |
|----------|-----------------|-----------------|------------|
| endpointToPlayerId_ | Write | Read | NO |
| playerInputBits_ | Write | Read | NO |
| playerLives_ | Read | Write | NO |
| playerScores_ | Write | Write | NO |
| lastSeen_ | Write | Read | NO |
| ECS Registry | Write | Read/Write | NO |
| clients_ (TCP) | Write | - | NO |

---

## Client Analysis

### Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                         App                              │
│                    (Main Loop)                           │
│                         │                                │
│              ┌──────────▼──────────┐                    │
│              │      Screens        │                    │
│              │  (State Machine)    │                    │
│              └──────────┬──────────┘                    │
│                         │                                │
│    ┌────────┬───────────┼───────────┬────────┐         │
│    ▼        ▼           ▼           ▼        ▼         │
│  Menu  Multiplayer  Waiting   Gameplay  GameOver       │
│                         │           │                   │
│                    ┌────▼───┐  ┌────▼───┐              │
│                    │  Net   │  │  ECS   │              │
│                    │(UDP/TCP)│  │(Single │              │
│                    └────────┘  │ player)│              │
│                                └────────┘              │
└─────────────────────────────────────────────────────────┘
```

### ECS Usage by Mode

| Mode | Uses ECS? | Notes |
|------|-----------|-------|
| Menu | No | Pure UI |
| Multiplayer Lobby | No | Network state only |
| Multiplayer Gameplay | No | Raw entity maps |
| Singleplayer | Partial | ECS + parallel vectors |

---

## Known Issues Root Causes

### Issue: Lack of Sprites

**Root Cause:** Hardcoded path search fails silently

**Location:** `client/src/ui/Screens.cpp:69-79`

```cpp
std::string findSpritePath(const std::string& name) {
    for (auto& dir : {"sprites/", "client/sprites/", "../sprites/", "../client/sprites/"}) {
        std::string p = std::string(dir) + name;
        if (FileExists(p.c_str())) return p;
    }
    return "";  // Silent failure
}
```

**Fix:**
- Log attempted paths on failure
- Provide user feedback about where to place sprites
- Consider embedded fallback sprites

---

### Issue: Constant Lag in Multiplayer

**Root Causes:**

1. **Thread race conditions** (CRIT-001) — data corruption
2. **Tick/broadcast desync** (CRIT-004) — state from mid-tick
3. **20Hz state rate** vs 60Hz game rate — 50ms between updates
4. **No client-side prediction** — pure server authority
5. **Input throttled to 30Hz** (Gameplay.cpp:72)

**Fix:**
- Synchronize state broadcast with game ticks
- Increase state rate or add client prediction
- Fix thread safety issues

---

### Issue: Connection Drops in Lobby

**Root Causes:**

1. **TCP cleanup race** (CRIT-002) — crashes during disconnect
2. **No keepalive** (HIGH-005) — silent timeout after 10s
3. **Thread races** (CRIT-001) — endpoint maps corrupted
4. **Blocking connect** (HIGH-003) — timeout perceived as failure

**Fix:**
- Add mutex to TCP client set operations
- Implement Ping/Pong keepalive
- Use async connection with feedback

---

## Recommended Fix Order

### Phase 1: Critical Stability (Week 1)

1. **Add mutex to GameSession** — wrap all shared map access
2. **Fix TCP client cleanup race** — synchronize clients_ access
3. **Remove Net.cpp dead code** — consolidate networking
4. **Add atomic flags** — `running_`, `gameStarted_`

### Phase 2: Network Reliability (Week 2)

5. **Synchronize state broadcast** — send on tick boundary
6. **Implement Ping/Pong** — detect dead connections
7. **Add connection timeout feedback** — show progress to user
8. **Increase state rate** — 30Hz minimum

### Phase 3: Architecture Cleanup (Week 3)

9. **Decouple engine from Protocol.hpp** — use tag components
10. **Remove server duplicate state** — use ECS as source of truth
11. **Consolidate component systems** — merge rt/components and rt/game
12. **Add entity limits** — prevent OOM attacks

### Phase 4: Quality Improvements (Week 4)

13. **Add proper error logging** — structured, leveled
14. **Extract magic numbers** — configuration files
15. **Client ECS for multiplayer** — consistent architecture
16. **Add unit tests** — especially for protocol parsing
