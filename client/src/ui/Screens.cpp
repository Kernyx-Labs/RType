#include "../../include/client/ui/Screens.hpp"
#include "../../include/client/ui/widgets/Button.hpp"
#include "../../include/client/ui/widgets/InputBox.hpp"
#include "../../include/client/ui/widgets/Title.hpp"
#include "common/Protocol.hpp"
#include <algorithm>
#include <asio.hpp>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <raylib.h>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
using namespace client::ui;

namespace {
static constexpr float kEnemyHitboxScale = 1.25f;
static constexpr float kEnemyAabbW = 27.0f;
static constexpr float kEnemyAabbH = 18.0f;
} // namespace

bool Screens::assetsAvailable() const {
  // Try to locate both player and enemy spritesheets
  std::string p1 = findSpritePath("r-typesheet42.gif");
  std::string p2 = findSpritePath("r-typesheet19.gif");
  return !p1.empty() && !p2.empty();
}

static int baseFontFromHeight(int h) {
  int baseFont = (int)(h * 0.045f);
  if (baseFont < 16)
    baseFont = 16;
  return baseFont;
}

// Logger utilitaire
void Screens::logMessage(const std::string &msg, const char *level) {
  if (level)
    std::cout << "[" << level << "] " << msg << std::endl;
  else
    std::cout << "[INFO] " << msg << std::endl;
}

// --- Spritesheet helpers ---
std::string Screens::findSpritePath(const char *name) const {
  std::vector<std::string> candidates;
  candidates.emplace_back(std::string("client/sprites/") + name);
  candidates.emplace_back(std::string("../client/sprites/") + name);
  candidates.emplace_back(std::string("../../client/sprites/") + name);
  candidates.emplace_back(std::string("../../../client/sprites/") + name);
  for (const auto &c : candidates) {
    if (FileExists(c.c_str()))
      return c;
  }
  return {};
}

void Screens::loadSprites() {
  if (_sheetLoaded)
    return;
  std::string path = findSpritePath("r-typesheet42.gif");
  if (path.empty()) {
    logMessage("Spritesheet r-typesheet42.gif not found.", "WARN");
    return;
  }
  _sheet = LoadTexture(path.c_str());
  if (_sheet.id == 0) {
    logMessage("Failed to load spritesheet texture.", "ERROR");
    return;
  }
  _sheetLoaded = true;
  _frameW = (float)_sheet.width / (float)_sheetCols;
  _frameH = (float)_sheet.height / (float)_sheetRows;
  logMessage("Spritesheet loaded: " + std::to_string(_sheet.width) + "x" +
                 std::to_string(_sheet.height) + ", frame " +
                 std::to_string((int)_frameW) + "x" +
                 std::to_string((int)_frameH),
             "INFO");
}

void Screens::loadEnemySprites() {
  if (_enemyLoaded)
    return;
  std::string path = findSpritePath("r-typesheet19.gif");
  if (path.empty()) {
    logMessage("Enemy spritesheet r-typesheet19.gif not found.", "WARN");
    return;
  }
  _enemySheet = LoadTexture(path.c_str());
  if (_enemySheet.id == 0) {
    logMessage("Failed to load enemy spritesheet texture.", "ERROR");
    return;
  }
  _enemyLoaded = true;
  // Per user spec: r-typesheet19.gif (230x97) is a 7x3 grid
  _enemyCols = 7;
  _enemyRows = 3;

  _enemyFrameW = (float)_enemySheet.width / (float)_enemyCols;
  _enemyFrameH = (float)_enemySheet.height / (float)_enemyRows;
  logMessage("Enemy sheet loaded: " + std::to_string(_enemySheet.width) + "x" +
                 std::to_string(_enemySheet.height) + ", grid " +
                 std::to_string(_enemyCols) + "x" + std::to_string(_enemyRows) +
                 ", frame " + std::to_string((int)_enemyFrameW) + "x" +
                 std::to_string((int)_enemyFrameH),
             "INFO");
}

