#include "Screens.hpp"
#include "widgets/Title.hpp"
#include <raylib.h>
#include <algorithm>
#include "common/Protocol.hpp"

namespace client { namespace ui {

void Screens::drawGameplay(ScreenState& screen) {
    // Multiplayer gameplay adapted to match singleplayer UI and feel
    if (!_connected) {
        titleCentered("Not connected. Press ESC.", GetScreenHeight()*0.5f, 24, RAYWHITE);
        if (IsKeyPressed(KEY_ESCAPE)) { leaveSession(); screen = ScreenState::Menu; }
        return;
    }
    ensureNetSetup();
    // Keep the latest snapshot fresh
    pumpNetworkOnce();
    if (_serverReturnToMenu) { leaveSession(); screen = ScreenState::NotEnoughPlayers; return; }

    // Compute playable band similar to singleplayer (reserve bottom bar height)
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    int hudFont = std::max(16, (int)(h * 0.045f));
    int margin = 16;
    int bottomBarH = std::max((int)(h * 0.10f), hudFont + margin);
    int playableMinY = 0 + margin; // no top teammates bar in this layout
    int playableMaxY = h - bottomBarH;
    if (playableMaxY < playableMinY + 1) playableMaxY = playableMinY + 1;

    // Find self entity for input edge-gating
    const PackedEntity* self = nullptr;
    for (const auto& ent : _entities) { if (ent.type == 1 && ent.id == _selfId) { self = &ent; break; } }

    // Build input from arrows OR WASD, gate within playable area based on self pos
    bool isAlive = (_playerLives > 0) && !_gameOver;
    bool wantLeft  = isAlive && (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A));
    bool wantRight = isAlive && (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D));
    bool wantUp    = isAlive && (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W));
    bool wantDown  = isAlive && (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S));
    bool wantShoot = isAlive && IsKeyDown(KEY_SPACE);
    std::uint8_t bits = 0;
    // Assume player rect 24x16 like singleplayer for gating
    const float pW = 24.f, pH = 16.f;
    if (self) {
        float dstX = self->x;
        float dstY = self->y;
        if (wantLeft  && dstX > 0.0f) bits |= rtype::net::InputLeft;
        if (wantRight && dstX + pW < (float)w) bits |= rtype::net::InputRight;
        if (wantUp    && dstY > (float)playableMinY) bits |= rtype::net::InputUp;
        if (wantDown  && dstY + pH < (float)playableMaxY) bits |= rtype::net::InputDown;
    } else {
        if (wantLeft)  bits |= rtype::net::InputLeft;
        if (wantRight) bits |= rtype::net::InputRight;
        if (wantUp)    bits |= rtype::net::InputUp;
        if (wantDown)  bits |= rtype::net::InputDown;
    }

    // Simple overheat UI mechanic (client-side only): drains while firing, regens when not
    if (wantShoot) {
        _spHeat -= _spHeatDrainPerSec * GetFrameTime();
    } else {
        _spHeat += _spHeatRegenPerSec * GetFrameTime();
    }
    if (_spHeat < 0.f) _spHeat = 0.f; if (_spHeat > 1.f) _spHeat = 1.f;

    // Only send shoot input if not overheated (to mimic singleplayer feel)
    if (isAlive && wantShoot && _spHeat > 0.f) bits |= rtype::net::InputShoot;

    // Send inputs at ~30Hz
    double now = GetTime();
    if (now - _lastSend > 1.0/30.0) { sendInput(bits); _lastSend = now; }

    // --- Bottom HUD: Lives (left) + Overheat bar (center) ---
    int bottomY = h - bottomBarH;
    DrawRectangle(0, bottomY, w, bottomBarH, (Color){0, 0, 0, 140});
    // Make HP squares smaller so they don't collide with the charge bar
    int sqSize = std::max(6, (int)((bottomBarH - 2 * margin) * 0.6f));
    int gap = std::max(6, sqSize / 3);
    int livesToDraw = std::min(10, std::max(0, _playerLives));
    int startX = margin;
    for (int i = 0; i < 10; ++i) {
        Color c = (i < livesToDraw) ? (Color){100, 220, 120, 255} : (Color){80, 80, 80, 180};
        DrawRectangle(startX + i * (sqSize + gap), bottomY + margin, sqSize, sqSize, c);
    }
    // Overheat bar centered
    int barW = (int)(w * 0.35f);
    int barX = (w - barW) / 2;
    int barY = bottomY + margin;
    int barH = sqSize;
    DrawRectangle(barX, barY, barW, barH, (Color){60, 60, 60, 180});
    int fillW = (int)(barW * _spHeat);
    Color fillC = _spHeat > 0.2f ? (Color){220, 90, 90, 220} : (Color){220, 40, 40, 240};
    DrawRectangle(barX, barY, fillW, barH, fillC);
    DrawRectangleLines(barX, barY, barW, barH, (Color){220, 220, 220, 200});

    // --- Team score (top-left) ---
    int hudFontScore = hudFont;
    int scoreMargin = margin;
    std::string scoreText = std::string("Score: ") + std::to_string(_score);
    DrawText(scoreText.c_str(), scoreMargin, scoreMargin, hudFontScore, RAYWHITE);

    // --- World rendering (rectangles like singleplayer) ---
    if (_entities.empty()) {
        titleCentered("Connecting to game...", (int)(GetScreenHeight()*0.5f), 24, RAYWHITE);
    }
    double nowSec = GetTime();
    for (auto& e : _entities) {
        // Extrapolate position if entity stopped updating (prevent freezing)
        if (_lastSeenAt.count(e.id)) {
            double elapsed = nowSec - _lastSeenAt[e.id];
            if (elapsed > 0.05 && elapsed < 2.0) { // Only extrapolate for 50ms-2s gap
                e.x += e.vx * elapsed;
                e.y += e.vy * elapsed;
            }
        }
        if (e.type == 1) {
            // Player ship
            if (e.id == _selfId && _playerLives <= 0) {
                continue; // hide local ship if dead
            }
            float x = e.x, y = e.y;
            if (y < playableMinY) y = (float)playableMinY;
            if (y + pH > playableMaxY) y = (float)(playableMaxY - pH);
            if (x < 0) x = 0; if (x + pW > w) x = (float)(w - pW);
            DrawRectangle((int)x, (int)y, (int)pW, (int)pH, (Color){100, 200, 255, 255});

            // Draw invincibility shield if player has it (check based on entity color alpha or a special marker)
            // Since we can't easily track server-side invincibility on client, we'll skip the shield visual
            // Or we could add it based on recent damage (temporary solution)
        } else if (e.type == 2) {
            // Enemy
            DrawRectangle((int)e.x, (int)e.y, 24, 16, (Color){220, 80, 80, 255});
        } else if (e.type == 3) {
            // Bullet
            DrawRectangle((int)e.x, (int)e.y, 6, 3, (Color){240, 220, 80, 255});
        } else if (e.type == 4) {
            // Power-up - draw as a circle with color based on rgba
            int cx = (int)(e.x + 9.f);
            int cy = (int)(e.y + 9.f);
            float radius = 9.f;
            // Extract color from rgba
            std::uint32_t rgba = e.rgba;
            std::uint8_t r = (rgba >> 24) & 0xFF;
            std::uint8_t g = (rgba >> 16) & 0xFF;
            std::uint8_t b = (rgba >> 8) & 0xFF;
            std::uint8_t a = rgba & 0xFF;
            Color fill = {r, g, b, a};
            Color line = {(std::uint8_t)std::min(255, r + 40), (std::uint8_t)std::min(255, g + 40), (std::uint8_t)std::min(255, b + 40), a};
            DrawCircle(cx, cy, radius, fill);
            DrawCircleLines(cx, cy, radius, line);
        }
    }

    // If everyone is dead, go to dedicated Game Over screen
    bool everyoneDead = (_playerLives <= 0);
    if (everyoneDead) { for (const auto& op : _otherPlayers) { if (op.lives > 0) { everyoneDead = false; break; } } }
    if (everyoneDead) {
        leaveSession();
        _connected = false;
        _entities.clear();
        _gameOver = true;
        screen = ScreenState::GameOver;
        return;
    }

    // Game over overlay, back to menu on ESC (self dead but others alive)
    if (_gameOver) {
        DrawRectangle(0, 0, w, h, (Color){0, 0, 0, 180});
        titleCentered("Game Over", (int)(h * 0.40f), (int)(h * 0.10f), RAYWHITE);
        if (IsKeyPressed(KEY_ESCAPE)) { teardownNet(); _connected = false; _entities.clear(); _gameOver = false; screen = ScreenState::Menu; return; }
    }
}

} } // namespace client::ui
