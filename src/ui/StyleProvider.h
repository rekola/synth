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

  // A bar boundary (Song::getRowsPerBar()) - a stronger accent
  // than window_accent_*_color's own plain beat one, since a bar-start
  // row is always also a beat-start row and should read as "more
  // important" than an ordinary one.
  Color window_bar_accent_fg_color = "#ffffff";
  Color window_bar_accent_bg_color = "#3d3d3d";

  Color command_column_color = "#c67610";

  // A hit lane's cell background in DrumMachineTrack's compact
  // step-sequencer NOTE column (PatternEditor::renderRow()) - dark enough
  // for the row's own white-ish note text to stay readable on top.
  Color drum_step_hit_bg_color = "#0e5f6e";

  // Same hue as command_column_color, but at half that color's lightness
  // and a moderately lower saturation - PatternEditor's master-track
  // ancestor row (a whole title bar, not text on a dark background).
  Color master_track_color = "#533918";
};

#endif

