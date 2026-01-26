#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <raylib.h>
#include <utility>
#include <random>
#include <asio.hpp>

// ECS Engine (standalone) headers for local singleplayer test
#include "rt/ecs/Registry.hpp"
#include "rt/components/Position.hpp"
#include "rt/components/Velocity.hpp"
#include "rt/components/Controller.hpp"
#include "rt/components/Player.hpp"
#include "rt/components/Size.hpp"
#include "rt/components/Collided.hpp"
#include "rt/components/AiController.hpp"
#include "rt/components/Enemy.hpp"
#include "rt/systems/PlayerControlSystem.hpp"
#include "rt/systems/MovementSystem.hpp"
#include "rt/systems/AiControlSystem.hpp"
#include "rt/systems/CollisionSystem.hpp"


namespace client {
namespace ui {

enum class ScreenState {
    Menu,
    Singleplayer,
    Multiplayer,
    Waiting,
    Gameplay,
    GameOver,
    Options,
    Leaderboard,
    NotEnoughPlayers,
    Exiting
};

struct MultiplayerForm {
    std::string username;
    std::string serverAddress;
    std::string serverPort;
};

struct SingleplayerForm {
    std::string username;
};

class Screens {
public:
    void drawMenu(ScreenState& screen);
    void drawSingleplayer(ScreenState& screen, SingleplayerForm& form);
    void drawMultiplayer(ScreenState& screen, MultiplayerForm& form);
    // Programmatic connect to multiplayer using the provided form
    bool autoConnect(ScreenState& screen, MultiplayerForm& form);
    void drawWaiting(ScreenState& screen);
    void drawGameplay(ScreenState& screen);
    void drawGameOver(ScreenState& screen);
    void drawOptions();
    void drawLeaderboard();
    void drawNotEnoughPlayers(ScreenState& screen);
    static void logMessage(const std::string& msg, const char* level = "INFO");
    // Gracefully leave any active multiplayer session (sends Disconnect, closes socket)
    void leaveSession();
    // Release GPU textures (must be called before closing the window)
    void unloadGraphics();
    ~Screens();
    // Draw global background if available (fills the window keeping aspect ratio)
    void drawBackground(float dt);
    // Attempt to load background once (safe to call multiple times)
    void loadBackground();
    // Whether a background texture is currently loaded
    bool hasBackground() const { return _backgroundLoaded; }
    // Allow changing scroll speed (pixels per second in scaled space)
    void setBackgroundSpeed(float pxPerSec) { _bgSpeed = pxPerSec; }
private:
    // --- Local Singleplayer test (engine sandbox) ---
    void initSingleplayerWorld();
    void shutdownSingleplayerWorld();
    void updateSingleplayerWorld(float dt);
    void drawSingleplayerWorld();
    // Power-ups helpers (bonus logic isolated)
    void spHandleScoreThresholdSpawns(int screenW);
    void spUpdatePowerups(float dt);
    void spDrawPowerups();
    // Formation wave spawners (singleplayer sandbox)
    void spSpawnLine(int count, float y);
    void spSpawnSnake(int count, float y, float amplitude, float frequency, float spacing);
    void spSpawnTriangle(int rows, float y, float spacing);
    void spSpawnDiamond(int rows, float y, float spacing);
    void spScheduleNextSpawn();
    // Boss helper
    void spSpawnBoss();
    bool _singleplayerActive = false;
    bool _spPaused = false;
    std::unique_ptr<rt::ecs::Registry> _spWorld;
    rt::ecs::Entity _spPlayer = 0;
    // Singleplayer enemies and waves
    enum class SpFormationKind { Line, Snake, Triangle, Diamond };
    struct SpEnemy {
        rt::ecs::Entity id{0};
        SpFormationKind kind{SpFormationKind::Line};
        int index{0};            // index within formation
        float baseY{0.f};        // initial baseline Y
        float spacing{36.f};     // horizontal/vertical spacing
        float amplitude{60.f};   // for Snake vertical amplitude
        float frequency{2.0f};   // for Snake frequency
        float spawnTime{0.f};    // time when spawned (for animation phase)
        float localX{0.f};       // initial local X within formation
        float localY{0.f};       // initial local Y within formation
        // Shooting capability (subset of enemies get this enabled)
        bool shooter{false};
        float shootCooldown{0.f};
        float shootInterval{1.2f};
        float bulletSpeed{220.f};
        float accuracy{0.62f};   // 0.5..0.8 (higher = better aim)
    };
    std::vector<SpEnemy> _spEnemies;
    struct SpBullet { float x; float y; float vx; float vy; float w; float h; };
    std::vector<SpBullet> _spBullets;
    // Enemy bullets (orange) fired by shooters
    std::vector<SpBullet> _spEnemyBullets;
    float _spElapsed = 0.f;
    float _spSpawnTimer = 0.f;
    int _spNextFormation = 0;
    // Random spawn control
    std::mt19937 _spRng{};
    float _spNextSpawnDelay = 2.0f; // seconds until next spawn
    float _spMinSpawnDelay = 1.8f;
    float _spMaxSpawnDelay = 3.6f;
    std::size_t _spEnemyCap = 40; // maximum active enemies in sandbox
    // Shooting config
    float _spShootCooldown = 0.f;
    float _spShootInterval = 0.18f;
    float _spBulletSpeed = 420.f;
    float _spBulletW = 8.f;
    float _spBulletH = 3.f;
    // Enemy shooter tuning (percentage of enemies that shoot)
    int _spShooterPercent = 15; // 0..100
    // Player hit cooldown (i-frames)
    float _spHitIframes = 1.f;      // seconds left of invincibility
    float _spHitIframesDuration = 1.0f;
    // Overheat mechanic (center bar): drains while holding fire, locks shooting at 0, recovers when not firing
    float _spHeat = 1.0f;           // 0..1
    float _spHeatDrainPerSec = 0.30f;
    float _spHeatRegenPerSec = 0.15f;
    // Singleplayer power-ups (Life / Invincibility / ClearBoard / InfiniteFire)
    enum class SpPowerupType { Life = 0, Invincibility = 1, ClearBoard = 2, InfiniteFire = 3 };
    struct SpPowerup { float x; float y; float vx; float radius; SpPowerupType type; };
    std::vector<SpPowerup> _spPowerups;
    int _spNextPowerupScore = 1500;        // next score threshold for spawning a power-up
    int _spPowerupMinPts = 1500;           // min interval in points between spawns
    int _spPowerupMaxPts = 2000;           // max interval in points between spawns
    float _spPowerupSpeed = 90.f;          // pixels per second to the left
    float _spPowerupRadius = 9.f;          // visual/collision radius
    // Invincibility shield state
    float _spInvincibleTimer = 0.f;        // seconds remaining of invincibility (shield)
    float _spInvincibleDuration = 10.0f;   // seconds of invincibility on pickup
    float _spShieldRadius = 20.0f;         // visual radius of the shield around player
    // Infinite fire state
    float _spInfiniteFireTimer = 0.f;      // seconds remaining of infinite fire
    float _spInfiniteFireDuration = 10.0f; // seconds of infinite fire on pickup
    // We keep systems owned by the world; stored here for clarity
    bool _spInitialized = false;

