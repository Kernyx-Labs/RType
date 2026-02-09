#pragma once
#include <raylib.h>

namespace client {
namespace ui {

// Draw a centered title text at y with fontSize and color
void titleCentered(const char *title, int y, int fontSize, Color color);

// Draw a centered title text with custom font
void titleCentered(const char *title, int y, int fontSize, Color color, Font font);

} // namespace ui
} // namespace client
