#pragma once

#include <cellui/CellBuffer.h>
#include <cellui/Image.h>
#include <cellui/term/Capabilities.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cellui::term {

// The bytes that show a frame's images, by kitty graphics, iTerm2's inline
// images or Sixel. Only
// what changed is sent: a new picture, a move, or a change in which of its
// cells other planes cover (a popup over a chart). Cells covered by another
// plane are left out - a kitty image is placed as the rectangles of cells
// still showing it, a Sixel image is drawn with those cells transparent -
// an iTerm2 image is sent as one PNG per such rectangle - and an image that
// is gone is deleted (kitty) or overwritten by the text the frame now has
// there (Sixel, iTerm2). A Sixel or iTerm2 image never reaches the screen's
// last row, where drawing one could scroll the screen.
class ImageEncoder {
 public:
  struct Profile {
    PixelProtocol protocol = PixelProtocol::NONE;
    int cell_px_height = 0, cell_px_width = 0;
    // Sent once, before the first Sixel image.
    std::string sixel_setup;
    static Profile from(const Capabilities & caps);
  };

  ImageEncoder() = default;
  explicit ImageEncoder(Profile profile) : profile_(std::move(profile)) { }

  // `frame` is the composed frame, its image cells marked; `cleared` says
  // the screen is cleared before these bytes, taking every image with it.
  std::string encode(const CellBuffer & frame, const std::vector<ImagePlacement> & images, bool cleared);
  // Removes every image (kitty graphics; Sixel images go with the text).
  std::string clear();

  // One rectangle of cells, screen coordinates.
  struct Rect {
    int row = 0, col = 0, rows = 0, cols = 0;
    bool operator==(const Rect & o) const { return row == o.row && col == o.col && rows == o.rows && cols == o.cols; }
  };

 private:
  struct Shown {
    uint32_t id = 0;  // kitty image id
    uint64_t version = 0;
    int row = 0, col = 0;
    std::vector<Rect> rects;  // what shows of it
  };

  std::string kitty(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared);
  std::string sixel(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared);
  std::string iterm2(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared);
  std::vector<Rect> visibleRects(const CellBuffer & frame, const ImagePlacement & p, bool avoid_last_row) const;

  Profile profile_;
  std::map<uint64_t, Shown> shown_;
  uint32_t next_id_ = 0;
  bool sixel_ready_ = false;
};

// The pixels encoded as Sixel: covered pixels (where `keep` says false, one
// entry per pixel) are left transparent, and colours go to the 240 xterm
// cube and grey-ramp entries. Exposed for tests.
std::string encodeSixel(const Image & image, int x, int y, int width, int height, const std::vector<bool> & keep);

// A rectangle of the pixels as a PNG file (8-bit RGBA, stored uncompressed).
// Exposed for tests.
std::vector<uint8_t> encodePng(const Image & image, int x, int y, int width, int height);

}  // namespace cellui::term
