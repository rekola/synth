#pragma once

#include <cellui/Color.h>

namespace cellui {

// The colour tokens of a theme, by role: widgets ask for what a colour
// means, a theme says what it is. Themes are plain data.
struct Theme {
  // Background tones, lowest to highest elevation.
  Color surface_base;
  Color surface_panel;
  Color surface_raised;
  Color surface_overlay;

  Color text_primary;
  Color text_secondary;
  Color text_disabled;
  Color text_on_accent;

  // The cursor and region of the focused widget (accent), the same
  // for a widget without focus (accent_dim), and the text on both.
  Color accent;
  Color accent_dim;
  Color focus;

  // State, each with its dim form (paused; armed or stopping).
  Color state_playing;
  Color state_playing_dim;
  Color state_recording;
  Color state_recording_dim;

  // Pattern editor rows: beat and bar accents, as tones.
  Color grid_beat_fg;
  Color grid_beat_bg;
  Color grid_bar_fg;
  Color grid_bar_bg;

  // Roles outside the surfaces: the cursor tints, text on a track colour,
  // the command and reader text, the info bar, the meter and spectrum
  // ramps, track flags, locators, buttons and the master track's bar.
  Color cursor_tint;
  Color cursor_row_tint;
  Color command_column;
  Color text_on_track;
  Color reader_text;
  Color info_line_fg;
  Color info_line_bg;
  Color meter_clip;
  Color meter_active;
  Color meter_low;
  Color meter_mid;
  Color meter_lime;
  Color meter_warn;
  Color meter_hot;
  Color spectrum_low;
  Color spectrum_high;
  Color mute;
  Color solo;
  Color monitor;
  Color clip_override;
  Color locator;
  Color locator_bg;
  Color locator_mark;
  Color button_fg;
  Color button_bg;
  Color button_pressed_bg;
  Color row_pressed_bg;
  Color master_track;

  static Theme dark() {
    Theme t;
    t.surface_base = Color("#111111");
    t.surface_panel = Color("#292929");
    t.surface_raised = Color("#3d3d3d");
    t.surface_overlay = Color("#323232");
    t.text_primary = Color("#ffffff");
    t.text_secondary = Color("#9e9e9e");
    t.text_disabled = Color("#323232");
    t.text_on_accent = Color("#000000");
    t.accent = Color("#bcd4e0");
    t.accent_dim = Color("#3a4a54");
    t.focus = Color("#bcd4e0");
    t.state_playing = Color("#50e070");
    t.state_playing_dim = Color("#2e7a3e");
    t.state_recording = Color("#ff3c3c");
    t.state_recording_dim = Color("#a04040");
    t.grid_beat_fg = Color("#c4c4c4");
    t.grid_beat_bg = Color("#232323");
    t.grid_bar_fg = Color("#ffffff");
    t.grid_bar_bg = Color("#323232");
    t.cursor_tint = Color("#d0ecff");
    t.cursor_row_tint = Color("#90b8cc");
    t.command_column = Color("#c67610");
    t.text_on_track = Color("#ffffff");
    t.reader_text = Color("#c080c0");
    t.info_line_fg = Color("#1e1e1e");
    t.info_line_bg = Color("#787878");
    t.meter_clip = Color("#e01040");
    t.meter_active = Color("#10e040");
    t.meter_low = Color("#0b5f2a");
    t.meter_mid = Color("#0f9a3a");
    t.meter_lime = Color("#9ae02e");
    t.meter_warn = Color("#f09020");
    t.meter_hot = Color("#ff5a20");
    t.spectrum_low = Color("#1e5fd0");
    t.spectrum_high = Color("#7af0d8");
    t.mute = Color("#ff5a5a");
    t.solo = Color("#ffdc5a");
    t.monitor = Color("#5ad2ff");
    t.clip_override = Color("#ff9a3c");
    t.locator = Color("#e03030");
    t.locator_bg = Color("#702020");
    t.locator_mark = Color("#c8c8c8");
    t.button_fg = Color("#ffffff");
    t.button_bg = Color("#c04080");
    t.button_pressed_bg = Color("#7a2850");
    t.row_pressed_bg = Color("#5c7c8c");
    t.master_track = Color("#533918");
    return t;
  }
};

}  // namespace cellui