    // Boss state (spawns once when reaching a score threshold, moves in from right then holds at right side)
    bool _spBossActive = false;
    bool _spBossSpawned = false;
    // Next score threshold at which to spawn a boss (recurs every 15000 points)
    int _spBossThreshold = 15000;
    rt::ecs::Entity _spBossId = 0;
    float _spBossW = 160.f;            // larger boss size
    float _spBossH = 120.f;
    float _spBossStopX = 0.f;          // x at which boss stops moving left
    float _spBossRightMargin = 20.f;   // margin from right edge when stopped
    // Boss combat state
    int _spBossHpMax = 50;
    int _spBossHp = 0;
    // Boss vertical movement state
    bool _spBossAtStop = false;        // has reached its stop X position
    bool _spBossDirDown = true;        // vertical patrol direction
    float _spBossSpeedY = 100.f;       // effective speed is governed by Ai bits; value used for tuning if needed
    // Boss shooting state (singleplayer)
    float _spBossShootCooldown = 0.f;  // time until next volley
    float _spBossShootInterval = 1.1f; // seconds between volleys
    float _spBossBulletSpeed = 280.f;  // bullet speed in px/s
    int   _spBossBurstCount = 5;       // number of bullets per volley (fan)
    float _spBossSpread = 0.30f;       // half-angle spread in radians (~17deg)

