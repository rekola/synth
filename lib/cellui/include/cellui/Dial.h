#pragma once

#include <cellui/Color.h>
#include <cellui/Plane.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cellui {

// A dial: a ring open at the bottom for a one-letter caption, and a pointer
// from the centre that sweeps the ring from its bottom-left end (fraction 0)
// over the top to its bottom-right end (fraction 1).
struct DialReading {
  std::string letter;
  float fraction = 0.0f;  // 0..1
  // What the dial's slot shows where no picture can be: under the letter.
  std::string text;
};

// The ring and pointer in `foreground`, on `background` (which the pointer's
// outline is drawn in too, to keep it apart from the ring).
struct DialLook {
  Color background;
  Color foreground;
};

namespace dial_image {

// A width x height picture of the dial, as large as fits, centred. Returns
// width*height pixels, row 0 at the top, bytes R, G, B, A (all opaque), or
// nothing where canvas has no pixel backend. Drawn at `scale`, the display
// scale: the ring is 4 pixels thick at scale 1.
std::vector<uint32_t> render(const DialReading & reading, const DialLook & look, unsigned width, unsigned height, float scale = 1.0f);

}  // namespace dial_image

// Draws a dial in the `rows` x `cols` cells from (row, col), only in the plane
// rows from clip_top up to clip_bottom. A plane that shows pictures
// (Plane::cellPixelSize()) gets the picture, with no number; any other gets
// the letter over `reading.text` in the same cells. `id` names the dial for
// the plane's images (see Plane::drawImageAt()).
void drawDial(Plane & plane, int row, int col, int rows, int cols, const DialReading & reading, const DialLook & look, int clip_top,
              int clip_bottom, uint64_t id = 0);

// The text version of drawDial().
void drawCellDial(Plane & plane, int row, int col, int rows, int cols, const DialReading & reading, const DialLook & look, int clip_top,
                  int clip_bottom);

}  // namespace cellui
