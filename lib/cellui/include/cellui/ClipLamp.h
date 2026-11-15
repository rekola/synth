#pragma once

#include <cellui/Color.h>
#include <cellui/Picture.h>
#include <cellui/Plane.h>
#include <cellui/StyleProvider.h>

#include <cstdint>

namespace cellui {

// A clip lamp: a round lamp, kClipLampCols cells wide and one row tall, that a level meter has above its
// bar. Dim while the meter is not clipping, and when it is the clip colour lit well past white
// (StyleProvider::clipLedColor()), so a display that blooms light makes it glow like an LED. How long it
// stays lit after a clip is the owner's to say (the signal's own clip is gone by the next block).
constexpr int kClipLampCols = 2;

// What the lamp is made of where no picture can be shown: a large circle in the first cell, and the
// second left blank to make the lamp two cells wide.
inline constexpr const char * kClipLampGlyph = "⬤";

namespace clip_lamp_image {

// A width x height picture of the lamp on `background`: a disc as large as fits with a margin, centred.
// Drawn on a float canvas when `prefer_float` and the backend has one (the lit lamp's colour kept as given,
// HDR above 1), else on an 8-bit one that maps it to sRGB. A zero size gives a blank picture.
Picture renderPicture(bool lit, unsigned width, unsigned height, const Color & background, const StyleProvider & styles, bool prefer_float);

}  // namespace clip_lamp_image

// Draws the lamp over the cells (row, col) and (row, col + 1), only where `row` is within the plane rows
// clip_top up to clip_bottom. A plane that shows pictures (Plane::cellPixelSize()) gets the round picture, as
// a float one where the display takes them (Plane::hdrPictures()); any other gets drawCellClipLamp().
// `id` names the lamp for the plane's images (see Plane::drawImageAt()).
void drawClipLamp(Plane & plane, int row, int col, bool lit, const Color & background, int clip_top, int clip_bottom, uint64_t id = 0);

// The text version: kClipLampGlyph and a space, in the lamp's colour on `background`. Lit, the colour is past
// white, which a window shows as a glow and a terminal as bold.
void drawCellClipLamp(Plane & plane, int row, int col, bool lit, const Color & background, int clip_top, int clip_bottom);

}  // namespace cellui
