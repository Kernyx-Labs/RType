#include "Screens.hpp"
#include "widgets/Button.hpp"
#include "widgets/Title.hpp"
#include <raylib.h>

namespace client {
namespace ui {

static int baseFontFromHeight(int h) {
  int baseFont = (int)(h * 0.045f);
  if (baseFont < 16)
    baseFont = 16;
  return baseFont;
}

void Screens::drawWaiting(ScreenState &screen) {
  int w = GetScreenWidth();
  int h = GetScreenHeight();
  int baseFont = baseFontFromHeight(h);

  ensureNetSetup();
  pumpNetworkOnce();

  if (_serverReturnToMenu) {
    leaveSession();
    screen = ScreenState::NotEnoughPlayers;
    return;
  }

  int playerCount = 1 + static_cast<int>(_otherPlayers.size());

  Font font = getCurrentFont();

  titleCentered("Lobby", (int)(h * 0.20f), (int)(h * 0.08f), RAYWHITE, font);
  std::string sub = "Players connected: " + std::to_string(playerCount);
  titleCentered(sub.c_str(), (int)(h * 0.32f), baseFont, RAYWHITE, font);

  // Precompute bottom layout to place Start just above Cancel
  int btnWidth = (int)(w * 0.18f);
  int btnHeight = (int)(h * 0.08f);
  int bottomMargin = std::max(10, (int)(h * 0.04f));
  int cancelBtnHeightPlan = (int)(h * 0.08f);
  int bottomMarginPlan = std::max(10, (int)(h * 0.04f));
  int cancelYPlan = std::max(0, h - bottomMarginPlan - cancelBtnHeightPlan);

  // Draw player list on the left side
  int listX = (int)(w * 0.05f);
  int listY = (int)(h * 0.40f);
  int listItemHeight = (int)(h * 0.05f);
  DrawTextEx(font, "Players:", {(float)listX, (float)listY}, (float)baseFont, 1.0f, RAYWHITE);

  // Show self first
  int currentY = listY + listItemHeight;
  if (_selfId != 0) {
    std::string selfDisplay = _username + " (You)";
    DrawTextEx(font, selfDisplay.c_str(), {(float)(listX + 10), (float)currentY}, (float)(baseFont - 2), 1.0f, GREEN);
    currentY += listItemHeight;
  }

  // Show other players
  for (const auto &op : _otherPlayers) {
    if (currentY >= cancelYPlan - listItemHeight)
      break; // Don't overlap with buttons
    DrawTextEx(font, op.name.c_str(), {(float)(listX + 10), (float)currentY}, (float)(baseFont - 2), 1.0f, RAYWHITE);
    currentY += listItemHeight;
  }

  // Host-only settings panel
  if (_selfId != 0 && _selfId == _hostId) {
    int panelW = (int)(w * 0.60f);
    int panelH = (int)(h * 0.30f);
    int px = (w - panelW) / 2;
    int py = (int)(h * 0.40f);
    DrawRectangle(px, py, panelW, panelH, (Color){0, 0, 0, 120});

    titleCentered("Host settings", py + (int)(h * 0.02f), baseFont, RAYWHITE, font);

    // Difficulty selector
    const char *diffNames[3] = {"Easy", "Normal", "Hard"};
    int diffY = py + (int)(h * 0.10f);
    std::string dlabel = std::string("Difficulty: ") +
                         diffNames[std::min(2, (int)_lobbyDifficulty)];
    titleCentered(dlabel.c_str(), diffY, baseFont, RAYWHITE, font);

    int btnW = (int)(w * 0.06f);
    int btnH = (int)(h * 0.06f);
    int bx = (w - (btnW * 2 + (int)(w * 0.02f))) / 2;
    Rectangle dMinus{(float)bx, (float)(diffY + (int)(h * 0.04f)), (float)btnW,
                     (float)btnH};
    Rectangle dPlus{(float)(bx + btnW + (int)(w * 0.02f)),
                    (float)(diffY + (int)(h * 0.04f)), (float)btnW,
                    (float)btnH};
    bool changed = false;
    if (button(dMinus, "-", baseFont, BLACK, LIGHTGRAY, GRAY, font)) {
      if (_lobbyDifficulty > 0) {
        _lobbyDifficulty--;
        changed = true;
      }
    }
    if (button(dPlus, "+", baseFont, BLACK, LIGHTGRAY, GRAY, font)) {
      if (_lobbyDifficulty < 2) {
        _lobbyDifficulty++;
        changed = true;
      }
    }

    // Base lives selector
    int livesY = diffY + (int)(h * 0.12f);
    std::string llabel = std::string("Base lives: ") +
                         std::to_string((int)_lobbyBaseLives) + " (max 6)";
    titleCentered(llabel.c_str(), livesY, baseFont, RAYWHITE, font);
    Rectangle lMinus{(float)bx, (float)(livesY + (int)(h * 0.04f)), (float)btnW,
                     (float)btnH};
    Rectangle lPlus{(float)(bx + btnW + (int)(w * 0.02f)),
                    (float)(livesY + (int)(h * 0.04f)), (float)btnW,
                    (float)btnH};
    if (button(lMinus, "-", baseFont, BLACK, LIGHTGRAY, GRAY, font)) {
      if (_lobbyBaseLives > 1) {
        _lobbyBaseLives--;
        changed = true;
      }
    }
    if (button(lPlus, "+", baseFont, BLACK, LIGHTGRAY, GRAY, font)) {
      if (_lobbyBaseLives < 6) {
        _lobbyBaseLives++;
        changed = true;
      }
    }

    if (changed) {
      sendLobbyConfig(_lobbyDifficulty, _lobbyBaseLives);
    }

    // Start match button (require >=2 players) positioned just above Cancel
    int startW = (int)(w * 0.22f);
    int startH = (int)(h * 0.08f);
    int gapY = std::max(8, (int)(h * 0.02f));
    int startY = std::max(0, cancelYPlan - gapY - startH);
    Rectangle startBtn{(float)((w - startW) / 2), (float)startY, (float)startW,
                       (float)startH};
    Color bg = playerCount >= 2 ? (Color){120, 200, 120, 255}
                                : (Color){80, 120, 80, 255};
    Color hg = playerCount >= 2 ? (Color){150, 230, 150, 255}
                                : (Color){90, 140, 90, 255};
    if (button(startBtn, "Start", baseFont, BLACK, bg, hg, font)) {
      if (playerCount >= 2)
        sendStartMatch();
    }
  } else {
    int dots = ((int)(GetTime() * 2)) % 4;
    std::string hint = "Waiting for host" + std::string(dots, '.');
    titleCentered(hint.c_str(), (int)(h * 0.48f), baseFont, LIGHTGRAY, font);
  }

  // Cancel button at bottom
  int x = (w - btnWidth) / 2;
  int y = std::max(0, h - bottomMargin - btnHeight);
  Rectangle cancelBtn{(float)x, (float)y, (float)btnWidth, (float)btnHeight};
  if (button(cancelBtn, "Cancel", baseFont, BLACK, LIGHTGRAY, GRAY, font)) {
    teardownNet();
    _connected = false;
    _entities.clear();
    screen = ScreenState::Menu;
    return;
  }

  // Transition to gameplay when server announces start
  if (_lobbyStarted) {
    if (assetsAvailable()) {
      screen = ScreenState::Gameplay;
    } else {
      titleCentered("Missing spritesheet assets. Place sprites/ and try again.",
                    (int)(h * 0.80f), baseFont, RED, font);
    }
  }
}

} // namespace ui
} // namespace client
