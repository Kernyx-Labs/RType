#include "../../../include/client/ui/widgets/InputBox.hpp"
#include <raylib.h>

namespace client {
namespace ui {

bool inputBox(const Rectangle &bounds, const char *label, std::string &text,
              bool focused, int fontSize, Color fg, Color bg, Color border,
              bool numericOnly) {
  DrawRectangleRec(bounds, bg);
  DrawRectangleLinesEx(bounds, 2.0f, focused ? RAYWHITE : border);
  DrawText(label, (int)bounds.x, (int)(bounds.y - fontSize - 6), fontSize, fg);
  std::string display = text;
  if (focused) {
    if (((int)(GetTime() * 2.0)) % 2 == 0)
      display.push_back('|');
  }
  int padding = 8;
  int textY = (int)(bounds.y + (bounds.height - fontSize) / 2);
  DrawText(display.c_str(), (int)bounds.x + padding, textY, fontSize, fg);

  if (focused) {
    int codepoint = GetCharPressed();
    while (codepoint > 0) {
      if (codepoint >= 32 && codepoint <= 126) {
        char c = (char)codepoint;
        bool ok = true;
        if (numericOnly)
          ok = (c >= '0' && c <= '9');
        if (ok)
          text.push_back(c);
      }
      codepoint = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !text.empty())
      text.pop_back();
  }

  bool clicked = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
                 CheckCollisionPointRec(GetMousePosition(), bounds);
  return clicked;
}

bool inputBox(const Rectangle &bounds, const char *label, std::string &text,
              bool focused, int fontSize, Color fg, Color bg, Color border,
              bool numericOnly, Font font) {
  DrawRectangleRec(bounds, bg);
  DrawRectangleLinesEx(bounds, 2.0f, focused ? RAYWHITE : border);
  DrawTextEx(font, label, {bounds.x, bounds.y - fontSize - 6}, (float)fontSize, 1.0f, fg);
  std::string display = text;
  if (focused) {
    if (((int)(GetTime() * 2.0)) % 2 == 0)
      display.push_back('|');
  }
  int padding = 8;
  Vector2 textSize = MeasureTextEx(font, display.c_str(), (float)fontSize, 1.0f);
  int textY = (int)(bounds.y + (bounds.height - textSize.y) / 2);
  DrawTextEx(font, display.c_str(), {bounds.x + padding, (float)textY}, (float)fontSize, 1.0f, fg);

  if (focused) {
    int codepoint = GetCharPressed();
    while (codepoint > 0) {
      if (codepoint >= 32 && codepoint <= 126) {
        char c = (char)codepoint;
        bool ok = true;
        if (numericOnly)
          ok = (c >= '0' && c <= '9');
        if (ok)
          text.push_back(c);
      }
      codepoint = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !text.empty())
      text.pop_back();
  }

  bool clicked = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
                 CheckCollisionPointRec(GetMousePosition(), bounds);
  return clicked;
}

} // namespace ui
} // namespace client
