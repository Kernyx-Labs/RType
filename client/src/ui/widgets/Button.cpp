#include "../../../include/client/ui/widgets/Button.hpp"
#include <raylib.h>

namespace client {
namespace ui {

bool button(const Rectangle &bounds, const char *label, int fontSize, Color fg,
            Color bg, Color hoverBg) {
  Vector2 mouse = GetMousePosition();
  bool hovered = CheckCollisionPointRec(mouse, bounds);
  Color fill = hovered ? hoverBg : bg;
  DrawRectangleRec(bounds, fill);
  int textWidth = MeasureText(label, fontSize);
  int textX = (int)(bounds.x + (bounds.width - textWidth) / 2);
  int textY = (int)(bounds.y + (bounds.height - fontSize) / 2);
  DrawText(label, textX, textY, fontSize, fg);
  return hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

bool button(const Rectangle &bounds, const char *label, int fontSize, Color fg,
            Color bg, Color hoverBg, Font font) {
  Vector2 mouse = GetMousePosition();
  bool hovered = CheckCollisionPointRec(mouse, bounds);
  Color fill = hovered ? hoverBg : bg;
  DrawRectangleRec(bounds, fill);
  Vector2 textSize = MeasureTextEx(font, label, (float)fontSize, 1.0f);
  int textX = (int)(bounds.x + (bounds.width - textSize.x) / 2);
  int textY = (int)(bounds.y + (bounds.height - textSize.y) / 2);
  DrawTextEx(font, label, {(float)textX, (float)textY}, (float)fontSize, 1.0f, fg);
  return hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

} // namespace ui
} // namespace client
