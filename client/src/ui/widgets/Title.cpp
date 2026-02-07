#include "../../../include/client/ui/widgets/Title.hpp"
#include <raylib.h>

namespace client {
namespace ui {

void titleCentered(const char *title, int y, int fontSize, Color color) {
  int w = MeasureText(title, fontSize);
  DrawText(title, (GetScreenWidth() - w) / 2, y, fontSize, color);
}

void titleCentered(const char *title, int y, int fontSize, Color color, Font font) {
  Vector2 textSize = MeasureTextEx(font, title, (float)fontSize, 1.0f);
  int x = (GetScreenWidth() - (int)textSize.x) / 2;
  DrawTextEx(font, title, {(float)x, (float)y}, (float)fontSize, 1.0f, color);
}

} // namespace ui
} // namespace client
