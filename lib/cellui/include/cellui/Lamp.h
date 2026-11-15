#pragma once

#include <cellui/Color.h>
#include <cellui/Picture.h>
#include <cellui/Plane.h>
#include <cellui/StyleProvider.h>

#include <cstdint>

namespace cellui {

// A lamp: a round indicator light, one cell wide and one row tall. Dim while off, and when lit the theme's
// alarm colour (level_full_color) well past white (StyleProvider::lampColor()), so a display that blooms light
// makes it glow like an LED. What lit means, and for how long, is the owner's to say.
constexpr int kLampCols = 1;

// What the lamp is made of where no picture can be shown: a large circle.
inline constexpr const char * kLampGlyph = "⬤";

namespace lamp_image {

// A width x height picture of the lamp: a disc 0.8 of the smaller side across, centred, on `background`.
// Drawn on a float canvas when `prefer_float` and the backend has one (the lit lamp's colour kept as given,
// HDR above 1, and the rest of the picture transparent, so a window's glow has no seam at its edge), else on
// an 8-bit one that maps it to sRGB and is opaque. A zero size gives a blank picture.
Picture renderPicture(bool lit, unsigned width, unsigned height, const Color & background, const StyleProvider & styles, bool prefer_float);

}  // namespace lamp_image

// Draws the lamp over the cell (row, col), only where `row` is within the plane rows
// clip_top up to clip_bottom. A plane that shows pictures (Plane::cellPixelSize()) gets the round picture, as
// a float one where the display takes them (Plane::hdrPictures()); any other gets drawCellLamp().
// `id` names the lamp for the plane's images (see Plane::drawImageAt()).
void drawLamp(Plane & plane, int row, int col, bool lit, const Color & background, int clip_top, int clip_bottom, uint64_t id = 0);

// The text version: kLampGlyph, in the lamp's colour on `background`. Lit, the colour is past
// white, which a window shows as a glow and a terminal as bold.
void drawCellLamp(Plane & plane, int row, int col, bool lit, const Color & background, int clip_top, int clip_bottom);

}  // namespace cellui
