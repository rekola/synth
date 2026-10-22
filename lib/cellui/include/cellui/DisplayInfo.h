#pragma once

#include <cellui/Color.h>
#include <cellui/KeymapProfile.h>

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
  // Whether a plane's content can be shown shifted by a fraction of a cell
  // (UIPlane::setScrollOffset()), so a view can scroll smoothly.
  bool smooth_scroll = false;
  // The key chords the application binds commands to here: the terminal's
  // are Emacs's, a window's the usual desktop ones.
  KeymapProfile keymap_profile = KeymapProfile::EMACS;
  // Whether a bare Escape may be the prefix of a Meta chord (a terminal sends
  // Meta-x as ESC x). A backend that reports Alt as a modifier clears it, and
  // Escape is then an ordinary key.
  bool escape_as_meta = true;
};

}  // namespace cellui
