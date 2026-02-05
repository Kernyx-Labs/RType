#include "Screens.hpp"
#include "widgets/Button.hpp"
#include "widgets/InputBox.hpp"
#include "widgets/Title.hpp"
#include <raylib.h>
#include <cmath>
#include <random>
#include <algorithm>

namespace client { namespace ui {

static int baseFontFromHeight(int h) {
    int baseFont = (int)(h * 0.045f);
    if (baseFont < 16) baseFont = 16;
    return baseFont;
}

void Screens::drawSingleplayer(ScreenState& screen, SingleplayerForm& /*form*/) {
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    int baseFont = baseFontFromHeight(h);

    // Header: show only when not actively playing
    if (!_singleplayerActive) {
        titleCentered("Singleplayer", (int)(h * 0.10f), (int)(h * 0.08f), RAYWHITE);
    }

    if (!_singleplayerActive) {
        // Idle screen with Start/Back
        int btnWidth = (int)(w * 0.22f);
        int btnHeight = (int)(h * 0.08f);
        int btnGap = (int)(w * 0.02f);
        int btnY = (int)(h * 0.45f);
        int btnX = (w - (btnWidth * 2 + btnGap)) / 2;

        Rectangle startBtn{(float)btnX, (float)btnY, (float)btnWidth, (float)btnHeight};
        if (button(startBtn, "Start", baseFont, BLACK, (Color){120, 200, 120, 255}, (Color){150, 230, 150, 255})) {
            initSingleplayerWorld();
            _singleplayerActive = true;
            _spPaused = false;
        }
        Rectangle backBtn{(float)(btnX + btnWidth + btnGap), (float)btnY, (float)btnWidth, (float)btnHeight};
        if (button(backBtn, "Back", baseFont, BLACK, LIGHTGRAY, GRAY)) {
            shutdownSingleplayerWorld();
            screen = ScreenState::Menu;
            return;
        }
        // Controls hint
        titleCentered("Controls: WASD/Arrows to move. ESC to pause.", (int)(h * 0.70f), baseFont, LIGHTGRAY);
    } else {
        // In-game: update and draw world if not paused
        if (!_gameOver && IsKeyPressed(KEY_ESCAPE)) {
            _spPaused = !_spPaused;
        }

        if (!_spPaused) {
            float dt = GetFrameTime();
            updateSingleplayerWorld(dt);
        }
        drawSingleplayerWorld();

        // Overlays
        if (_gameOver) {
            DrawRectangle(0, 0, w, h, (Color){0, 0, 0, 180});
            titleCentered("Game Over", (int)(h * 0.35f), (int)(h * 0.10f), RAYWHITE);
            int btnWidth = (int)(w * 0.22f);
            int btnHeight = (int)(h * 0.08f);
            int btnGap = (int)(w * 0.02f);
            int btnY = (int)(h * 0.55f);
            int btnX = (w - (btnWidth * 2 + btnGap)) / 2;
            if (button({(float)btnX, (float)btnY, (float)btnWidth, (float)btnHeight}, "Restart", baseFont, BLACK, (Color){180, 180, 220, 255}, (Color){210, 210, 240, 255})) {
                shutdownSingleplayerWorld();
                initSingleplayerWorld();
                _singleplayerActive = true;
                return;
            }
            if (button({(float)(btnX + btnWidth + btnGap), (float)btnY, (float)btnWidth, (float)btnHeight}, "Exit", baseFont, BLACK, (Color){200, 80, 80, 255}, (Color){230, 120, 120, 255})) {
                shutdownSingleplayerWorld();
                screen = ScreenState::Menu;
                return;
            }
        } else if (_spPaused) {
            DrawRectangle(0, 0, w, h, (Color){0, 0, 0, 160});
            titleCentered("Paused", (int)(h * 0.35f), (int)(h * 0.10f), RAYWHITE);
            int btnWidth = (int)(w * 0.22f);
            int btnHeight = (int)(h * 0.08f);
            int btnGap = (int)(w * 0.02f);
            int btnY = (int)(h * 0.55f);
            int btnX = (w - (btnWidth * 2 + btnGap)) / 2;
            if (button({(float)btnX, (float)btnY, (float)btnWidth, (float)btnHeight}, "Resume", baseFont, BLACK, LIGHTGRAY, GRAY)) {
                _spPaused = false;
            }
            if (button({(float)(btnX + btnWidth + btnGap), (float)btnY, (float)btnWidth, (float)btnHeight}, "Exit", baseFont, BLACK, (Color){200, 80, 80, 255}, (Color){230, 120, 120, 255})) {
                shutdownSingleplayerWorld();
                screen = ScreenState::Menu;
                return;
            }
        }
    }
}

// --- Local Singleplayer sandbox (Engine test) ---
void Screens::initSingleplayerWorld() {
    if (_spInitialized) return;

    // Load sound effects
    loadSoundEffects();

    _spWorld = std::make_unique<rt::ecs::Registry>();
    // Systems
    _spWorld->addSystem(std::make_unique<rt::systems::PlayerControlSystem>());
    _spWorld->addSystem(std::make_unique<rt::systems::AiControlSystem>());
    _spWorld->addSystem(std::make_unique<rt::systems::MovementSystem>());
    _spWorld->addSystem(std::make_unique<rt::systems::CollisionSystem>());
    // Player entity
    auto player = _spWorld->create();
    _spWorld->emplace<rt::components::Position>((rt::ecs::Entity)player, rt::components::Position{100.f, 100.f});
    _spWorld->emplace<rt::components::Controller>((rt::ecs::Entity)player, rt::components::Controller{});
    _spWorld->emplace<rt::components::Player>((rt::ecs::Entity)player, rt::components::Player{});
    _spWorld->emplace<rt::components::Size>((rt::ecs::Entity)player, rt::components::Size{24.f, 16.f});
    _spPlayer = player;

    // Reset score at the start of a singleplayer run
    _score = 0;

    // Reset power-ups and schedule first threshold between 1500 and 2000 points
    _spPowerups.clear();
    {
        std::uniform_int_distribution<int> dd(_spPowerupMinPts, _spPowerupMaxPts);
        _spNextPowerupScore = dd(_spRng);
    }

    // Start with an initial simple line
    _spEnemies.clear();
    _spBullets.clear();
    _spEnemyBullets.clear();
    _spShootCooldown = 0.f;
    _spElapsed = 0.f;
    _spSpawnTimer = 0.f;
    _spNextFormation = 0;
    // Seed RNG with time
    std::random_device rd; _spRng.seed(rd());
    // Reset power-up states
    _spInvincibleTimer = 0.f;
    _spInfiniteFireTimer = 0.f;
    // Boss state reset
    _spBossActive = false;
    _spBossSpawned = false;
    _spBossThreshold = 15000;
    _spBossId = 0;
    _spBossStopX = 0.f;
    _spBossAtStop = false;
    _spBossDirDown = true;
    _spBossHp = 0;
    // Schedule first spawn with random delay
    spScheduleNextSpawn();
    _spInitialized = true;
}

void Screens::shutdownSingleplayerWorld() {
    if (_spWorld && _spBossId != 0) {
        _spWorld->destroy(_spBossId);
    }
    _spBossActive = false;
    _spBossSpawned = false;
    _spBossId = 0;

    _spWorld.reset();
    _spPlayer = 0;
    _spEnemies.clear();
    _spBullets.clear();
    _spEnemyBullets.clear();
    _spInitialized = false;
    _singleplayerActive = false;
    _spPaused = false;
    _gameOver = false;
    _spInvincibleTimer = 0.f;
    _spInfiniteFireTimer = 0.f;
}

void Screens::updateSingleplayerWorld(float dt) {
    if (!_spWorld) return;
    // Map keyboard to Controller bits (disabled on game over)
    constexpr std::uint8_t kUp = 1 << 0;
    constexpr std::uint8_t kDown = 1 << 1;
    constexpr std::uint8_t kLeft = 1 << 2;
    constexpr std::uint8_t kRight = 1 << 3;
    std::uint8_t bits = 0;
    if (!_gameOver) {
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) bits |= kUp;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) bits |= kDown;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) bits |= kLeft;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) bits |= kRight;
    }
    if (auto* c = _spWorld->get<rt::components::Controller>(_spPlayer)) c->bits = bits;

    // Global timers
    _spElapsed += dt;
    _spSpawnTimer += dt;
    _spShootCooldown -= dt;
    if (_spShootCooldown < 0.f) _spShootCooldown = 0.f;
    // Tick invincibility frames
    if (_spHitIframes > 0.f) { _spHitIframes -= dt; if (_spHitIframes < 0.f) _spHitIframes = 0.f; }
    // Tick shield (invincibility power-up)
    if (_spInvincibleTimer > 0.f) { _spInvincibleTimer -= dt; if (_spInvincibleTimer < 0.f) _spInvincibleTimer = 0.f; }
    // Tick infinite fire
    if (_spInfiniteFireTimer > 0.f) { _spInfiniteFireTimer -= dt; if (_spInfiniteFireTimer < 0.f) _spInfiniteFireTimer = 0.f; }

    if (!_spBossActive && _score >= _spBossThreshold) {
        spSpawnBoss();
        _spBossActive = true;
        _spBossSpawned = true;
    }

    // Overheat: if firing, drain heat; otherwise regenerate
    bool shootHeld = !_gameOver && IsKeyDown(KEY_SPACE);
    if (_spInfiniteFireTimer > 0.f) {
        // While infinite fire is active, keep heat full and don't drain
        _spHeat = 1.0f;
    } else {
        if (shootHeld) {
            _spHeat -= _spHeatDrainPerSec * dt;
        } else {
            _spHeat += _spHeatRegenPerSec * dt;
        }
        if (_spHeat < 0.f) _spHeat = 0.f; if (_spHeat > 1.f) _spHeat = 1.f;
    }

    // Player shooting: Space creates a bullet with cooldown; during InfiniteFire, ignore heat gating
    if (shootHeld && _spShootCooldown <= 0.f && (_spHeat > 0.f || _spInfiniteFireTimer > 0.f)) {
        if (auto* pp = _spWorld->get<rt::components::Position>(_spPlayer)) {
            float bx = pp->x + 24.f; // from player front
            float by = pp->y + 6.f;  // roughly center
            _spBullets.push_back({bx, by, _spBulletSpeed, 0.f, _spBulletW, _spBulletH});
            _spShootCooldown = _spShootInterval;
            playShootSound(); // Play shooting sound effect
        }
    }

    // Randomized spawn scheduler, with cap on active enemies
    if (!_gameOver && !_spBossActive && _spSpawnTimer >= _spNextSpawnDelay && _spEnemies.size() < _spEnemyCap) {
        _spSpawnTimer = 0.f;
        // choose formation cyclically to keep variety while still randomizing timing
        int k = _spNextFormation++ % 4;
        int h = GetScreenHeight();
        float topMargin = h * 0.10f;
        float bottomMargin = h * 0.05f;
        float minY = topMargin + 40.f;
        float maxY = h - bottomMargin - 80.f;
        if (maxY < minY) maxY = minY + 1.f;
        std::uniform_real_distribution<float> ydist(minY, maxY);
        float y = ydist(_spRng);
        switch (k) {
            case 0: spSpawnLine(6, y); break;
            case 1: spSpawnSnake(6, y, 70.f, 2.2f, 36.f); break;
            case 2: spSpawnTriangle(5, y, 36.f); break;
            case 3: spSpawnDiamond(4, y, 36.f); break;
        }
        spScheduleNextSpawn();
    }

    // Drive AI bits for each enemy (prefer up-left overall)
    for (auto it = _spEnemies.begin(); it != _spEnemies.end(); ) {
        auto& en = *it;
        // If entity no longer exists (basic check: no Position), erase
        auto* pos = _spWorld->get<rt::components::Position>(en.id);
        auto* ai = _spWorld->get<rt::components::AiController>(en.id);
        if (!pos || !ai) { it = _spEnemies.erase(it); continue; }
        float t = _spElapsed - en.spawnTime;
        std::uint8_t bitsAI = 0;
        // On game over, freeze enemies
        if (!_gameOver) {
            // Always move to the left
            bitsAI |= kLeft;
        }
        switch (en.kind) {
            case SpFormationKind::Snake: {
                float phase = t * en.frequency + en.index * 0.5f;
                float s = std::sin(phase);
                if (!_gameOver) bitsAI |= (s > 0.f ? kUp : 0);
                // small downward occasionally to prevent sticking at top if extremely high
                if (!_gameOver && s <= 0.f && ((en.index + (int)(t)) % 3 == 0)) bitsAI |= kDown;
                break;
            }
            case SpFormationKind::Triangle:
            case SpFormationKind::Diamond:
            case SpFormationKind::Line: {
                // move straight left only for these formations
                break;
            }
        }
        ai->bits = bitsAI;
        // Despawn when far left off-screen
        if (pos->x < -80.f) { _spWorld->destroy(en.id); it = _spEnemies.erase(it); }
        else { ++it; }
    }

    // Enemy shooting: enemies with shooter=true fire towards the player with some inaccuracy
    if (!_gameOver) {
        // Player center
        float px = 0.f, py = 0.f;
        if (auto* pp = _spWorld->get<rt::components::Position>(_spPlayer)) { px = pp->x + 12.f; py = pp->y + 8.f; }
        for (auto& en : _spEnemies) {
            if (!en.shooter) continue;
            en.shootCooldown -= dt;
            if (en.shootCooldown > 0.f) continue;
            if (auto* ep = _spWorld->get<rt::components::Position>(en.id)) {
                // direction to player
                float ex = ep->x + 12.f;
                float ey = ep->y + 8.f;
                float dx = px - ex;
                float dy = py - ey;
                float len = std::sqrt(dx*dx + dy*dy);
                if (len < 1e-3f) { dx = -1.f; dy = 0.f; len = 1.f; }
                dx /= len; dy /= len;
                // inaccuracy
                float acc = std::clamp(en.accuracy, 0.5f, 0.8f);
                float maxAngle = (1.f - acc) * 0.5f;
                std::uniform_real_distribution<float> ang(-maxAngle, maxAngle);
                float a = ang(_spRng);
                float cs = std::cos(a), sn = std::sin(a);
                float dirx = dx * cs - dy * sn;
                float diry = dx * sn + dy * cs;
                float vx = dirx * en.bulletSpeed;
                float vy = diry * en.bulletSpeed;
                float bx = ep->x - 6.f;
                float by = ep->y + 6.f;
                _spEnemyBullets.push_back({bx, by, vx, vy, 6.f, 3.f});
                en.shootCooldown += en.shootInterval;
            }
        }
    }

    if (_spBossActive && _spBossId != 0) {
        auto* pos = _spWorld->get<rt::components::Position>(_spBossId);
        auto* ai = _spWorld->get<rt::components::AiController>(_spBossId);
        if (!pos || !ai) {
            _spBossActive = false;
            _spBossId = 0;
        } else {
            std::uint8_t b = 0;
            if (!_gameOver) {
                int screenW = GetScreenWidth();
                int screenH = GetScreenHeight();
                float minY = 0.f;
                float maxY = screenH - _spBossH;
                if (minY > maxY) maxY = minY;
                _spBossStopX = (float)screenW - _spBossRightMargin - _spBossW;
                if (!_spBossAtStop) {
                    if (pos->x > _spBossStopX) {
                        b |= kLeft;
                    } else {
                        _spBossAtStop = true;
                        if (pos->y < minY) pos->y = minY;
                        if (pos->y > maxY) pos->y = maxY;
                        // reset boss shooting when it reaches stop
                        _spBossShootCooldown = 0.4f;
                    }
                }
                if (_spBossAtStop) {
                    if (_spBossDirDown) {
                        b |= kDown;
                        if (pos->y >= maxY) _spBossDirDown = false;
                    } else {
                        b |= kUp;
                        if (pos->y <= minY) _spBossDirDown = true;
                    }
                    // Boss shooting: fire a fan toward the player at intervals
                    _spBossShootCooldown -= dt;
                    if (_spBossShootCooldown <= 0.f) {
                        float px = 0.f, py = 0.f;
                        if (auto* pp = _spWorld->get<rt::components::Position>(_spPlayer)) { px = pp->x + 12.f; py = pp->y + 8.f; }
                        float bx = pos->x;
                        float by = pos->y + _spBossH * 0.5f;
                        float dx = px - bx;
                        float dy = py - by;
                        float len = std::sqrt(dx*dx + dy*dy);
                        if (len < 1e-3f) { dx = -1.f; dy = 0.f; len = 1.f; }
                        dx /= len; dy /= len;
                        int n = std::max(1, _spBossBurstCount);
                        for (int i = 0; i < n; ++i) {
                            float tnorm = (n == 1) ? 0.f : (float)i / (float)(n - 1);
                            float angle = (tnorm - 0.5f) * 2.f * _spBossSpread;
                            float cs = std::cos(angle), sn = std::sin(angle);
                            float dirx = dx * cs - dy * sn;
                            float diry = dx * sn + dy * cs;
                            float vx = dirx * _spBossBulletSpeed;
                            float vy = diry * _spBossBulletSpeed;
                            float sx = pos->x - 8.f;
                            float sy = by - 2.f;
                            _spEnemyBullets.push_back({sx, sy, vx, vy, 8.f, 4.f});
                        }
                        _spBossShootCooldown += _spBossShootInterval;
                    }
                }
            }
            ai->bits = b;
        }
    }

    // Update bullets and handle collisions
    const int screenW = GetScreenWidth();
    if (!_gameOver)
    for (std::size_t i = 0; i < _spBullets.size(); ) {
        auto& b = _spBullets[i];
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        bool destroyBullet = false;
        // Offscreen to the right
        if (b.x > screenW + 50.f) destroyBullet = true;
        // Collide with enemies (AABB)
        for (std::size_t ei = 0; ei < _spEnemies.size() && !destroyBullet; ++ei) {
            auto& en = _spEnemies[ei];
            if (auto* ep = _spWorld->get<rt::components::Position>(en.id)) {
                float ex = ep->x, ey = ep->y, ew = 24.f, eh = 16.f;
                float bx1 = b.x, by1 = b.y, bx2 = b.x + b.w, by2 = b.y + b.h;
                float ex2 = ex + ew, ey2 = ey + eh;
                bool hit = !(bx2 < ex || ex2 < bx1 || by2 < ey || ey2 < by1);
                if (hit) {
                    // Enemy dies on hit: remove entity and from list
                    _spWorld->destroy(en.id);
                    _spEnemies.erase(_spEnemies.begin() + (long)ei);
                    // Play explosion sound
                    playExplosionSound();
                    // Award score: +50 per enemy killed
                    _score += 50;
                    // Spawn power-ups for any crossed thresholds
                    spHandleScoreThresholdSpawns(screenW);
                    destroyBullet = true;
                }
            }
        }
        if (!destroyBullet && _spBossActive && _spBossId != 0) {
            if (auto* bp = _spWorld->get<rt::components::Position>(_spBossId)) {
                float ex = bp->x, ey = bp->y, ew = _spBossW, eh = _spBossH;
                float bx1 = b.x, by1 = b.y, bx2 = b.x + b.w, by2 = b.y + b.h;
                float ex2 = ex + ew, ey2 = ey + eh;
                bool hit = !(bx2 < ex || ex2 < bx1 || by2 < ey || ey2 < by1);
                if (hit) {
                    if (_spBossHp > 0) _spBossHp -= 1;
                    if (_spBossHp < 0) _spBossHp = 0;
                    destroyBullet = true;
                    if (_spBossHp == 0) {
                        _spWorld->destroy(_spBossId);
                        _spBossId = 0;
                        _spBossActive = false;
                        _spBossAtStop = false;
                        _spBossSpawned = false;
                        // Play explosion sound for boss defeat
                        playExplosionSound();
                        _score += 1000;
                        _spBossThreshold += 15000;
                        _spSpawnTimer = _spNextSpawnDelay;
                    }
                }
            }
        }
        if (destroyBullet) _spBullets.erase(_spBullets.begin() + (long)i);
        else ++i;
    }

    // Enemy bullets: move, despawn, and collide with player
    if (!_gameOver) {
        int screenH = GetScreenHeight();
        for (std::size_t i = 0; i < _spEnemyBullets.size(); ) {
            auto& b = _spEnemyBullets[i];
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            bool destroyBullet = false;
            // Offscreen
            if (b.x + b.w < -40.f || b.x > screenW + 60.f || b.y + b.h < -40.f || b.y > screenH + 60.f) destroyBullet = true;
            // Collide with player
            if (!destroyBullet) {
                if (auto* pp = _spWorld->get<rt::components::Position>(_spPlayer)) {
                    float px = pp->x, py = pp->y, pw = 24.f, ph = 16.f;
                    // expand pw/ph if Size component set
                    if (auto* sz = _spWorld->get<rt::components::Size>(_spPlayer)) { pw = sz->w; ph = sz->h; }
                    float bx1 = b.x, by1 = b.y, bx2 = b.x + b.w, by2 = b.y + b.h;
                    float px2 = px + pw, py2 = py + ph;
                    bool hit = !(bx2 < px || px2 < bx1 || by2 < py || py2 < by1);
                    if (hit) {
                        // If shield or i-frames, ignore life loss but still remove the bullet
                        if (_spInvincibleTimer <= 0.f && _spHitIframes <= 0.f && _playerLives > 0) {
                            _playerLives = std::max(0, _playerLives - 1);
                            _spHitIframes = _spHitIframesDuration;
                        }
                        destroyBullet = true;
                    }
                }
            }
            if (destroyBullet) _spEnemyBullets.erase(_spEnemyBullets.begin() + (long)i);
            else ++i;
        }
    }

    // Move power-ups and handle pickup/offscreen
    if (!_gameOver) {
        spUpdatePowerups(dt);
    }

    _spWorld->update(dt);

    // Clamp player inside the game area (stay within screen and above the bottom HUD bar)
    if (auto* pos = _spWorld->get<rt::components::Position>(_spPlayer)) {
        float pw = 24.f, ph = 16.f;
        if (auto* sz = _spWorld->get<rt::components::Size>(_spPlayer)) {
            pw = sz->w;
            ph = sz->h;
        }
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        int barH = (int)(sh * 0.06f);
        float minX = 0.f;
        float minY = 0.f;
        float maxX = (float)sw - pw;
        float maxY = (float)sh - barH - ph;
        if (maxX < minX) maxX = minX;
        if (maxY < minY) maxY = minY;
        pos->x = std::clamp(pos->x, minX, maxX);
        pos->y = std::clamp(pos->y, minY, maxY);
    }

    // If engine marked player as collided, decrement life and clear flag
    if (auto* col = _spWorld->get<rt::components::Collided>(_spPlayer)) {
        if (col->value && _spHitIframes <= 0.f && _playerLives > 0 && _spInvincibleTimer <= 0.f) {
            _playerLives = std::max(0, _playerLives - 1);
            _spHitIframes = _spHitIframesDuration;
        }
        col->value = false;
    }
    if (_playerLives <= 0) {
        _gameOver = true;
    }
}