Screens::~Screens() {
  // Destructor: textures should already be unloaded via unloadGraphics() before
  // window closes. As a safety net, try to unload if the window is still valid.
  if (IsWindowReady()) {
    if (_sheetLoaded) {
      UnloadTexture(_sheet);
      _sheetLoaded = false;
    }
    if (_enemyLoaded) {
      UnloadTexture(_enemySheet);
      _enemyLoaded = false;
    }
    unloadSoundEffects();
    unloadFonts();
  }
}

void Screens::unloadGraphics() {
  if (_sheetLoaded) {
    UnloadTexture(_sheet);
    _sheetLoaded = false;
  }
  if (_enemyLoaded) {
    UnloadTexture(_enemySheet);
    _enemyLoaded = false;
  }
  unloadSoundEffects();
  unloadFonts();
}

void Screens::loadSoundEffects() {
  if (_shootSoundLoaded)
    return;

  // Try to find the shooting sound effect
  std::vector<std::string> candidates;
  candidates.emplace_back("sound/Blaster-Shot.mp3");
  candidates.emplace_back("client/sound/Blaster-Shot.mp3");
  candidates.emplace_back("../client/sound/Blaster-Shot.mp3");
  candidates.emplace_back("../../client/sound/Blaster-Shot.mp3");

  for (const auto &path : candidates) {
    if (FileExists(path.c_str())) {
      _shootSound = LoadSound(path.c_str());
      if (_shootSound.frameCount > 0) {
        _shootSoundLoaded = true;
        SetSoundVolume(_shootSound, 0.4f); // 40% volume
        logMessage("Shoot sound effect loaded from: " + path);
        return;
      }
    }
  }
  logMessage("Warning: Shoot sound effect not found", "WARN");
}

void Screens::unloadSoundEffects() {
  if (_shootSoundLoaded) {
    UnloadSound(_shootSound);
    _shootSoundLoaded = false;
  }
}

void Screens::playShootSound() {
  if (_shootSoundLoaded) {
    PlaySound(_shootSound);
  }
}

// drawMenu is implemented in screens/Menu.cpp

// drawSingleplayer is implemented in screens/Singleplayer.cpp

// drawMultiplayer is implemented in screens/Multiplayer.cpp

// drawOptions is implemented in screens/Options.cpp

// drawLeaderboard is implemented in screens/Leaderboard.cpp

// drawNotEnoughPlayers is implemented in screens/NotEnoughPlayers.cpp

