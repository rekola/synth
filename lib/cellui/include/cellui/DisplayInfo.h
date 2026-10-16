#pragma once

#include <cellui/Color.h>

#include <optional>

namespace cellui {

// What a backend tells the application about the display it draws on,
// whatever the backend is.
struct DisplayInfo {
  // The cell size in pixels, 0 when unknown. It is also the base text size.
  int cell_px_height = 0, cell_px_width = 0;
  // Whether pictures can be shown (the cell size is then known).
  bool pictures = false;
  // The colours cells without one are drawn in, when known.
  std::optional<Color> default_fg, default_bg;
  // 1 for a 96-dpi display, 2 for a typical HiDPI one; what the application
  // draws in pixels is scaled by it.
  double display_scale = 1.0;
  // Whether the sextant glyphs can be drawn.
  bool sextants = false;
};

}  // namespace cellui
