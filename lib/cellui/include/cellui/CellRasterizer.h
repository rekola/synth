#pragma once

#include <cellui/CellBuffer.h>
#include <cellui/Color.h>
#include <cellui/Image.h>

#include <memory>
#include <string>
#include <vector>

namespace cellui {

// Draws composed cells as pixels, the way a display that shows pixels
// would: each cell's background, its glyph in a font of the system's, and
// the images over the cells marked with them. What a backdrop blurs when the
// display can show it as an image.
class CellRasterizer {
 public:
  virtual ~CellRasterizer() = default;
  // Cells [row, row + rows) x [col, col + cols) of `cells` at `cell_h` x
  // `cell_w` pixels, drawn `scale` times smaller (a picture to be blurred
  // needs no more): an opaque picture of rows * cell_h / scale by
  // cols * cell_w / scale pixels. `images` are what the cells' image marks
  // refer to.
  virtual Image render(const CellBuffer & cells, const std::vector<ImagePlacement> & images, int row, int col, int rows, int cols,
                       int cell_h, int cell_w, int scale, Color default_fg, Color default_bg) = 0;
};

// A rasterizer drawing with canvas, its glyphs in the font family `family`
// ("monospace": the system's own monospace font). nullptr where canvas has no
// pixel backend built.
std::unique_ptr<CellRasterizer> canvasRasterizer(const std::string & family = "monospace");

}  // namespace cellui