void Screens::handleNetPacket(const char *data, std::size_t n) {
  if (!data || n < sizeof(rtype::net::Header))
    return;
  const auto *h = reinterpret_cast<const rtype::net::Header *>(data);
  if (h->version != rtype::net::ProtocolVersion)
    return;
  if (h->type == rtype::net::MsgType::State) {
    const char *p = data + sizeof(rtype::net::Header);
    if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::StateHeader))
      return;
    auto *sh = reinterpret_cast<const rtype::net::StateHeader *>(p);
    p += sizeof(rtype::net::StateHeader);
    std::size_t count = sh->count;
    if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::StateHeader) +
                count * sizeof(rtype::net::PackedEntity))
      return;
    // Reconciliation: update or insert all received entities; mark as seen
    auto *arr = reinterpret_cast<const rtype::net::PackedEntity *>(p);
    std::unordered_set<unsigned> seenIds;
    seenIds.reserve(count);
    double nowSec = GetTime();
    for (std::size_t i = 0; i < count; ++i) {
      unsigned id = arr[i].id;

      // Safeguard: if new entity and we are at limit, ignore
      if (_entityById.find(id) == _entityById.end() &&
          _entityById.size() >= kMaxEntities) {
        continue;
      }

      PackedEntity e{};
      e.id = arr[i].id;
      e.type = static_cast<unsigned char>(arr[i].type);
      e.x = arr[i].x;
      e.y = arr[i].y;
      e.vx = arr[i].vx;
      e.vy = arr[i].vy;
      e.rgba = arr[i].rgba;
      _entityById[e.id] = e;
      _missedById[e.id] = 0;
      _lastSeenAt[e.id] = nowSec;
      seenIds.insert(e.id);
    }
    // Increment miss counters for any id not seen in this snapshot
    std::vector<unsigned> toErase;
    toErase.reserve(_entityById.size());
    for (const auto &kv : _entityById) {
      unsigned id = kv.first;
      if (seenIds.find(id) == seenIds.end()) {
        int missed = (_missedById.count(id) ? _missedById[id] : 0) + 1;
        _missedById[id] = missed;
        double lastSeen = (_lastSeenAt.count(id) ? _lastSeenAt[id] : nowSec);
        double elapsed = nowSec - lastSeen;
        unsigned char type = kv.second.type;
        double ttl = (type == 2 /* Enemy */) ? _expireSecondsEnemy
                                             : _expireSecondsDefault;
        if (missed >= _missThreshold && elapsed >= ttl)
          toErase.push_back(id);
      }
    }
    for (unsigned id : toErase) {
      _entityById.erase(id);
      _missedById.erase(id);
      _lastSeenAt.erase(id);
    }
    // Rebuild render list with a stable ordering: players, bullets, powerups,
    // enemies
    _entities.clear();
    _entities.reserve(_entityById.size());
    auto appendByType = [&](unsigned char type) {
      for (const auto &kv : _entityById) {
        if (kv.second.type == type)
          _entities.push_back(kv.second);
      }
    };
    appendByType(1); // Player
    appendByType(3); // Bullet
    appendByType(4); // Powerup (if used)
    appendByType(2); // Enemy
  } else if (h->type == rtype::net::MsgType::Despawn) {
    // Server explicitly told us to remove an entity - do it immediately
    const char *p = data + sizeof(rtype::net::Header);
    if (n < sizeof(rtype::net::Header) + sizeof(std::uint32_t))
      return;
    std::uint32_t entityId;
    std::memcpy(&entityId, p, sizeof(entityId));
    _entityById.erase(entityId);
    _missedById.erase(entityId);
    _lastSeenAt.erase(entityId);
    // Rebuild render list immediately
    _entities.clear();
    _entities.reserve(_entityById.size());
    auto appendByType = [&](unsigned char type) {
      for (const auto &kv : _entityById) {
        if (kv.second.type == type)
          _entities.push_back(kv.second);
      }
    };
    appendByType(1); // Player
    appendByType(3); // Bullet
    appendByType(4); // Powerup
    appendByType(2); // Enemy
  } else if (h->type == rtype::net::MsgType::Roster) {
    const char *p = data + sizeof(rtype::net::Header);
    if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::RosterHeader))
      return;
    auto *rh = reinterpret_cast<const rtype::net::RosterHeader *>(p);
    p += sizeof(rtype::net::RosterHeader);
    std::size_t count = rh->count;
    if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::RosterHeader) +
                count * sizeof(rtype::net::PlayerEntry))
      return;
    _otherPlayers.clear();
    // Determine truncated username as stored by server (15 chars max)
    std::string unameTrunc = _username.substr(0, 15);
    for (std::size_t i = 0; i < count; ++i) {
      auto *pe = reinterpret_cast<const rtype::net::PlayerEntry *>(
          p + i * sizeof(rtype::net::PlayerEntry));
      std::string name(pe->name,
                       pe->name + strnlen(pe->name, sizeof(pe->name)));
      int lives = std::clamp<int>(pe->lives, 0, 10);
      _spriteRowById[pe->id] = pe->shipId;
      if (name == unameTrunc) {
        _playerLives = lives;
        _selfId = pe->id;
        continue; // don't include self in top bar list
      }
      _otherPlayers.push_back({pe->id, name, lives});
    }
    // Keep at most 3 teammates in top bar
    if (_otherPlayers.size() > 3)
      _otherPlayers.resize(3);
  } else if (h->type == rtype::net::MsgType::LivesUpdate) {
    const char *p = data + sizeof(rtype::net::Header);
    if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::LivesUpdatePayload))
      return;
    auto *lu = reinterpret_cast<const rtype::net::LivesUpdatePayload *>(p);
    unsigned id = lu->id;
    int lives = std::clamp<int>(lu->lives, 0, 10);
    if (id == _selfId) {
      _playerLives = lives;
      _gameOver = (_playerLives <= 0);
    } else {
      for (auto &op : _otherPlayers) {
        if (op.id == id) {
          op.lives = lives;
          break;
        }
      }
    }
  } else if (h->type == rtype::net::MsgType::ScoreUpdate) {
    const char *p = data + sizeof(rtype::net::Header);
    if (n < sizeof(rtype::net::Header) + sizeof(rtype::net::ScoreUpdatePayload))
      return;
    auto *su = reinterpret_cast<const rtype::net::ScoreUpdatePayload *>(p);
    // Team-wide score
    _score = su->score;
  } else if (h->type == rtype::net::MsgType::ReturnToMenu) {
    _serverReturnToMenu = true;
  } else if (h->type == rtype::net::MsgType::Ping) {
    sendPong();
  } else {
    // ignore unknown types
  }
}

