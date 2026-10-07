#ifndef _STYLEPROVIDER_H_
#define _STYLEPROVIDER_H_

#include "../model/Color.h"

#include <algorithm>
#include <iterator>
#include <utility>

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
  // Live View each track's own), the clip grid's scene row and the
  // arrangement grid's playing row.
  Color cursor_row_tint_color = "#90b8cc";
  static constexpr float kRowTintAlpha = 0.35f;
  Color cursorRowTint(Color base) const { return base.blend(kRowTintAlpha, cursor_row_tint_color); }

  Color window_border_color = "#323232";
  Color window_fg_color = "#9e9e9e";
  Color window_bg_color = "#111111";
  Color window_accent_fg_color = "#ffffff";
  Color window_accent_bg_color = "#292929";

  // The pattern editor's beat rows, and its bar rows (Song::
  // getRowsPerBar()) a step above them. Both stay well below the cursor
  // row's tint, so the cursor still stands out on an accented row.
  Color window_beat_accent_fg_color = "#c4c4c4";
  Color window_beat_accent_bg_color = "#232323";
  Color window_bar_accent_fg_color = "#ffffff";
  Color window_bar_accent_bg_color = "#323232";

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
  // The level meters' bars shade by height, quiet to loud: greens, then
  // lime, orange and hot orange, ending in meter_clip_color at full scale.
  Color meter_low_color = "#0b5f2a";
  Color meter_mid_color = "#0f9a3a";
  Color meter_lime_color = "#9ae02e";
  Color meter_warn_color = "#f09020";
  Color meter_hot_color = "#ff5a20";
  // The spectrum's bars shade from low to high.
  Color spectrum_low_color = "#1e5fd0";
  Color spectrum_high_color = "#7af0d8";

  // The color of a level meter at `fraction` (0..1, level_meter::fraction())
  // of its height.
  Color meterColor(float fraction) const {
    const std::pair<float, const Color *> stops[] = {
      { 0.0f, &meter_low_color }, { 0.3f, &meter_mid_color }, { 0.55f, &meter_active_color }, { 0.78f, &meter_lime_color },
      { 0.86f, &meter_warn_color }, { 0.94f, &meter_hot_color }, { 1.0f, &meter_clip_color },
    };
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    for (size_t i = 1; i < std::size(stops); i++) {
      if (fraction <= stops[i].first) {
        float t = (fraction - stops[i - 1].first) / (stops[i].first - stops[i - 1].first);
        return stops[i - 1].second->blend(t, *stops[i].second);
      }
    }
    return meter_clip_color;
  }
  Color spectrumColor(float fraction) const { return spectrum_low_color.blend(std::clamp(fraction, 0.0f, 1.0f), spectrum_high_color); }

  // The clip grid header's track flags when on: Mute, Solo, and a Monitor
  // set to In (Auto shows in window_fg_color, Off in window_border_color).
  Color mute_color = "#ff5a5a";
  Color solo_color = "#ffdc5a";
  Color monitor_color = "#5ad2ff";
  // The mark on a track Live View has taken over from the arrangement.
  Color clip_override_color = "#ff9a3c";

  // A clip slot's state glyph in the clip grid (ClipHighlight):
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

  // The Live View's panel headers (OutlineView's heading, ClipGrid's
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
  // While the mouse is held down on a button, all of it.
  Color button_pressed_bg_color = "#7a2850";
  // While the mouse is held down on a list row (OutlineView's tree).
  Color row_pressed_bg_color = "#5c7c8c";

  // Same hue as command_column_color, but at half that color's lightness
  // and a moderately lower saturation - PatternEditor's master-track
  // ancestor row (a whole title bar, not text on a dark background).
  Color master_track_color = "#533918";
};

#endif

