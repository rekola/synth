#ifndef _STYLEPROVIDER_H_
#define _STYLEPROVIDER_H_

#include "../model/Color.h"

class StyleProvider {
 public:
  // Cursor and playhead colors lean cyan, so they never read as the
  // neutral grey bar/beat highlighting.
  // The cursor/region of the focused widget.
  Color highlight_fg_color = "#000000";
  Color highlight_bg_color = "#bcd4e0";
  // The cursor of an unfocused widget - where its edits would land - on
  // plain window_fg_color text.
  Color highlight_unfocused_bg_color = "#3a4a54";
  // What a colored cell (a clip, an arrangement instance) brightens toward
  // under a cursor or playhead, instead of taking either background above.
  Color cursor_tint_color = "#d0ecff";
  // The translucent tint over a marked row: the pattern editor's cursor
  // row and playheads (in Arrangement view the transport's row, in
  // Session view each track's own), the clip grid's scene row and the
  // arrangement grid's playing row.
  Color cursor_row_tint_color = "#90b8cc";
  static constexpr float kRowTintAlpha = 0.35f;
  Color cursorRowTint(Color base) const { return base.blend(kRowTintAlpha, cursor_row_tint_color); }

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

  // Text drawn on a clip's own track color (a clip's name, a placed
  // instance's digit).
  Color clip_text_color = "#ffffff";

  // Typed text in a reader (the M-x minibuffer, inline name editors).
  Color reader_text_color = "#c080c0";

  // The info bar at the bottom, and the controls sitting inline in it.
  Color info_line_fg_color = "#1e1e1e";
  Color info_line_bg_color = "#787878";

  // A track's level indicators: clipping, and sounding.
  Color meter_clip_color = "#e01040";
  Color meter_active_color = "#10e040";

  // The clip grid header's track flags when on: Mute, Solo, and a Monitor
  // set to In (Auto shows in window_fg_color, Off in window_border_color).
  Color mute_color = "#ff5a5a";
  Color solo_color = "#ffdc5a";
  Color monitor_color = "#5ad2ff";
  // The mark on a track Session view has taken over from the arrangement.
  Color session_override_color = "#ff9a3c";

  // A clip slot's state glyph in the clip grid (SessionPadHighlight):
  // playing or queued; launched while the transport is paused; recording
  // or queued to record; an armed track's empty slot, or a take queued to
  // stop.
  Color clip_playing_color = "#50e070";
  Color clip_paused_color = "#2e7a3e";
  Color clip_recording_color = "#ff3c3c";
  Color clip_armed_color = "#a04040";

  // Pattern editor locators.
  Color locator_color = "#e03030";
  Color locator_bg_color = "#702020";
  // The arrangement grid's mark for a bar with a locator.
  Color locator_mark_color = "#c8c8c8";

  // The Session view's panel headers (OutlineView's heading, ClipGrid's
  // track header row, and the divider between them) - brighter than
  // window_accent_bg_color, so they read as a stronger accent than the
  // content beneath them.
  Color heading_bg_color = "#3d3d3d";


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

