#pragma once

#include <cellui/Color.h>
#include <cellui/KeymapProfile.h>

#include <optional>

namespace cellui {

// The primaries of a display: sRGB's, Display-P3's or Rec.2020's.
enum class Gamut { SRGB, DISPLAY_P3, REC2020 };

// What a display that shows HDR says of itself. Luminances are relative to
// SDR white (1.0 is what a Color of 1 shows), so the application never deals
// in nits.
struct HdrInfo {
  // The display shows HDR right now.
  bool available = false;
  // Its peak luminance, in SDR whites.
  float peak = 1.0f;
  // What SDR white is in nits, 0 when unknown.
  float sdr_white_nits = 0.0f;
  // The display's primaries.
  Gamut gamut = Gamut::SRGB;

  bool operator==(const HdrInfo & o) const {
    return available == o.available && peak == o.peak && sdr_white_nits == o.sdr_white_nits && gamut == o.gamut;
  }
  bool operator!=(const HdrInfo & o) const { return !(*this == o); }
};

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
  // Whether the terminal fallback charts (cellCharts()) can use the finer
  // sextant glyphs. A display that shows pictures has no use for it.
  bool sextants = false;
  // Unset for a backend with no HDR path at all (the terminal, a window drawn
  // in sRGB): it clips every colour with Color::toSrgb8(). It changes while
  // the program runs (HDR toggled in the OS, a window moved to another
  // monitor): BackendEvent::Type::DISPLAY says so.
  std::optional<HdrInfo> hdr;
  // Whether a plane's content can be shown shifted by a fraction of a cell
  // (Plane::setScrollOffset()), so a view can scroll smoothly.
  bool smooth_scroll = false;
  // Whether the display lays out text itself, so Plane::putText() can set it
  // in a proportional face; any other has the cells' own.
  bool proportional_text = false;
  // The key shortcuts the application binds commands to here: the terminal's
  // are Emacs's, a window's the usual desktop ones.
  KeymapProfile keymap_profile = KeymapProfile::TERMINAL;
  // Whether a bare Escape may be the prefix of a Meta shortcut (a terminal sends
  // Meta-x as ESC x). A backend that reports Alt as a modifier clears it, and
  // Escape is then an ordinary key.
  bool escape_as_meta = true;
  // What input the display reports. A widget that needs one of these
  // checks the flag and offers another way when it is unset.
  // Key releases (KeyUp); otherwise only presses arrive.
  bool key_release = false;
  // Pointer positions finer than a cell (MouseDown::x and y have fractions).
  bool precise_mouse = false;
  // The pointer moving with no button held (MouseMove, Enter and Leave).
  bool hover = false;
};

}  // namespace cellui
