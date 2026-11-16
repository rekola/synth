#pragma once

#include <cellui/Color.h>
#include <cellui/Plane.h>
#include <cellui/StyleProvider.h>

namespace cellui {

// A lamp: a round indicator light, one cell wide and one row tall, a large circle in the lamp's colour.
// Colourless while off, a grey, so there is no doubt whether it is lit; lit it is the theme's alarm colour
// (level_full_color) well past white (StyleProvider::lampColor()), which a display that blooms light shows as a
// glow in the lamp's own hue (the window blooms any cell colour past white, as it does a picture's) and a
// terminal as bold. What lit means, and for how long, is the owner's to say.
constexpr int kLampCols = 1;

// The circle drawn: a character, so every display has it, whether or not it shows pictures. A font without it
// falls back to one that has it.
inline constexpr const char * kLampGlyph = "⬤";

// Draws the lamp in the cell (row, col), only where `row` is within the plane rows clip_top up to clip_bottom.
void drawLamp(Plane & plane, int row, int col, bool lit, const Color & background, int clip_top, int clip_bottom);

}  // namespace cellui