// drawWaiting is implemented in screens/Waiting.cpp

void Screens::drawGameplay(ScreenState &screen) {
  // Do not run gameplay if assets are missing; show message and bounce back to
  // menu on ESC
  if (!assetsAvailable()) {
    int h = GetScreenHeight();
    int baseFont = baseFontFromHeight(h);
    Font font = getCurrentFont();
    titleCentered("Spritesheets not found.", (int)(h * 0.40f), (int)(h * 0.08f),
                  RED, font);
    titleCentered(
        "Place the sprites/ folder next to the executable then press ESC.",
        (int)(h * 0.52f), baseFont, RAYWHITE, font);
    if (IsKeyPressed(KEY_ESCAPE)) {
      leaveSession();
      screen = ScreenState::Menu;
    }
    return;
  }
  if (!_connected) {
    Font font = getCurrentFont();
    titleCentered("Not connected. Press ESC.", GetScreenHeight() * 0.5f, 24,
                  RAYWHITE, font);
    return;
  }
  ensureNetSetup();

  pumpNetworkOnce();

  if (_serverReturnToMenu) {
    // Server asked us to return: cleanly leave and show info screen
    leaveSession();
    screen = ScreenState::NotEnoughPlayers;
    return;
  }

  pumpNetworkOnce();

  if (_serverReturnToMenu) {
    // Server asked us to return: cleanly leave and show info screen
    leaveSession();
    screen = ScreenState::NotEnoughPlayers;
    return;
  }

  // Lazy-load spritesheet
  if (!_sheetLoaded)
    loadSprites();
  if (!_enemyLoaded)
    loadEnemySprites();

  // Update world snapshot first so we can gate inputs using current position
  pumpNetworkOnce();

  // Gate player inputs so the player cannot leave the playable area (between
  // top and bottom bars)
  int gw = GetScreenWidth();
  int gh = GetScreenHeight();
  int ghudFont = (int)(gh * 0.045f);
  if (ghudFont < 16)
    ghudFont = 16;
  int gmargin = 16;
  int gTopReserved = gmargin + ghudFont + gmargin;
  int gBottomBarH = std::max((int)(gh * 0.10f), ghudFont + gmargin);
  int gPlayableMinY = gTopReserved;
  int gPlayableMaxY = gh - gBottomBarH;
  if (gPlayableMaxY < gPlayableMinY + 1)
    gPlayableMaxY = gPlayableMinY + 1;
  // Find self entity
  const PackedEntity *self = nullptr;
  for (const auto &ent : _entities) {
    if (ent.type == 1 && ent.id == _selfId) {
      self = &ent;
      break;
    }
  }
  // Build input bits with edge gating (disabled when dead)
  std::uint8_t bits = 0;
  bool isAlive = (_playerLives > 0) && !_gameOver;
  bool wantLeft = isAlive && IsKeyDown(KEY_LEFT);
  bool wantRight = isAlive && IsKeyDown(KEY_RIGHT);
  bool wantUp = isAlive && IsKeyDown(KEY_UP);
  bool wantDown = isAlive && IsKeyDown(KEY_DOWN);
  bool wantShoot = isAlive && IsKeyDown(KEY_SPACE);
  if (self) {
    // Estimate drawn size for the player sprite
    const float playerScale = 1.18f;
    float drawW =
        (_sheetLoaded && _frameW > 0) ? (_frameW * playerScale) : 24.0f;
    float drawH =
        (_sheetLoaded && _frameH > 0) ? (_frameH * playerScale) : 14.0f;
    const float xOffset = -6.0f; // applied at draw time
    float dstX = self->x + xOffset;
    float dstY = self->y;
    float minX = 0.0f;
    float maxX = (float)gw - drawW;
    float minY = (float)gPlayableMinY;
    float maxY = (float)gPlayableMaxY - drawH;
    if (wantLeft && dstX > minX)
      bits |= rtype::net::InputLeft;
    if (wantRight && dstX < maxX)
      bits |= rtype::net::InputRight;
    if (wantUp && dstY > minY)
      bits |= rtype::net::InputUp;
    if (wantDown && dstY < maxY)
      bits |= rtype::net::InputDown;
  } else {
    // Fallback: no gating if we don't know self position yet
    if (wantLeft)
      bits |= rtype::net::InputLeft;
    if (wantRight)
      bits |= rtype::net::InputRight;
    if (wantUp)
      bits |= rtype::net::InputUp;
    if (wantDown)
      bits |= rtype::net::InputDown;
  }
  // Toggle shot mode on Ctrl press (once)
  if (isAlive &&
      (IsKeyPressed(KEY_LEFT_CONTROL) || IsKeyPressed(KEY_RIGHT_CONTROL))) {
    _shotMode =
        (_shotMode == ShotMode::Normal) ? ShotMode::Charge : ShotMode::Normal;
  }
  // Charge beam handling: hold Space when in Charge mode (Alt no longer
  // required)
  bool altDown = false;
  bool chargeHeld =
      isAlive && (_shotMode == ShotMode::Charge) && IsKeyDown(KEY_SPACE);
  if (chargeHeld) {
    if (!_isCharging) {
      _isCharging = true;
      _chargeStart = GetTime();
    }
  } else {
    if (isAlive && _isCharging && IsKeyReleased(KEY_SPACE)) {
      // Fire beam
      double chargeDur = std::min(2.0, std::max(0.1, GetTime() - _chargeStart));
      _beamActive = true;
      _beamEndTime = GetTime() + 0.25; // beam visible for 250ms
      // Beam origin at player position
      float px = self ? self->x : 0.0f;
      float py = self ? self->y : (float)((gPlayableMinY + gPlayableMaxY) / 2);
      _beamX = px;
      _beamY = py;
      // Thickness scales with charge duration
      _beamThickness = (float)(8.0 + chargeDur * 22.0); // 8..52 px
    }
    _isCharging = false;
  }
  // Normal shoot bit only when not charging
  if (isAlive && wantShoot && _shotMode == ShotMode::Normal)
    bits |= rtype::net::InputShoot;
  // Send charge bit to engine so server handles charge logic when shot mode is
  // Charge
  if (isAlive && chargeHeld)
    bits |= rtype::net::InputCharge;

  double now = GetTime();
  if (now - _lastSend > 1.0 / 30.0) {
    sendInput(bits);
    _lastSend = now;
  }

  // --- HUD (Top other players + Bottom bar for lives/score/level) ---
  int w = GetScreenWidth();
  int h = GetScreenHeight();
  int hudFont = (int)(h * 0.045f);
  if (hudFont < 16)
    hudFont = 16;
  int margin = 16;
  Font font = getCurrentFont();

  // Top area: show up to 3 teammates (exclude self), capping lives icons to 3
  int topY = margin;
  int xCursor = margin;
  int shown = 0;
  for (size_t i = 0; i < _otherPlayers.size() && shown < 3; ++i, ++shown) {
    const auto &op = _otherPlayers[i];
    // Name
    DrawTextEx(font, op.name.c_str(), {(float)xCursor, (float)topY}, (float)hudFont, 1.0f, RAYWHITE);
    Vector2 textSize = MeasureTextEx(font, op.name.c_str(), (float)hudFont, 1.0f);
    int nameW = (int)textSize.x;
    xCursor += nameW + 12;
    // Lives as small red squares (cap to 3)
    int iconSize = std::max(6, hudFont / 2 - 2);
    int iconGap = std::max(3, iconSize / 3);
    int livesToDraw = std::min(3, std::max(0, op.lives));
    int iconsW = livesToDraw * iconSize +
                 (livesToDraw > 0 ? (livesToDraw - 1) * iconGap : 0);
    int iconY = topY + (hudFont - iconSize) / 2;
    for (int k = 0; k < livesToDraw; ++k) {
      int ix = xCursor + k * (iconSize + iconGap);
      DrawRectangle(ix, iconY, iconSize, iconSize, (Color){220, 80, 80, 255});
    }
    xCursor += iconsW + 24; // spacing to next player
  }
  // If more than 3 teammates, show overflow counter
  if (_otherPlayers.size() > 3) {
    std::string more = "x " + std::to_string(_otherPlayers.size() - 3);
    DrawTextEx(font, more.c_str(), {(float)xCursor, (float)topY}, (float)hudFont, 1.0f, LIGHTGRAY);
  }

  // Height reserved by top area (no longer used for clamping player)
  int topReserved = topY + hudFont + margin;

  // Bottom bar: player's lives (left), score (center), level (right)
  int bottomBarH = std::max((int)(h * 0.10f), hudFont + margin);
  int bottomY = h - bottomBarH;
  DrawRectangle(0, bottomY, w, bottomBarH, (Color){20, 20, 20, 200});
  DrawRectangleLines(0, bottomY, w, bottomBarH, (Color){60, 60, 60, 255});

  // Left: lives 0..10
  int iconSize = std::max(10, std::min(18, bottomBarH - margin - 10));
  int iconGap = std::max(4, iconSize / 3);
  int livesToDraw = std::min(10, std::max(0, _playerLives));
  int iconRowY = bottomY + (bottomBarH - iconSize) / 2;
  int iconRowX = margin;
  for (int i = 0; i < 10; ++i) {
    Color c = (i < livesToDraw) ? (Color){220, 80, 80, 255}
                                : (Color){70, 70, 70, 255};
    DrawRectangle(iconRowX + i * (iconSize + iconGap), iconRowY, iconSize,
                  iconSize, c);
  }

  // Center: Score
  std::string scoreStr = "Score: " + std::to_string(_score);
  Vector2 scoreSize = MeasureTextEx(font, scoreStr.c_str(), (float)hudFont, 1.0f);
  int scoreW = (int)scoreSize.x;
  int scoreX = (w - scoreW) / 2;
  int textY = bottomY + (bottomBarH - (int)scoreSize.y) / 2;
  DrawTextEx(font, scoreStr.c_str(), {(float)scoreX, (float)textY}, (float)hudFont, 1.0f, RAYWHITE);

  // Right: Shot mode instead of Level
  const char *modeStr =
      (_shotMode == ShotMode::Normal) ? "Shot: Normal" : "Shot: Charge";
  Vector2 modeSize = MeasureTextEx(font, modeStr, (float)hudFont, 1.0f);
  int modeW = (int)modeSize.x;
  int modeX = w - margin - modeW;
  DrawTextEx(font, modeStr, {(float)modeX, (float)textY}, (float)hudFont, 1.0f, (Color){200, 200, 80, 255});

  // Playable area bounds for drawing: between top bar and bottom bar
  int playableMinY = topReserved;
  int playableMaxY = h - bottomBarH;
  if (playableMaxY < playableMinY + 1)
    playableMaxY = playableMinY + 1;

  if (_entities.empty()) {
    titleCentered("Connecting to game...", (int)(GetScreenHeight() * 0.5f), 24,
                  RAYWHITE, font);
  }

  // Render entities using persistent sprite-row assignment per player id
  for (std::size_t i = 0; i < _entities.size(); ++i) {
    auto &e = _entities[i];
    Color c = {(unsigned char)((e.rgba >> 24) & 0xFF),
               (unsigned char)((e.rgba >> 16) & 0xFF),
               (unsigned char)((e.rgba >> 8) & 0xFF),
               (unsigned char)(e.rgba & 0xFF)};
    if (e.type == 1) { // Player (on applique les contraintes)
      // Hide self ship if dead
      if (e.id == _selfId && _playerLives <= 0) {
        continue;
      }
      // Taille du vaisseau pour le fallback rect
      int shipW = 20, shipH = 12;
      // Clamp to full window vertically and horizontally
      if (e.y < playableMinY)
        e.y = (float)playableMinY;
      if (e.y + shipH > playableMaxY)
        e.y = (float)(playableMaxY - shipH);
      if (e.x < 0)
        e.x = 0;
      if (e.x + shipW > w)
        e.x = (float)(w - shipW);
      // Get or assign a fixed row for this player id
      int rowIndex = 0;
      auto it = _spriteRowById.find(e.id);
      if (it != _spriteRowById.end()) {
        rowIndex = it->second;
      }
      if (_sheetLoaded && _frameW > 0 && _frameH > 0) {
        // Calculate tilt column based on logic:
        // Left (0) = Descending (vy > 0), Right (4) = Ascending (vy < 0)
        int colIndex = 2; // Default straight
        if (e.vy < -50.f)
          colIndex = 4; // Max Up
        else if (e.vy < -10.f)
          colIndex = 3; // Mid Up
        else if (e.vy > 50.f)
          colIndex = 0; // Max Down
        else if (e.vy > 10.f)
          colIndex = 1; // Mid Down
        const float playerScale = 1.18f;
        float drawW = _frameW * playerScale;
        float drawH = _frameH * playerScale;
        // Apply a small left offset and clamp using destination coords
        const float xOffset = -6.0f;
        float dstX = e.x + xOffset;
        float dstY = e.y;
        if (dstY < playableMinY)
          dstY = (float)playableMinY;
        if (dstX < 0)
          dstX = 0;
        if (dstX + drawW > w)
          dstX = (float)(w - drawW);
        if (dstY + drawH > playableMaxY)
          dstY = (float)(playableMaxY - drawH);
        Rectangle src{_frameW * colIndex, _frameH * rowIndex, _frameW, _frameH};
        Rectangle dst{dstX, dstY, drawW, drawH};
        Vector2 origin{0.0f, 0.0f};
        DrawTexturePro(_sheet, src, dst, origin, 0.0f, WHITE);
      } else {
        // Fallback debug rectangle
        Rectangle dst{e.x, e.y, 20.0f, 12.0f};
        DrawRectangleRec(dst, RED);
      }
    } else if (e.type == 2) { // Enemy
      if (_enemyLoaded && _enemyFrameW > 0 && _enemyFrameH > 0) {
        // Last row is staggered (quinconce): use fractional column index 3.5 on
        // row 2 (zero-based)
        const float colIndex = 2.5f; // zero-based fractional column
        const int rowIndex = 2;      // zero-based last row
        const float cropPx = 10.0f;
        float srcH = _enemyFrameH - cropPx;
        if (srcH < 1.0f)
          srcH = 1.0f; // avoid zero/negative height
        // Only draw when the enemy AABB is fully inside the playable area to
        // avoid edge pop-in
        float ex = e.x;
        float ey = e.y;
        if (ey < playableMinY || ey + kEnemyAabbH > playableMaxY)
          continue;
        if (ex < 0 || ex + kEnemyAabbW > w)
          continue;
        Rectangle src{_enemyFrameW * colIndex, _enemyFrameH * rowIndex,
                      _enemyFrameW, srcH};
        Rectangle dst{ex, ey, kEnemyAabbW, kEnemyAabbH};
        Vector2 origin{0.0f, 0.0f};
        DrawTexturePro(_enemySheet, src, dst, origin, 0.0f, WHITE);
        // Removed enemy debug hitbox outline
      } else {
        // No fallback rectangle for enemies to avoid drawing squares
      }
    } else if (e.type == 3) { // Bullet
      DrawRectangle((int)e.x, (int)e.y, 6, 3, c);
    }
  }

  // Draw charged beam if active
  if (_beamActive) {
    if (GetTime() > _beamEndTime) {
      _beamActive = false;
    } else {
      // Beam goes to the right across the playable area
      float bx = _beamX;
      float by = _beamY;
      // Clamp vertical beam center to playable band
      if (by < playableMinY)
        by = (float)playableMinY;
      if (by > playableMaxY)
        by = (float)playableMaxY;
      float halfT = _beamThickness * 0.5f;
      float y0 = std::max((float)playableMinY, by - halfT);
      float y1 = std::min((float)playableMaxY, by + halfT);
      if (y1 > y0) {
        // Core
        DrawRectangle((int)bx, (int)y0, w - (int)bx, (int)(y1 - y0),
                      (Color){120, 200, 255, 220});
        // Glow borders
        DrawRectangle((int)bx, (int)(y0 - 4), w - (int)bx, 4,
                      (Color){120, 200, 255, 120});
        DrawRectangle((int)bx, (int)y1, w - (int)bx, 4,
                      (Color){120, 200, 255, 120});
      }
    }
  }

  // Detect if all players are dead -> go to dedicated Game Over screen
  bool everyoneDead = (_playerLives <= 0);
  if (everyoneDead) {
    for (const auto &op : _otherPlayers) {
      if (op.lives > 0) {
        everyoneDead = false;
        break;
      }
    }
  }
  if (everyoneDead) {
    leaveSession();
    _connected = false;
    _entities.clear();
    _gameOver = true;
    screen = ScreenState::GameOver;
    return;
  }

  // Game Over overlay and input to return to menu (self dead but others alive)
  if (_gameOver && !everyoneDead) {
    DrawRectangle(0, 0, w, h, (Color){0, 0, 0, 180});
    titleCentered("GAME OVER", (int)(h * 0.40f), (int)(h * 0.10f), RED, font);
    std::string finalScore = "Score: " + std::to_string(_score);
    titleCentered(finalScore.c_str(), (int)(h * 0.52f), (int)(h * 0.06f),
                  RAYWHITE, font);
    titleCentered("Press ESC to return to menu", (int)(h * 0.62f),
                  (int)(h * 0.04f), LIGHTGRAY, font);
    if (IsKeyPressed(KEY_ESCAPE)) {
      teardownNet();
      _connected = false;
      _entities.clear();
      _gameOver = false;
      screen = ScreenState::Menu;
      return;
    }
  }
}

// drawGameOver is implemented in screens/GameOver.cpp
