#include "Screens.hpp"
#include "widgets/Button.hpp"
#include "widgets/Title.hpp"
#include <raylib.h>

namespace client {
namespace ui {

void Screens::drawGameOver(ScreenState &screen) {
  int w = GetScreenWidth();
  int h = GetScreenHeight();
  int baseFont = std::max(16, (int)(h * 0.045f));
  Font font = getCurrentFont();

  DrawRectangle(0, 0, w, h, (Color){0, 0, 0, 200});
  titleCentered("ALL PLAYERS ARE DEAD", (int)(h * 0.32f), (int)(h * 0.10f),
                RED, font);
  std::string total = std::string("Total Score: ") + std::to_string(_score);
  titleCentered(total.c_str(), (int)(h * 0.48f), (int)(h * 0.07f), RAYWHITE, font);

  int btnWidth = (int)(w * 0.24f);
  int btnHeight = (int)(h * 0.09f);
  int x = (w - btnWidth) / 2;
  int y = (int)(h * 0.65f);
  if (button({(float)x, (float)y, (float)btnWidth, (float)btnHeight},
             "Back to Menu", baseFont, BLACK, LIGHTGRAY, GRAY, font)) {
    screen = ScreenState::Menu;
    _gameOver = false;
  }
}

} // namespace ui
} // namespace client
