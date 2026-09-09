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

  // OutlineView's own Outline/Details panel headers - brighter than the
  // plain window_accent_bg_color pair other widgets' headers use, so they
  // read as a stronger accent than the panel content (buttons included)
  // beneath them.
  Color heading_bg_color = "#3d3d3d";

  // The heading's own shadow row (see renderHeading()) - a subtle step
  // between window_bg_color and window_border_color, distinct from both.
  Color heading_shadow_color = "#242424";

  // OutlineView's Details panel action buttons (Delete/Add to Song/
  // Preview/Stop) - distinct from command_column_color so a clickable
  // button reads as its own kind of thing rather than borrowing the
  // pattern editor's command-column hue.
  Color button_fg_color = "#ffffff";
  Color button_bg_color = "#c04080";

  // Same hue as command_column_color, but at half that color's lightness
  // and a moderately lower saturation - PatternEditor's master-track
  // ancestor row (a whole title bar, not text on a dark background).
  Color master_track_color = "#533918";
};

#endif

