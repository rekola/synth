#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace cellui {

// Pixels a plane shows instead of text: width*height pixels, row 0 at the
// top, one uint32_t per pixel with bytes R, G, B, A in memory order. Each
// new picture gets a new version, so a backend sends it only when it
// changed.
struct Image {
  int width = 0, height = 0;
  std::vector<uint32_t> pixels;
  uint64_t version = 0;
  // The cells the picture is stretched over (a picture drawn at a lower
  // resolution than the display's, a blurred backdrop); 0 for its natural
  // size, a pixel to a pixel.
  int cols = 0, rows = 0;

  bool stretched() const { return cols > 0 && rows > 0; }
  // The cells it covers at the display's cell size.
  int coveredRows(int cell_h) const { return stretched() ? rows : (height + cell_h - 1) / cell_h; }
  int coveredCols(int cell_w) const { return stretched() ? cols : (width + cell_w - 1) / cell_w; }
  // The picture's own pixels under `cells` cells from cell `first`, along
  // one axis: [lo, hi).
  void pixelSpan(int first, int cells, int cell_px, bool horizontal, int & lo, int & hi) const {
    int size = horizontal ? width : height;
    int count = horizontal ? cols : rows;
    if (stretched()) {
      lo = first * size / count;
      hi = (first + cells) * size / count;
    } else {
      lo = first * cell_px;
      hi = (first + cells) * cell_px;
    }
    if (lo < 0) lo = 0;
    if (hi > size) hi = size;
  }

  static uint8_t red(uint32_t p) { return static_cast<uint8_t>(reinterpret_cast<const uint8_t *>(&p)[0]); }
  static uint8_t green(uint32_t p) { return static_cast<uint8_t>(reinterpret_cast<const uint8_t *>(&p)[1]); }
  static uint8_t blue(uint32_t p) { return static_cast<uint8_t>(reinterpret_cast<const uint8_t *>(&p)[2]); }
  static uint8_t alpha(uint32_t p) { return static_cast<uint8_t>(reinterpret_cast<const uint8_t *>(&p)[3]); }
  static uint32_t pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    uint32_t p;
    uint8_t * bytes = reinterpret_cast<uint8_t *>(&p);
    bytes[0] = r;
    bytes[1] = g;
    bytes[2] = b;
    bytes[3] = a;
    return p;
  }
};

// Where a composed frame shows an image: the image, the plane it belongs to
// (a key stable for the plane's lifetime), and the screen cell of the
// image's top-left corner. The cells it occupies in the frame are marked
// with `index` + 1 (CellBuffer::Cell::image); a cell another plane covers
// is not marked.
struct ImagePlacement {
  std::shared_ptr<const Image> image;
  uint64_t key = 0;
  int row = 0, col = 0;
  int index = 0;
};

}  // namespace cellui
