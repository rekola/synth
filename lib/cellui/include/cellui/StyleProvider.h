#pragma once

#include <cellui/Color.h>
#include <cellui/Theme.h>

#include <algorithm>
#include <iterator>
#include <utility>

namespace cellui {

class StyleProvider {
 public:
  StyleProvider() { applyTheme(Theme::dark()); }
  explicit StyleProvider(const Theme & initial) { applyTheme(initial); }

  // Sets every colour from the theme's tokens.
  void applyTheme(const Theme & t) {
    theme = t;
    tracks = t.tracks;
    highlight_fg_color = t.text_on_accent;
    highlight_bg_color = t.accent;
    highlight_unfocused_bg_color = t.accent_dim;
    window_border_color = t.surface_overlay;
    window_fg_color = t.text_secondary;
    window_bg_color = t.surface_base;
    window_accent_fg_color = t.text_primary;
    window_accent_bg_color = t.surface_panel;
    window_beat_accent_fg_color = t.grid_beat_fg;
    window_beat_accent_bg_color = t.grid_beat_bg;
    window_bar_accent_fg_color = t.grid_bar_fg;
    window_bar_accent_bg_color = t.grid_bar_bg;
    clip_playing_color = t.state_playing;
    clip_paused_color = t.state_playing_dim;
    clip_recording_color = t.state_recording;
    clip_armed_color = t.state_recording_dim;
    heading_bg_color = t.surface_raised;
    cursor_tint_color = t.cursor_tint;
    cursor_row_tint_color = t.cursor_row_tint;
    command_column_color = t.command_column;
    clip_text_color = t.text_on_track;
    reader_text_color = t.reader_text;
    info_line_fg_color = t.info_line_fg;
    info_line_bg_color = t.info_line_bg;
    meter_clip_color = t.meter_clip;
    meter_active_color = t.meter_active;
    meter_low_color = t.meter_low;
    meter_mid_color = t.meter_mid;
    meter_lime_color = t.meter_lime;
    meter_warn_color = t.meter_warn;
    meter_hot_color = t.meter_hot;
    spectrum_low_color = t.spectrum_low;
    spectrum_high_color = t.spectrum_high;
    mute_color = t.mute;
    solo_color = t.solo;
    monitor_color = t.monitor;
    clip_override_color = t.clip_override;
    locator_color = t.locator;
    locator_bg_color = t.locator_bg;
    locator_mark_color = t.locator_mark;
    button_fg_color = t.button_fg;
    button_bg_color = t.button_bg;
    button_pressed_bg_color = t.button_pressed_bg;
    row_pressed_bg_color = t.row_pressed_bg;
    master_track_color = t.master_track;
  }

  Theme theme;

  // A track's identity colour, from the theme's palette.
  TrackPalette tracks;
  Color trackColor(int ordinal) const { return tracks.color(ordinal); }

  // Cursor and playhead colors lean cyan, so they never read as the
  // neutral grey bar/beat highlighting.
  // The cursor/region of the focused widget.
  Color highlight_fg_color;
  Color highlight_bg_color;
  // The cursor of an unfocused widget - where its edits would land - on
  // plain window_fg_color text.
  Color highlight_unfocused_bg_color;
  // What a colored cell (a clip, an arrangement instance) brightens toward
  // under a cursor or playhead, instead of taking either background above.
  Color cursor_tint_color;
  // The translucent tint over a marked row: the pattern editor's cursor
  // row and playheads (in Arrangement view the transport's row, in
  // Live View each track's own), the clip grid's scene row and the
  // arrangement grid's playing row.
  Color cursor_row_tint_color;
  static constexpr float kRowTintAlpha = 0.35f;
  Color cursorRowTint(Color base) const { return base.blend(kRowTintAlpha, cursor_row_tint_color); }

  Color window_border_color;
  Color window_fg_color;
  Color window_bg_color;
  Color window_accent_fg_color;
  Color window_accent_bg_color;

  // The pattern editor's beat rows, and its bar rows (Song::
  // getRowsPerBar()) a step above them. Both stay well below the cursor
  // row's tint, so the cursor still stands out on an accented row.
  Color window_beat_accent_fg_color;
  Color window_beat_accent_bg_color;
  Color window_bar_accent_fg_color;
  Color window_bar_accent_bg_color;

  Color command_column_color;

  // Text drawn on a clip's own track color (a clip's name, a placed
  // instance's digit).
  Color clip_text_color;

  // Typed text in a reader (the M-x minibuffer, inline name editors).
  Color reader_text_color;

  // The info bar at the bottom, and the controls sitting inline in it.
  Color info_line_fg_color;
  Color info_line_bg_color;

  // A track's level indicators: clipping, and sounding.
  Color meter_clip_color;
  Color meter_active_color;
  // The level meters' bars shade by height, quiet to loud: greens, then
  // lime, orange and hot orange, ending in meter_clip_color at full scale.
  Color meter_low_color;
  Color meter_mid_color;
  Color meter_lime_color;
  Color meter_warn_color;
  Color meter_hot_color;
  // The spectrum's bars shade from low to high.
  Color spectrum_low_color;
  Color spectrum_high_color;

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
  Color mute_color;
  Color solo_color;
  Color monitor_color;
  // The mark on a track Live View has taken over from the arrangement.
  Color clip_override_color;

  // A clip slot's state glyph in the clip grid (ClipHighlight):
  // playing or queued; launched while the transport is paused; recording
  // or queued to record; an armed track's empty slot, or a take queued to
  // stop.
  Color clip_playing_color;
  Color clip_paused_color;
  Color clip_recording_color;
  Color clip_armed_color;

  // Pattern editor locators.
  Color locator_color;
  Color locator_bg_color;
  // The arrangement grid's mark for a bar with a locator.
  Color locator_mark_color;

  // Panel headers (a list's heading, a grid's track header row, and
  // the divider between them) - brighter than
  // window_accent_bg_color, so they read as a stronger accent than the
  // content beneath them.
  Color heading_bg_color;


  // Action buttons in a details panel - distinct from
  // command_column_color so a clickable button reads as its own kind of
  // thing rather than borrowing the pattern editor's command-column hue.
  Color button_fg_color;
  Color button_bg_color;
  // While the mouse is held down on a button, all of it.
  Color button_pressed_bg_color;
  // While the mouse is held down on a list row.
  Color row_pressed_bg_color;

  // Same hue as command_column_color, but at half that color's lightness
  // and a moderately lower saturation - the pattern editor's
  // master-track ancestor row (a whole title bar, not text on a dark
  // background).
  Color master_track_color;
};

}  // namespace cellui