    // Check if required sprite assets are available on disk
    bool assetsAvailable() const;
    // Parse a single UDP datagram payload according to our protocol and update local state
    void handleNetPacket(const char* data, std::size_t n);
    int _focusedField = 0;
    std::string _statusMessage;
    // network state for gameplay
    bool _connected = false;
    std::string _username;
    std::string _serverAddr;
    std::string _serverPort;
    // TCP connection (handshake)
    std::unique_ptr<asio::io_context> _tcpIo;
    std::unique_ptr<asio::ip::tcp::socket> _tcpSocket;
    std::uint16_t _udpPort = 0;  // received HelloAck
    // TCP handshake methods
    bool connectTcp();
    void disconnectTcp();
    // UDP client method for gameplay
    void ensureNetSetup();
    void teardownNet();
    void sendDisconnect();
    void sendInput(std::uint8_t bits);
    void sendLobbyConfig(std::uint8_t difficulty, std::uint8_t baseLives);
    void sendStartMatch();
    void pumpNetworkOnce();
    bool waitHelloAck(double timeoutSec);
    struct PackedEntity { unsigned id; unsigned char type; float x; float y; float vx; float vy; unsigned rgba; };
    std::vector<PackedEntity> _entities;
    // Entity reconciliation buffers: avoid dropping entities on transient packet loss or truncation
    std::unordered_map<unsigned, PackedEntity> _entityById; // id -> last known entity state
    std::unordered_map<unsigned, int> _missedById;          // id -> consecutive snapshots missed
    std::unordered_map<unsigned, double> _lastSeenAt;       // id -> last time seen in a snapshot (seconds)
    int _missThreshold = 3;                                 // safeguard: remove only if both miss-count and time exceed
    double _expireSecondsEnemy = 2.0;                       // enemies expire after not seen for this time
    double _expireSecondsDefault = 1.0;                     // players/bullets/powerups
    double _lastSend = 0.0;
    bool _serverReturnToMenu = false;
    // --- spritesheet handling ---
    void loadSprites();
    void loadEnemySprites();
    std::string findSpritePath(const char* name) const;
    Texture2D _sheet{};
    bool _sheetLoaded = false;
    int _sheetCols = 5; // spritesheet is 5x5 per user spec
    int _sheetRows = 5;
    float _frameW = 0.f;
    float _frameH = 0.f;
    // Enemy spritesheet (r-typesheet19.gif, 230x97)
    Texture2D _enemySheet{};
    bool _enemyLoaded = false;
    int _enemyCols = 7; // per user spec: 7 columns
    int _enemyRows = 3; // per user spec: 3 rows
    float _enemyFrameW = 0.f;
    float _enemyFrameH = 0.f;
    // Fixed sprite assignment per player id
    std::unordered_map<unsigned, int> _spriteRowById; // id -> row index
    int _nextSpriteRow = 0; // next row to assign on first sight

    // --- Gameplay HUD state (placeholders until server data is wired) ---
    int _playerLives = 4; // 0..10
    int _maxLives = 6;
    unsigned _selfId = 0;  // our player id (from roster)
    int _score = 0;
    int _level = 1;
        struct OtherPlayer { std::uint32_t id; std::string name; int lives; };
        std::vector<OtherPlayer> _otherPlayers; // show up to 3 then "+ x"
        std::uint32_t _localPlayerId = 0; // received from Roster
        bool _haveLocalId = false;
    bool _gameOver = false; // set when our lives reach 0
    // Lobby/match state
    std::uint32_t _hostId = 0;
    std::uint8_t _lobbyBaseLives = 4;   // 1..6
    std::uint8_t _lobbyDifficulty = 1;  // 0..2
    bool _lobbyStarted = false;

    // --- Client-side charge beam (Alt + Space) ---
    bool _isCharging = false;
    double _chargeStart = 0.0;
    bool _beamActive = false;
    double _beamEndTime = 0.0;
    float _beamX = 0.0f;        // origin X at release
    float _beamY = 0.0f;        // center Y at release
    float _beamThickness = 0.0f; // computed from charge duration

    // --- Shot mode toggle (Normal vs Charge), switched with Ctrl key ---
    enum class ShotMode { Normal = 0, Charge = 1 };
    ShotMode _shotMode = ShotMode::Normal;

    // --- Global background texture ---
    Texture2D _background{};
    bool _backgroundLoaded = false;
    float _bgScrollX = 0.0f; // accumulated scroll offset in pixels (scaled)
    float _bgSpeed = 60.0f;  // pixels per second, scrolling to the left
};

} // namespace ui
} // namespace client