void Screens::drawSingleplayerWorld() {
    if (!_spWorld) return;
    auto* p = _spWorld->get<rt::components::Position>(_spPlayer);
    if (p) {
        Rectangle rect{p->x, p->y, 24.f, 16.f};
        DrawRectangleRec(rect, (Color){100, 200, 255, 255});
        // Draw shield if invincible
        if (_spInvincibleTimer > 0.f) {
            int cx = (int)(p->x + 12.f);
            int cy = (int)(p->y + 8.f);
            float r = _spShieldRadius;
            // Translucent blue aura
            Color fill = (Color){80, 170, 255, 80};
            Color line = (Color){120, 200, 255, 180};
            DrawCircle(cx, cy, r, fill);
            DrawCircleLines(cx, cy, r, line);
        }
    }
    // draw all enemies as red rectangles
    for (auto& en : _spEnemies) {
        if (auto* ep = _spWorld->get<rt::components::Position>(en.id)) {
            Rectangle rect{ep->x, ep->y, 24.f, 16.f};
            DrawRectangleRec(rect, (Color){220, 80, 80, 255});
        }
    }
    // draw boss if active
    if (_spBossActive && _spBossId != 0) {
        if (auto* bp = _spWorld->get<rt::components::Position>(_spBossId)) {
            Rectangle rect{bp->x, bp->y, _spBossW, _spBossH};
            DrawRectangleRec(rect, (Color){150, 60, 180, 255});
            DrawRectangleLines((int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height, (Color){220, 200, 240, 255});
        }
    }
    // draw bullets as yellow rectangles
    for (auto& b : _spBullets) {
        Rectangle rect{b.x, b.y, b.w, b.h};
        DrawRectangleRec(rect, (Color){240, 220, 80, 255});
    }
    // draw enemy bullets as orange rectangles
    for (auto& b : _spEnemyBullets) {
        Rectangle rect{b.x, b.y, b.w, b.h};
        DrawRectangleRec(rect, (Color){255, 170, 0, 255});
    }
    // draw power-ups (delegated)
    spDrawPowerups();

    // Draw lives bar at bottom-left: squares representing HP
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    int barH = (int)(h * 0.06f);
    int margin = 8;
    DrawRectangle(0, h - barH, w, barH, (Color){0, 0, 0, 140});
    // Make HP squares smaller so they don't sit under the charge bar
    int sqSize = std::max(6, (int)((barH - 2 * margin) * 0.6f));
    if (sqSize < 8) sqSize = 8;
    int gap = 6;
    int total = _maxLives;
    int startX = margin; // left-aligned
    for (int i = 0; i < total; ++i) {
        Color c = i < _playerLives ? (Color){100, 220, 120, 255} : (Color){80, 80, 80, 180};
        DrawRectangle(startX + i * (sqSize + gap), h - barH + margin, sqSize, sqSize, c);
    }

    // Draw overheat bar at center bottom: red depleting bar while firing, recharges when idle
    int barW = (int)(w * 0.35f);
    int barX = (w - barW) / 2;
    int barY = h - barH + margin;
    int barInnerH = sqSize;
    // background
    DrawRectangle(barX, barY, barW, barInnerH, (Color){60, 60, 60, 180});
    // fill
    int fillW = (int)(barW * _spHeat);
    Color fillC = _spHeat > 0.2f ? (Color){220, 90, 90, 220} : (Color){220, 40, 40, 240};
    DrawRectangle(barX, barY, fillW, barInnerH, fillC);
    // outline
    DrawRectangleLines(barX, barY, barW, barInnerH, (Color){220, 220, 220, 200});

    // Draw current score at the top-left corner
    int font = baseFontFromHeight(h);
    std::string scoreText = std::string("Score: ") + std::to_string(_score);
    DrawText(scoreText.c_str(), margin, margin, font, RAYWHITE);

    if (_spBossActive && _spBossHpMax > 0) {
        float ratio = (float)_spBossHp / (float)_spBossHpMax;
        if (ratio < 0.f) ratio = 0.f; if (ratio > 1.f) ratio = 1.f;
        int barW = (int)(w * 0.36f);
        int barH2 = (int)(h * 0.03f);
        int barX = (w - barW) / 2;
        int barY = margin + font + 6; // below score line
        const char* label = "BOSS";
        int tw = MeasureText(label, font);
        int labelX = barX + (barW - tw) / 2;
        int labelY = barY - font - 4;
        if (labelY < 0) labelY = 0;
        DrawText(label, labelX, labelY, font, RAYWHITE);
        DrawRectangle(barX, barY, barW, barH2, (Color){30, 30, 30, 200});
        Color hpC = (Color){220, 70, 70, 230};
        DrawRectangle(barX, barY, (int)(barW * ratio), barH2, hpC);
        DrawRectangleLines(barX, barY, barW, barH2, (Color){220, 220, 220, 220});
    }
}

void Screens::spHandleScoreThresholdSpawns(int screenW) {
    while (_score >= _spNextPowerupScore) {
        int h = GetScreenHeight();
        float topMargin = h * 0.10f;
        float bottomMargin = h * 0.05f;
        float minY = topMargin + 16.f;
        float maxY = h - bottomMargin - 16.f;
        if (maxY < minY) maxY = minY + 1.f;
        std::uniform_real_distribution<float> ydist(minY, maxY);
        float y = ydist(_spRng);
        float x = (float)screenW + _spPowerupRadius + 8.f;
        std::uniform_int_distribution<int> tdist(0, 3);
        int slot = tdist(_spRng);
        SpPowerupType type = SpPowerupType::Life;
        if (slot == 0) type = SpPowerupType::Life;
        else if (slot == 1) type = SpPowerupType::Invincibility;
        else if (slot == 2) type = SpPowerupType::ClearBoard;
        else type = SpPowerupType::InfiniteFire;
        _spPowerups.push_back({x, y, -_spPowerupSpeed, _spPowerupRadius, type});
        std::uniform_int_distribution<int> dd(_spPowerupMinPts, _spPowerupMaxPts);
        _spNextPowerupScore += dd(_spRng);
    }
}

void Screens::spUpdatePowerups(float dt) {
    // Player rectangle for collision
    float px = 0.f, py = 0.f, pw = 24.f, ph = 16.f;
    if (auto* pp = _spWorld->get<rt::components::Position>(_spPlayer)) { px = pp->x; py = pp->y; }
    auto rectCircleHit = [&](float cx, float cy, float r) {
        float rx1 = px, ry1 = py, rx2 = px + pw, ry2 = py + ph;
        float closestX = std::clamp(cx, rx1, rx2);
        float closestY = std::clamp(cy, ry1, ry2);
        float dx = cx - closestX;
        float dy = cy - closestY;
        return (dx*dx + dy*dy) <= (r * r);
    };
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

    for (std::size_t i = 0; i < _spPowerups.size(); ) {
        auto& pu = _spPowerups[i];
        pu.x += pu.vx * dt;
        bool remove = false;
        // Pickup
        if (rectCircleHit(pu.x, pu.y, pu.radius)) {
            switch (pu.type) {
                case SpPowerupType::Life:
                    if (_playerLives < _maxLives) _playerLives += 1;
                    break;
                case SpPowerupType::Invincibility:
                    _spInvincibleTimer = _spInvincibleDuration;
                    break;
                case SpPowerupType::ClearBoard: {
                    int killed = 0;
                    for (std::size_t ei = 0; ei < _spEnemies.size(); ) {
                        auto& en = _spEnemies[ei];
                        if (auto* ep = _spWorld->get<rt::components::Position>(en.id)) {
                            float ex = ep->x, ey = ep->y, ew = 24.f, eh = 16.f;
                            bool onScreen = !(ex + ew < 0.f || ex > (float)screenW || ey + eh < 0.f || ey > (float)screenH);
                            if (onScreen) {
                                _spWorld->destroy(en.id);
                                _spEnemies.erase(_spEnemies.begin() + (long)ei);
                                // Play explosion sound for each enemy cleared
                                playExplosionSound();
                                ++killed;
                                continue;
                            }
                        }
                        ++ei;
                    }
                    if (killed > 0) {
                        _score += 50 * killed;
                        spHandleScoreThresholdSpawns(screenW);
                    }
                    break;
                }
                case SpPowerupType::InfiniteFire:
                    _spInfiniteFireTimer = _spInfiniteFireDuration;
                    break;
            }
            remove = true;
        }
        if (pu.x + pu.radius < -20.f) remove = true;
        if (remove) _spPowerups.erase(_spPowerups.begin() + (long)i);
        else ++i;
    }
}

void Screens::spDrawPowerups() {
    for (auto& pu : _spPowerups) {
        Color fill;
        Color line;
        if (pu.type == SpPowerupType::Invincibility) {
            fill = (Color){80, 170, 255, 220};
            line = (Color){120, 200, 255, 255};
        } else if (pu.type == SpPowerupType::ClearBoard) {
            fill = (Color){170, 80, 200, 230};
            line = (Color){210, 120, 240, 255};
        } else if (pu.type == SpPowerupType::InfiniteFire) {
            fill = (Color){240, 220, 80, 230};
            line = (Color){255, 240, 120, 255};
        } else {
            fill = (Color){100, 220, 120, 255};
            line = (Color){60, 160, 80, 255};
        }
        DrawCircle((int)pu.x, (int)pu.y, pu.radius, fill);
        DrawCircleLines((int)pu.x, (int)pu.y, pu.radius, line);
    }
}

void Screens::spScheduleNextSpawn() {
    std::uniform_real_distribution<float> dd(_spMinSpawnDelay, _spMaxSpawnDelay);
    _spNextSpawnDelay = dd(_spRng);
}
void Screens::spSpawnLine(int count, float y) {
    float startX = (float)GetScreenWidth() + 40.f;
    float spacing = 40.f;
    std::uniform_int_distribution<int> chance(0, 99);
    for (int i = 0; i < count; ++i) {
        auto e = _spWorld->create();
        float x = startX + i * spacing;
        _spWorld->emplace<rt::components::Position>((rt::ecs::Entity)e, rt::components::Position{x, y});
        _spWorld->emplace<rt::components::Enemy>((rt::ecs::Entity)e, rt::components::Enemy{});
        _spWorld->emplace<rt::components::AiController>((rt::ecs::Entity)e, rt::components::AiController{});
        _spWorld->emplace<rt::components::Size>((rt::ecs::Entity)e, rt::components::Size{24.f, 16.f});
        SpEnemy info{e, SpFormationKind::Line, i, y, spacing, 0.f, 0.f, _spElapsed, (float)i * spacing, 0.f};
        if (chance(_spRng) < _spShooterPercent) {
            info.shooter = true;
            info.shootInterval = 1.2f; // similar to server normal difficulty
            info.bulletSpeed = 240.f;
            info.accuracy = 0.62f;
            info.shootCooldown = 0.2f * (float)i; // slight staggering
        }
        _spEnemies.push_back(info);
    }
}

void Screens::spSpawnSnake(int count, float y, float amplitude, float frequency, float spacing) {
    float startX = (float)GetScreenWidth() + 40.f;
    std::uniform_int_distribution<int> chance(0, 99);
    for (int i = 0; i < count; ++i) {
        auto e = _spWorld->create();
        float x = startX + i * spacing;
        _spWorld->emplace<rt::components::Position>((rt::ecs::Entity)e, rt::components::Position{x, y});
        _spWorld->emplace<rt::components::Enemy>((rt::ecs::Entity)e, rt::components::Enemy{});
        _spWorld->emplace<rt::components::AiController>((rt::ecs::Entity)e, rt::components::AiController{});
        _spWorld->emplace<rt::components::Size>((rt::ecs::Entity)e, rt::components::Size{24.f, 16.f});
        SpEnemy info{e, SpFormationKind::Snake, i, y, spacing, amplitude, frequency, _spElapsed, (float)i * spacing, 0.f};
        if (chance(_spRng) < _spShooterPercent) {
            info.shooter = true;
            info.shootInterval = 1.2f;
            info.bulletSpeed = 240.f;
            info.accuracy = 0.65f;
            info.shootCooldown = 0.15f * (float)i;
        }
        _spEnemies.push_back(info);
    }
}

void Screens::spSpawnTriangle(int rows, float y, float spacing) {
    float startX = (float)GetScreenWidth() + 40.f;
    int idx = 0;
    std::uniform_int_distribution<int> chance(0, 99);
    for (int col = 0; col < rows; ++col) {
        int count = col + 1; // 1,2,3,...
        float localX = col * spacing;
        float startY = -0.5f * (count - 1) * spacing;
        for (int r = 0; r < count; ++r) {
            float localY = startY + r * spacing;
            auto e = _spWorld->create();
            _spWorld->emplace<rt::components::Position>((rt::ecs::Entity)e, rt::components::Position{startX + localX, y + localY});
            _spWorld->emplace<rt::components::Enemy>((rt::ecs::Entity)e, rt::components::Enemy{});
            _spWorld->emplace<rt::components::AiController>((rt::ecs::Entity)e, rt::components::AiController{});
            _spWorld->emplace<rt::components::Size>((rt::ecs::Entity)e, rt::components::Size{24.f, 16.f});
            SpEnemy info{e, SpFormationKind::Triangle, idx++, y, spacing, 0.f, 0.f, _spElapsed, localX, localY};
            if (chance(_spRng) < _spShooterPercent) {
                info.shooter = true;
                info.shootInterval = 1.3f;
                info.bulletSpeed = 220.f;
                info.accuracy = 0.60f;
                info.shootCooldown = 0.1f * (float)idx;
            }
            _spEnemies.push_back(info);
        }
    }
}

void Screens::spSpawnDiamond(int rows, float y, float spacing) {
    // Diamond shape: columns increase then decrease
    float startX = (float)GetScreenWidth() + 40.f;
    int idx = 0;
    std::uniform_int_distribution<int> chance(0, 99);
    for (int col = 0; col < rows; ++col) {
        int count = col + 1;
        float localX = col * spacing;
        float startY = -0.5f * (count - 1) * spacing;
        for (int r = 0; r < count; ++r) {
            float localY = startY + r * spacing;
            auto e = _spWorld->create();
            _spWorld->emplace<rt::components::Position>((rt::ecs::Entity)e, rt::components::Position{startX + localX, y + localY});
            _spWorld->emplace<rt::components::Enemy>((rt::ecs::Entity)e, rt::components::Enemy{});
            _spWorld->emplace<rt::components::AiController>((rt::ecs::Entity)e, rt::components::AiController{});
            _spWorld->emplace<rt::components::Size>((rt::ecs::Entity)e, rt::components::Size{24.f, 16.f});
            SpEnemy info{e, SpFormationKind::Diamond, idx++, y, spacing, 0.f, 0.f, _spElapsed, localX, localY};
            if (chance(_spRng) < _spShooterPercent) {
                info.shooter = true;
                info.shootInterval = 1.3f;
                info.bulletSpeed = 220.f;
                info.accuracy = 0.60f;
                info.shootCooldown = 0.1f * (float)idx;
            }
            _spEnemies.push_back(info);
        }
    }
    for (int col = rows - 2; col >= 0; --col) {
        int count = col + 1;
        float localX = (2 * rows - 2 - col) * spacing;
        float startY = -0.5f * (count - 1) * spacing;
        for (int r = 0; r < count; ++r) {
            float localY = startY + r * spacing;
            auto e = _spWorld->create();
            _spWorld->emplace<rt::components::Position>((rt::ecs::Entity)e, rt::components::Position{startX + localX, y + localY});
            _spWorld->emplace<rt::components::Enemy>((rt::ecs::Entity)e, rt::components::Enemy{});
            _spWorld->emplace<rt::components::AiController>((rt::ecs::Entity)e, rt::components::AiController{});
            _spWorld->emplace<rt::components::Size>((rt::ecs::Entity)e, rt::components::Size{24.f, 16.f});
            SpEnemy info{e, SpFormationKind::Diamond, idx++, y, spacing, 0.f, 0.f, _spElapsed, localX, localY};
            if (chance(_spRng) < _spShooterPercent) {
                info.shooter = true;
                info.shootInterval = 1.3f;
                info.bulletSpeed = 220.f;
                info.accuracy = 0.60f;
                info.shootCooldown = 0.1f * (float)idx;
            }
            _spEnemies.push_back(info);
        }
    }
}

void Screens::spSpawnBoss() {
    if (!_spWorld) return;
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    float spawnX = (float)screenW + 40.f;
    float minY = 0.f;
    float maxY = screenH - _spBossH;
    if (maxY < minY) maxY = minY;
    float y = 0.5f * (minY + maxY);

    auto e = _spWorld->create();
    _spWorld->emplace<rt::components::Position>((rt::ecs::Entity)e, rt::components::Position{spawnX, y});
    _spWorld->emplace<rt::components::Enemy>((rt::ecs::Entity)e, rt::components::Enemy{});
    _spWorld->emplace<rt::components::AiController>((rt::ecs::Entity)e, rt::components::AiController{});
    _spWorld->emplace<rt::components::Size>((rt::ecs::Entity)e, rt::components::Size{_spBossW, _spBossH});

    _spBossId = e;
    _spBossActive = true;
    _spBossAtStop = false;
    _spBossDirDown = true;
    _spBossHp = _spBossHpMax;
    _spBossStopX = (float)screenW - _spBossRightMargin - _spBossW;
}

} } // namespace client::ui
