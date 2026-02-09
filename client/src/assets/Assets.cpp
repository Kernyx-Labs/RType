#include "Screens.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <raylib.h>
#include <string>
#include <vector>

namespace client {
namespace ui {

std::string Screens::findSpritePath(const char *name) const {
  std::vector<std::string> candidates;
  // Helper to add paths relative to base
  auto add = [&](std::string base) {
    if (base.empty())
      return;
    if (base.back() != '/' && base.back() != '\\')
      base += '/';
    candidates.emplace_back(base + "client/sprites/" + name);
    candidates.emplace_back(base + "../client/sprites/" + name);
    candidates.emplace_back(base + "../../client/sprites/" +
                            name); // e.g. build/bin/ -> root/client/sprites
  };

  add(GetApplicationDirectory());
  add("./"); // Fallback to CWD

  // Legacy hardcoded fallback
  candidates.emplace_back(std::string("client/sprites/") + name);
  candidates.emplace_back(std::string("../client/sprites/") + name);
  for (const auto &c : candidates) {
    std::cout << "[INFO] Checking asset path: " << c << std::endl;
    if (FileExists(c.c_str())) {
      std::cout << "[INFO] Found asset: " << c << std::endl;
      return c;
    }
  }
  std::cout << "[ERROR] Asset not found: " << name << std::endl;
  return {};
}

bool Screens::assetsAvailable() const {
  std::string p1 = findSpritePath("r-typesheet42.gif");
  std::string p2 = findSpritePath("r-typesheet19.gif");
  return !p1.empty() && !p2.empty();
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

void Screens::loadBackground() {
  if (_backgroundLoaded)
    return;
  std::string path = findSpritePath("background.jpg");
  if (path.empty()) {
    return;
  }
  _background = LoadTexture(path.c_str());
  if (_background.id == 0) {
    logMessage("Failed to load background texture.", "ERROR");
    return;
  }
  _backgroundLoaded = true;
  logMessage("Background loaded: " + std::to_string(_background.width) + "x" +
                 std::to_string(_background.height),
             "INFO");
}

void Screens::drawBackground(float dt) {
  if (!_backgroundLoaded)
    return;
  const int sw = GetScreenWidth();
  const int sh = GetScreenHeight();
  const float bw = (float)_background.width;
  const float bh = (float)_background.height;
  if (sw <= 0 || sh <= 0 || bw <= 0.f || bh <= 0.f)
    return;

  float scale = std::max((float)sw / bw, (float)sh / bh);
  float drawW = bw * scale;
  float drawH = bh * scale;

  _bgScrollX += _bgSpeed * dt;
  if (drawW > 0.0f) {
    _bgScrollX = std::fmod(_bgScrollX, drawW);
    if (_bgScrollX < 0.0f)
      _bgScrollX += drawW;
  }
  float dy = (sh - drawH) * 0.5f;
  float startX = -_bgScrollX;

  Rectangle src{0.f, 0.f, bw, bh};
  Vector2 origin{0.f, 0.f};

  int tiles = (int)std::ceil((float)sw / drawW) + 1;
  for (int i = 0; i < tiles; ++i) {
    float dx = startX + i * drawW;
    Rectangle dst{dx, dy, drawW, drawH};
    DrawTexturePro(_background, src, dst, origin, 0.f, WHITE);
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
  if (_backgroundLoaded) {
    UnloadTexture(_background);
    _backgroundLoaded = false;
  }
  unloadSoundEffects();
}

Screens::~Screens() {
  if (IsWindowReady()) {
    if (_sheetLoaded) {
      UnloadTexture(_sheet);
      _sheetLoaded = false;
    }
    if (_enemyLoaded) {
      UnloadTexture(_enemySheet);
      _enemyLoaded = false;
    }
    if (_backgroundLoaded) {
      UnloadTexture(_background);
      _backgroundLoaded = false;
    }
    unloadSoundEffects();
  }
}

} // namespace ui
} // namespace client
