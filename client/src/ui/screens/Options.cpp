#include "Screens.hpp"
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

void Screens::drawOptions() {
  int h = GetScreenHeight();
  int baseFont = baseFontFromHeight(h);
  Font font = getCurrentFont();
  titleCentered("Options", (int)(h * 0.10f), (int)(h * 0.08f), RAYWHITE, font);
  titleCentered("Coming soon... Press ESC to go back.", (int)(h * 0.50f),
                baseFont, RAYWHITE, font);
}

} // namespace ui
} // namespace client
