#ifndef _STYLEPROVIDER_H_
#define _STYLEPROVIDER_H_

#include "../model/Color.h"

class StyleProvider {
 public:
  Color highlight_fg_color = "#000000";
  Color highlight_bg_color = "#a0ffa0";

  Color window_border_color = "#323232";
  Color window_fg_color = "#9e9e9e";
  Color window_bg_color = "#151515";
  Color window_accent_fg_color = "#ffffff";
  Color window_accent_bg_color = "#292929";

  Color command_column_color = "#c67610";

  // Same hue as command_column_color, but at half that color's lightness
  // and a moderately lower saturation - PatternEditor's master-track
  // ancestor row (a whole title bar, not text on a dark background).
  Color master_track_color = "#533918";

  // First use: PatternMatrix's not-applicable cell glyph (a DrumMachineTrack
  // column). No prior red/error color existed here to reuse.
  Color error_fg_color = "#dc3c3c";
};

#endif

