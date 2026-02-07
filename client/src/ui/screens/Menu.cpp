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

void Screens::drawMenu(ScreenState &screen) {
  int w = GetScreenWidth();
  int h = GetScreenHeight();
  int baseFont = baseFontFromHeight(h);
  Font font = getCurrentFont();

  // Draw title with current font
  titleCentered("R-Type", (int)(h * 0.12f), (int)(h * 0.10f), RAYWHITE, font);

  int btnWidth = (int)(w * 0.28f);
  int btnHeight = (int)(h * 0.08f);
  int gap = (int)(h * 0.02f);
  int startY = (int)(h * 0.30f);
  int x = (w - btnWidth) / 2;

  Rectangle singleBtn{(float)x, (float)startY, (float)btnWidth,
                      (float)btnHeight};
  if (button(singleBtn, "Singleplayer", baseFont, BLACK, LIGHTGRAY, GRAY, font)) {
    screen = ScreenState::Singleplayer;
    _focusedField = 0;
  }

  Rectangle multiBtn{(float)x, (float)(startY + (btnHeight + gap) * 1),
                     (float)btnWidth, (float)btnHeight};
  if (button(multiBtn, "Multiplayer", baseFont, BLACK, LIGHTGRAY, GRAY, font)) {
    screen = ScreenState::Multiplayer;
    _focusedField = 0;
  }

  Rectangle quitBtn{(float)x, (float)(startY + (btnHeight + gap) * 2),
                    (float)btnWidth, (float)btnHeight};
  if (button(quitBtn, "Quit", baseFont, BLACK, (Color){200, 80, 80, 255},
             (Color){230, 120, 120, 255}, font)) {
    screen = ScreenState::Exiting;
  }

  // Font toggle button at the bottom
  Rectangle fontBtn{(float)x, (float)(startY + (btnHeight + gap) * 3),
                    (float)btnWidth, (float)btnHeight};
  const char* fontLabel = isUsingCustomFont() ? "Font: OpenDyslexic" : "Font: Normal";
  if (button(fontBtn, fontLabel, baseFont, BLACK, (Color){100, 100, 180, 255},
             (Color){130, 130, 210, 255}, font)) {
    toggleFont();
  }
}

} // namespace ui
} // namespace client
