#pragma once

#include <cellui/Color.h>

#include <optional>
#include <string>

namespace cellui::term {

enum class ColorDepth { NONE, C8, C256, TRUECOLOR };
enum class PixelProtocol { NONE, SIXEL, ITERM2, KITTY };
// Synchronized output as a private mode, or as the DCS pair some terminals
// implement without answering a mode query.
enum class SyncForm { NONE, MODE, DCS };

// What start-up learned about the terminal, frozen once start-up ends.
struct Capabilities {
  // The identified terminal ("xterm", "kitty", "foot", ...), or $TERM.
  std::string terminal;
  std::string version;
  // Whether the terminal answered before the deadline.
  bool answered = false;

  int rows = 0, cols = 0;
  // The cell size in pixels, 0 when unknown. It is also the base text size.
  int cell_px_height = 0, cell_px_width = 0;
  // estimateDisplayScale() of the cell height.
  double display_scale = 1.0;
  std::optional<Color> default_fg, default_bg;

  ColorDepth colors = ColorDepth::C8;
  // An absolute column move (CHA) exists; without it every move is a CUP.
  bool column_move = true;
  bool kitty_keyboard = false;
  SyncForm sync = SyncForm::NONE;
  bool pixel_mouse = false;

  PixelProtocol pixels = PixelProtocol::NONE;
  bool kitty_graphics_animation = false;
  int sixel_registers = 0, sixel_max_width = 0, sixel_max_height = 0;
  // Sent once before the first Sixel image.
  std::string sixel_setup;

  bool half_blocks = true, quadrants = false, sextants = false, braille = true;
  bool undercurl = false;
  // kitty draws a background equal to its own default as the default
  // (translucent under background opacity), so an explicit one must differ.
  bool distinct_default_bg = false;
  // rxvt ends colour replies with a bare ESC.
  bool rxvt = false;
};

// A guess at the display's scale factor (1 for a 96-dpi display, 2 for a
// typical HiDPI one) from the cell height in pixels: terminals report no
// DPI, but a default-sized monospace font fills a cell of about 18 pixels
// at scale 1. Rounded to quarter steps, as display scaling settings are,
// within 1 to 4; 1 when the cell size is unknown. The application scales
// what it draws in pixels by it - icons, text, line widths, but not
// hairlines - so nothing comes out too small; a large font at scale 1 reads
// as a larger scale, which suits that too.
double estimateDisplayScale(int cell_px_height);

// What discovery needs from the environment. The caller fills it from the
// process environment; tests fill it by hand.
struct Environment {
  std::string term;           // $TERM
  std::string colorterm;      // $COLORTERM
  std::string term_program;   // $TERM_PROGRAM
  bool konsole = false;       // $KONSOLE_VERSION set
  bool ssh = false;           // $SSH_CONNECTION or $SSH_TTY set
  bool linux_console = false; // the tty is a Linux virtual console
  bool windows = false;
  // Overrides: "none", "8", "256" or "24bit"; "none", "sixel", "kitty" or
  // "iterm2". Empty means detect.
  std::string colors_override;
  std::string pixels_override;

  // Reads the process environment (CELLUI_COLORS and CELLUI_PIXELS hold
  // the overrides).
  static Environment fromProcess();
};

}  // namespace cellui::term
