#pragma once
#include <raylib.h>
#include <string>

namespace client {
namespace ui {

// Draws a labeled input box; returns true if the box was clicked (to focus)
bool inputBox(const Rectangle &bounds, const char *label, std::string &text,
              bool focused, int fontSize, Color fg, Color bg, Color border,
              bool numericOnly);

// Draws a labeled input box with custom font; returns true if the box was clicked (to focus)
bool inputBox(const Rectangle &bounds, const char *label, std::string &text,
              bool focused, int fontSize, Color fg, Color bg, Color border,
              bool numericOnly, Font font);

} // namespace ui
} // namespace client
